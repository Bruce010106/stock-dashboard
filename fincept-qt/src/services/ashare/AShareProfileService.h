#pragma once
#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace fincept::services::ashare {
struct BusinessItem { QString name; double income = 0, ratio = 0, gross_margin = 0; bool has_margin = false; };
struct BusinessBreakdown { QString report_date, kind; QVector<BusinessItem> items; };
struct Holder { int rank = 0; QString name, change; double shares = 0, ratio = 0; };
struct HolderCount { QString date; double holders = 0, change_ratio = 0; bool has_change = false; };
struct FinanceRow {
    QString period;
    // NaN when the provider omits a metric (e.g. gross margin for banks).
    double revenue, revenue_yoy, net_profit, net_profit_yoy, roe, gross_margin, debt_ratio, eps;
};
struct StockProfile {
    QString symbol, name, full_name, industry, csrc_industry, board, province, chairman, employees, intro, scope;
    QStringList concepts;
    QVector<BusinessBreakdown> business;
    QString holders_date;
    QVector<Holder> holders;
    QVector<HolderCount> holder_counts;
    QVector<FinanceRow> finance;
    QStringList errors;   // per-section failures; other sections remain usable
    QDateTime fetched;
    bool empty() const { return name.isEmpty() && concepts.isEmpty() && business.isEmpty() && holders.isEmpty() && finance.isEmpty(); }
};

// Eastmoney F10 page APIs (public, no SLA). Each parser fills only its own section.
QString eastmoney_f10_code(const QString& symbol);
bool parse_profile_survey(const QByteArray& body, StockProfile& profile, QString& error);
bool parse_profile_concepts(const QByteArray& body, StockProfile& profile, QString& error);
bool parse_profile_business(const QByteArray& body, StockProfile& profile, QString& error);
bool parse_profile_holders(const QByteArray& body, StockProfile& profile, QString& error);
bool parse_profile_finance(const QByteArray& body, StockProfile& profile, QString& error);

class AShareProfileService : public QObject {
    Q_OBJECT
  public:
    explicit AShareProfileService(QObject* parent = nullptr);
    // Serves a 30-minute cache; force bypasses it at most once per minute per symbol.
    void fetch(const QString& symbol, bool force = false);
  signals:
    void ready(const QString& symbol, const fincept::services::ashare::StockProfile& profile);
    void failed(const QString& symbol, const QString& error);
  private:
    void step(const QString& symbol, int section, StockProfile profile);
    QNetworkAccessManager network_;
    QHash<QString, StockProfile> cache_;
    QHash<QString, QDateTime> attempted_;
    QString running_;
};
} // namespace fincept::services::ashare
