#include "screens/ashare/AShareScreen.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>
#include <QComboBox>
#include <QScreen>
#include "services/ashare/AShareResearchService.h"
#include "services/ashare/AShareProfileService.h"
#include <QTabWidget>
#include <functional>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("AShareBoard");
    app.setOrganizationName("AShareBoard");
#ifdef Q_OS_WIN
    // The offscreen Qt platform does not enumerate Windows fonts automatically.
    // Load installed system fonts without copying or redistributing font files.
    const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
    QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
    QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
#endif
    app.setFont(QFont("Microsoft YaHei UI", 10));
    const auto args = app.arguments();
    const bool research_smoke = args.contains("--research-smoke-output");
    const bool feature_smoke = args.contains("--feature-smoke-output");
    const int capture_index = args.indexOf(research_smoke ? "--research-smoke-output"
                                           : feature_smoke ? "--feature-smoke-output" : "--smoke-output");
    const QString output = capture_index >= 0 ? args.value(capture_index + 1) : QString();
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output + "/settings");
    }
    if (feature_smoke) {
        // Fresh isolated settings: one main-board, one STAR and one SZ stock.
        QSettings seed(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
        seed.clear();
        seed.setValue("symbols", QStringList{"sh600519", "sh688981", "sz000001"}); seed.setValue("selected", "sh600519");
    }
    fincept::screens::AShareScreen screen;
    screen.setWindowTitle(QStringLiteral("A 股观察台 · 个人版"));
    screen.resize(1320, 940);
    if (output.isEmpty() && app.primaryScreen()) {
        const auto available = app.primaryScreen()->availableGeometry().size() - QSize(40, 70);
        screen.resize(QSize(1320, 940).boundedTo(available).expandedTo(screen.minimumSize()));
    }
    // Smoke runs pin the theme: default dark; --smoke-light forces the light variant.
    if (!output.isEmpty()) screen.findChild<QComboBox*>("ashareTheme")->setCurrentIndex(args.contains("--smoke-light") ? 0 : 1);
    if (!output.isEmpty() && args.contains("--smoke-index")) screen.restore_state({{"selected", "sh000001"}});
    screen.show();
    bool captured = false;
    QJsonObject research_results;
    if (!output.isEmpty() && research_smoke) {
        using namespace fincept::services::ashare;
        auto* service = screen.findChild<AShareResearchService*>();
        auto* combo = screen.findChild<QComboBox*>("ashareResearchKind");
        auto record = [&, combo](const QString& kind, bool ok, const QString& error, int count) {
            research_results[kind] = QJsonObject{{"ok", ok}, {"error", error}, {"count", count}};
            QTimer::singleShot(150, &app, [&, combo, kind]() {
                screen.grab().save(output + "/" + kind + ".png");
                if (combo->currentIndex() < 3) combo->setCurrentIndex(combo->currentIndex() + 1);
                else {
                    QFile file(output + "/research-smoke.json"); file.open(QIODevice::WriteOnly);
                    file.write(QJsonDocument(research_results).toJson()); file.close();
                    app.exit(research_results["reports"].toObject()["ok"].toBool() && research_results["lives"].toObject()["ok"].toBool() ? 0 : 4);
                }
            });
        };
        QObject::connect(service, &AShareResearchService::ready, &app, [record](const QString&, const QString& kind, const ResearchFeed& feed) {
            record(kind, true, {}, feed.items.size());
        });
        QObject::connect(service, &AShareResearchService::failed, &app, [record](const QString&, const QString& kind, const QString& error) {
            record(kind, false, error, 0);
        });
        QTimer::singleShot(500, combo, [combo]() { combo->setCurrentIndex(1); });
        QTimer::singleShot(60000, &app, [&]() { app.exit(3); });
    }
    // Live tier-one feature walk-through: book/ticks, periods + indicators, F10, STAR units, dark theme.
    QJsonObject feature;
    int feature_step = 0;
    QDateTime step_started = QDateTime::currentDateTimeUtc();
    bool profile_done = false, profile_ok = false;
    QJsonObject profile_result;
    if (!output.isEmpty() && feature_smoke) {
        auto* profiles = screen.findChild<fincept::services::ashare::AShareProfileService*>();
        QObject::connect(profiles, &fincept::services::ashare::AShareProfileService::ready, &app,
            [&](const QString&, const fincept::services::ashare::StockProfile& p) {
                profile_done = true; profile_ok = !p.name.isEmpty() && !p.finance.isEmpty();
                profile_result = QJsonObject{{"name", p.name}, {"concepts", p.concepts.size()}, {"business_groups", p.business.size()},
                    {"holders", p.holders.size()}, {"holder_counts", p.holder_counts.size()}, {"finance_rows", p.finance.size()},
                    {"errors", QJsonArray::fromStringList(p.errors)}};
            });
        QObject::connect(profiles, &fincept::services::ashare::AShareProfileService::failed, &app, [&](const QString&, const QString& e) {
            profile_done = true; profile_result = QJsonObject{{"error", e}};
        });
        auto* poll = new QTimer(&app); poll->setInterval(300);
        auto snap = [&](const QString& name, const QString& key) {
            screen.grab().save(output + "/" + name + ".png");
            auto d = QJsonObject::fromVariantMap(screen.diagnostics()); d["image"] = name + ".png"; feature[key] = d;
        };
        auto set_combo = [&](const char* name, int index) { screen.findChild<QComboBox*>(name)->setCurrentIndex(index); };
        auto finish = [&, poll]() {
            poll->stop();
            const auto book = feature["intraday_book"].toObject();
            const auto star = feature["star_intraday"].toObject();
            const double ratio = star["last"].toDouble() > 0 ? star["last_average"].toDouble() / star["last"].toDouble() : 0;
            QJsonObject checks{
                {"book_levels", book["book_levels"].toInt() >= 8},
                {"trades_or_reported", book["trades"].toInt() > 0 || !book["trades_error"].toString().isEmpty()},
                {"week_candles", feature["week_macd"].toObject()["candles"].toInt() >= 60},
                {"m15_candles", feature["m15_kdj"].toObject()["candles"].toInt() >= 60},
                {"day_candles", feature["day_boll_rsi"].toObject()["candles"].toInt() >= 120},
                {"profile", profile_ok},
                {"star_average_plausible", ratio > 0.9 && ratio < 1.1}};
            bool ok = true; for (const auto& v : checks) ok = ok && v.toBool();
            feature["checks"] = checks; feature["profile_detail"] = profile_result; feature["ok"] = ok;
            QFile file(output + "/feature-smoke.json"); file.open(QIODevice::WriteOnly);
            file.write(QJsonDocument(feature).toJson()); file.close();
            app.exit(ok ? 0 : 5);
        };
        auto next = [&](int step) { feature_step = step; step_started = QDateTime::currentDateTimeUtc(); };
        QObject::connect(poll, &QTimer::timeout, &app, [&, snap, set_combo, finish, next]() {
            const auto d = screen.diagnostics();
            const bool waited = step_started.msecsTo(QDateTime::currentDateTimeUtc()) > 1500;
            const bool timed_out = step_started.secsTo(QDateTime::currentDateTimeUtc()) > 25;
            switch (feature_step) {
            case 0: // intraday + five levels + ticks
                if ((d["has_quote"].toBool() && screen.has_daily() &&
                     (d["trades"].toInt() > 0 || !d["trades_error"].toString().isEmpty()) && waited) || timed_out) {
                    snap("1-intraday-book", "intraday_book"); set_combo("ashareOscillator", 0); screen.select_period(2); next(1);
                }
                break;
            case 1: // weekly qfq + MACD
                if ((d["candles"].toInt() > 0 && waited) || timed_out) {
                    snap("2-week-macd", "week_macd"); set_combo("ashareOscillator", 1); screen.select_period(5); next(2);
                }
                break;
            case 2: // 15-minute + KDJ
                if ((d["candles"].toInt() > 0 && waited) || timed_out) {
                    snap("3-m15-kdj", "m15_kdj"); set_combo("ashareOverlay", 1); set_combo("ashareOscillator", 2);
                    screen.select_period(1); next(3);
                }
                break;
            case 3: // daily + BOLL + RSI, then open F10
                if ((d["candles"].toInt() > 0 && waited) || timed_out) {
                    snap("4-day-boll-rsi", "day_boll_rsi");
                    screen.findChild<QTabWidget*>("ashareInfoTabs")->setCurrentIndex(1); next(4);
                }
                break;
            case 4: // F10 profile
                if ((profile_done && waited) || timed_out) {
                    snap("5-profile", "profile");
                    screen.findChild<QWidget*>("ashareProfilePanel")->grab().save(output + "/5-profile-panel.png");
                    screen.restore_state({{"selected", "sh688981"}, {"period", 0}}); next(5);
                }
                break;
            case 5: // STAR intraday: average-price units
                if ((d["has_quote"].toBool() && d["last_average"].toDouble() > 0 && waited) || timed_out) {
                    snap("6-star-intraday", "star_intraday"); set_combo("ashareTheme", 0); next(6);
                }
                break;
            case 6:
                if (waited) { snap("7-light", "light"); finish(); }
                break;
            }
        });
        poll->start();
        QTimer::singleShot(240000, &app, [&]() { app.exit(3); });
    }
    if (!output.isEmpty() && !research_smoke && !feature_smoke) {
        QObject::connect(&screen, &fincept::screens::AShareScreen::data_loaded, &app, [&]() {
            if (captured || !screen.has_quote() || !screen.has_daily()) return;
            captured = true;
            QTimer::singleShot(250, &app, [&]() {
                const bool intraday_ok = screen.grab().save(output + "/intraday.png");
                screen.select_daily(true);
                QTimer::singleShot(250, &app, [&, intraday_ok]() {
                    const bool daily_ok = screen.grab().save(output + "/daily.png");
                    QFile report(output + "/smoke.json"); report.open(QIODevice::WriteOnly);
                    report.write(QJsonDocument(QJsonObject{{"live_quote", screen.has_quote()},
                        {"live_daily", screen.has_daily()}, {"intraday_image", intraday_ok}, {"daily_image", daily_ok}}).toJson());
                    report.close(); app.exit(intraday_ok && daily_ok ? 0 : 2);
                });
            });
        });
        QTimer::singleShot(30000, &app, [&]() {
            screen.grab().save(output + "/timeout.png"); app.exit(3);
        });
    }
    return app.exec();
}
