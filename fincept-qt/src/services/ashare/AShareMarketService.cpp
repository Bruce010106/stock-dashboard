#include "services/ashare/AShareMarketService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace fincept::services::ashare {
namespace {
double number(const QJsonValue& value) {
    return value.isString() ? value.toString().toDouble() : value.toDouble();
}
QJsonObject stock_data(const QJsonObject& root, const QString& symbol) {
    return root.value("data").toObject().value(symbol).toObject();
}
// a-stock-data v3.10.1: Tencent STAR (688/689) historical volume is shares, others lots.
bool star_board(const QString& symbol) { return symbol.startsWith("sh688") || symbol.startsWith("sh689"); }
QString tencent_period(KPeriod period) {
    switch (period) {
    case KPeriod::Day: return "day";
    case KPeriod::Week: return "week";
    case KPeriod::Month: return "month";
    case KPeriod::Min5: return "m5";
    case KPeriod::Min15: return "m15";
    case KPeriod::Min30: return "m30";
    case KPeriod::Min60: return "m60";
    }
    return "day";
}
}

bool is_minute_period(KPeriod period) {
    return period == KPeriod::Min5 || period == KPeriod::Min15 || period == KPeriod::Min30 || period == KPeriod::Min60;
}
QString period_name(KPeriod period) {
    switch (period) {
    case KPeriod::Day: return QStringLiteral("日 K");
    case KPeriod::Week: return QStringLiteral("周 K");
    case KPeriod::Month: return QStringLiteral("月 K");
    case KPeriod::Min5: return QStringLiteral("5 分钟 K");
    case KPeriod::Min15: return QStringLiteral("15 分钟 K");
    case KPeriod::Min30: return QStringLiteral("30 分钟 K");
    case KPeriod::Min60: return QStringLiteral("60 分钟 K");
    }
    return {};
}
Adjust effective_adjust(const QString& symbol, KPeriod period, Adjust requested) {
    return is_index(symbol) || is_minute_period(period) ? Adjust::None : requested;
}
QString kline_key(const QString& symbol, KPeriod period, Adjust requested) {
    return symbol + "|" + tencent_period(period) +
           (effective_adjust(symbol, period, requested) == Adjust::Forward ? "|qfq" : "|raw");
}
QString eastmoney_secid(const QString& symbol) {
    if (normalize_symbol(symbol) != symbol) return {};
    return (symbol.startsWith("sh") ? "1." : "0.") + symbol.mid(2);
}

QString normalize_symbol(QString input) {
    input = input.trimmed().toLower();
    const QRegularExpression suffix("^(\\d{6})\\.(sh|sz|bj)$");
    auto match = suffix.match(input);
    if (match.hasMatch()) input = match.captured(2) + match.captured(1);
    if (QRegularExpression("^\\d{6}$").match(input).hasMatch()) {
        const auto prefix = input.startsWith('6') ? "sh" :
                            (input.startsWith('0') || input.startsWith('3')) ? "sz" : "bj";
        input.prepend(prefix);
    }
    // A-share securities only: reject ambiguous ETFs, B-shares, indexes and malformed input.
    return QRegularExpression("^(sh6\\d{5}|sz[03]\\d{5}|bj[489]\\d{5})$").match(input).hasMatch()
               ? input : QString();
}
QVector<StockMatch> index_catalog() {
    return {{"sh000001", QStringLiteral("上证指数")}, {"sz399001", QStringLiteral("深证成指")},
        {"sz399006", QStringLiteral("创业板指")}, {"sh000300", QStringLiteral("沪深300")},
        {"sh000016", QStringLiteral("上证50")}, {"sh000905", QStringLiteral("中证500")},
        {"sh000852", QStringLiteral("中证1000")}, {"sh000688", QStringLiteral("科创50")},
        {"sz399005", QStringLiteral("中小100")}};
}
bool is_index(const QString& symbol) {
    const auto catalog = index_catalog();
    return std::any_of(catalog.begin(), catalog.end(), [&](const StockMatch& item) { return item.symbol == symbol; });
}

int session_minute(const QString& hhmm) {
    const auto time = QTime::fromString(hhmm, "hhmm");
    if (!time.isValid()) return -1;
    if (time >= QTime(9, 30) && time <= QTime(11, 30)) return QTime(9, 30).secsTo(time) / 60;
    if (time >= QTime(13, 0) && time <= QTime(15, 0)) return 121 + QTime(13, 0).secsTo(time) / 60;
    return -1;
}

bool is_market_session(const QDateTime& now) {
    const auto local = now.toTimeZone(QTimeZone("Asia/Shanghai"));
    if (local.date().dayOfWeek() > 5) return false;
    const auto time = local.time();
    // Holiday determination comes from the returned trading date, not the weekday alone.
    return (time >= QTime(9, 25) && time <= QTime(11, 31)) ||
           (time >= QTime(13, 0) && time <= QTime(15, 1));
}

bool parse_quote(const QJsonObject& root, const QString& symbol, Quote& out, QString& error) {
    const auto data = stock_data(root, symbol);
    const auto values = data.value("qt").toObject().value(symbol).toArray();
    if (root.value("code").toInt(-1) != 0 || values.size() < 38) {
        error = QStringLiteral("行情源未返回该股票，请检查代码或稍后重试");
        return false;
    }
    Quote quote;
    quote.symbol = symbol;
    quote.name = values[1].toString();
    quote.last = number(values[3]);
    quote.previous = number(values[4]);
    quote.open = number(values[5]);
    quote.high = number(values[33]);
    quote.low = number(values[34]);
    quote.amount = number(values[37]) * 10000; // Tencent quote turnover is 10,000 CNY
    // Snapshot volume is lots on most boards but shares on STAR; confirm against turnover
    // (2026-10-15 live check: sh688981 field 6 matched Sina's share count).
    const double raw_volume = number(values[6]);
    double unit = star_board(symbol) ? 1 : 100;
    if (raw_volume > 0 && quote.amount > 0 && number(values[3]) > 0)
        unit = quote.amount / (raw_volume * number(values[3])) > 10 ? 100 : 1;
    quote.volume = raw_volume * unit;
    quote.outer = number(values[7]) * unit;
    quote.inner = number(values[8]) * unit;
    // Order book volumes are lots on every board (cross-checked with Sina share counts).
    for (int level = 0; level < 5; ++level) {
        quote.bids.append({number(values[9 + level * 2]), number(values[10 + level * 2]) * 100});
        quote.asks.append({number(values[19 + level * 2]), number(values[20 + level * 2]) * 100});
    }
    if (values.size() > 49) {
        quote.turnover_rate = number(values[38]); quote.pe_ttm = number(values[39]);
        quote.amplitude = number(values[43]);
        quote.float_cap = number(values[44]) * 1e8; quote.total_cap = number(values[45]) * 1e8; // 100 million CNY
        quote.pb = number(values[46]); quote.limit_up = number(values[47]); quote.limit_down = number(values[48]);
        quote.volume_ratio = number(values[49]);
    }
    quote.time = QDateTime(QDate::fromString(values[30].toString().left(8), "yyyyMMdd"),
                          QTime::fromString(values[30].toString().mid(8), "hhmmss"), QTimeZone("Asia/Shanghai"));
    if (!quote.time.isValid() || quote.previous <= 0 || quote.last <= 0 || !std::isfinite(quote.last)) {
        error = QStringLiteral("行情价格或时间无效，保留上次数据");
        return false;
    }
    const auto series = data.value("data").toObject();
    quote.date = QDate::fromString(series.value("date").toString(), "yyyyMMdd");
    QMap<int, QStringList> sorted;
    // Do not join yesterday's intraday data to today's quote (common before opening).
    if (quote.date == quote.time.date()) {
        for (const auto value : series.value("data").toArray()) {
            const auto fields = value.toString().split(' ', Qt::SkipEmptyParts);
            if (fields.size() < 3) continue;
            const int minute = session_minute(fields[0]);
            if (minute >= 0) sorted[minute] = fields;
        }
    }
    double previous_volume = 0;
    for (auto it = sorted.cbegin(); it != sorted.cend(); ++it) {
        const auto& fields = it.value();
        const double price = fields[1].toDouble();
        const double cumulative = fields[2].toDouble() * unit;
        if (price <= 0 || cumulative < previous_volume || !std::isfinite(price)) continue;
        const double turnover = fields.size() > 3 ? fields[3].toDouble() : 0;
        // Index points cannot be inferred by dividing constituent turnover by shares.
        const double average = !is_index(symbol) && cumulative > 0 && turnover > 0 ? turnover / cumulative : 0;
        quote.minutes.push_back({fields[0].left(2) + ":" + fields[0].mid(2), it.key(), price,
                                 average, cumulative - previous_volume});
        previous_volume = cumulative;
    }
    out = quote;
    return true;
}

bool parse_kline(const QJsonObject& root, const QString& symbol, KPeriod period, Adjust adjust,
                 QVector<Candle>& out, QString& error) {
    const auto data = stock_data(root, symbol);
    const bool minute = is_minute_period(period);
    const bool forward = effective_adjust(symbol, period, adjust) == Adjust::Forward;
    // Never silently substitute one adjustment basis for another.
    const auto rows = data.value((forward ? "qfq" : "") + tencent_period(period)).toArray();
    if (root.value("code").toInt(-1) != 0 || rows.isEmpty()) {
        if (is_index(symbol)) error = QStringLiteral("指数%1暂不可用").arg(period_name(period));
        else if (minute) error = QStringLiteral("%1线暂不可用").arg(period_name(period));
        else if (forward && data.contains(tencent_period(period)))
            error = QStringLiteral("数据源未返回前复权%1（该股可能没有除权记录）；未替换为其他口径，可切换「不复权」查看")
                        .arg(period_name(period));
        else error = forward ? QStringLiteral("前复权%1暂不可用；未替换为其他复权口径").arg(period_name(period))
                             : QStringLiteral("不复权%1暂不可用").arg(period_name(period));
        return false;
    }
    const double unit = star_board(symbol) ? 1 : 100;
    QMap<QDateTime, Candle> sorted;
    for (auto value : rows) {
        auto row = value.toArray();
        if (row.size() < 6) continue;
        Candle candle;
        if (minute) {
            const auto stamp = QDateTime::fromString(row[0].toString(), "yyyyMMddhhmm");
            candle.date = stamp.date(); candle.time = stamp.time();
        } else candle.date = QDate::fromString(row[0].toString(), "yyyy-MM-dd");
        candle.open = number(row[1]); candle.close = number(row[2]);
        candle.high = number(row[3]); candle.low = number(row[4]); candle.volume = number(row[5]) * unit;
        if (!candle.date.isValid() || (minute && !candle.time.isValid()) || candle.low <= 0 ||
            candle.high < std::max(candle.open, candle.close) ||
            candle.low > std::min(candle.open, candle.close) || candle.volume < 0) continue;
        sorted[QDateTime(candle.date, minute ? candle.time : QTime(0, 0))] = candle;
    }
    if (sorted.isEmpty()) { error = QStringLiteral("%1数据格式无效").arg(period_name(period)); return false; }
    out = sorted.values().toVector();
    return true;
}
bool parse_candles(const QJsonObject& root, const QString& symbol, QVector<Candle>& out, QString& error) {
    return parse_kline(root, symbol, KPeriod::Day, Adjust::Forward, out, error);
}

bool parse_trades(const QByteArray& body, QVector<Trade>& out, QString& error) {
    QJsonParseError json_error;
    const auto doc = QJsonDocument::fromJson(body, &json_error);
    if (!doc.isObject() || json_error.error != QJsonParseError::NoError) {
        error = QStringLiteral("分笔接口返回非 JSON 数据"); return false;
    }
    const auto data = doc.object().value("data");
    if (doc.object().value("rc").toInt(-1) != 0 || !data.isObject() || !data.toObject().value("details").isArray()) {
        error = QStringLiteral("分笔接口未返回该股票数据"); return false;
    }
    QVector<Trade> trades;
    for (const auto value : data.toObject().value("details").toArray()) {
        // time, price, volume (lots on every board, incl. STAR), trade count, direction (2 buy / 1 sell)
        const auto fields = value.toString().split(',');
        if (fields.size() < 5) { error = QStringLiteral("分笔数据字段不完整"); return false; }
        Trade trade{fields[0], fields[1].toDouble(), fields[2].toDouble() * 100,
                    fields[4] == "2" ? 1 : fields[4] == "1" ? -1 : 0};
        if (!QTime::fromString(trade.time, "hh:mm:ss").isValid() || trade.price <= 0 || trade.volume < 0) continue;
        trades.append(trade);
    }
    out = trades; return true;
}

AShareMarketService::AShareMarketService(QObject* parent) : QObject(parent), network_(this) {}
bool parse_stock_search(const QByteArray& body, QVector<StockMatch>& matches, QString& error) {
    const auto match = QRegularExpression("^\\s*v_hint\\s*=\\s*(\"[^\"]*\")\\s*;?\\s*$").match(QString::fromUtf8(body));
    if (!match.hasMatch()) { error = QStringLiteral("搜索源返回格式异常，请稍后重试"); return false; }
    // Decode Unicode escapes as JSON, never execute the provider's JavaScript.
    const auto doc = QJsonDocument::fromJson(("[" + match.captured(1) + "]").toUtf8());
    if (!doc.isArray() || doc.array().size() != 1 || !doc.array()[0].isString()) {
        error = QStringLiteral("搜索结果无法解析"); return false;
    }
    QVector<StockMatch> result; QSet<QString> seen;
    const auto text = doc.array()[0].toString();
    if (text != "N" && !text.isEmpty()) for (const auto& row : text.split('^')) {
        const auto fields = row.split('~');
        if (fields.size() < 5) { error = QStringLiteral("搜索结果字段不完整"); return false; }
        const auto symbol = normalize_symbol(fields[0] + fields[1]);
        if (fields[4] != "GP-A" || symbol.isEmpty() || fields[2].isEmpty() || seen.contains(symbol)) continue;
        result.append({symbol, fields[2]}); seen.insert(symbol);
    }
    matches = result; return true;
}
void AShareMarketService::search_stocks(const QString& query) {
    const auto generation = ++search_generation_;
    QUrl url("https://smartbox.gtimg.cn/s3/"); QUrlQuery params;
    params.addQueryItem("q", query); params.addQueryItem("t", "gp"); url.setQuery(params);
    QNetworkRequest req(url); req.setTransferTimeout(8000);
    req.setRawHeader("Referer", "https://gu.qq.com/");
    auto* reply = network_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query, generation]() {
        QVector<StockMatch> matches; QString error;
        if (reply->error() != QNetworkReply::NoError) error = QStringLiteral("搜索连接失败，请重试或直接输入股票代码");
        else parse_stock_search(reply->readAll(), matches, error);
        reply->deleteLater();
        if (generation == search_generation_) emit search_ready(query, matches, error);
    });
}
void AShareMarketService::fetch_quote(const QString& symbol) {
    tencent_get(symbol, "quote", "/appstock/app/minute/query", "code", symbol,
        [this, symbol](const QJsonObject& root, QString& error) {
            Quote quote;
            if (!parse_quote(root, symbol, quote, error)) return false;
            emit quote_ready(quote); return true;
        });
}
void AShareMarketService::fetch_daily(const QString& symbol) { fetch_kline(symbol, KPeriod::Day, Adjust::Forward); }
void AShareMarketService::fetch_kline(const QString& symbol, KPeriod period, Adjust adjust) {
    const auto key = kline_key(symbol, period, adjust);
    const bool forward = effective_adjust(symbol, period, adjust) == Adjust::Forward;
    const bool minute = is_minute_period(period);
    const int count = period == KPeriod::Day ? 400 : period == KPeriod::Month ? 240 : 320;
    const auto param = minute ? QStringLiteral("%1,%2,,%3").arg(symbol, tencent_period(period)).arg(count)
                              : QStringLiteral("%1,%2,,,%3,%4").arg(symbol, tencent_period(period)).arg(count)
                                    .arg(forward ? QStringLiteral("qfq") : QString());
    tencent_get(symbol, key, minute ? "/appstock/app/kline/mkline" : "/appstock/app/fqkline/get", "param", param,
        [this, symbol, period, adjust, key](const QJsonObject& root, QString& error) {
            QVector<Candle> candles;
            if (!parse_kline(root, symbol, period, adjust, candles, error)) return false;
            emit kline_ready(key, symbol, candles);
            if (key == kline_key(symbol, KPeriod::Day, Adjust::Forward)) emit daily_ready(symbol, candles);
            return true;
        });
}

void AShareMarketService::fetch_trades(const QString& symbol) {
    const auto secid = eastmoney_secid(symbol);
    if (secid.isEmpty() || pending_.contains("trades" + symbol)) return;
    pending_.insert("trades" + symbol);
    // Eastmoney public tick snapshots (~3 s aggregation), not exchange tick-by-tick data.
    QUrl url("https://push2.eastmoney.com/api/qt/stock/details/get"); QUrlQuery query;
    query.addQueryItem("secid", secid); query.addQueryItem("fields1", "f1,f2,f3,f4");
    query.addQueryItem("fields2", "f51,f52,f53,f54,f55"); query.addQueryItem("pos", "-40");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 AShareBoard/0.3");
    request.setRawHeader("Referer", "https://quote.eastmoney.com/");
    request.setTransferTimeout(10000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        pending_.remove("trades" + symbol);
        const auto body = reply->readAll(); const auto network_error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        QVector<Trade> trades; QString error;
        if (status == 403 || status == 429)
            error = QStringLiteral("东方财富拒绝了分笔请求（HTTP %1），可能被限流；稍后会自动重试").arg(status);
        else if (network_error != QNetworkReply::NoError)
            error = status ? QStringLiteral("分笔数据请求失败（HTTP %1）").arg(status) : QStringLiteral("分笔数据连接失败或超时");
        else if (parse_trades(body, trades, error)) { emit trades_ready(symbol, trades); return; }
        emit failed(symbol, "trades", error);
    });
}

void AShareMarketService::tencent_get(const QString& symbol, const QString& kind, const QString& path,
                                      const QString& param_name, const QString& param, const Handler& handle, int attempt) {
    if (!is_index(symbol) && normalize_symbol(symbol) != symbol) return;
    const QString key = kind + symbol;
    if (attempt == 0) {
        if (pending_.contains(key)) return;
        pending_.insert(key);
    }
    // These are alternate gateways of ONE provider, not independent market sources.
    static const QStringList hosts{"https://web.ifzq.gtimg.cn", "https://proxy.finance.qq.com/ifzqgtimg", "https://ifzq.gtimg.cn"};
    QUrl url(hosts.at(attempt) + path);
    QUrlQuery query;
    query.addQueryItem(param_name, param);
    query.addQueryItem("_", QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "AShareBoard/0.3 (personal dashboard)");
    request.setRawHeader("Referer", "https://gu.qq.com/");
    request.setTransferTimeout(12000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [=, this]() {
        const auto body = reply->readAll();
        const auto network_error = reply->error();
        const auto network_message = reply->errorString();
        reply->deleteLater();
        // Transport failures are retried on the alternate gateways; data-level answers
        // (e.g. no qfq series for this stock) are reported as-is after the retries.
        auto fail = [=, this](const QString& error, bool transport = true) {
            if (attempt < 2) { tencent_get(symbol, kind, path, param_name, param, handle, attempt + 1); return; }
            pending_.remove(key);
            emit failed(symbol, kind, transport ? QStringLiteral("腾讯三个入口均未取得有效数据：") + error : error);
        };
        if (network_error != QNetworkReply::NoError) { fail(network_message); return; }
        QJsonParseError json_error;
        const auto doc = QJsonDocument::fromJson(body, &json_error);
        if (!doc.isObject() || json_error.error != QJsonParseError::NoError) {
            fail(QStringLiteral("行情源返回非 JSON 数据")); return;
        }
        QString error;
        // Clear before emitting so a slot may immediately re-request the same data.
        pending_.remove(key);
        if (!handle(doc.object(), error)) { pending_.insert(key); fail(error, false); }
    });
}
} // namespace fincept::services::ashare
