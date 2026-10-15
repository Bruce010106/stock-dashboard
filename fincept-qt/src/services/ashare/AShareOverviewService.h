#pragma once
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QUrl>
#include <QVector>
#include <functional>

namespace fincept::services::ashare {
// Market-wide ("看大盘") data. All sources are public web endpoints without an SLA.
// Tencent board ranks + constituents; Eastmoney push2ex limit pools / change
// distribution; Eastmoney datacenter dragon-tiger list. Requests use an honest
// User-Agent; when a provider refuses them the panel reports it instead of retrying aggressively.

struct Breadth {
    QDate date;
    int up = 0, down = 0, flat = 0, limit_up = 0, limit_down = 0;
    QVector<QPair<int, int>> buckets; // key -11..11 -> count; ±11 = limit, 0 = flat, k = (k-1, k]% band
    int total() const { return up + down + flat; }
};
struct Sector {
    QString code, name, leader_code, leader_name;
    double change = 0, leader_change = 0;
    double main_net = 0;   // CNY, main-force net inflow (provider definition)
    double turnover = 0;   // CNY
    int up = 0, total = 0;
};
struct BoardStock { QString symbol, name; double price = 0, change = 0, turnover_rate = 0, amount = 0; };
struct PoolStock {
    QString symbol, name, industry, first_time, last_time;
    double price = 0, change = 0, amount = 0, seal_fund = 0, turnover_rate = 0;
    int boards = 0, breaks = 0; // consecutive limit-ups / times the seal was broken
};
struct LimitPool { QDate date; int total = 0; QVector<PoolStock> rows; };
struct BillboardRow { QString symbol, name, reason; QDate date; double close = 0, change = 0, net = 0, buy = 0, sell = 0; };

bool parse_breadth(const QByteArray& body, Breadth& out, QString& error);
bool parse_sectors(const QByteArray& body, QVector<Sector>& out, QString& error);
bool parse_board_stocks(const QByteArray& body, QVector<BoardStock>& out, QString& error);
// kind: "zt" limit-up, "zb" broken limit-up, "dt" limit-down
bool parse_limit_pool(const QByteArray& body, const QString& kind, LimitPool& out, QString& error);
bool parse_billboard(const QByteArray& body, QVector<BillboardRow>& out, QString& error);

class AShareOverviewService : public QObject {
    Q_OBJECT
  public:
    explicit AShareOverviewService(QObject* parent = nullptr);
    void fetch_breadth();
    void fetch_sectors(const QString& board_type); // "hy" (SW level 1), "hy2", "gn" (top + bottom 40)
    void fetch_board_stocks(const QString& board_code);
    void fetch_pool(const QString& kind);
    void fetch_billboard();
  signals:
    void breadth_ready(const fincept::services::ashare::Breadth& breadth);
    void sectors_ready(const QString& board_type, const QVector<fincept::services::ashare::Sector>& sectors);
    void board_stocks_ready(const QString& board_code, const QVector<fincept::services::ashare::BoardStock>& stocks);
    void pool_ready(const QString& kind, const fincept::services::ashare::LimitPool& pool);
    void billboard_ready(const QVector<fincept::services::ashare::BillboardRow>& rows);
    void failed(const QString& kind, const QString& error);
  private:
    struct Job { QString key; QUrl url; QByteArray referer; std::function<void(const QByteArray&)> done; };
    void enqueue(const QString& key, const QUrl& url, const QByteArray& referer, std::function<void(const QByteArray&)> done);
    void pump();
    QNetworkAccessManager network_;
    QQueue<Job> queue_;
    QSet<QString> queued_;
    bool busy_ = false;
};
} // namespace fincept::services::ashare
