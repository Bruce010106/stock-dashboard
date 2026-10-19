#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTime>
#include <QVector>

#include <functional>

namespace fincept::services::ashare {
struct StockMatch {
    QString symbol{}, name{};
};
QVector<StockMatch> index_catalog();
bool is_index(const QString& symbol);
bool parse_stock_search(const QByteArray& body, QVector<StockMatch>& matches, QString& error);

struct MinutePoint {
    QString time{};
    int session_minute = 0;
    double price = 0, average = 0, volume = 0; // volume in shares, not lots
};

struct BookLevel {
    double price = 0, volume = 0;
}; // volume in shares; price 0 = empty level

struct Quote {
    QString symbol{}, name{};
    QDate date{};
    QDateTime time{};
    double last = 0, previous = 0, open = 0, high = 0, low = 0;
    double volume = 0, amount = 0;
    // Extended Tencent snapshot fields; 0 when the source omits them (e.g. indexes).
    double turnover_rate = 0, pe_ttm = 0, pb = 0, amplitude = 0, volume_ratio = 0;
    double float_cap = 0, total_cap = 0; // CNY
    double limit_up = 0, limit_down = 0;
    double outer = 0, inner = 0;       // active buy / sell volume, shares
    QVector<BookLevel> bids{}, asks{}; // best level first
    QVector<MinutePoint> minutes{};
    double change_percent() const { return previous > 0 ? (last / previous - 1) * 100 : 0; }
};

struct Candle {
    QDate date{};
    double open = 0, close = 0, high = 0, low = 0, volume = 0;
    QTime time{}; // bar end time for minute bars; invalid for day/week/month
};

enum class KPeriod { Day, Week, Month, Min5, Min15, Min30, Min60 };
enum class Adjust { Forward, None };
bool is_minute_period(KPeriod period);
QString period_name(KPeriod period);
// Tencent minute bars and indexes have no adjusted series; report what is actually shown.
Adjust effective_adjust(const QString& symbol, KPeriod period, Adjust requested);
QString kline_key(const QString& symbol, KPeriod period, Adjust requested);

struct Trade {
    QString time{};
    double price = 0, volume = 0; // volume in shares
    int side = 0;                 // 1 active buy, -1 active sell, 0 neutral/unknown
};

// Pure parsing functions are also used by the fixture tests.
QString normalize_symbol(QString input);
QString eastmoney_secid(const QString& symbol);
int session_minute(const QString& hhmm);
bool parse_quote(const QJsonObject& root, const QString& symbol, Quote& out, QString& error);
bool parse_kline(const QJsonObject& root, const QString& symbol, KPeriod period, Adjust adjust, QVector<Candle>& out,
                 QString& error);
bool parse_candles(const QJsonObject& root, const QString& symbol, QVector<Candle>& out, QString& error);
bool parse_trades(const QByteArray& body, QVector<Trade>& out, QString& error);
bool is_market_session(const QDateTime& now);

// Native async adapter. Public Tencent endpoints are best-effort, with no latency SLA.
// Kept separate from widgets so a licensed provider can be substituted later.
class AShareMarketService : public QObject {
    Q_OBJECT
  public:
    explicit AShareMarketService(QObject* parent = nullptr);
    void fetch_quote(const QString& symbol);
    void fetch_daily(const QString& symbol);
    void fetch_kline(const QString& symbol, KPeriod period, Adjust adjust);
    void fetch_trades(const QString& symbol);
    void search_stocks(const QString& query);

  signals:
    void search_ready(const QString& query, const QVector<fincept::services::ashare::StockMatch>& matches,
                      const QString& error);
    void quote_ready(const fincept::services::ashare::Quote& quote);
    void daily_ready(const QString& symbol, const QVector<fincept::services::ashare::Candle>& candles);
    void kline_ready(const QString& key, const QString& symbol,
                     const QVector<fincept::services::ashare::Candle>& candles);
    void trades_ready(const QString& symbol, const QVector<fincept::services::ashare::Trade>& trades);
    // kind: "quote", "trades", or a kline_key() for K-line requests.
    void failed(const QString& symbol, const QString& kind, const QString& error);

  private:
    using Handler = std::function<bool(const QJsonObject&, QString&)>;
    void tencent_get(const QString& symbol, const QString& kind, const QString& path, const QString& param_name,
                     const QString& param, const Handler& handle, int attempt = 0);
    QNetworkAccessManager network_;
    QSet<QString> pending_;
    quint64 search_generation_ = 0;
};

} // namespace fincept::services::ashare
