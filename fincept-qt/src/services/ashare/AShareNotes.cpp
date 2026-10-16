#include "services/ashare/AShareNotes.h"
#include "services/ashare/AShareMarketService.h"
#include "services/ashare/AShareStorage.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace fincept::services::ashare {
QString notes_path() { return data_file("notes.json"); }
QStringList note_categories() { return {"research", "trade", "review", "idea", "other"}; }
QString note_category_label(const QString& id) {
    if (id == "research") return QStringLiteral("研究");
    if (id == "trade") return QStringLiteral("交易");
    if (id == "review") return QStringLiteral("复盘");
    if (id == "idea") return QStringLiteral("想法");
    return QStringLiteral("其他");
}
QString note_sentiment_label(const QString& id) {
    return id == "bullish" ? QStringLiteral("看多") : id == "bearish" ? QStringLiteral("看空") : QStringLiteral("中性");
}
QStringList parse_tickers(const QString& text) {
    QStringList out;
    for (const auto& token : text.split(QRegularExpression("[\\s,，;；、]+"), Qt::SkipEmptyParts)) {
        const auto symbol = normalize_symbol(token);
        if (!symbol.isEmpty() && !out.contains(symbol)) out << symbol;
    }
    return out;
}
bool note_matches(const Note& n, const QString& query, const QString& category, const QString& ticker, bool favorites_only) {
    if (favorites_only && !n.favorite) return false;
    if (!category.isEmpty() && n.category != category) return false;
    if (!ticker.isEmpty() && !n.tickers.contains(ticker)) return false;
    const auto q = query.trimmed();
    if (q.isEmpty()) return true;
    if (n.title.contains(q, Qt::CaseInsensitive) || n.content.contains(q, Qt::CaseInsensitive)) return true;
    for (const auto& t : n.tags) if (t.contains(q, Qt::CaseInsensitive)) return true;
    for (const auto& t : n.tickers) if (t.contains(q, Qt::CaseInsensitive)) return true;
    return false;
}
QString note_to_markdown(const Note& n) {
    QString md = "# " + (n.title.isEmpty() ? QStringLiteral("未命名笔记") : n.title) + "\n\n";
    md += QStringLiteral("- 分类：%1\n- 观点：%2\n").arg(note_category_label(n.category), note_sentiment_label(n.sentiment));
    if (!n.tickers.isEmpty()) md += QStringLiteral("- 相关股票：%1\n").arg(n.tickers.join(", ").toUpper());
    if (!n.tags.isEmpty()) md += QStringLiteral("- 标签：%1\n").arg(n.tags.join(", "));
    md += QStringLiteral("- 更新：%1\n\n").arg(n.updated.toLocalTime().toString("yyyy-MM-dd HH:mm"));
    return md + n.content + "\n";
}
bool load_notes(const QString& path, QVector<Note>& out, QString& error) {
    QFile file(path);
    if (!file.exists()) { out.clear(); return true; }
    if (!file.open(QIODevice::ReadOnly)) { error = QStringLiteral("无法读取笔记文件：") + file.errorString(); return false; }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || !doc.object().value("notes").isArray()) { error = QStringLiteral("笔记文件格式无效，未加载（原文件保留）"); return false; }
    QVector<Note> notes;
    for (const auto v : doc.object().value("notes").toArray()) {
        const auto o = v.toObject();
        Note n;
        n.id = o.value("id").toString(); n.title = o.value("title").toString(); n.content = o.value("content").toString();
        n.category = note_categories().contains(o.value("category").toString()) ? o.value("category").toString() : QStringLiteral("other");
        n.sentiment = o.value("sentiment").toString(); if (n.sentiment != "bullish" && n.sentiment != "bearish") n.sentiment = "neutral";
        for (const auto t : o.value("tickers").toArray()) { const auto s = normalize_symbol(t.toString()); if (!s.isEmpty()) n.tickers << s; }
        for (const auto t : o.value("tags").toArray()) if (!t.toString().trimmed().isEmpty()) n.tags << t.toString().trimmed();
        n.favorite = o.value("favorite").toBool();
        n.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
        n.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODate);
        if (!n.id.isEmpty()) notes.append(n);
    }
    out = notes; return true;
}
bool save_notes(const QString& path, const QVector<Note>& notes, QString& error) {
    QJsonArray list;
    for (const auto& n : notes)
        list.append(QJsonObject{{"id", n.id}, {"title", n.title}, {"content", n.content}, {"category", n.category},
                                {"sentiment", n.sentiment}, {"tickers", QJsonArray::fromStringList(n.tickers)},
                                {"tags", QJsonArray::fromStringList(n.tags)}, {"favorite", n.favorite},
                                {"created", n.created.toString(Qt::ISODate)}, {"updated", n.updated.toString(Qt::ISODate)}});
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = QStringLiteral("无法写入笔记文件：") + file.errorString(); return false; }
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"notes", list}}).toJson());
    if (!file.commit()) { error = QStringLiteral("保存笔记失败：") + file.errorString(); return false; }
    return true;
}
} // namespace fincept::services::ashare
