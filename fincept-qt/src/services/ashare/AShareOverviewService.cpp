#include "services/ashare/AShareOverviewService.h"

#include "services/ashare/AShareMarketService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace fincept::services::ashare {
namespace {
const char* kUserAgent = "AShareBoard/0.3 (personal dashboard)";
const char* kEastmoneyToken = "7eea3edcaed734bea9cbfc24409ed989"; // public page token used by quote.eastmoney.com
double num(const QJsonValue& v) {
    return v.isString() ? v.toString().toDouble() : v.toDouble();
}
bool json_object(const QByteArray& body, QJsonObject& root, QString& error) {
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        error = QStringLiteral("返回非 JSON 内容，可能被限流或接口已变更");
        return false;
    }
    root = doc.object();
    return true;
}
// push2ex times are integers like 92500 / 145435 (HHMMSS without leading zero).
QString clock(const QJsonValue& v) {
    const int t = int(num(v));
    if (t <= 0)
        return {};
    return QStringLiteral("%1:%2:%3")
        .arg(t / 10000, 2, 10, QChar('0'))
        .arg(t / 100 % 100, 2, 10, QChar('0'))
        .arg(t % 100, 2, 10, QChar('0'));
}
QDate qdate(const QJsonValue& v) {
    return QDate::fromString(QString::number(qint64(num(v))), "yyyyMMdd");
}
} // namespace

bool parse_breadth(const QByteArray& body, Breadth& out, QString& error) {
    QJsonObject root;
    if (!json_object(body, root, error))
        return false;
    const auto data = root.value("data").toObject();
    if (root.value("rc").toInt(-1) != 0 || !data.value("fenbu").isArray()) {
        error = QStringLiteral("涨跌分布接口未返回数据");
        return false;
    }
    Breadth b;
    b.date = qdate(data.value("qdate"));
    QMap<int, int> buckets;
    for (const auto item : data.value("fenbu").toArray()) {
        const auto obj = item.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            const int key = it.key().toInt(&ok);
            if (!ok || key < -11 || key > 11)
                continue;
            buckets[key] += int(num(it.value()));
        }
    }
    if (buckets.isEmpty()) {
        error = QStringLiteral("涨跌分布为空（非交易日或尚未开盘）");
        return false;
    }
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        b.buckets.append({it.key(), it.value()});
        if (it.key() > 0)
            b.up += it.value();
        else if (it.key() < 0)
            b.down += it.value();
        else
            b.flat += it.value();
    }
    b.limit_up = buckets.value(11);
    b.limit_down = buckets.value(-11);
    out = b;
    return true;
}

bool parse_sectors(const QByteArray& body, QVector<Sector>& out, QString& error) {
    QJsonObject root;
    if (!json_object(body, root, error))
        return false;
    const auto list = root.value("data").toObject().value("rank_list");
    if (root.value("code").toInt(-1) != 0 || !list.isArray()) {
        error = QStringLiteral("板块排行接口未返回数据");
        return false;
    }
    QVector<Sector> sectors;
    for (const auto item : list.toArray()) {
        const auto r = item.toObject();
        Sector s;
        s.code = r.value("code").toString();
        s.name = r.value("name").toString();
        s.change = num(r.value("zdf"));
        s.main_net = num(r.value("zljlr")) * 1e4;
        s.turnover = num(r.value("turnover")) * 1e4; // 万元
        const auto counts = r.value("zgb").toString().split('/');
        if (counts.size() == 2) {
            s.up = counts[0].toInt();
            s.total = counts[1].toInt();
        }
        const auto leader = r.value("lzg").toObject();
        s.leader_code = normalize_symbol(leader.value("code").toString());
        s.leader_name = leader.value("name").toString();
        s.leader_change = num(leader.value("zdf"));
        if (!s.code.isEmpty() && !s.name.isEmpty())
            sectors.append(s);
    }
    out = sectors;
    return true;
}

bool parse_board_stocks(const QByteArray& body, QVector<BoardStock>& out, QString& error) {
    QJsonObject root;
    if (!json_object(body, root, error))
        return false;
    const auto list = root.value("data").toObject().value("rank_list");
    if (root.value("code").toInt(-1) != 0 || !list.isArray()) {
        error = QStringLiteral("板块成分股接口未返回数据");
        return false;
    }
    QVector<BoardStock> stocks;
    for (const auto item : list.toArray()) {
        const auto r = item.toObject();
        BoardStock s{normalize_symbol(r.value("code").toString()),
                     r.value("name").toString(),
                     num(r.value("zxj")),
                     num(r.value("zdf")),
                     num(r.value("hsl")),
                     num(r.value("turnover")) * 1e4};
        if (!s.symbol.isEmpty() && s.price > 0)
            stocks.append(s);
    }
    out = stocks;
    return true;
}

bool parse_limit_pool(const QByteArray& body, const QString& kind, LimitPool& out, QString& error) {
    QJsonObject root;
    if (!json_object(body, root, error))
        return false;
    const auto data = root.value("data");
    if (root.value("rc").toInt(-1) != 0 || !data.isObject() || !data.toObject().value("pool").isArray()) {
        error = QStringLiteral("该交易日暂无%1数据（非交易日或接口未更新）")
                    .arg(kind == "zt"   ? QStringLiteral("涨停池")
                         : kind == "zb" ? QStringLiteral("炸板池")
                                        : QStringLiteral("跌停池"));
        return false;
    }
    LimitPool pool;
    pool.date = qdate(data.toObject().value("qdate"));
    pool.total = data.toObject().value("tc").toInt();
    for (const auto item : data.toObject().value("pool").toArray()) {
        const auto r = item.toObject();
        PoolStock s;
        s.symbol = normalize_symbol(r.value("c").toString());
        s.name = r.value("n").toString();
        s.price = num(r.value("p")) / 1000;
        s.change = num(r.value("zdp"));
        s.amount = num(r.value("amount"));
        s.seal_fund = num(r.value("fund"));
        s.turnover_rate = num(r.value("hs"));
        s.industry = r.value("hybk").toString();
        s.first_time = clock(r.value("fbt"));
        s.last_time = clock(r.value("lbt"));
        s.boards = kind == "dt" ? int(num(r.value("days"))) : int(num(r.value("lbc")));
        s.breaks = kind == "dt" ? int(num(r.value("oc"))) : int(num(r.value("zbc")));
        if (!s.symbol.isEmpty() && s.price > 0)
            pool.rows.append(s);
    }
    pool.total = std::max<int>(pool.total, pool.rows.size());
    out = pool;
    return true;
}

bool parse_billboard(const QByteArray& body, QVector<BillboardRow>& out, QString& error) {
    QJsonObject root;
    if (!json_object(body, root, error))
        return false;
    const auto result = root.value("result");
    if (!result.isObject() || !result.toObject().value("data").isArray()) {
        error = QStringLiteral("龙虎榜接口未返回数据");
        return false;
    }
    QVector<BillboardRow> rows;
    QHash<QString, int> index;
    QDate latest;
    for (const auto item : result.toObject().value("data").toArray())
        latest =
            std::max(latest, QDate::fromString(item.toObject().value("TRADE_DATE").toString().left(10), "yyyy-MM-dd"));
    for (const auto item : result.toObject().value("data").toArray()) {
        const auto r = item.toObject();
        const auto date = QDate::fromString(r.value("TRADE_DATE").toString().left(10), "yyyy-MM-dd");
        if (date != latest)
            continue; // only the most recent published trading day
        const auto symbol = normalize_symbol(r.value("SECURITY_CODE").toString());
        const auto reason = r.value("EXPLANATION").toString();
        if (symbol.isEmpty())
            continue;
        // One stock can be listed for several reasons; show it once with the reasons joined.
        if (index.contains(symbol)) {
            auto& row = rows[index[symbol]];
            if (!row.reason.contains(reason))
                row.reason += QStringLiteral("；") + reason;
            continue;
        }
        index[symbol] = rows.size();
        rows.append({symbol, r.value("SECURITY_NAME_ABBR").toString(), reason, date, num(r.value("CLOSE_PRICE")),
                     num(r.value("CHANGE_RATE")), num(r.value("BILLBOARD_NET_AMT")), num(r.value("BILLBOARD_BUY_AMT")),
                     num(r.value("BILLBOARD_SELL_AMT"))});
    }
    out = rows;
    return true;
}

AShareOverviewService::AShareOverviewService(QObject* parent) : QObject(parent), network_(this) {}

void AShareOverviewService::enqueue(const QString& key, const QUrl& url, const QByteArray& referer,
                                    std::function<void(const QByteArray&)> done) {
    if (queued_.contains(key))
        return;
    queued_.insert(key);
    queue_.enqueue({key, url, referer, std::move(done)});
    pump();
}
void AShareOverviewService::pump() {
    if (busy_ || queue_.isEmpty())
        return;
    busy_ = true;
    const auto job = queue_.dequeue();
    QNetworkRequest request(job.url);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Referer", job.referer);
    request.setTransferTimeout(12000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
        const auto body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        reply->deleteLater();
        queued_.remove(job.key);
        const auto kind = job.key.section(':', 0, 0);
        if (status == 403 || status == 429)
            emit failed(kind, QStringLiteral("数据源拒绝了请求（HTTP %1），可能被限流；稍后自动重试").arg(status));
        else if (error != QNetworkReply::NoError)
            emit failed(kind, status ? QStringLiteral("请求失败（HTTP %1）").arg(status)
                                     : QStringLiteral("网络连接失败或超时"));
        else
            job.done(body);
        // Serialise and space out requests instead of bursting several providers at once.
        QTimer::singleShot(300, this, [this]() {
            busy_ = false;
            pump();
        });
    });
}

void AShareOverviewService::fetch_breadth() {
    QUrl url("https://push2ex.eastmoney.com/getTopicZDFenBu");
    QUrlQuery q;
    q.addQueryItem("ut", kEastmoneyToken);
    q.addQueryItem("dpt", "wz.ztzt");
    url.setQuery(q);
    enqueue("breadth", url, "https://quote.eastmoney.com/", [this](const QByteArray& body) {
        Breadth b;
        QString error;
        if (parse_breadth(body, b, error))
            emit breadth_ready(b);
        else
            emit failed("breadth", error);
    });
}

void AShareOverviewService::fetch_sectors(const QString& board_type) {
    if (board_type != "hy" && board_type != "hy2" && board_type != "gn")
        return;
    auto rank_url = [board_type](const QString& direct, int count) { // by value: also used from a later callback
        QUrl url("https://proxy.finance.qq.com/cgi/cgi-bin/rank/pt/getRank");
        QUrlQuery q;
        q.addQueryItem("board_type", board_type);
        q.addQueryItem("sort_type", "priceRatio");
        q.addQueryItem("direct", direct);
        q.addQueryItem("offset", "0");
        q.addQueryItem("count", QString::number(count));
        url.setQuery(q);
        return url;
    };
    if (board_type != "gn") {
        enqueue("sectors:" + board_type, rank_url("down", 200), "https://gu.qq.com/",
                [this, board_type](const QByteArray& body) {
                    QVector<Sector> sectors;
                    QString error;
                    if (parse_sectors(body, sectors, error))
                        emit sectors_ready(board_type, sectors);
                    else
                        emit failed("sectors", error);
                });
        return;
    }
    // 800+ concept boards: show the 40 strongest and 40 weakest instead of all of them.
    enqueue("sectors:gn-top", rank_url("down", 40), "https://gu.qq.com/", [this, rank_url](const QByteArray& top_body) {
        QVector<Sector> top;
        QString error;
        if (!parse_sectors(top_body, top, error)) {
            emit failed("sectors", error);
            return;
        }
        enqueue(
            "sectors:gn-bottom", rank_url("up", 40), "https://gu.qq.com/", [this, top](const QByteArray& bottom_body) {
                QVector<Sector> bottom, merged = top;
                QString error;
                if (!parse_sectors(bottom_body, bottom, error)) {
                    emit failed("sectors", error);
                    return;
                }
                std::reverse(bottom.begin(), bottom.end());
                for (const auto& s : bottom)
                    if (std::none_of(merged.begin(), merged.end(), [&](const Sector& m) { return m.code == s.code; }))
                        merged.append(s);
                emit sectors_ready("gn", merged);
            });
    });
}

void AShareOverviewService::fetch_board_stocks(const QString& board_code) {
    if (!QRegularExpression("^pt[0-9A-Z]+$").match(board_code).hasMatch())
        return;
    QUrl url("https://proxy.finance.qq.com/cgi/cgi-bin/rank/hs/getBoardRankList");
    QUrlQuery q;
    q.addQueryItem("board_code", board_code);
    q.addQueryItem("sort_type", "priceRatio");
    q.addQueryItem("direct", "down");
    q.addQueryItem("offset", "0");
    q.addQueryItem("count", "40");
    url.setQuery(q);
    enqueue("board:" + board_code, url, "https://gu.qq.com/", [this, board_code](const QByteArray& body) {
        QVector<BoardStock> stocks;
        QString error;
        if (parse_board_stocks(body, stocks, error))
            emit board_stocks_ready(board_code, stocks);
        else
            emit failed("board", error);
    });
}

void AShareOverviewService::fetch_pool(const QString& kind) {
    static const QHash<QString, QPair<QString, QString>> endpoints{{"zt", {"getTopicZTPool", "fbt:asc"}},
                                                                   {"zb", {"getTopicZBPool", "fbt:asc"}},
                                                                   {"dt", {"getTopicDTPool", "fund:asc"}}};
    if (!endpoints.contains(kind))
        return;
    QUrl url("https://push2ex.eastmoney.com/" + endpoints[kind].first);
    QUrlQuery q;
    q.addQueryItem("ut", kEastmoneyToken);
    q.addQueryItem("dpt", "wz.ztzt");
    q.addQueryItem("Pageindex", "0");
    q.addQueryItem("pagesize", "300");
    q.addQueryItem("sort", endpoints[kind].second);
    q.addQueryItem("date",
                   QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date().toString("yyyyMMdd"));
    url.setQuery(q);
    enqueue("pool:" + kind, url, "https://quote.eastmoney.com/", [this, kind](const QByteArray& body) {
        LimitPool pool;
        QString error;
        if (parse_limit_pool(body, kind, pool, error))
            emit pool_ready(kind, pool);
        else
            emit failed("pool:" + kind, error);
    });
}

void AShareOverviewService::fetch_billboard() {
    QUrl url("https://datacenter-web.eastmoney.com/api/data/v1/get");
    QUrlQuery q;
    q.addQueryItem("reportName", "RPT_DAILYBILLBOARD_DETAILSNEW");
    q.addQueryItem("columns", "SECURITY_CODE,SECURITY_NAME_ABBR,TRADE_DATE,CLOSE_PRICE,CHANGE_RATE,BILLBOARD_NET_AMT,"
                              "BILLBOARD_BUY_AMT,BILLBOARD_SELL_AMT,EXPLANATION");
    q.addQueryItem("pageNumber", "1");
    q.addQueryItem("pageSize", "200");
    q.addQueryItem("sortColumns", "TRADE_DATE,BILLBOARD_NET_AMT");
    q.addQueryItem("sortTypes", "-1,-1");
    url.setQuery(q);
    enqueue("billboard", url, "https://data.eastmoney.com/", [this](const QByteArray& body) {
        QVector<BillboardRow> rows;
        QString error;
        if (parse_billboard(body, rows, error))
            emit billboard_ready(rows);
        else
            emit failed("billboard", error);
    });
}
} // namespace fincept::services::ashare
