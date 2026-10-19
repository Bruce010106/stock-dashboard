#include "services/ashare/AShareProfileService.h"

#include "services/ashare/AShareMarketService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fincept::services::ashare {
namespace {
constexpr double kMissing = std::numeric_limits<double>::quiet_NaN();
bool object_of(const QByteArray& body, QJsonObject& root, QString& error) {
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        error = QStringLiteral("返回非 JSON 内容，可能受到限流");
        return false;
    }
    root = doc.object();
    return true;
}
QString text(const QJsonObject& row, const char* key) {
    const auto value = row.value(key);
    const auto result = value.isDouble() ? QString::number(value.toDouble()) : value.toString().trimmed();
    return result == "None" ? QString() : result;
}
double metric(const QJsonObject& row, const char* key) {
    const auto value = row.value(key);
    return value.isDouble() ? value.toDouble() : kMissing;
}
QString day(const QString& stamp) {
    return stamp.left(10);
}
} // namespace

QString eastmoney_f10_code(const QString& symbol) {
    return normalize_symbol(symbol) == symbol ? symbol.left(2).toUpper() + symbol.mid(2) : QString();
}

bool parse_profile_survey(const QByteArray& body, StockProfile& profile, QString& error) {
    QJsonObject root;
    if (!object_of(body, root, error))
        return false;
    const auto rows = root.value("jbzl").toArray();
    if (rows.isEmpty() || !rows[0].isObject()) {
        error = QStringLiteral("公司概况缺少基本资料");
        return false;
    }
    const auto row = rows[0].toObject();
    profile.name = text(row, "SECURITY_NAME_ABBR");
    profile.full_name = text(row, "ORG_NAME");
    profile.industry = text(row, "EM2016");
    profile.csrc_industry = text(row, "INDUSTRYCSRC1");
    profile.board = text(row, "SECURITY_TYPE");
    profile.province = text(row, "PROVINCE");
    profile.chairman = text(row, "CHAIRMAN");
    profile.employees = text(row, "EMP_NUM");
    profile.intro = text(row, "ORG_PROFILE");
    profile.scope = text(row, "BUSINESS_SCOPE");
    if (profile.name.isEmpty()) {
        error = QStringLiteral("公司概况缺少证券简称");
        return false;
    }
    return true;
}

bool parse_profile_concepts(const QByteArray& body, StockProfile& profile, QString& error) {
    QJsonObject root;
    if (!object_of(body, root, error))
        return false;
    if (!root.value("ssbk").isArray()) {
        error = QStringLiteral("概念板块缺少数据");
        return false;
    }
    QStringList names;
    for (const auto value : root.value("ssbk").toArray()) {
        auto name = text(value.toObject(), "BOARD_NAME");
        while (name.endsWith('_'))
            name.chop(1); // Eastmoney marks some index boards with a trailing "_"
        if (!name.isEmpty() && !names.contains(name))
            names.append(name);
    }
    profile.concepts = names;
    return true;
}

bool parse_profile_business(const QByteArray& body, StockProfile& profile, QString& error) {
    QJsonObject root;
    if (!object_of(body, root, error))
        return false;
    if (!root.value("zygcfx").isArray()) {
        error = QStringLiteral("主营构成缺少数据");
        return false;
    }
    QString latest;
    for (const auto value : root.value("zygcfx").toArray())
        latest = std::max(latest, text(value.toObject(), "REPORT_DATE"));
    // MAINOP_TYPE: 1 by industry, 2 by product, 3 by region.
    QMap<QString, BusinessBreakdown> groups;
    for (const auto value : root.value("zygcfx").toArray()) {
        const auto row = value.toObject();
        if (text(row, "REPORT_DATE") != latest)
            continue;
        const auto type = text(row, "MAINOP_TYPE");
        BusinessItem item{text(row, "ITEM_NAME"), metric(row, "MAIN_BUSINESS_INCOME"), metric(row, "MBI_RATIO"),
                          metric(row, "GROSS_RPOFIT_RATIO"), false};
        item.has_margin = std::isfinite(item.gross_margin);
        if (item.name.isEmpty() || !std::isfinite(item.income))
            continue;
        auto& group = groups[type];
        group.report_date = day(latest);
        group.kind = type == "1"   ? QStringLiteral("按行业")
                     : type == "2" ? QStringLiteral("按产品")
                                   : QStringLiteral("按地区");
        group.items.append(item);
    }
    profile.business.clear();
    for (const auto* type : {"2", "1", "3"})
        if (groups.contains(type)) {
            auto group = groups[type];
            std::sort(group.items.begin(), group.items.end(),
                      [](const auto& a, const auto& b) { return a.income > b.income; });
            profile.business.append(group);
        }
    return true;
}

bool parse_profile_holders(const QByteArray& body, StockProfile& profile, QString& error) {
    QJsonObject root;
    if (!object_of(body, root, error))
        return false;
    if (!root.value("sdgd").isArray() || !root.value("gdrs").isArray()) {
        error = QStringLiteral("股东数据缺少字段");
        return false;
    }
    profile.holders.clear();
    profile.holders_date.clear();
    for (const auto value : root.value("sdgd").toArray()) {
        const auto row = value.toObject();
        const auto date = day(text(row, "END_DATE"));
        if (profile.holders_date.isEmpty())
            profile.holders_date = date;
        if (date != profile.holders_date)
            continue;
        profile.holders.append({row.value("HOLDER_RANK").toInt(), text(row, "HOLDER_NAME"),
                                text(row, "HOLD_NUM_CHANGE"), metric(row, "HOLD_NUM"), metric(row, "HOLD_NUM_RATIO")});
    }
    profile.holder_counts.clear();
    for (const auto value : root.value("gdrs").toArray()) {
        const auto row = value.toObject();
        HolderCount count{day(text(row, "END_DATE")), metric(row, "HOLDER_TOTAL_NUM"), metric(row, "TOTAL_NUM_RATIO"),
                          false};
        count.has_change = std::isfinite(count.change_ratio);
        if (!count.date.isEmpty() && std::isfinite(count.holders))
            profile.holder_counts.append(count);
    }
    return true;
}

bool parse_profile_finance(const QByteArray& body, StockProfile& profile, QString& error) {
    QJsonObject root;
    if (!object_of(body, root, error))
        return false;
    if (!root.value("data").isArray()) {
        error = QStringLiteral("财务指标缺少数据");
        return false;
    }
    profile.finance.clear();
    for (const auto value : root.value("data").toArray()) {
        const auto row = value.toObject();
        FinanceRow f{text(row, "REPORT_DATE_NAME"),
                     metric(row, "TOTALOPERATEREVE"),
                     metric(row, "TOTALOPERATEREVETZ"),
                     metric(row, "PARENTNETPROFIT"),
                     metric(row, "PARENTNETPROFITTZ"),
                     metric(row, "ROEJQ"),
                     metric(row, "XSMLL"),
                     metric(row, "ZCFZL"),
                     metric(row, "EPSJB")};
        if (!f.period.isEmpty())
            profile.finance.append(f);
    }
    return true;
}

AShareProfileService::AShareProfileService(QObject* parent) : QObject(parent), network_(this) {}

void AShareProfileService::fetch(const QString& symbol, bool force) {
    if (eastmoney_f10_code(symbol).isEmpty())
        return;
    const auto now = QDateTime::currentDateTimeUtc();
    const bool cached = cache_.contains(symbol) && cache_[symbol].fetched.secsTo(now) < 1800;
    if (cached && (!force || attempted_.value(symbol).secsTo(now) < 60)) {
        emit ready(symbol, cache_[symbol]);
        return;
    }
    if (!running_.isEmpty()) {
        if (running_ != symbol)
            QTimer::singleShot(1200, this, [this, symbol, force]() { fetch(symbol, force); });
        return;
    }
    if (!cached && attempted_.contains(symbol) && attempted_[symbol].secsTo(now) < 60) {
        emit failed(symbol, QStringLiteral("个股资料每分钟最多请求一次，请稍后重试"));
        return;
    }
    running_ = symbol;
    attempted_[symbol] = now;
    StockProfile profile;
    profile.symbol = symbol;
    step(symbol, 0, profile);
}

void AShareProfileService::step(const QString& symbol, int section, StockProfile profile) {
    static const QStringList paths{"CompanySurvey/PageAjax", "CoreConception/PageAjax", "BusinessAnalysis/PageAjax",
                                   "ShareholderResearch/PageAjax", "NewFinanceAnalysis/ZYZBAjaxNew"};
    if (section >= paths.size()) {
        running_.clear();
        profile.fetched = QDateTime::currentDateTimeUtc();
        if (profile.empty())
            emit failed(symbol, QStringLiteral("东方财富 F10 各项均未取得数据：") + profile.errors.join("；"));
        else {
            cache_[symbol] = profile;
            emit ready(symbol, profile);
        }
        return;
    }
    QUrl url("https://emweb.securities.eastmoney.com/PC_HSF10/" + paths[section]);
    url.setQuery((section == 4 ? "type=0&code=" : "code=") + eastmoney_f10_code(symbol));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 AShareBoard/0.3");
    request.setRawHeader("Referer", "https://emweb.securities.eastmoney.com/");
    request.setTransferTimeout(12000);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol, section, profile]() mutable {
        const auto body = reply->readAll();
        const auto network_error = reply->error();
        reply->deleteLater();
        QString error;
        if (network_error != QNetworkReply::NoError)
            error = QStringLiteral("网络请求失败");
        else {
            using Parser = bool (*)(const QByteArray&, StockProfile&, QString&);
            static const Parser parsers[] = {parse_profile_survey, parse_profile_concepts, parse_profile_business,
                                             parse_profile_holders, parse_profile_finance};
            parsers[section](body, profile, error);
        }
        static const QStringList labels{QStringLiteral("公司概况"), QStringLiteral("概念板块"),
                                        QStringLiteral("主营构成"), QStringLiteral("十大股东"),
                                        QStringLiteral("财务指标")};
        if (!error.isEmpty())
            profile.errors.append(labels[section] + "：" + error);
        // Space out the five F10 requests instead of bursting the provider.
        QTimer::singleShot(400, this, [this, symbol, section, profile]() { step(symbol, section + 1, profile); });
    });
}
} // namespace fincept::services::ashare
