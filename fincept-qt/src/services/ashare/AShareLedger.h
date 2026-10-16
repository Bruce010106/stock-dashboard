#pragma once
#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::services::ashare {
// Personal position ledger. Entity shapes follow Fincept's portfolio module
// (Transaction / HoldingWithQuote); calculations are re-implemented in C++ so the
// standalone board needs no Python. Cost basis uses the weighted-average method.
struct LedgerTxn {
    QString id, symbol, name, type, note; // type: BUY, SELL, DIVIDEND
    QDate date;
    double quantity = 0; // shares (BUY / SELL); unused for DIVIDEND
    double price = 0;    // per share; for DIVIDEND the total cash received
    double fee = 0;      // commission + stamp duty + transfer fee, entered by the user
};
struct Position {
    QString symbol, name;
    double quantity = 0, cost = 0; // cost = remaining cost basis including fees
    double realized = 0, dividends = 0;
    QDate first_date;
    double avg_cost() const { return quantity > 0 ? cost / quantity : 0; }
};
struct LedgerResult {
    QVector<Position> open;      // quantity > 0
    QVector<Position> closed;    // fully sold, kept for realised P&L
    double realized = 0, dividends = 0, fees = 0;
    QStringList warnings;
};
LedgerResult compute_ledger(QVector<LedgerTxn> txns);
bool validate_txn(const LedgerTxn& txn, QString& error);
bool load_ledger(const QString& path, QVector<LedgerTxn>& out, QString& error);
bool save_ledger(const QString& path, const QVector<LedgerTxn>& txns, QString& error);
QString ledger_path();
} // namespace fincept::services::ashare
