#include "screens/ashare/AShareAiPanel.h"
#include "screens/ashare/AShareChart.h"
#include "screens/ashare/AShareMarketPanel.h"
#include "screens/ashare/AShareNotesPanel.h"
#include "screens/ashare/AShareOrderBook.h"
#include "screens/ashare/ASharePortfolioPanel.h"
#include "screens/ashare/AShareProfilePanel.h"
#include "screens/ashare/AShareResearchPanel.h"
#include "screens/ashare/AShareScreen.h"
#include "screens/ashare/AShareScreen_internal.h"
#include "screens/ashare/AShareTheme.h"
#include "services/ashare/AShareIndicators.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace fincept::screens {
using namespace services::ashare;
using namespace ashare_detail;

QWidget* AShareScreen::build_stock_content() {
    // ---- Quote, chart, order book, info tabs ----
    auto* content = new theme::Panel;
    auto* right = new QVBoxLayout(content);
    right->setContentsMargins(10, 8, 10, 8);
    right->setSpacing(6);
    // Quote header: name/code on the left, large monospaced price on the right of it.
    auto* quote_row = new QHBoxLayout;
    quote_row->setSpacing(16);
    title_ = new QLabel;
    title_->setStyleSheet("font-size:16px;font-weight:600;");
    quote_row->addWidget(title_);
    price_ = new QLabel;
    price_->setFont(theme::mono(17, true));
    quote_row->addWidget(price_);
    quote_row->addStretch();
    right->addLayout(quote_row);
    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setObjectName("ashareDetails");
    details_->setFont(theme::mono(9));
    right->addWidget(details_);
    auto* controls = new QHBoxLayout;
    mode_ = new QComboBox;
    mode_->setObjectName("ashareChartMode");
    mode_->addItems({QStringLiteral("当天分时"), QStringLiteral("日 K"), QStringLiteral("周 K"), QStringLiteral("月 K"),
                     QStringLiteral("5 分钟"), QStringLiteral("15 分钟"), QStringLiteral("30 分钟"),
                     QStringLiteral("60 分钟")});
    controls->addWidget(mode_);
    adjust_ = new QComboBox;
    adjust_->setObjectName("ashareAdjust");
    adjust_->addItem(QStringLiteral("前复权"), "qfq");
    adjust_->addItem(QStringLiteral("不复权"), "raw");
    controls->addWidget(adjust_);
    overlay_ = new QComboBox;
    overlay_->setObjectName("ashareOverlay");
    overlay_->addItems({QStringLiteral("均线 MA"), QStringLiteral("布林 BOLL"), QStringLiteral("无主图指标")});
    controls->addWidget(overlay_);
    oscillator_ = new QComboBox;
    oscillator_->setObjectName("ashareOscillator");
    oscillator_->addItems(
        {QStringLiteral("MACD"), QStringLiteral("KDJ"), QStringLiteral("RSI"), QStringLiteral("无副图")});
    controls->addWidget(oscillator_);
    chart_info_ = new QLabel;
    chart_info_->setProperty("muted", true);
    chart_info_->setWordWrap(true);
    controls->addWidget(chart_info_, 1);
    right->addLayout(controls);
    // Chart and info area share a draggable splitter so F10 tables can be given more room.
    auto* vertical = new QSplitter(Qt::Vertical);
    vertical->setObjectName("ashareChartSplit");
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(6);
    auto* chart_box = new QWidget;
    auto* chart_row = new QHBoxLayout(chart_box);
    chart_row->setContentsMargins(0, 0, 0, 0);
    chart_row->setSpacing(6);
    chart_ = new AShareChart;
    chart_row->addWidget(chart_, 1);
    order_book_ = new AShareOrderBook;
    chart_row->addWidget(order_book_);
    auto* lower = new QWidget;
    auto* lower_layout = new QVBoxLayout(lower);
    lower_layout->setContentsMargins(0, 4, 0, 0);
    analysis_ = new QLabel;
    analysis_->setWordWrap(true);
    analysis_->setObjectName("ashareAnalysis");
    analysis_->setFont(theme::mono(9));
    lower_layout->addWidget(analysis_);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("ashareInfoTabs");
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setDrawBase(false); // the native base line renders as a bright rule on the dark theme
    research_ = new AShareResearchPanel;
    research_->set_symbol(selected_);
    profile_ = new AShareProfilePanel;
    tabs_->addTab(research_, QStringLiteral("资讯 · 研报 / 新闻 / 快讯"));
    tabs_->addTab(profile_, QStringLiteral("个股资料 · F10"));
    ai_ = new AShareAiPanel;
    tabs_->addTab(ai_, QStringLiteral("AI 解读"));
    ai_->set_context_builder([this](bool include_position) { return ai_context(include_position); });
    // Notes and portfolio pages are created here so the stock tabs can reference them.
    notes_ = new AShareNotesPanel;
    portfolio_ = new ASharePortfolioPanel;
    stock_notes_ = new AShareStockNotes(notes_);
    tabs_->addTab(stock_notes_, QStringLiteral("本股笔记"));
    lower_layout->addWidget(tabs_, 1);
    vertical->addWidget(chart_box);
    vertical->addWidget(lower);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    right->addWidget(vertical, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        profile_->set_active(index == 1);
        if (index == 1)
            QTimer::singleShot(0, this, [this]() {
                if (auto* scroll = findChild<QScrollArea*>("ashareBodyScroll"))
                    scroll->ensureWidgetVisible(tabs_, 0, 0);
            });
    });
    return content;
}

bool AShareScreen::intraday() const {
    return mode_->currentIndex() <= 0;
}
KPeriod AShareScreen::current_period() const {
    return kPeriods.value(mode_->currentIndex(), KPeriod::Day);
}
Adjust AShareScreen::current_adjust() const {
    return adjust_->currentIndex() == 1 ? Adjust::None : Adjust::Forward;
}
QString AShareScreen::analysis_key() const {
    return intraday() ? kline_key(selected_, KPeriod::Day, current_adjust())
                      : kline_key(selected_, current_period(), current_adjust());
}
bool AShareScreen::has_daily() const {
    return candles_.contains(kline_key(selected_, KPeriod::Day, current_adjust()));
}
void AShareScreen::fetch_selected_kline(bool force) {
    if (selected_.isEmpty())
        return;
    const auto key = analysis_key();
    if (!force && kline_fetched_.contains(key) && kline_fetched_[key].secsTo(QDateTime::currentDateTimeUtc()) < 60)
        return;
    service_->fetch_kline(selected_, intraday() ? KPeriod::Day : current_period(), current_adjust());
}

void AShareScreen::update_chart_controls() {
    const bool index = is_index(selected_);
    const bool minute = !intraday() && is_minute_period(current_period());
    mode_->setItemText(1, index ? QStringLiteral("日 K · 指数点位") : QStringLiteral("日 K"));
    // Hidden where it does not apply; the chart caption states the basis actually shown.
    adjust_->setVisible(!index && !minute);
    adjust_->setToolTip(index    ? QStringLiteral("指数为原始点位，不适用复权")
                        : minute ? QStringLiteral("分钟 K 线数据源仅提供不复权数据")
                                 : QStringLiteral("日 / 周 / 月 K 复权口径；分时与五档始终为实际成交价"));
    overlay_->setEnabled(!intraday());
    oscillator_->setEnabled(!intraday());
}

QString AShareScreen::ai_context(bool include_position) const {
    if (selected_.isEmpty() || is_index(selected_))
        return QStringLiteral("（未选中个股）");
    QStringList lines;
    const auto& q = quotes_.value(selected_);
    if (quotes_.contains(selected_)) {
        lines << QStringLiteral("股票：%1 %2，行情时间 %3（北京时间，腾讯公开行情）")
                     .arg(q.name, selected_.toUpper(), q.time.toString("yyyy-MM-dd HH:mm:ss"));
        lines << QStringLiteral("最新 %1，昨收 %2，涨跌幅 %3%，今开 %4，最高 %5，最低 %6，成交额 %7 亿元")
                     .arg(q.last, 0, 'f', 2)
                     .arg(q.previous, 0, 'f', 2)
                     .arg(q.change_percent(), 0, 'f', 2)
                     .arg(q.open, 0, 'f', 2)
                     .arg(q.high, 0, 'f', 2)
                     .arg(q.low, 0, 'f', 2)
                     .arg(q.amount / 1e8, 0, 'f', 2);
        lines << QStringLiteral(
                     "量比 %1，换手率 %2%，振幅 %3%，总市值 %4 亿元，PE(TTM) %5，PB %6，涨停价 %7，跌停价 %8")
                     .arg(q.volume_ratio, 0, 'f', 2)
                     .arg(q.turnover_rate, 0, 'f', 2)
                     .arg(q.amplitude, 0, 'f', 2)
                     .arg(q.total_cap / 1e8, 0, 'f', 0)
                     .arg(q.pe_ttm > 0 ? QString::number(q.pe_ttm, 'f', 1) : QStringLiteral("亏损或无"))
                     .arg(q.pb, 0, 'f', 2)
                     .arg(q.limit_up, 0, 'f', 2)
                     .arg(q.limit_down, 0, 'f', 2);
        const double bids = std::accumulate(q.bids.begin(), q.bids.end(), 0.0,
                                            [](double s, const BookLevel& l) { return s + l.volume; });
        const double asks = std::accumulate(q.asks.begin(), q.asks.end(), 0.0,
                                            [](double s, const BookLevel& l) { return s + l.volume; });
        if (bids + asks > 0)
            lines << QStringLiteral("五档委比 %1%，外盘 %2 手，内盘 %3 手")
                         .arg((bids - asks) / (bids + asks) * 100, 0, 'f', 1)
                         .arg(q.outer / 100, 0, 'f', 0)
                         .arg(q.inner / 100, 0, 'f', 0);
    } else
        lines << QStringLiteral("股票：%1（实时行情尚未加载）").arg(selected_.toUpper());
    const auto key = kline_key(selected_, KPeriod::Day, Adjust::Forward);
    if (candles_.contains(key)) {
        const auto& c = candles_[key];
        const auto ind = compute_indicators(c);
        const int i = c.size() - 1;
        auto f = [](double v, int d = 2) { return has_value(v) ? QString::number(v, 'f', d) : QStringLiteral("—"); };
        lines << QStringLiteral("日 K 技术指标（前复权，截至 %1）：MA5 %2，MA10 %3，MA20 %4，MA60 %5；MACD DIF %6 DEA "
                                "%7 柱 %8；KDJ K %9 D %10 J %11；RSI14 %12；BOLL 上 %13 中 %14 下 %15")
                     .arg(c[i].date.toString(Qt::ISODate), f(ind.ma5[i]), f(ind.ma10[i]), f(ind.ma20[i]),
                          f(ind.ma60[i]), f(ind.dif[i], 3), f(ind.dea[i], 3), f(ind.macd[i], 3), f(ind.k[i]))
                     .arg(f(ind.d[i]), f(ind.j[i]), f(ind.rsi[i], 1), f(ind.boll_upper[i]), f(ind.boll_mid[i]),
                          f(ind.boll_lower[i]));
        lines << QStringLiteral("最近 30 个交易日（前复权）：日期 开 高 低 收 成交量(万股)");
        for (int k = std::max(0, int(c.size()) - 30); k < c.size(); ++k)
            lines << QStringLiteral("%1 %2 %3 %4 %5 %6")
                         .arg(c[k].date.toString("MM-dd"))
                         .arg(c[k].open, 0, 'f', 2)
                         .arg(c[k].high, 0, 'f', 2)
                         .arg(c[k].low, 0, 'f', 2)
                         .arg(c[k].close, 0, 'f', 2)
                         .arg(c[k].volume / 1e4, 0, 'f', 1);
    } else
        lines << QStringLiteral("日 K：未加载（若该股数据源不提供前复权序列，则此处为空）");
    const auto f10 = profile_->summary_for(selected_);
    lines << (f10.isEmpty() ? QStringLiteral("F10：未加载（打开「个股资料」页签后才会获取）")
                            : QStringLiteral("F10 摘要（东方财富）：") + QChar('\n') + f10);
    const auto market = market_->summary();
    if (!market.isEmpty())
        lines << market;
    if (include_position)
        lines << portfolio_->position_summary(selected_);
    return lines.join(QChar('\n'));
}

void AShareScreen::select_symbol(const QString& symbol) {
    selected_ = symbol;
    const bool index = is_index(symbol);
    if (index) {
        auto* choice = findChild<QComboBox*>("ashareIndexChoice");
        choice->setCurrentIndex(choice->findData(symbol));
    }
    tabs_->setVisible(!index);
    order_book_->setVisible(!index);
    research_->set_symbol(index ? QString() : symbol);
    profile_->set_symbol(index ? QString() : symbol);
    ai_->set_symbol(index ? QString() : symbol, quotes_.value(symbol).name);
    stock_notes_->set_symbol(index ? QString() : symbol, quotes_.value(symbol).name);
    update_chart_controls();
    render_indexes();
    {
        QSignalBlocker blocker(table_);
        table_->setCurrentCell(-1, -1);
        table_->selectRow(visible_.indexOf(symbol));
    }
    chart_->clear(QStringLiteral("正在加载…"));
    order_book_->clear(QStringLiteral("正在加载盘口…"));
    render_selected();
    if (!symbol.isEmpty()) {
        service_->fetch_quote(symbol);
        fetch_selected_kline(false);
        if (!index)
            service_->fetch_trades(symbol);
    }
}
void AShareScreen::refresh(bool force) {
    for (const auto& symbol : indexes_)
        service_->fetch_quote(symbol);
    for (const auto& symbol : symbols_)
        service_->fetch_quote(symbol);
    fetch_selected_kline(force);
    if (!selected_.isEmpty() && !is_index(selected_))
        service_->fetch_trades(selected_);
}
void AShareScreen::render_selected() {
    render_indexes();
    const bool index = is_index(selected_);
    tabs_->setVisible(!index && !selected_.isEmpty());
    order_book_->setVisible(!index && !selected_.isEmpty());
    update_chart_controls();
    if (selected_.isEmpty()) {
        title_->setText(QStringLiteral("添加第一只自选股"));
        price_->setText("—");
        details_->clear();
        analysis_->clear();
        chart_info_->clear();
        chart_->clear(QStringLiteral("在左侧输入股票代码，开始查看行情"));
        status_->setText(QStringLiteral("自选列表为空"));
        return;
    }
    const bool kline = !intraday();
    const auto period = current_period();
    const auto basis = index                               ? QStringLiteral("指数点位")
                       : is_minute_period(period) && kline ? QStringLiteral("不复权")
                       : effective_adjust(selected_, period, current_adjust()) == Adjust::Forward
                           ? QStringLiteral("前复权")
                           : QStringLiteral("不复权");
    const auto now = QDateTime::currentDateTimeUtc();
    const auto today = now.toTimeZone(QTimeZone("Asia/Shanghai")).date();
    title_->setText(selected_.toUpper());
    price_->setText("—");
    details_->clear();
    status_->setText(QStringLiteral("正在加载腾讯公开行情…"));
    if (quotes_.contains(selected_)) {
        const auto& q = quotes_[selected_];
        title_->setText(q.name + "  " + selected_.toUpper());
        const QString sign = q.last >= q.previous ? "+" : "";
        price_->setText(QStringLiteral("%1  %2%3  %4%5%")
                            .arg(q.last, 0, 'f', 2)
                            .arg(sign)
                            .arg(q.last - q.previous, 0, 'f', 2)
                            .arg(sign)
                            .arg(q.change_percent(), 0, 'f', 2));
        const auto& pal = theme::palette(theme_->currentIndex() == 1);
        price_->setStyleSheet(QStringLiteral("color:%1;").arg((q.last >= q.previous ? pal.rise : pal.fall).name()));
        QString detail = QStringLiteral("昨收 %1    今开 %2    最高 %3    最低 %4    成交额 %5 元")
                             .arg(q.previous, 0, 'f', 2)
                             .arg(q.open, 0, 'f', 2)
                             .arg(q.high, 0, 'f', 2)
                             .arg(q.low, 0, 'f', 2)
                             .arg(money(q.amount));
        if (!index && q.limit_up > 0)
            detail += QStringLiteral("    涨停 %1    跌停 %2").arg(q.limit_up, 0, 'f', 2).arg(q.limit_down, 0, 'f', 2);
        if (!index)
            detail +=
                QStringLiteral("\n量比 %1    换手 %2    振幅 %3    总市值 %4    流通市值 %5    PE(TTM) %6    PB %7")
                    .arg(column_text(q, VolumeRatio), column_text(q, Turnover), column_text(q, Amplitude),
                         column_text(q, TotalCap), q.float_cap > 0 ? money(q.float_cap) : "—", column_text(q, PE),
                         column_text(q, PB));
        details_->setText(detail);
        if (!kline)
            chart_->set_quote(q);
        if (!index)
            order_book_->set_quote(q);
        QString state = is_market_session(now) ? QStringLiteral("交易时段 · 自动请求 15 秒")
                                               : QStringLiteral("非连续交易时段 · 自动刷新待机");
        if (q.time.date() != today)
            state = QStringLiteral("非今日行情 · 展示最近交易日，可能休市或数据未更新");
        else if (is_market_session(now) && q.time.secsTo(now) > 120)
            state = QStringLiteral("行情超过 2 分钟未更新 · 可能停牌或源延迟");
        if (pause_->isChecked())
            state += QStringLiteral(" · 已暂停");
        status_->setText(QStringLiteral("腾讯公开行情  |  行情时间 %1（北京时间）  |  %2%3")
                             .arg(q.time.toString("yyyy-MM-dd HH:mm:ss"), state,
                                  index ? QString() : QStringLiteral("  |  分笔：东方财富 · 约 3 秒快照")));
        if (!kline)
            chart_info_->setText(
                (index ? QStringLiteral("%1 · 指数点位 · 午休压缩") : QStringLiteral("%1 · 价格蓝 / 均价黄 · 午休压缩"))
                    .arg(q.date.toString("yyyy-MM-dd")));
    }
    if (kline)
        chart_info_->setText(QStringLiteral("%1 · %2 · 滚轮缩放 · 鼠标查看").arg(period_name(period), basis));
    const auto key = analysis_key();
    const auto analysis_period = kline ? period : KPeriod::Day;
    const auto analysis_basis = kline   ? basis
                                : index ? QStringLiteral("指数点位")
                                : effective_adjust(selected_, KPeriod::Day, current_adjust()) == Adjust::Forward
                                    ? QStringLiteral("前复权")
                                    : QStringLiteral("不复权");
    analysis_->setText(
        QStringLiteral("正在加载%1，以计算 MA / MACD / KDJ / RSI / BOLL…").arg(period_name(analysis_period)));
    if (candles_.contains(key)) {
        const auto& candles = candles_[key];
        if (kline)
            chart_->set_candles(candles);
        const auto ind = compute_indicators(candles);
        const int i = candles.size() - 1;
        const auto& last = candles[i];
        const QString when = is_minute_period(analysis_period)
                                 ? last.date.toString("yyyy-MM-dd") + " " + last.time.toString("HH:mm")
                                 : last.date.toString("yyyy-MM-dd");
        analysis_->setToolTip(QStringLiteral("最新一根 K 线未收盘时指标随行情变化；历史不足指标窗口时显示 —。"));
        analysis_->setText(
            QStringLiteral(
                "%1指标  |  MA5 %2  MA10 %3  MA20 %4  MA60 %5  |  MACD DIF %6  DEA %7  柱 %8\n"
                "KDJ  K %9  D %10  J %11  |  RSI14 %12  |  BOLL 上 %13  中 %14  下 %15  |  截至 %16 · %17 · %18 根")
                .arg(period_name(analysis_period), fmt(ind.ma5[i]), fmt(ind.ma10[i]), fmt(ind.ma20[i]),
                     fmt(ind.ma60[i]), fmt(ind.dif[i], 3), fmt(ind.dea[i], 3), fmt(ind.macd[i], 3))
                .arg(fmt(ind.k[i]), fmt(ind.d[i]), fmt(ind.j[i]), fmt(ind.rsi[i], 1), fmt(ind.boll_upper[i]),
                     fmt(ind.boll_mid[i]), fmt(ind.boll_lower[i]), when, analysis_basis)
                .arg(candles.size()));
    }
    const auto quote_error = errors_.value("quote" + selected_);
    const auto kline_error = errors_.value(key);
    if (!quote_error.isEmpty())
        status_->setText(QStringLiteral("行情刷新失败（保留数据时间 %1）：%2")
                             .arg(quotes_.contains(selected_) ? quotes_[selected_].time.toString("yyyy-MM-dd HH:mm:ss")
                                                              : QStringLiteral("无"),
                                  quote_error));
    if (!kline_error.isEmpty())
        analysis_->setText(
            QStringLiteral("%1刷新失败，已有指标未更新：%2").arg(period_name(analysis_period), kline_error));
    if (kline && !kline_error.isEmpty() && !candles_.contains(key))
        chart_->clear(kline_error);
    if (!kline && !quote_error.isEmpty() && !quotes_.contains(selected_))
        chart_->clear(quote_error);
}

} // namespace fincept::screens
