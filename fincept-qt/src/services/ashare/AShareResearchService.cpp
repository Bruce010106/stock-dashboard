// Adapted from simonlin1212/a-stock-data (Apache-2.0), with native Qt validation,
// asynchronous transport and rate limiting. See ashare/THIRD_PARTY.md.
#include "AShareResearchService.h"
#include "AShareMarketService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

namespace fincept::services::ashare {
namespace {
QString plain(QString value) { value.remove(QRegularExpression("<[^>]*>")); return value; }
QUrl safe_url(const QString& value) {
    QUrl url(value);
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") ? url : QUrl();
}
}
bool parse_research(const QByteArray& body, const QString& kind, ResearchFeed& feed, QString& error) {
    QByteArray json = body.trimmed();
    if (kind == "news" && !json.startsWith('{')) {
        const int begin = json.indexOf('('), end = json.lastIndexOf(')');
        if (begin < 1 || end <= begin) { error = QStringLiteral("新闻源返回无效 JSONP"); return false; }
        json = json.mid(begin + 1, end - begin - 1);
    }
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) { error = QStringLiteral("数据源返回非 JSON 内容"); return false; }
    const auto root = doc.object();
    QJsonValue rows;
    ResearchFeed result;
    if (kind == "news") {
        if (root.value("code").toInt(-1) != 0) { error = QStringLiteral("新闻接口返回错误"); return false; }
        rows = root.value("result").toObject().value("cmsArticleWebOld");
        result.source = QStringLiteral("东方财富 · 个股关键词新闻");
    } else if (kind == "reports") {
        rows = root.value("data");
        result.source = QStringLiteral("东方财富 · 券商研报");
    } else if (kind == "lives") {
        if (root.value("code").toInt() != 20000) { error = QStringLiteral("快讯接口返回错误"); return false; }
        rows = root.value("data").toObject().value("items");
        result.source = QStringLiteral("华尔街见闻 · A 股频道（全市场）");
    } else { error = QStringLiteral("不支持的数据类型"); return false; }
    // Missing news arrays (e.g. passportWeb-only responses) are not 'no news'.
    if (!rows.isArray()) { error = QStringLiteral("接口缺少预期数据，可能受到限流或格式已变化；稍后重试"); return false; }
    for (const auto value : rows.toArray()) {
        if (!value.isObject()) { error = QStringLiteral("数据条目格式无效"); return false; }
        const auto row = value.toObject();
        ResearchItem item;
        item.title = plain(row.value("title").toString());
        if (kind == "news") {
            item.time = row.value("date").toString(); item.source = row.value("mediaName").toString();
            item.summary = plain(row.value("content").toString()).left(300); item.url = safe_url(row.value("url").toString());
        } else if (kind == "reports") {
            item.time = row.value("publishDate").toString().left(10); item.source = row.value("orgSName").toString();
            item.summary = row.value("emRatingName").toString();
            const auto code = row.value("infoCode").toString();
            if (QRegularExpression("^AP[0-9]+$").match(code).hasMatch())
                item.url = QUrl("https://data.eastmoney.com/report/zw_stock.jshtml?infocode=" + code);
        } else {
            const auto stamp = row.value("display_time");
            if (!stamp.isDouble() || stamp.toDouble() <= 0) { error = QStringLiteral("快讯时间无效"); return false; }
            item.time = QDateTime::fromSecsSinceEpoch(stamp.toInteger(), QTimeZone("Asia/Shanghai")).toString("yyyy-MM-dd HH:mm:ss");
            item.source = QStringLiteral("华尔街见闻"); item.summary = plain(row.value("content_text").toString());
            if (item.title.isEmpty()) { item.title = item.summary; item.summary.clear(); }
            item.url = safe_url(row.value("uri").toString());
        }
        if (item.title.isEmpty() || item.time.isEmpty()) { error = QStringLiteral("数据缺少标题或发布时间"); return false; }
        result.items.append(item);
    }
    result.fetched = QDateTime::currentDateTimeUtc(); feed = result; return true;
}
AShareResearchService::AShareResearchService(QObject* parent) : QObject(parent), network_(this) {}
void AShareResearchService::fetch(const QString& symbol, const QString& kind) {
    if (normalize_symbol(symbol) != symbol || (kind != "news" && kind != "reports" && kind != "lives")) return;
    const QString key = kind + (kind == "lives" ? QString() : symbol);
    const auto now = QDateTime::currentDateTimeUtc();
    if (cache_.contains(key) && cache_[key].fetched.secsTo(now) < 60) { emit ready(symbol, kind, cache_[key]); return; }
    if (pending_.contains(kind + symbol)) return;
    if (attempted_.contains(key) && attempted_[key].secsTo(now) < 60) {
        emit failed(symbol, kind, QStringLiteral("该接口每分钟最多刷新一次，请稍后重试")); return;
    }
    pending_.insert(kind + symbol); queue_.enqueue({symbol, kind, key}); pump();
}
void AShareResearchService::pump() {
    if (busy_ || queue_.isEmpty()) return;
    busy_ = true;
    const auto job = queue_.dequeue();
    attempted_[job.key] = QDateTime::currentDateTimeUtc();
    QUrl url; QUrlQuery query;
    if (job.kind == "reports") {
        url = QUrl("https://reportapi.eastmoney.com/report/list");
        query.addQueryItem("code", job.symbol.mid(2)); query.addQueryItem("pageSize", "20");
        query.addQueryItem("pageNo", "1"); query.addQueryItem("qType", "0");
        query.addQueryItem("beginTime", "2000-01-01"); query.addQueryItem("endTime", QDate::currentDate().toString(Qt::ISODate));
        for (const auto* field : {"industryCode", "industry", "rating", "ratingChange"}) query.addQueryItem(field, "*");
    } else if (job.kind == "news") {
        url = QUrl("https://search-api-web.eastmoney.com/search/jsonp");
        const QJsonObject options{{"searchScope", "default"}, {"sort", "default"}, {"pageIndex", 1}, {"pageSize", 20}, {"preTag", ""}, {"postTag", ""}};
        const QJsonObject params{{"uid", ""}, {"keyword", job.symbol.mid(2)}, {"type", QJsonArray{"cmsArticleWebOld"}},
            {"client", "web"}, {"clientType", "web"}, {"clientVersion", "curr"}, {"param", QJsonObject{{"cmsArticleWebOld", options}}}};
        query.addQueryItem("cb", "jQuery_news");
        query.addQueryItem("param", QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)))));
    } else {
        url = QUrl("https://api-one-wscn.awtmt.com/apiv1/content/lives");
        query.addQueryItem("channel", "a-stock-channel"); query.addQueryItem("limit", "20");
    }
    url.setQuery(query); QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 AShareBoard/0.2");
    request.setRawHeader("Referer", job.kind == "news" ? "https://so.eastmoney.com/" : "https://data.eastmoney.com/");
    request.setTransferTimeout(15000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
        ResearchFeed feed; QString error;
        const auto body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            error = status ? QStringLiteral("数据源请求失败（HTTP %1），请稍后重试").arg(status)
                           : QStringLiteral("网络连接失败或超时，请检查网络后重试");
        }
        else if (parse_research(body, job.kind, feed, error)) {
            cache_[job.key] = feed; emit ready(job.symbol, job.kind, feed);
        }
        if (!error.isEmpty()) emit failed(job.symbol, job.kind, error);
        pending_.remove(job.kind + job.symbol); reply->deleteLater();
        QTimer::singleShot(2000, this, [this]() { busy_ = false; pump(); });
    });
}
}
