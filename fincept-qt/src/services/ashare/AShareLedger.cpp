#include "services/ashare/AShareLedger.h"

#include "services/ashare/AShareMarketService.h"
#include "services/ashare/AShareStorage.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace fincept::services::ashare {
QString ledger_path() {
    return data_file("portfolio.json");
}

bool validate_txn(const LedgerTxn& t, QString& error) {
    if (normalize_symbol(t.symbol) != t.symbol) {
        error = QStringLiteral("股票代码无效（仅支持沪深京 A 股）");
        return false;
    }
    if (!t.date.isValid()) {
        error = QStringLiteral("日期无效");
        return false;
    }
    if (t.type != "BUY" && t.type != "SELL" && t.type != "DIVIDEND") {
        error = QStringLiteral("类型无效");
        return false;
    }
    if (t.type != "DIVIDEND" && (!(t.quantity > 0) || !std::isfinite(t.quantity))) {
        error = QStringLiteral("数量必须大于 0");
        return false;
    }
    if (!(t.price > 0) || !std::isfinite(t.price)) {
        error = t.type == "DIVIDEND" ? QStringLiteral("分红金额必须大于 0") : QStringLiteral("价格必须大于 0");
        return false;
    }
    if (t.fee < 0 || !std::isfinite(t.fee)) {
        error = QStringLiteral("费用不能为负");
        return false;
    }
    return true;
}

LedgerResult compute_ledger(QVector<LedgerTxn> txns) {
    // Chronological, stable for same-day entries (entry order is kept).
    std::stable_sort(txns.begin(), txns.end(), [](const LedgerTxn& a, const LedgerTxn& b) { return a.date < b.date; });
    LedgerResult r;
    QHash<QString, Position> book;
    QStringList order;
    for (const auto& t : txns) {
        if (!book.contains(t.symbol)) {
            Position fresh;
            fresh.symbol = t.symbol;
            fresh.name = t.name;
            book[t.symbol] = fresh;
            order << t.symbol;
        }
        auto& p = book[t.symbol];
        if (!t.name.isEmpty())
            p.name = t.name;
        r.fees += t.fee;
        if (t.type == "BUY") {
            if (p.quantity <= 0) {
                p.first_date = t.date;
                p.cost = 0;
                p.quantity = 0;
            }
            p.quantity += t.quantity;
            p.cost += t.quantity * t.price + t.fee;
        } else if (t.type == "SELL") {
            double qty = t.quantity;
            if (qty > p.quantity + 1e-9) {
                r.warnings << QStringLiteral("%1 在 %2 卖出 %3 股，超过当时持仓 %4 股，已按持仓数量计算")
                                  .arg(t.symbol.toUpper(), t.date.toString(Qt::ISODate))
                                  .arg(qty)
                                  .arg(p.quantity);
                qty = p.quantity;
            }
            if (qty <= 0)
                continue;
            const double avg = p.avg_cost();
            const double gain = qty * t.price - t.fee - qty * avg;
            p.realized += gain;
            r.realized += gain;
            p.cost -= qty * avg;
            p.quantity -= qty;
            if (p.quantity < 1e-9) {
                p.quantity = 0;
                p.cost = 0;
            }
        } else { // DIVIDEND: cash received, net of withholding entered as fee
            const double cash = t.price - t.fee;
            p.dividends += cash;
            r.dividends += cash;
        }
    }
    for (const auto& symbol : order)
        (book[symbol].quantity > 0 ? r.open : r.closed).append(book[symbol]);
    return r;
}

bool load_ledger(const QString& path, QVector<LedgerTxn>& out, QString& error) {
    QFile file(path);
    if (!file.exists()) {
        out.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取持仓文件：") + file.errorString();
        return false;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || !doc.object().value("transactions").isArray()) {
        error = QStringLiteral("持仓文件格式无效，未加载（原文件保留）");
        return false;
    }
    QVector<LedgerTxn> txns;
    for (const auto v : doc.object().value("transactions").toArray()) {
        const auto o = v.toObject();
        LedgerTxn t{o.value("id").toString(),       o.value("symbol").toString(),
                    o.value("name").toString(),     o.value("type").toString(),
                    o.value("note").toString(),     QDate::fromString(o.value("date").toString(), Qt::ISODate),
                    o.value("quantity").toDouble(), o.value("price").toDouble(),
                    o.value("fee").toDouble()};
        QString why;
        if (validate_txn(t, why))
            txns.append(t);
    }
    out = txns;
    return true;
}

bool save_ledger(const QString& path, const QVector<LedgerTxn>& txns, QString& error) {
    QJsonArray list;
    for (const auto& t : txns)
        list.append(QJsonObject{{"id", t.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : t.id},
                                {"symbol", t.symbol},
                                {"name", t.name},
                                {"type", t.type},
                                {"date", t.date.toString(Qt::ISODate)},
                                {"quantity", t.quantity},
                                {"price", t.price},
                                {"fee", t.fee},
                                {"note", t.note}});
    // QSaveFile writes to a temp file and renames, so a crash never leaves a half-written ledger.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("无法写入持仓文件：") + file.errorString();
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"transactions", list}}).toJson());
    if (!file.commit()) {
        error = QStringLiteral("保存持仓文件失败：") + file.errorString();
        return false;
    }
    return true;
}
} // namespace fincept::services::ashare
