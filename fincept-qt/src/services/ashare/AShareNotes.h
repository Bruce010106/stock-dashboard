#pragma once
#include <QDateTime>
#include <QStringList>
#include <QVector>

namespace fincept::services::ashare {
// Note model adapted from Fincept's FinancialNote (category, sentiment, tickers,
// tags, favourite); stored as local JSON instead of Fincept's SQLite repository.
struct Note {
    QString id, title, content;
    QString category = QStringLiteral("research"); // research, trade, review, idea, other
    QString sentiment = QStringLiteral("neutral");  // bullish, bearish, neutral
    QStringList tickers, tags;                       // tickers are normalized A-share symbols
    bool favorite = false;
    QDateTime created, updated;
};
QStringList note_categories();
QString note_category_label(const QString& id);
QString note_sentiment_label(const QString& id);
QStringList parse_tickers(const QString& text); // "600519 平安银行? sz000001" -> valid symbols only
bool note_matches(const Note& note, const QString& query, const QString& category, const QString& ticker, bool favorites_only);
QString note_to_markdown(const Note& note);
bool load_notes(const QString& path, QVector<Note>& out, QString& error);
bool save_notes(const QString& path, const QVector<Note>& notes, QString& error);
QString notes_path();
} // namespace fincept::services::ashare
