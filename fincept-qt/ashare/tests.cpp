#include "services/ashare/AShareMarketService.h"
#include "screens/ashare/AShareChart.h"
#include "screens/ashare/AShareScreen.h"
#include "screens/ashare/AShareResearchPanel.h"
#include "services/ashare/AShareResearchService.h"
#include "core/layout/LayoutTemplates.h"
#include "screens/ashare/AShareOrderBook.h"
#include "screens/ashare/AShareMarketPanel.h"
#include "services/ashare/AShareOverviewService.h"
#include <QLabel>
#include "screens/ashare/AShareTheme.h"
#include "screens/ashare/AShareProfilePanel.h"
#include "services/ashare/AShareIndicators.h"
#include "services/ashare/AShareProfileService.h"
#include <QHeaderView>
#include <QComboBox>
#include <QTextBrowser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <cmath>

using namespace fincept::services::ashare;
namespace {
QJsonObject fixture(const QString& date = "20261013") {
    QJsonArray q;
    for (int i = 0; i < 38; ++i) q.append("");
    q[1] = QStringLiteral("平安银行"); q[3] = "10.10"; q[4] = "10.00"; q[5] = "10.00";
    q[6] = "30"; q[30] = "20261013130100"; q[33] = "10.2"; q[34] = "9.9"; q[37] = "3.03";
    return {{"code", 0}, {"data", QJsonObject{{"sz000001", QJsonObject{
        {"qt", QJsonObject{{"sz000001", q}}},
        {"data", QJsonObject{{"date", date}, {"data", QJsonArray{
            "0931 10.10 20 20100", "0930 10.00 10 10000", "1200 10.00 21 21100", "1301 10.10 30 30200"}}}}}}}}};
}
}
class AShareTests : public QObject {
    Q_OBJECT
  private slots:
    void workbench_template_and_panel_state_roundtrip() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        auto workspace = fincept::layout::LayoutTemplates::make("ashare_research");
        QCOMPARE(workspace.frames.size(), 1);
        auto& panel = workspace.frames[0].panels[0];
        QCOMPARE(panel.type_id, "ashare");
        fincept::screens::AShareScreen source;
        source.restore_state({{"selected", "sh000001"}, {"daily", true}});
        panel.state_blob = QJsonDocument(QJsonObject::fromVariantMap(source.save_state())).toJson();
        auto restored_panel = fincept::layout::PanelState::from_json(panel.to_json());
        fincept::screens::AShareScreen restored;
        restored.restore_state(QJsonDocument::fromJson(restored_panel.state_blob).object().toVariantMap());
        QCOMPARE(restored.save_state(), source.save_state());
        QCOMPARE(restored.save_state()["selected"].toString(), "sh000001");
        QVERIFY(restored.save_state()["daily"].toBool());
    }
    void initTestCase() {
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
    }
    void symbols() {
        QCOMPARE(normalize_symbol("600519"), "sh600519");
        QCOMPARE(normalize_symbol("000001.SZ"), "sz000001");
        QCOMPARE(normalize_symbol("  BJ920001 "), "bj920001");
        QVERIFY(normalize_symbol("sh000001").isEmpty());
        QVERIFY(normalize_symbol("510300").isEmpty());
        QVERIFY(normalize_symbol("../600519").isEmpty());
    }
    void stock_search_parser() {
        QVector<StockMatch> matches; QString error;
        QVERIFY(parse_stock_search(R"(v_hint="sh~600519~\u8d35\u5dde\u8305\u53f0~gzmt~GP-A^hk~01833~Other~other~GP^sh~000001~Index~zs~ZS^sh~600519~Duplicate~gzmt~GP-A";)", matches, error));
        QCOMPARE(matches.size(), 1); QCOMPARE(matches[0].symbol, "sh600519");
        QCOMPARE(matches[0].name, QStringLiteral("贵州茅台"));
        QVERIFY(parse_stock_search("v_hint=\"N\";", matches, error)); QVERIFY(matches.isEmpty());
        QVERIFY(!parse_stock_search("<html>error</html>", matches, error));
    }
    void stock_search_selection_and_stale_result() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        fincept::screens::AShareScreen screen; screen.resize(1320,820); screen.show();
        auto* input = screen.findChild<QLineEdit*>("ashareSymbolInput");
        auto* results = screen.findChild<QListWidget*>("ashareSearchResults");
        auto* table = screen.findChild<QTableWidget*>("ashareWatchlist");
        auto* service = screen.findChild<AShareMarketService*>();
        input->setText(QStringLiteral("电工"));
        service->search_ready(QStringLiteral("茅台"), {{"sh600519",QStringLiteral("贵州茅台")}}, {});
        QCOMPARE(results->count(), 0);
        service->search_ready(QStringLiteral("电工"), {{"sz001359",QStringLiteral("平安电工")}}, {});
        QCOMPARE(results->count(), 1); QCOMPARE(table->rowCount(), 4);
        results->itemClicked(results->item(0));
        QCOMPARE(table->rowCount(), 5); QCOMPARE(screen.save_state()["selected"].toString(), "sz001359");
        QVERIFY(input->text().isEmpty()); QVERIFY(results->isHidden());
        service->search_ready(QStringLiteral("电工"), {{"sz001359",QStringLiteral("平安电工")}}, {});
        QCOMPARE(results->count(), 0);
    }
    void live_stock_search() {
        if (!qEnvironmentVariableIsSet("ASHARE_LIVE_SEARCH")) QSKIP("Opt-in network test");
        AShareMarketService service; bool done = false; QVector<StockMatch> result; QString error;
        connect(&service, &AShareMarketService::search_ready, this, [&](const QString&, const QVector<StockMatch>& matches, const QString& failure) {
            result = matches; error = failure; done = true;
        });
        service.search_stocks(QStringLiteral("茅台"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 12000); QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(std::any_of(result.begin(), result.end(), [](const StockMatch& s) { return s.symbol == "sh600519" && s.name == QStringLiteral("贵州茅台"); }));
    }
    void sessions() {
        QCOMPARE(session_minute("0930"), 0); QCOMPARE(session_minute("1130"), 120);
        QCOMPARE(session_minute("1300"), 121); QCOMPARE(session_minute("1500"), 241);
        QCOMPARE(session_minute("1200"), -1); QCOMPARE(session_minute("0900"), -1);
        QVERIFY(is_market_session(QDateTime(QDate(2026,10,13), QTime(2,0), QTimeZone::utc())));
        QVERIFY(!is_market_session(QDateTime(QDate(2026,10,13), QTime(4,0), QTimeZone::utc())));
        QVERIFY(!is_market_session(QDateTime(QDate(2026,10,11), QTime(2,0), QTimeZone::utc())));
    }
    void window_can_shrink_vertically_without_clipping_chart() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        fincept::screens::AShareScreen screen; screen.resize(1320,940); screen.show();
        auto* scroll = screen.findChild<QScrollArea*>("ashareBodyScroll"); QVERIFY(scroll);
        auto* chart = screen.findChild<fincept::screens::AShareChart*>(); QVERIFY(chart);
        screen.resize(1320,560); QCoreApplication::processEvents();
        QCOMPARE(screen.height(), 560); QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(chart->height() >= chart->minimumHeight());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        screen.resize(1320,1000); QCoreApplication::processEvents();
        QCOMPARE(screen.height(), 1000); QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
    }
    void theme_toggle_preserves_chart_and_settings() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        {
            fincept::screens::AShareScreen screen; screen.resize(1320,940); screen.show();
            auto* theme = screen.findChild<QComboBox*>("ashareTheme"); QVERIFY(theme);
            // Direction B: the dark terminal is the default.
            QCOMPARE(theme->currentIndex(), 1); QVERIFY(screen.property("darkTheme").toBool());
            screen.restore_state({{"selected", "sz000001"}});
            Quote q; QString error; QVERIFY(parse_quote(fixture(), "sz000001", q, error));
            screen.findChild<AShareMarketService*>()->quote_ready(q);
            auto* chart = screen.findChild<fincept::screens::AShareChart*>(); QVERIFY(chart);
            const auto state = screen.save_state();
            auto chart_bg = [chart]() { return chart->grab().toImage().pixelColor(10, 10); };
            QCOMPARE(chart_bg(), fincept::screens::theme::palette(true).chart_bg);
            // The chart follows the theme now, but selection and data are untouched.
            theme->setCurrentIndex(0); QVERIFY(!screen.property("darkTheme").toBool());
            QCOMPARE(chart_bg(), fincept::screens::theme::palette(false).chart_bg);
            QCOMPARE(screen.save_state(), state); QVERIFY(screen.has_quote());
            theme->setCurrentIndex(1); QCOMPARE(chart_bg(), fincept::screens::theme::palette(true).chart_bg);
            theme->setCurrentIndex(0);
        }
        fincept::screens::AShareScreen restored;
        QCOMPARE(restored.findChild<QComboBox*>("ashareTheme")->currentIndex(), 0); // explicit light choice kept
        QVERIFY(!restored.property("darkTheme").toBool());
    }
    void index_units_and_daily_contract() {
        QVERIFY(is_index("sh000001")); QVERIFY(is_index("sz399006")); QVERIFY(!is_index("sz000001"));
        auto root = fixture(); auto stock = root["data"].toObject()["sz000001"].toObject();
        stock["qt"] = QJsonObject{{"sh000001", stock["qt"].toObject()["sz000001"]}};
        root["data"] = QJsonObject{{"sh000001", stock}};
        Quote q; QString error; QVERIFY(parse_quote(root, "sh000001", q, error));
        QCOMPARE(q.minutes[0].average, 0.0); QCOMPARE(q.volume, 3000.0);
        const QJsonArray rows{QJsonArray{"2026-10-13", "3000", "3100", "3200", "2900", "1000"}};
        root["data"] = QJsonObject{{"sh000001", QJsonObject{{"day", rows}}}};
        QVector<Candle> candles; QVERIFY(parse_candles(root, "sh000001", candles, error));
        QCOMPARE(candles[0].close, 3100.0); QCOMPARE(candles[0].volume, 100000.0);
        root["data"] = QJsonObject{{"sh600519", QJsonObject{{"day", rows}}}};
        QVERIFY(!parse_candles(root, "sh600519", candles, error));
    }
    void index_watchlist_persistence() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        {
            fincept::screens::AShareScreen screen;
            auto* choice = screen.findChild<QComboBox*>("ashareIndexChoice");
            auto* add = screen.findChild<QPushButton*>("ashareAddIndex");
            auto* remove = screen.findChild<QPushButton*>("ashareRemoveIndex");
            QVERIFY(screen.findChild<QPushButton*>("index_sh000001"));
            choice->setCurrentIndex(choice->findData("sh000300")); add->click(); add->click();
            QCOMPARE(screen.findChildren<QPushButton*>("index_sh000300").size(), 1);
            QCOMPARE(screen.save_state()["selected"].toString(), "sh000300");
            QCOMPARE(screen.findChild<QTableWidget*>("ashareWatchlist")->rowCount(), 4);
            QCOMPARE(screen.findChild<QComboBox*>("ashareChartMode")->itemText(1), QStringLiteral("日 K · 指数点位"));
            remove->click(); QVERIFY(!screen.findChild<QPushButton*>("index_sh000300"));
            choice->setCurrentIndex(choice->findData("sh000688")); add->click();
        }
        fincept::screens::AShareScreen restored;
        QVERIFY(restored.findChild<QPushButton*>("index_sh000688"));
        QVERIFY(!restored.findChild<QPushButton*>("index_sh000300"));
        QCOMPARE(restored.save_state()["selected"].toString(), "sh000688");
    }
    void live_index_sources() {
        if (!qEnvironmentVariableIsSet("ASHARE_LIVE_INDEX")) QSKIP("Opt-in network test");
        AShareMarketService service; QSet<QString> quotes, daily; QStringList errors;
        connect(&service, &AShareMarketService::quote_ready, this, [&](const Quote& q) { quotes.insert(q.symbol); });
        connect(&service, &AShareMarketService::daily_ready, this, [&](const QString& s, const QVector<Candle>& c) { if (c.size() >= 20) daily.insert(s); });
        connect(&service, &AShareMarketService::failed, this, [&](const QString& s, const QString&, const QString& e) { errors.append(s + e); });
        for (const auto& item : index_catalog()) { service.fetch_quote(item.symbol); service.fetch_daily(item.symbol); }
        QTRY_VERIFY_WITH_TIMEOUT(quotes.size() == index_catalog().size() && daily.size() == index_catalog().size(), 40000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void quote_units_and_average() {
        Quote q; QString error;
        QVERIFY2(parse_quote(fixture(), "sz000001", q, error), qPrintable(error));
        QCOMPARE(q.name, QStringLiteral("平安银行")); QCOMPARE(q.minutes.size(), 3);
        QCOMPARE(q.minutes[0].time, "09:30"); QCOMPARE(q.minutes[1].volume, 1000.0);
        QCOMPARE(q.minutes[1].average, 10.05); QCOMPARE(q.volume, 3000.0);
        QCOMPARE(q.amount, 30300.0); QVERIFY(std::abs(q.change_percent() - 1) < 1e-9);
    }
    void stale_intraday_is_not_joined() {
        Quote q; QString error;
        QVERIFY(parse_quote(fixture("20261012"), "sz000001", q, error));
        QVERIFY(q.minutes.isEmpty());
    }
    void invalid_response() {
        Quote q; QString error; QVERIFY(!parse_quote({}, "sz000001", q, error)); QVERIFY(!error.isEmpty());
    }
    void candles_adjustment_and_sorting() {
        QJsonArray rows{QJsonArray{"2026-10-13","10","11","12","9","10"},
                        QJsonArray{"2026-10-12","9","10","11","8","20"}};
        QVector<Candle> candles; QString error;
        QJsonObject root{{"code",0}, {"data",QJsonObject{{"sz000001",QJsonObject{{"day",rows}}}}}};
        QVERIFY(!parse_candles(root, "sz000001", candles, error));
        root["data"] = QJsonObject{{"sz000001",QJsonObject{{"qfqday",rows}}}};
        QVERIFY(parse_candles(root, "sz000001", candles, error));
        QCOMPARE(candles[0].date, QDate(2026,10,12)); QCOMPARE(candles[0].volume, 2000.0);
    }
    void chart_flat_prices_and_single_candle() {
        fincept::screens::AShareChart chart; chart.resize(900,500);
        Quote q; QString error; QVERIFY(parse_quote(fixture(), "sz000001", q, error));
        for (auto& m : q.minutes) { m.price = 10; m.average = 10; }
        chart.set_quote(q); chart.show(); QVERIFY(!chart.grab().isNull());
        chart.set_daily(true); chart.set_candles({{QDate(2026,10,13),10,10,10,10,0}});
        QVERIFY(!chart.grab().isNull()); chart.clear("offline"); QVERIFY(!chart.grab().isNull());
    }
    void star_daily_volume_is_shares() {
        QVector<Candle> candles; QString error;
        const QJsonArray rows{QJsonArray{"2026-10-13", "10", "11", "12", "9", "12345"}};
        for (const auto& symbol : {QString("sh688001"), QString("sh689009"), QString("sh600519")}) {
            const QJsonObject root{{"code", 0}, {"data", QJsonObject{{symbol, QJsonObject{{"qfqday", rows}}}}}};
            QVERIFY(parse_candles(root, symbol, candles, error));
            QCOMPARE(candles[0].volume, symbol == "sh600519" ? 1234500.0 : 12345.0);
        }
    }
    void research_contracts() {
        ResearchFeed feed; QString error;
        QVERIFY(!parse_research(R"({"code":0,"result":{"passportWeb":[]}})", "news", feed, error));
        QVERIFY(!parse_research("<html>blocked</html>", "reports", feed, error));
        QVERIFY(parse_research(R"JSON(callback({"code":0,"result":{"cmsArticleWebOld":[{"title":"<em>News</em>","date":"2026-10-13","url":"javascript:alert(1)"}]}}))JSON", "news", feed, error));
        QCOMPARE(feed.items[0].title, "News"); QVERIFY(feed.items[0].url.isEmpty());
        QVERIFY(parse_research(R"({"data":[{"title":"Report","publishDate":"2026-10-13","infoCode":"AP1234","orgSName":"Broker"}]})", "reports", feed, error));
        QCOMPARE(feed.items[0].url.host(), "data.eastmoney.com");
        QVERIFY(parse_research(R"({"data":[]})", "reports", feed, error)); QVERIFY(feed.items.isEmpty());
        QVERIFY(!parse_research(R"({"code":20000,"data":{"items":[{"content_text":"News","display_time":null}]}})", "lives", feed, error));
        QVERIFY(parse_research(R"({"code":20000,"data":{"items":[{"content_text":"News","display_time":1791873105}]}})", "lives", feed, error));
        QCOMPARE(feed.items[0].title, "News"); QVERIFY(!feed.items[0].time.isEmpty());
    }
    void research_late_response_and_html_escape() {
        fincept::screens::AShareResearchPanel panel;
        panel.set_symbol("sz000001");
        panel.findChild<QComboBox*>()->setCurrentIndex(1);
        auto* service = panel.findChild<AShareResearchService*>();
        auto* browser = panel.findChild<QTextBrowser*>();
        ResearchFeed feed; feed.source = "fixture"; feed.fetched = QDateTime::currentDateTimeUtc();
        feed.items.append({"<img src='file:///secret'>", "2026-10-13", "source", "", QUrl()});
        service->ready("sh600519", "reports", feed);
        QVERIFY(!browser->toPlainText().contains("secret"));
        service->ready("sz000001", "reports", feed);
        QVERIFY(browser->toPlainText().contains("<img"));
        panel.set_symbol("");
        service->ready("sz000001", "reports", feed);
        QVERIFY(!browser->toPlainText().contains("secret"));
    }
    void watchlist_persistence_and_late_response() {
        QTemporaryDir settings_dir;
        QVERIFY(settings_dir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
        {
            fincept::screens::AShareScreen screen;
            auto* input = screen.findChild<QLineEdit*>("ashareSymbolInput");
            auto* add = screen.findChild<QPushButton*>("ashareAdd");
            auto* remove = screen.findChild<QPushButton*>("ashareRemove");
            auto* table = screen.findChild<QTableWidget*>("ashareWatchlist");
            QVERIFY(input && add && remove && table);
            QCOMPARE(table->rowCount(), 4);
            input->setText("bad-code"); add->click(); QCOMPARE(table->rowCount(), 4);
            input->setText("920001.BJ"); add->click(); QCOMPARE(table->rowCount(), 5);
            QCOMPARE(screen.save_state()["selected"].toString(), "bj920001");
            input->setText("920001"); add->click(); QCOMPARE(table->rowCount(), 5);
            Quote q; QString error; QVERIFY(parse_quote(fixture(), "sz000001", q, error));
            auto* service = screen.findChild<AShareMarketService*>(); QVERIFY(service);
            service->quote_ready(q); // response from a different previously selected symbol
            QCOMPARE(screen.save_state()["selected"].toString(), "bj920001");
            QVERIFY(!screen.has_quote());
            remove->click(); QCOMPARE(table->rowCount(), 4);
            input->setText("920001"); add->click();
        }
        {
            fincept::screens::AShareScreen restored;
            QCOMPARE(restored.save_state()["selected"].toString(), "bj920001");
            QCOMPARE(restored.findChild<QTableWidget*>("ashareWatchlist")->rowCount(), 5);
        }
    }
    // ---- Tier-one features: extended quote, order book, periods, indicators, F10, watchlist ----
    void quote_extended_fields_and_star_units() {
        // Field layout from a live sh688981 response on 2026-10-15 (88 fields).
        QJsonArray q;
        for (int i = 0; i < 88; ++i) q.append("");
        q[1] = QStringLiteral("中芯国际"); q[3] = "100.16"; q[4] = "102.55"; q[5] = "99.56";
        q[6] = "20379386"; q[7] = "10566793"; q[8] = "9812593";
        q[9] = "100.15"; q[10] = "8"; q[19] = "100.16"; q[20] = "30";
        q[30] = "20261015111147"; q[33] = "101.00"; q[34] = "99.10"; q[37] = "203644";
        q[38] = "1.02"; q[39] = "118.99"; q[43] = "1.85"; q[44] = "2003.79"; q[45] = "8576.08";
        q[46] = "5.00"; q[47] = "123.06"; q[48] = "82.04"; q[49] = "1.38";
        const QJsonObject root{{"code", 0}, {"data", QJsonObject{{"sh688981", QJsonObject{
            {"qt", QJsonObject{{"sh688981", q}}},
            {"data", QJsonObject{{"date", "20261015"}, {"data", QJsonArray{"1112 100.15 20382191 2036717034.00",
                                                                            "1113 100.05 20435809 2042085662"}}}}}}}}};
        Quote quote; QString error;
        QVERIFY2(parse_quote(root, "sh688981", quote, error), qPrintable(error));
        QCOMPARE(quote.volume, 20379386.0);                     // STAR snapshot volume is shares
        QCOMPARE(quote.outer, 10566793.0);
        QVERIFY(std::abs(quote.minutes.last().average - 99.927) < 0.01); // was ~0.999 with a lots assumption
        QCOMPARE(quote.bids[0].price, 100.15); QCOMPARE(quote.bids[0].volume, 800.0); // book is lots on every board
        QCOMPARE(quote.asks[0].volume, 3000.0);
        QCOMPARE(quote.turnover_rate, 1.02); QCOMPARE(quote.pe_ttm, 118.99); QCOMPARE(quote.pb, 5.0);
        QCOMPARE(quote.total_cap, 8576.08e8); QCOMPARE(quote.limit_up, 123.06); QCOMPARE(quote.volume_ratio, 1.38);
        // The 38-field fixture keeps working and keeps lots semantics.
        QVERIFY(parse_quote(fixture(), "sz000001", quote, error));
        QCOMPARE(quote.volume, 3000.0); QCOMPARE(quote.turnover_rate, 0.0);
    }
    void kline_periods_and_adjustment() {
        QString error; QVector<Candle> candles;
        const QJsonArray rows{QJsonArray{"2026-10-09", "10", "11", "12", "9", "100"},
                              QJsonArray{"2026-10-15", "11", "12", "12.5", "10.5", "50"}};
        QJsonObject root{{"code", 0}, {"data", QJsonObject{{"sz000001", QJsonObject{{"qfqweek", rows}}}}}};
        QVERIFY(parse_kline(root, "sz000001", KPeriod::Week, Adjust::Forward, candles, error));
        QCOMPARE(candles.size(), 2); QCOMPARE(candles[1].volume, 5000.0); QVERIFY(!candles[1].time.isValid());
        QVERIFY(!parse_kline(root, "sz000001", KPeriod::Week, Adjust::None, candles, error));
        // Source only returned raw rows: refuse to label them as qfq, but point at the raw option.
        root["data"] = QJsonObject{{"sh688981", QJsonObject{{"day", rows}}}};
        QVERIFY(!parse_kline(root, "sh688981", KPeriod::Day, Adjust::Forward, candles, error));
        QVERIFY(error.contains(QStringLiteral("不复权")));
        QVERIFY(parse_kline(root, "sh688981", KPeriod::Day, Adjust::None, candles, error));
        QCOMPARE(candles[0].volume, 100.0); // STAR candles are shares
        const QJsonArray minute{QJsonArray{"202610151110", "1312.87", "1309.00", "1313.99", "1308.61", "1178.00", QJsonObject{}, "0.94"},
                                QJsonArray{"202610151105", "1309.30", "1312.48", "1313.00", "1308.95", "1075.00", QJsonObject{}, "0.86"}};
        root["data"] = QJsonObject{{"sh600519", QJsonObject{{"m5", minute}}}};
        QVERIFY(parse_kline(root, "sh600519", KPeriod::Min5, Adjust::Forward, candles, error));
        QCOMPARE(candles[0].time, QTime(11, 5)); QCOMPARE(candles[0].volume, 107500.0);
        QCOMPARE(kline_key("sh600519", KPeriod::Min5, Adjust::Forward), kline_key("sh600519", KPeriod::Min5, Adjust::None));
        QCOMPARE(kline_key("sh000001", KPeriod::Day, Adjust::Forward), "sh000001|day|raw");
        QVERIFY(kline_key("sh600519", KPeriod::Day, Adjust::Forward) != kline_key("sh600519", KPeriod::Day, Adjust::None));
        QCOMPARE(eastmoney_secid("sh600519"), "1.600519"); QCOMPARE(eastmoney_secid("bj920001"), "0.920001");
        QVERIFY(eastmoney_secid("sh000001").isEmpty());
    }
    void trades_parser() {
        QVector<Trade> trades; QString error;
        QVERIFY(parse_trades(R"({"rc":0,"data":{"details":["11:10:17,1308.90,2,2,1","11:10:20,1310.00,110,47,2","11:10:23,1310.00,5,1,4"]}})", trades, error));
        QCOMPARE(trades.size(), 3);
        QCOMPARE(trades[0].side, -1); QCOMPARE(trades[1].side, 1); QCOMPARE(trades[2].side, 0);
        QCOMPARE(trades[1].volume, 11000.0);
        QVERIFY(parse_trades(R"({"rc":0,"data":{"details":[]}})", trades, error)); QVERIFY(trades.isEmpty());
        QVERIFY(!parse_trades(R"({"rc":0,"data":null})", trades, error));
        QVERIFY(!parse_trades("<html></html>", trades, error));
    }
    void indicators_reference_values() {
        QVector<Candle> rising;
        for (int i = 1; i <= 40; ++i) rising.append({QDate(2026, 1, 1).addDays(i), double(i), double(i), i + 0.5, i - 0.5, 1000.0 * i});
        auto ind = compute_indicators(rising);
        QVERIFY(!has_value(ind.ma5[3])); QCOMPARE(ind.ma5[39], 38.0); QCOMPARE(ind.ma20[39], 30.5);
        QVERIFY(!has_value(ind.ma60[39]));
        QCOMPARE(ind.rsi[39], 100.0); QVERIFY(!has_value(ind.rsi[13]));
        QVERIFY(!has_value(ind.dif[24])); QVERIFY(ind.dif[39] > 0 && ind.macd[39] >= 0);
        QCOMPARE(ind.vol_ma5[39], 38000.0);
        QVector<Candle> flat;
        for (int i = 0; i < 40; ++i) flat.append({QDate(2026, 1, 1).addDays(i), 10, 10, 10.5, 9.5, 100});
        ind = compute_indicators(flat);
        QCOMPARE(ind.boll_upper[39], 10.0); QCOMPARE(ind.boll_lower[39], 10.0);
        QVERIFY(std::abs(ind.macd[39]) < 1e-12);
        QVERIFY(std::abs(ind.k[39] - 50) < 1e-9 && std::abs(ind.j[39] - 50) < 1e-9);
        QCOMPARE(ind.rsi[39], 50.0);
        // EMA seeded with the first value.
        const auto e = ema({10, 20}, 3); QCOMPARE(e[0], 10.0); QCOMPARE(e[1], 15.0);
    }
    void f10_parsers() {
        StockProfile p; QString error;
        QVERIFY(!parse_profile_survey("<html>blocked</html>", p, error));
        QVERIFY(parse_profile_survey(R"({"jbzl":[{"SECURITY_NAME_ABBR":"贵州茅台","ORG_NAME":"贵州茅台酒股份有限公司","EM2016":"食品饮料-饮料-白酒","STR_CODEB":"None","EMP_NUM":"34992"}]})", p, error));
        QCOMPARE(p.name, QStringLiteral("贵州茅台")); QCOMPARE(p.employees, "34992");
        QVERIFY(parse_profile_concepts(R"({"ssbk":[{"BOARD_NAME":"白酒"},{"BOARD_NAME":"上证50_"},{"BOARD_NAME":"白酒"}]})", p, error));
        QCOMPARE(p.concepts, (QStringList{QStringLiteral("白酒"), QStringLiteral("上证50")}));
        QVERIFY(parse_profile_business(R"({"zygcfx":[
            {"REPORT_DATE":"2025-12-31 00:00:00","MAINOP_TYPE":"2","ITEM_NAME":"旧","MAIN_BUSINESS_INCOME":1},
            {"REPORT_DATE":"2026-06-30 00:00:00","MAINOP_TYPE":"3","ITEM_NAME":"国内","MAIN_BUSINESS_INCOME":5,"MBI_RATIO":0.5},
            {"REPORT_DATE":"2026-06-30 00:00:00","MAINOP_TYPE":"2","ITEM_NAME":"系列酒","MAIN_BUSINESS_INCOME":2,"MBI_RATIO":0.2},
            {"REPORT_DATE":"2026-06-30 00:00:00","MAINOP_TYPE":"2","ITEM_NAME":"茅台酒","MAIN_BUSINESS_INCOME":8,"MBI_RATIO":0.8,"GROSS_RPOFIT_RATIO":0.92}]})", p, error));
        QCOMPARE(p.business.size(), 2); QCOMPARE(p.business[0].kind, QStringLiteral("按产品"));
        QCOMPARE(p.business[0].report_date, "2026-06-30"); QCOMPARE(p.business[0].items[0].name, QStringLiteral("茅台酒"));
        QVERIFY(p.business[0].items[0].has_margin); QVERIFY(!p.business[0].items[1].has_margin);
        QVERIFY(parse_profile_holders(R"({"sdgd":[
            {"END_DATE":"2026-06-30 00:00:00","HOLDER_RANK":1,"HOLDER_NAME":"<b>集团</b>","HOLD_NUM":681282935,"HOLD_NUM_RATIO":54.5,"HOLD_NUM_CHANGE":"不变"},
            {"END_DATE":"2026-03-31 00:00:00","HOLDER_RANK":1,"HOLDER_NAME":"旧","HOLD_NUM":1,"HOLD_NUM_RATIO":1}],
            "gdrs":[{"END_DATE":"2026-06-30 00:00:00","HOLDER_TOTAL_NUM":296404,"TOTAL_NUM_RATIO":21.9}]})", p, error));
        QCOMPARE(p.holders.size(), 1); QCOMPARE(p.holders_date, "2026-06-30"); QCOMPARE(p.holder_counts[0].holders, 296404.0);
        QVERIFY(parse_profile_finance(R"({"data":[{"REPORT_DATE_NAME":"2026中报","TOTALOPERATEREVE":92278072083.21,"ROEJQ":16.75}]})", p, error));
        QCOMPARE(p.finance.size(), 1); QVERIFY(!has_value(p.finance[0].gross_margin)); QCOMPARE(p.finance[0].roe, 16.75);
        QVERIFY(!parse_profile_finance(R"({"pages":0})", p, error));
        const auto html = fincept::screens::AShareProfilePanel::render_html(p, false);
        QVERIFY(!html.contains("<b>集团</b>")); QVERIFY(html.contains("&lt;b&gt;"));
        QVERIFY(html.contains(QStringLiteral("922.78"))); // revenue in 亿
        QCOMPARE(eastmoney_f10_code("bj920001"), "BJ920001"); QVERIFY(eastmoney_f10_code("sh000001").isEmpty());
    }
    void chart_periods_and_oscillators_render() {
        fincept::screens::AShareChart chart; chart.resize(900, 600); chart.show();
        QVector<Candle> candles;
        for (int i = 0; i < 120; ++i) {
            const double c = 10 + std::sin(i / 6.0);
            candles.append({QDate(2026, 10, 15), c - 0.1, c, c + 0.3, c - 0.3, 1000.0 + i, QTime(9, 35).addSecs(i * 300)});
        }
        chart.set_daily(true); chart.set_minute_bars(true); chart.set_candles(candles);
        QCOMPARE(chart.candle_count(), 120);
        using C = fincept::screens::AShareChart;
        for (auto overlay : {C::Overlay::MA, C::Overlay::BOLL, C::Overlay::None})
            for (auto osc : {C::Oscillator::MACD, C::Oscillator::KDJ, C::Oscillator::RSI, C::Oscillator::None}) {
                chart.set_overlay(overlay); chart.set_oscillator(osc); QVERIFY(!chart.grab().isNull());
            }
        chart.set_candles({candles.first()}); QVERIFY(!chart.grab().isNull());
    }
    void order_book_render_and_counts() {
        fincept::screens::AShareOrderBook book; book.resize(258, 600); book.show();
        QCOMPARE(book.level_count(), 0); QVERIFY(!book.grab().isNull());
        Quote q; q.symbol = "sz000001"; q.previous = 10; q.last = 10.1;
        for (int i = 0; i < 5; ++i) { q.bids.append({10.1 - i * 0.01, 100.0 * (i + 1)}); q.asks.append({10.11 + i * 0.01, 200}); }
        q.asks[4].price = 0; // empty level (e.g. near limit price)
        book.set_quote(q); QCOMPARE(book.level_count(), 9);
        book.clear("loading");
        // Ticks arriving before the first quote must survive that quote.
        book.set_trades("sz000001", {{"10:00:00", 10.1, 500, 1}, {"10:00:03", 10.09, 100, -1}}, QDateTime::currentDateTimeUtc());
        book.set_quote(q);
        QCOMPARE(book.trade_count(), 2); QVERIFY(!book.grab().isNull());
        book.set_trades_error("offline"); QCOMPARE(book.trade_count(), 2); // old data kept
        q.symbol = "sh600519"; book.set_quote(q); QCOMPARE(book.trade_count(), 0); // no cross-symbol leftovers
    }
    void period_state_roundtrip_and_legacy_daily_flag() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        fincept::screens::AShareScreen screen;
        screen.restore_state({{"selected", "sz000001"}, {"period", 5}});
        QCOMPARE(screen.save_state()["period"].toInt(), 5); QVERIFY(!screen.save_state()["daily"].toBool());
        QVERIFY(screen.findChild<QComboBox*>("ashareAdjust")->isHidden()); // minute bars are raw only
        screen.restore_state({{"selected", "sz000001"}, {"daily", true}});
        QCOMPARE(screen.save_state()["period"].toInt(), 1);
        QVERIFY(!screen.findChild<QComboBox*>("ashareAdjust")->isHidden());
        screen.restore_state({{"selected", "sh000001"}});
        QVERIFY(screen.findChild<QComboBox*>("ashareAdjust")->isHidden()); // index points are never adjusted
    }
    void watchlist_sorting_columns_and_groups() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
            settings.setValue("symbol_groups", QVariantMap{{"sh600519", QStringLiteral("持仓")}, {"sz000001", QStringLiteral("不存在")}});
        }
        {
            fincept::screens::AShareScreen screen; screen.resize(1320, 940); screen.show();
            auto* table = screen.findChild<QTableWidget*>("ashareWatchlist");
            auto* service = screen.findChild<AShareMarketService*>();
            const QList<QPair<QString, double>> moves{{"sh600519", 1.0}, {"sz000001", -2.0}, {"sz300750", 5.0}, {"sh601318", 0.0}};
            for (const auto& move : moves) {
                Quote q; q.symbol = move.first; q.name = move.first; q.previous = 100; q.last = 100 + move.second;
                q.time = QDateTime::currentDateTime(QTimeZone("Asia/Shanghai")); q.volume_ratio = move.second == 0 ? 0 : 1;
                service->quote_ready(q);
            }
            auto first = [table]() { return table->item(0, 0)->text().section('\n', 1).left(8).toLower(); };
            QCOMPARE(first(), "sh600519");
            table->horizontalHeader()->sectionClicked(2); QCOMPARE(first(), "sz300750");     // change desc
            table->horizontalHeader()->sectionClicked(2); QCOMPARE(first(), "sz000001");     // change asc
            table->horizontalHeader()->sectionClicked(2); QCOMPARE(first(), "sh600519");     // original order
            table->horizontalHeader()->sectionClicked(3);                                   // volume ratio desc
            QCOMPARE(table->item(3, 0)->text().section('\n', 1).left(8).toLower(), "sh601318"); // missing sorts last
            QVERIFY(table->isColumnHidden(5) && table->isColumnHidden(6) && table->isColumnHidden(9));
            QVERIFY(!table->isColumnHidden(8));
            auto* filter = screen.findChild<QComboBox*>("ashareGroupFilter");
            QCOMPARE(filter->itemText(1), QStringLiteral("持仓"));                          // default groups
            filter->setCurrentIndex(filter->findData(QStringLiteral("持仓")));
            QCOMPARE(table->rowCount(), 1);                                                 // unknown group dropped
            screen.findChild<QLineEdit*>("ashareSymbolInput")->setText("600036");
            screen.findChild<QPushButton*>("ashareAdd")->click();                           // added into 持仓
            QCOMPARE(table->rowCount(), 2);
            filter->setCurrentIndex(filter->findData(QStringLiteral("__none__")));
            QCOMPARE(table->rowCount(), 3);
            filter->setCurrentIndex(filter->findData(QStringLiteral("短线")));
            screen.findChild<QPushButton*>("ashareDeleteGroup")->click();
            QCOMPARE(filter->currentIndex(), 0); QCOMPARE(filter->findData(QStringLiteral("短线")), -1);
        }
        fincept::screens::AShareScreen restored;
        auto* filter = restored.findChild<QComboBox*>("ashareGroupFilter");
        QCOMPARE(filter->findData(QStringLiteral("短线")), -1);
        filter->setCurrentIndex(filter->findData(QStringLiteral("持仓")));
        QCOMPARE(restored.findChild<QTableWidget*>("ashareWatchlist")->rowCount(), 2);
    }
    // ---- Tier two: market overview ----
    void market_breadth_parser() {
        Breadth b; QString error;
        // Shape of a live push2ex getTopicZDFenBu response (2026-10-15, trimmed).
        QVERIFY(parse_breadth(R"({"rc":0,"data":{"qdate":20261015,"fenbu":[{"-1":324},{"-11":26},{"-2":672},{"0":26},{"1":149},{"11":36},{"2":81}]}})", b, error));
        QCOMPARE(b.date, QDate(2026, 10, 15));
        QCOMPARE(b.up, 149 + 36 + 81); QCOMPARE(b.down, 324 + 26 + 672); QCOMPARE(b.flat, 26);
        QCOMPARE(b.limit_up, 36); QCOMPARE(b.limit_down, 26);
        QCOMPARE(b.buckets.first().first, -11); QCOMPARE(b.buckets.last().first, 11); // sorted axis
        QVERIFY(!parse_breadth(R"({"rc":0,"data":{"fenbu":[]}})", b, error));
        QVERIFY(!parse_breadth("<html>403</html>", b, error));
    }
    void market_sector_and_constituent_parsers() {
        QVector<Sector> sectors; QString error;
        QVERIFY(parse_sectors(R"({"code":0,"data":{"rank_list":[{"code":"pt01801125","name":"白酒Ⅱ","zdf":"1.72","zgb":"11/19",
            "zljlr":"76783.43","turnover":"1574966","lzg":{"code":"sz000995","name":"皇台酒业","zdf":"10.04"}}],"total":124}})", sectors, error));
        QCOMPARE(sectors.size(), 1);
        QCOMPARE(sectors[0].up, 11); QCOMPARE(sectors[0].total, 19);
        QCOMPARE(sectors[0].main_net, 767834300.0); QCOMPARE(sectors[0].turnover, 15749660000.0); // 万元 -> CNY
        QCOMPARE(sectors[0].leader_code, "sz000995");
        QVERIFY(!parse_sectors(R"({"code":11,"data":""})", sectors, error));
        QVector<BoardStock> stocks;
        QVERIFY(parse_board_stocks(R"({"code":0,"data":{"rank_list":[{"code":"sz000995","name":"皇台酒业","zxj":"13.37","zdf":"10.04","hsl":"14.97","turnover":"34321"},
            {"code":"hk00700","name":"bad","zxj":"1","zdf":"1","hsl":"1","turnover":"1"}]}})", stocks, error));
        QCOMPARE(stocks.size(), 1); QCOMPARE(stocks[0].amount, 343210000.0);
    }
    void market_limit_pool_and_billboard_parsers() {
        LimitPool pool; QString error;
        QVERIFY(parse_limit_pool(R"({"rc":0,"data":{"tc":36,"qdate":20261015,"pool":[{"c":"002815","m":0,"n":"崇达技术","p":23250,"zdp":9.98,
            "amount":317925520,"hs":1.76,"lbc":2,"fbt":92500,"lbt":100212,"fund":383694750,"zbc":1,"hybk":"元件"}]}})", "zt", pool, error));
        QCOMPARE(pool.total, 36); QCOMPARE(pool.rows[0].symbol, "sz002815");
        QCOMPARE(pool.rows[0].price, 23.25); QCOMPARE(pool.rows[0].boards, 2); QCOMPARE(pool.rows[0].breaks, 1);
        QCOMPARE(pool.rows[0].first_time, "09:25:00"); QCOMPARE(pool.rows[0].last_time, "10:02:12");
        QVERIFY(parse_limit_pool(R"({"rc":0,"data":{"tc":26,"qdate":20261015,"pool":[{"c":"603344","n":"星德胜","p":19850,"zdp":-9.98,"days":3,"oc":29,"lbt":145435}]}})", "dt", pool, error));
        QCOMPARE(pool.rows[0].symbol, "sh603344"); QCOMPARE(pool.rows[0].boards, 3); QCOMPARE(pool.rows[0].breaks, 29);
        QVERIFY(!parse_limit_pool(R"({"rc":102,"data":null})", "zb", pool, error)); QVERIFY(error.contains(QStringLiteral("炸板池")));
        QVector<BillboardRow> rows;
        QVERIFY(parse_billboard(R"({"result":{"data":[
            {"SECURITY_CODE":"002029","SECURITY_NAME_ABBR":"七匹狼","TRADE_DATE":"2026-10-14 00:00:00","CLOSE_PRICE":8.6,"CHANGE_RATE":9.98,"BILLBOARD_NET_AMT":156359488.37,"EXPLANATION":"日涨幅偏离值达到7%"},
            {"SECURITY_CODE":"002029","SECURITY_NAME_ABBR":"七匹狼","TRADE_DATE":"2026-10-14 00:00:00","CLOSE_PRICE":8.6,"CHANGE_RATE":9.98,"BILLBOARD_NET_AMT":1,"EXPLANATION":"连续三日涨幅偏离20%"},
            {"SECURITY_CODE":"600000","SECURITY_NAME_ABBR":"旧","TRADE_DATE":"2026-10-13 00:00:00","CHANGE_RATE":1}]}})", rows, error));
        QCOMPARE(rows.size(), 1); // older day dropped, duplicate stock merged
        QVERIFY(rows[0].reason.contains(QStringLiteral("连续三日"))); QCOMPARE(rows[0].date, QDate(2026, 10, 14));
        QVERIFY(!parse_billboard(R"({"result":null})", rows, error));
    }
    void market_panel_renders_and_opens_stocks() {
        fincept::screens::AShareMarketPanel panel; panel.resize(1300, 900); panel.show();
        auto* service = panel.findChild<AShareOverviewService*>();
        Breadth b; b.date = QDate(2026, 10, 15); b.up = 417; b.down = 4925; b.flat = 26; b.limit_up = 36; b.limit_down = 26;
        for (int k = -11; k <= 11; ++k) b.buckets.append({k, 10 + std::abs(k)});
        service->breadth_ready(b);
        QVector<Sector> sectors;
        for (int i = 0; i < 31; ++i) sectors.append({QString("pt0180%1").arg(1100 + i), QString("行业%1").arg(i), "sz000001", "平安银行",
                                                     i - 15.0, 1.0, 1e8 * (i - 15), 1e10, i, 30});
        service->sectors_ready("hy", sectors);
        LimitPool zt; zt.total = 1; zt.rows.append({"sz002815", QStringLiteral("崇达技术"), QStringLiteral("元件"), "09:25:00", "09:25:00", 23.25, 9.98, 3e8, 3e8, 1.7, 2, 0});
        service->pool_ready("zt", zt);
        auto d = panel.diagnostics();
        QCOMPARE(d["breadth_total"].toInt(), 5368); QCOMPARE(d["heat_tiles"].toInt(), 31); QCOMPARE(d["zt"].toInt(), 1);
        QVERIFY(panel.findChild<QLabel*>("ashareMarketTurnover")->text().startsWith(QStringLiteral("3100.00亿"))); // 31 × 100 亿
        QVERIFY(!panel.grab().isNull());
        QStringList opened;
        connect(&panel, &fincept::screens::AShareMarketPanel::open_stock, this, [&](const QString& s) { opened << s; });
        auto* table = panel.findChild<QTableWidget*>("ashareZtPool");
        QCOMPARE(table->rowCount(), 1);
        table->cellDoubleClicked(0, 0);
        QCOMPARE(opened, QStringList{"sz002815"});
        // Sector flow table is ordered by main-force net inflow.
        auto* flow = panel.findChild<QTableWidget*>("ashareSectorFlow");
        QCOMPARE(flow->item(0, 0)->text(), QStringLiteral("行业30"));
        // A late constituents response for another board is ignored.
        service->board_stocks_ready("ptOTHER", {{"sz000001", "x", 1, 1, 1, 1}});
        QCOMPARE(panel.diagnostics()["board_stocks"].toInt(), 0);
    }
    void screen_market_view_switch_and_open_stock() {
        QTemporaryDir dir; QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        fincept::screens::AShareScreen screen; screen.resize(1320, 940); screen.show();
        QVERIFY(!screen.market_view());
        screen.findChild<QPushButton*>("ashareViewMarket")->click();
        QVERIFY(screen.market_view()); QCOMPARE(screen.save_state()["view"].toString(), "market");
        auto* panel = screen.findChild<fincept::screens::AShareMarketPanel*>();
        panel->open_stock("sz002815"); // double-click from a pool table
        QVERIFY(!screen.market_view());
        QCOMPARE(screen.save_state()["selected"].toString(), "sz002815");
        QCOMPARE(screen.findChild<QTableWidget*>("ashareWatchlist")->rowCount(), 5);
        screen.restore_state({{"selected", "sz002815"}, {"view", "market"}});
        QVERIFY(screen.market_view());
    }
};
QTEST_MAIN(AShareTests)
#include "tests.moc"
