#include "screens/ashare/AShareScreen.h"
#include "screens/ashare/AShareTheme.h"
#include "screens/ashare/AShareChart.h"
#include "screens/ashare/AShareMarketPanel.h"
#include "screens/ashare/AShareAiPanel.h"
#include "screens/ashare/AShareNotesPanel.h"
#include "screens/ashare/ASharePortfolioPanel.h"
#include "screens/ashare/AShareOrderBook.h"
#include "screens/ashare/AShareProfilePanel.h"
#include "screens/ashare/AShareResearchPanel.h"
#include "services/ashare/AShareIndicators.h"
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QPushButton>
#include <QButtonGroup>
#include <QSettings>
#include <QStackedWidget>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

namespace fincept::screens {
using namespace services::ashare;
namespace {
QString money(double value) {
    return value >= 1e8 ? QString::number(value / 1e8, 'f', 2) + QStringLiteral("亿")
                       : QString::number(value / 1e4, 'f', 1) + QStringLiteral("万");
}
// Watchlist columns. 0-2 are always shown; the rest can be toggled from the header menu.
enum Column { Name, Last, Change, VolumeRatio, Turnover, Amplitude, Amount, TotalCap, PE, PB, ColumnCount };
const QStringList kColumnTitles{QStringLiteral("股票"), QStringLiteral("最新价"), QStringLiteral("涨跌幅"),
    QStringLiteral("量比"), QStringLiteral("换手率"), QStringLiteral("振幅"), QStringLiteral("成交额"),
    QStringLiteral("总市值"), QStringLiteral("PE(TTM)"), QStringLiteral("PB")};
const QList<KPeriod> kPeriods{KPeriod::Day, KPeriod::Day, KPeriod::Week, KPeriod::Month,
                              KPeriod::Min5, KPeriod::Min15, KPeriod::Min30, KPeriod::Min60};
// NaN = not available, sorted last. Non-positive PE is a TTM loss, not a cheap valuation.
double column_value(const Quote& q, int column) {
    switch (column) {
    case Last: return q.last;
    case Change: return q.change_percent();
    case VolumeRatio: return q.volume_ratio > 0 ? q.volume_ratio : kNoValue;
    case Turnover: return q.turnover_rate > 0 ? q.turnover_rate : kNoValue;
    case Amplitude: return q.amplitude > 0 ? q.amplitude : kNoValue;
    case Amount: return q.amount > 0 ? q.amount : kNoValue;
    case TotalCap: return q.total_cap > 0 ? q.total_cap : kNoValue;
    case PE: return q.pe_ttm > 0 ? q.pe_ttm : kNoValue;
    case PB: return q.pb > 0 ? q.pb : kNoValue;
    }
    return kNoValue;
}
QString column_text(const Quote& q, int column) {
    const double v = column_value(q, column);
    switch (column) {
    case Last: return QString::number(q.last, 'f', 2);
    case Change: return (v >= 0 ? "+" : "") + QString::number(v, 'f', 2) + "%";
    case PE: return has_value(v) ? QString::number(v, 'f', 1) : q.pe_ttm < 0 ? QStringLiteral("亏损") : "—";
    case Amount: case TotalCap: return has_value(v) ? money(v) : "—";
    case Turnover: case Amplitude: return has_value(v) ? QString::number(v, 'f', 2) + "%" : "—";
    default: return has_value(v) ? QString::number(v, 'f', 2) : "—";
    }
}
QString fmt(double value, int digits = 2) { return has_value(value) ? QString::number(value, 'f', digits) : "—"; }
}

AShareScreen::AShareScreen(QWidget* parent) : QWidget(parent), service_(new AShareMarketService(this)) {
    setObjectName("ashareBoard");
    QFont ui_font;
    ui_font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI")});
    ui_font.setPixelSize(13);
    ui_font.setWeight(QFont::Normal);
    setFont(ui_font);
    setProperty("darkTheme", true);
    setStyleSheet(theme::stylesheet(true));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 6); root->setSpacing(6);
    // One compact terminal bar: brand, index ticker strip, index picker, hotkey hint, global controls.
    auto* top_bar = new theme::Panel; top_bar->setObjectName("ashareTopBar");
    auto* header = new QHBoxLayout(top_bar); header->setContentsMargins(10, 5, 8, 5); header->setSpacing(6);
    auto* heading = new QLabel(QStringLiteral("A股终端")); heading->setObjectName("ashareBrand");
    header->addWidget(heading);
    // View switch: single stock vs whole-market overview.
    view_stock_ = new QPushButton(QStringLiteral("个股")); view_stock_->setObjectName("ashareViewStock");
    view_market_ = new QPushButton(QStringLiteral("大盘")); view_market_->setObjectName("ashareViewMarket");
    auto* views = new QButtonGroup(this); views->setExclusive(true);
    view_portfolio_ = new QPushButton(QStringLiteral("持仓")); view_portfolio_->setObjectName("ashareViewPortfolio");
    view_notes_ = new QPushButton(QStringLiteral("笔记")); view_notes_->setObjectName("ashareViewNotes");
    // No focus ring: the checked state alone marks the active view (they also have hotkeys).
    for (auto* button : {view_stock_, view_market_, view_portfolio_, view_notes_}) {
        button->setCheckable(true); button->setFocusPolicy(Qt::NoFocus); views->addButton(button); header->addWidget(button);
    }
    view_stock_->setChecked(true);
    view_market_->setToolTip(QStringLiteral("市场全景：涨跌分布、涨跌停、板块热力图、龙虎榜（Ctrl+M）"));
    header->addSpacing(6);
    auto* index_scroll = new QScrollArea; index_scroll->setWidgetResizable(true);
    index_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    index_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    index_scroll->setFrameShape(QFrame::NoFrame); index_scroll->setFixedHeight(40);
    index_scroll->setStyleSheet("QScrollArea{border:0;background:transparent;}");
    index_scroll->viewport()->setAutoFillBackground(false);
    auto* cards = new QWidget; cards->setObjectName("ashareIndexCards");
    cards->setStyleSheet("QWidget#ashareIndexCards{background:transparent;}");
    index_cards_ = new QHBoxLayout(cards); index_cards_->setContentsMargins(0, 0, 0, 0); index_cards_->setSpacing(2);
    index_scroll->setWidget(cards); header->addWidget(index_scroll, 1);
    auto* index_choice = new QComboBox; index_choice->setObjectName("ashareIndexChoice");
    for (const auto& item : index_catalog()) index_choice->addItem(item.name, item.symbol);
    index_choice->setToolTip(QStringLiteral("选择指数后添加到行情条，或移除"));
    header->addWidget(index_choice);
    auto* add_index = new QPushButton(QStringLiteral("+")); add_index->setObjectName("ashareAddIndex");
    add_index->setToolTip(QStringLiteral("添加指数")); add_index->setFixedWidth(30);
    auto* remove_index = new QPushButton(QStringLiteral("−")); remove_index->setObjectName("ashareRemoveIndex");
    remove_index->setToolTip(QStringLiteral("移除下拉框所选指数")); remove_index->setFixedWidth(30);
    header->addWidget(add_index); header->addWidget(remove_index);
    // Hotkey list lives in a tooltip so the index ticker keeps the horizontal space.
    auto* hotkeys = new QLabel(QStringLiteral("快捷键"));
    hotkeys->setToolTip(QStringLiteral("Ctrl+K 搜索股票\nCtrl+M 切换个股 / 大盘\nCtrl+1–8 分时、日、周、月、5/15/30/60 分钟\nF5 立即刷新"));
    hotkeys->setProperty("muted", true); header->addSpacing(8); header->addWidget(hotkeys);
    theme_ = new QComboBox; theme_->setObjectName("ashareTheme");
    theme_->addItem(QStringLiteral("日间"), false); theme_->addItem(QStringLiteral("夜间"), true);
    theme_->setToolTip(QStringLiteral("切换日间 / 夜间主题，图表、盘口与表格同步切换")); header->addWidget(theme_);
    pause_ = new QPushButton(QStringLiteral("暂停刷新")); pause_->setCheckable(true); header->addWidget(pause_);
    auto* refresh_button = new QPushButton(QStringLiteral("立即刷新")); header->addWidget(refresh_button);
    root->addWidget(top_bar);
    setMinimumSize(1000, 520);
    connect(add_index, &QPushButton::clicked, this, [this, index_choice]() {
        const auto symbol = index_choice->currentData().toString();
        if (!indexes_.contains(symbol)) { indexes_.append(symbol); rebuild_indexes(); }
        select_symbol(symbol); persist();
    });
    connect(remove_index, &QPushButton::clicked, this, [this, index_choice]() {
        const auto symbol = index_choice->currentData().toString();
        indexes_.removeAll(symbol); rebuild_indexes();
        if (selected_ == symbol) select_symbol(indexes_.isEmpty() ? symbols_.value(0) : indexes_.first());
        persist();
    });

    // ---- Watchlist panel ----
    auto* splitter = new QSplitter; splitter->setHandleWidth(6);
    auto* watch = new theme::Panel;
    auto* left = new QVBoxLayout(watch); left->setContentsMargins(8, 8, 8, 8); left->setSpacing(6);
    auto* watch_title = new QLabel(QStringLiteral("自选 WATCHLIST")); watch_title->setProperty("section", true);
    left->addWidget(watch_title);
    auto* add_row = new QHBoxLayout;
    input_ = new QLineEdit; input_->setPlaceholderText(QStringLiteral("Ctrl+K · 名称 / 代码 / 拼音首字母"));
    input_->setMaxLength(40);
    input_->setObjectName("ashareSymbolInput");
    auto* add = new QPushButton(QStringLiteral("添加")); add->setObjectName("ashareAdd");
    add_row->addWidget(input_, 1); add_row->addWidget(add); left->addLayout(add_row);
    search_status_ = new QLabel; search_status_->setWordWrap(true); search_status_->hide(); left->addWidget(search_status_);
    suggestions_ = new QListWidget; suggestions_->setObjectName("ashareSearchResults");
    suggestions_->setMaximumHeight(150); suggestions_->hide();
    left->addWidget(suggestions_);
    search_timer_ = new QTimer(this); search_timer_->setSingleShot(true); search_timer_->setInterval(350);
    connect(input_, &QLineEdit::textChanged, this, [this]() {
        search_timer_->stop(); suggestions_->clear(); suggestions_->hide(); search_status_->hide();
        const auto query = input_->text().trimmed();
        if (!query.isEmpty() && normalize_symbol(query).isEmpty()) search_timer_->start();
    });
    connect(search_timer_, &QTimer::timeout, this, [this]() {
        search_status_->setText(QStringLiteral("正在搜索…")); search_status_->show();
        service_->search_stocks(input_->text().trimmed());
    });
    connect(service_, &AShareMarketService::search_ready, this, [this](const QString& query, const QVector<StockMatch>& matches, const QString& error) {
        if (query != input_->text().trimmed() || query.isEmpty()) return;
        suggestions_->clear();
        for (const auto& stock : matches) {
            auto* item = new QListWidgetItem(stock.name + "  " + stock.symbol.toUpper(), suggestions_);
            item->setData(Qt::UserRole, stock.symbol);
        }
        suggestions_->setVisible(!matches.isEmpty()); search_status_->show();
        search_status_->setText(!error.isEmpty() ? error : matches.isEmpty() ? QStringLiteral("未找到 A 股，请补充名称或输入六位代码") : QStringLiteral("点击候选加入自选 · 腾讯搜索"));
    });
    auto choose = [this, add](QListWidgetItem* item) {
        const auto symbol = item->data(Qt::UserRole).toString();
        input_->setText(symbol); add->click();
    };
    connect(suggestions_, &QListWidget::itemClicked, this, choose);
    connect(suggestions_, &QListWidget::itemActivated, this, choose);
    auto* group_row = new QHBoxLayout;
    group_filter_ = new QComboBox; group_filter_->setObjectName("ashareGroupFilter");
    group_filter_->setToolTip(QStringLiteral("按分组筛选自选；在列表上右键可把股票移入分组"));
    auto* new_group = new QPushButton(QStringLiteral("新建分组")); new_group->setObjectName("ashareNewGroup");
    delete_group_ = new QPushButton(QStringLiteral("删除分组")); delete_group_->setObjectName("ashareDeleteGroup");
    delete_group_->setToolTip(QStringLiteral("删除当前分组；组内股票保留在自选中，变为未分组"));
    group_row->addWidget(group_filter_, 1); group_row->addWidget(new_group); group_row->addWidget(delete_group_);
    left->addLayout(group_row);
    table_ = new QTableWidget(0, ColumnCount); table_->setObjectName("ashareWatchlist");
    table_->setHorizontalHeaderLabels(kColumnTitles);
    table_->verticalHeader()->hide(); table_->setShowGrid(false); table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->horizontalHeader()->setToolTip(QStringLiteral("点击表头排序（降序 → 升序 → 原顺序）；右键选择显示的列"));
    table_->setColumnWidth(Name, 128);
    for (int column = 1; column < ColumnCount; ++column) table_->setColumnWidth(column, column == Amount || column == TotalCap ? 86 : 74);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->verticalHeader()->setDefaultSectionSize(52); left->addWidget(table_, 1);
    auto* remove = new QPushButton(QStringLiteral("移除选中股票")); remove->setObjectName("ashareRemove"); left->addWidget(remove);
    auto* hint = new QLabel(QStringLiteral("红涨绿跌 · 本地保存 · 右键移入分组\n每 15 秒请求一次，实际新鲜度以行情时间为准"));
    hint->setWordWrap(true); hint->setProperty("muted", true); left->addWidget(hint);
    splitter->addWidget(watch);

    // ---- Quote, chart, order book, info tabs ----
    auto* content = new theme::Panel; auto* right = new QVBoxLayout(content); right->setContentsMargins(10, 8, 10, 8);
    right->setSpacing(6);
    // Quote header: name/code on the left, large monospaced price on the right of it.
    auto* quote_row = new QHBoxLayout; quote_row->setSpacing(16);
    title_ = new QLabel; title_->setStyleSheet("font-size:16px;font-weight:600;"); quote_row->addWidget(title_);
    price_ = new QLabel; price_->setFont(theme::mono(17, true)); quote_row->addWidget(price_);
    quote_row->addStretch(); right->addLayout(quote_row);
    details_ = new QLabel; details_->setWordWrap(true); details_->setObjectName("ashareDetails"); details_->setFont(theme::mono(9)); right->addWidget(details_);
    auto* controls = new QHBoxLayout;
    mode_ = new QComboBox; mode_->setObjectName("ashareChartMode");
    mode_->addItems({QStringLiteral("当天分时"), QStringLiteral("日 K"), QStringLiteral("周 K"), QStringLiteral("月 K"),
                     QStringLiteral("5 分钟"), QStringLiteral("15 分钟"), QStringLiteral("30 分钟"), QStringLiteral("60 分钟")});
    controls->addWidget(mode_);
    adjust_ = new QComboBox; adjust_->setObjectName("ashareAdjust");
    adjust_->addItem(QStringLiteral("前复权"), "qfq"); adjust_->addItem(QStringLiteral("不复权"), "raw");
    controls->addWidget(adjust_);
    overlay_ = new QComboBox; overlay_->setObjectName("ashareOverlay");
    overlay_->addItems({QStringLiteral("均线 MA"), QStringLiteral("布林 BOLL"), QStringLiteral("无主图指标")});
    controls->addWidget(overlay_);
    oscillator_ = new QComboBox; oscillator_->setObjectName("ashareOscillator");
    oscillator_->addItems({QStringLiteral("MACD"), QStringLiteral("KDJ"), QStringLiteral("RSI"), QStringLiteral("无副图")});
    controls->addWidget(oscillator_);
    chart_info_ = new QLabel; chart_info_->setProperty("muted", true); chart_info_->setWordWrap(true);
    controls->addWidget(chart_info_, 1);
    right->addLayout(controls);
    // Chart and info area share a draggable splitter so F10 tables can be given more room.
    auto* vertical = new QSplitter(Qt::Vertical); vertical->setObjectName("ashareChartSplit");
    vertical->setChildrenCollapsible(false); vertical->setHandleWidth(6);
    auto* chart_box = new QWidget; auto* chart_row = new QHBoxLayout(chart_box);
    chart_row->setContentsMargins(0, 0, 0, 0); chart_row->setSpacing(6);
    chart_ = new AShareChart; chart_row->addWidget(chart_, 1);
    order_book_ = new AShareOrderBook; chart_row->addWidget(order_book_);
    auto* lower = new QWidget; auto* lower_layout = new QVBoxLayout(lower);
    lower_layout->setContentsMargins(0, 4, 0, 0);
    analysis_ = new QLabel; analysis_->setWordWrap(true); analysis_->setObjectName("ashareAnalysis");
    analysis_->setFont(theme::mono(9));
    lower_layout->addWidget(analysis_);
    tabs_ = new QTabWidget; tabs_->setObjectName("ashareInfoTabs"); tabs_->setDocumentMode(true);
    tabs_->tabBar()->setDrawBase(false); // the native base line renders as a bright rule on the dark theme
    research_ = new AShareResearchPanel; research_->set_symbol(selected_);
    profile_ = new AShareProfilePanel;
    tabs_->addTab(research_, QStringLiteral("资讯 · 研报 / 新闻 / 快讯"));
    tabs_->addTab(profile_, QStringLiteral("个股资料 · F10"));
    ai_ = new AShareAiPanel; tabs_->addTab(ai_, QStringLiteral("AI 解读"));
    ai_->set_context_builder([this](bool include_position) { return ai_context(include_position); });
    // Notes and portfolio pages are created here so the stock tabs can reference them.
    notes_ = new AShareNotesPanel; portfolio_ = new ASharePortfolioPanel;
    stock_notes_ = new AShareStockNotes(notes_); tabs_->addTab(stock_notes_, QStringLiteral("本股笔记"));
    lower_layout->addWidget(tabs_, 1);
    vertical->addWidget(chart_box); vertical->addWidget(lower);
    vertical->setStretchFactor(0, 3); vertical->setStretchFactor(1, 1);
    right->addWidget(vertical, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        profile_->set_active(index == 1);
        if (index == 1) QTimer::singleShot(0, this, [this]() {
            if (auto* scroll = findChild<QScrollArea*>("ashareBodyScroll")) scroll->ensureWidgetVisible(tabs_, 0, 0);
        });
    });
    splitter->addWidget(content); splitter->setSizes({440, 940});
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1);
    // Keep the chart readable without forcing the entire native window to be tall.
    auto* body_scroll = new QScrollArea; body_scroll->setObjectName("ashareBodyScroll");
    body_scroll->setWidgetResizable(true); body_scroll->setFrameShape(QFrame::NoFrame);
    body_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    body_scroll->viewport()->setAutoFillBackground(false);
    body_scroll->setWidget(splitter); splitter->setAutoFillBackground(false);
    market_ = new AShareMarketPanel;
    auto* market_scroll = new QScrollArea; market_scroll->setObjectName("ashareMarketScroll");
    market_scroll->setWidgetResizable(true); market_scroll->setFrameShape(QFrame::NoFrame);
    market_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    market_scroll->viewport()->setAutoFillBackground(false); market_scroll->setWidget(market_);
    stack_ = new QStackedWidget; stack_->setObjectName("ashareViews");
    auto* portfolio_scroll = new QScrollArea; portfolio_scroll->setObjectName("asharePortfolioScroll");
    portfolio_scroll->setWidgetResizable(true); portfolio_scroll->setFrameShape(QFrame::NoFrame);
    portfolio_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    portfolio_scroll->viewport()->setAutoFillBackground(false); portfolio_scroll->setWidget(portfolio_);
    stack_->addWidget(body_scroll); stack_->addWidget(market_scroll); stack_->addWidget(portfolio_scroll); stack_->addWidget(notes_);
    root->addWidget(stack_, 1);
    status_ = new QLabel(QStringLiteral("正在连接行情源…")); status_->setWordWrap(true); root->addWidget(status_);
    connect(view_stock_, &QPushButton::clicked, this, [this]() { set_market_view(false); });
    connect(view_market_, &QPushButton::clicked, this, [this]() { set_market_view(true); });
    connect(view_portfolio_, &QPushButton::clicked, this, [this]() { set_view(PortfolioView); });
    connect(view_notes_, &QPushButton::clicked, this, [this]() { set_view(NotesView); });
    connect(portfolio_, &ASharePortfolioPanel::open_stock, this, &AShareScreen::open_stock);
    connect(notes_, &AShareNotesPanel::open_stock, this, &AShareScreen::open_stock);
    connect(stock_notes_, &AShareStockNotes::show_all, this, [this](const QString& symbol) { notes_->filter_ticker(symbol); set_view(NotesView); });
    connect(stock_notes_, &AShareStockNotes::create, this, [this](const QString& symbol, const QString& name) {
        set_view(NotesView); notes_->filter_ticker(symbol); notes_->new_note_for(symbol, name);
    });
    connect(market_, &AShareMarketPanel::open_stock, this, &AShareScreen::open_stock);

    // ---- Persisted preferences ----
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
    // Dark terminal is the default since direction B; an explicit saved choice is respected.
    theme_->setCurrentIndex(settings.value("appearance", "dark").toString() == "light" ? 0 : 1);
    apply_theme(theme_->currentIndex() == 1);
    connect(theme_, &QComboBox::currentIndexChanged, this, [this](int index) {
        apply_theme(index == 1);
        QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
        preferences.setValue("appearance", index == 1 ? "dark" : "light");
    });
    symbols_ = settings.value("symbols", QStringList{"sh600519", "sz000001", "sz300750", "sh601318"}).toStringList();
    symbols_.erase(std::remove_if(symbols_.begin(), symbols_.end(), [](const QString& s) { return normalize_symbol(s) != s || is_index(s); }), symbols_.end());
    symbols_.removeDuplicates(); symbols_ = symbols_.mid(0, 30);
    indexes_ = settings.value("indexes", QStringList{"sh000001", "sz399001", "sz399006"}).toStringList();
    indexes_.erase(std::remove_if(indexes_.begin(), indexes_.end(), [](const QString& s) { return !is_index(s); }), indexes_.end());
    indexes_.removeDuplicates(); rebuild_indexes();
    groups_ = settings.value("groups", QStringList{QStringLiteral("持仓"), QStringLiteral("观察"), QStringLiteral("短线")}).toStringList();
    groups_.removeAll(QString()); groups_.removeDuplicates(); groups_ = groups_.mid(0, 10);
    const auto assigned = settings.value("symbol_groups").toMap();
    for (auto it = assigned.cbegin(); it != assigned.cend(); ++it)
        if (symbols_.contains(it.key()) && groups_.contains(it.value().toString())) symbol_group_[it.key()] = it.value().toString();
    for (const auto& column : settings.value("hidden_columns", QVariantList{Amplitude, Amount, PB}).toList())
        if (column.toInt() > Change && column.toInt() < ColumnCount) hidden_columns_.insert(column.toInt());
    rebuild_groups();
    group_filter_->setCurrentIndex(std::max(0, group_filter_->findData(settings.value("group_filter").toString())));
    apply_column_visibility();
    adjust_->setCurrentIndex(settings.value("chart_adjust", "qfq").toString() == "raw" ? 1 : 0);
    overlay_->setCurrentIndex(std::clamp(settings.value("chart_overlay", 0).toInt(), 0, 2));
    oscillator_->setCurrentIndex(std::clamp(settings.value("chart_oscillator", 0).toInt(), 0, 3));
    chart_->set_overlay(AShareChart::Overlay(overlay_->currentIndex()));
    chart_->set_oscillator(AShareChart::Oscillator(oscillator_->currentIndex()));
    selected_ = settings.value("selected").toString();
    if (!symbols_.contains(selected_) && !indexes_.contains(selected_)) selected_ = symbols_.isEmpty() ? indexes_.value(0) : symbols_.value(0);
    research_->set_symbol(selected_); profile_->set_symbol(is_index(selected_) ? QString() : selected_);

    // ---- Watchlist actions ----
    connect(add, &QPushButton::clicked, this, [this]() {
        const auto symbol = normalize_symbol(input_->text());
        if (is_index(symbol)) {
            if (!indexes_.contains(symbol)) { indexes_.append(symbol); rebuild_indexes(); }
            input_->clear(); select_symbol(symbol); persist(); return;
        }
        if (symbol.isEmpty()) {
            if (!input_->text().trimmed().isEmpty()) { search_timer_->stop(); service_->search_stocks(input_->text().trimmed()); }
            search_status_->setText(QStringLiteral("请从搜索结果中选择股票，或输入六位代码")); search_status_->show(); return;
        }
        if (!symbols_.contains(symbol)) {
            if (symbols_.size() >= 30) { status_->setText(QStringLiteral("最多支持 30 只自选股")); return; }
            symbols_.append(symbol);
            // Adding while a group is shown puts the stock in that group.
            const auto group = group_filter_->currentData().toString();
            if (groups_.contains(group)) symbol_group_[symbol] = group;
        }
        input_->clear(); selected_ = symbol; rebuild_watchlist(); select_symbol(symbol); persist();
    });
    connect(input_, &QLineEdit::returnPressed, add, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (is_index(selected_)) { status_->setText(QStringLiteral("请使用顶部的移除指数按钮，移除下拉框所选指数")); return; }
        symbols_.removeAll(selected_); quotes_.remove(selected_); symbol_group_.remove(selected_);
        selected_ = visible_symbols().value(0, symbols_.value(0)); rebuild_watchlist(); select_symbol(selected_); persist();
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && row < visible_.size()) { select_symbol(visible_[row]); persist(); }
    });
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        if (column == Name) sort_column_ = -1;
        else if (sort_column_ != column) { sort_column_ = column; sort_desc_ = true; }
        else if (sort_desc_) sort_desc_ = false;
        else sort_column_ = -1;
        rebuild_watchlist();
    });
    connect(table_->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, &AShareScreen::show_header_menu);
    connect(table_, &QTableWidget::customContextMenuRequested, this, &AShareScreen::show_row_menu);
    connect(group_filter_, &QComboBox::currentIndexChanged, this, [this]() {
        delete_group_->setEnabled(groups_.contains(group_filter_->currentData().toString()));
        rebuild_watchlist(); persist();
    });
    connect(new_group, &QPushButton::clicked, this, [this]() {
        if (groups_.size() >= 10) { status_->setText(QStringLiteral("最多 10 个分组")); return; }
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("新建分组"), QStringLiteral("分组名称（最多 8 个字）"),
                                                QLineEdit::Normal, {}, &ok).trimmed().left(8);
        if (!ok || name.isEmpty()) return;
        if (groups_.contains(name) || name == QStringLiteral("全部自选") || name == QStringLiteral("未分组")) {
            status_->setText(QStringLiteral("分组名称已存在")); return;
        }
        groups_.append(name); rebuild_groups(); group_filter_->setCurrentIndex(group_filter_->findData(name)); persist();
    });
    connect(delete_group_, &QPushButton::clicked, this, [this]() {
        const auto group = group_filter_->currentData().toString();
        if (!groups_.contains(group)) return;
        groups_.removeAll(group);
        for (auto it = symbol_group_.begin(); it != symbol_group_.end();) it = it.value() == group ? symbol_group_.erase(it) : std::next(it);
        rebuild_groups(); group_filter_->setCurrentIndex(0); rebuild_watchlist(); persist();
    });

    // ---- Chart controls ----
    connect(mode_, &QComboBox::currentIndexChanged, this, [this]() {
        chart_->clear(QStringLiteral("正在加载…")); chart_->set_daily(!intraday());
        chart_->set_minute_bars(!intraday() && is_minute_period(current_period()));
        update_chart_controls(); render_selected(); fetch_selected_kline(false);
    });
    connect(adjust_, &QComboBox::currentIndexChanged, this, [this]() {
        chart_->clear(QStringLiteral("正在加载…")); render_selected(); fetch_selected_kline(false); persist();
    });
    connect(overlay_, &QComboBox::currentIndexChanged, this, [this](int index) {
        chart_->set_overlay(AShareChart::Overlay(index)); persist();
    });
    connect(oscillator_, &QComboBox::currentIndexChanged, this, [this](int index) {
        chart_->set_oscillator(AShareChart::Oscillator(index)); persist();
    });
    connect(refresh_button, &QPushButton::clicked, this, [this]() { refresh(true); });
    connect(pause_, &QPushButton::toggled, this, [this](bool paused) {
        pause_->setText(paused ? QStringLiteral("恢复刷新") : QStringLiteral("暂停刷新"));
        if (!paused) refresh(true); else status_->setText(QStringLiteral("自动刷新已暂停 · 可点击立即刷新"));
    });

    // ---- Data ----
    connect(service_, &AShareMarketService::quote_ready, this, [this](const Quote& quote) {
        if (!symbols_.contains(quote.symbol) && !indexes_.contains(quote.symbol)) return;
        quotes_[quote.symbol] = quote; errors_.remove("quote" + quote.symbol); rebuild_watchlist();
        if (quote.symbol == selected_) { render_selected(); emit data_loaded(); }
    });
    connect(service_, &AShareMarketService::kline_ready, this, [this](const QString& key, const QString& symbol, const QVector<Candle>& candles) {
        if (!symbols_.contains(symbol) && !indexes_.contains(symbol)) return;
        candles_[key] = candles; kline_fetched_[key] = QDateTime::currentDateTimeUtc(); errors_.remove(key);
        if (symbol == selected_) { render_selected(); emit data_loaded(); }
    });
    connect(service_, &AShareMarketService::trades_ready, this, [this](const QString& symbol, const QVector<Trade>& trades) {
        if (symbol != selected_) return;
        errors_.remove("trades" + symbol); order_book_->set_trades(symbol, trades, QDateTime::currentDateTimeUtc());
        emit data_loaded();
    });
    connect(service_, &AShareMarketService::failed, this, [this](const QString& symbol, const QString& kind, const QString& error) {
        if (!symbols_.contains(symbol) && !indexes_.contains(symbol)) return;
        errors_[kind.contains('|') ? kind : kind + symbol] = error;
        if (kind == "trades") { if (symbol == selected_) order_book_->set_trades_error(error); return; }
        rebuild_watchlist();
        if (symbol == selected_) render_selected();
    });
    timer_ = new QTimer(this); timer_->setInterval(15000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (!isVisible() || pause_->isChecked()) return;
        if (is_market_session(QDateTime::currentDateTimeUtc())) refresh();
        else render_selected();
    });
    // Terminal hotkeys, scoped to this board so they do not hijack other workbench panels.
    auto hotkey = [this](const QKeySequence& keys, auto handler) {
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    hotkey(QKeySequence(Qt::CTRL | Qt::Key_K), [this]() { input_->setFocus(Qt::ShortcutFocusReason); input_->selectAll(); });
    hotkey(QKeySequence(Qt::Key_F5), [this]() { if (market_view()) market_->refresh(true); else refresh(true); });
    hotkey(QKeySequence(Qt::CTRL | Qt::Key_M), [this]() { set_market_view(!market_view()); });
    for (int i = 0; i < 8; ++i)
        hotkey(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), [this, i]() { select_period(i); });
    update_chart_controls();
    timer_->start(); rebuild_watchlist(); render_selected();
    QTimer::singleShot(0, this, [this]() { refresh(true); });
}

void AShareScreen::paintEvent(QPaintEvent*) { theme::paint_background(this); }
void AShareScreen::apply_theme(bool dark) {
    setProperty("darkTheme", dark); setStyleSheet(theme::stylesheet(dark));
    const auto& c = theme::palette(dark);
    analysis_->setStyleSheet(QStringLiteral("color:%1;padding:8px 10px;background:%2;border:1px solid %3;border-radius:4px;")
                                 .arg(c.muted.name(), c.panel_alt.name(), c.border.name()));
    research_->set_dark(dark); profile_->set_dark(dark);
    // Item colours come from the palette, so rebuild the market views for the new theme.
    if (table_) { rebuild_watchlist(); render_selected(); }
    if (market_) market_->rerender();
    if (ai_) ai_->set_dark(dark);
    if (portfolio_) portfolio_->rerender();
    if (notes_) notes_->rerender();
    for (auto* widget : findChildren<QWidget*>()) widget->update();
    update();
}

bool AShareScreen::intraday() const { return mode_->currentIndex() <= 0; }
KPeriod AShareScreen::current_period() const { return kPeriods.value(mode_->currentIndex(), KPeriod::Day); }
Adjust AShareScreen::current_adjust() const { return adjust_->currentIndex() == 1 ? Adjust::None : Adjust::Forward; }
QString AShareScreen::analysis_key() const {
    return intraday() ? kline_key(selected_, KPeriod::Day, current_adjust()) : kline_key(selected_, current_period(), current_adjust());
}
bool AShareScreen::has_daily() const { return candles_.contains(kline_key(selected_, KPeriod::Day, current_adjust())); }
void AShareScreen::fetch_selected_kline(bool force) {
    if (selected_.isEmpty()) return;
    const auto key = analysis_key();
    if (!force && kline_fetched_.contains(key) && kline_fetched_[key].secsTo(QDateTime::currentDateTimeUtc()) < 60) return;
    service_->fetch_kline(selected_, intraday() ? KPeriod::Day : current_period(), current_adjust());
}

void AShareScreen::update_chart_controls() {
    const bool index = is_index(selected_);
    const bool minute = !intraday() && is_minute_period(current_period());
    mode_->setItemText(1, index ? QStringLiteral("日 K · 指数点位") : QStringLiteral("日 K"));
    // Hidden where it does not apply; the chart caption states the basis actually shown.
    adjust_->setVisible(!index && !minute);
    adjust_->setToolTip(index ? QStringLiteral("指数为原始点位，不适用复权")
                        : minute ? QStringLiteral("分钟 K 线数据源仅提供不复权数据")
                                 : QStringLiteral("日 / 周 / 月 K 复权口径；分时与五档始终为实际成交价"));
    overlay_->setEnabled(!intraday()); oscillator_->setEnabled(!intraday());
}

void AShareScreen::rebuild_groups() {
    QSignalBlocker blocker(group_filter_);
    const auto current = group_filter_->currentData().toString();
    group_filter_->clear();
    group_filter_->addItem(QStringLiteral("全部自选"), QString());
    for (const auto& group : groups_) group_filter_->addItem(group, group);
    group_filter_->addItem(QStringLiteral("未分组"), QStringLiteral("__none__"));
    group_filter_->setCurrentIndex(std::max(0, group_filter_->findData(current)));
    delete_group_->setEnabled(groups_.contains(group_filter_->currentData().toString()));
}
void AShareScreen::apply_column_visibility() {
    for (int column = 0; column < ColumnCount; ++column) table_->setColumnHidden(column, hidden_columns_.contains(column));
}
void AShareScreen::show_header_menu(const QPoint& pos) {
    QMenu menu(this); menu.setStyleSheet(theme::menu_stylesheet(theme_->currentIndex() == 1));
    for (int column = VolumeRatio; column < ColumnCount; ++column) {
        auto* action = menu.addAction(kColumnTitles[column]);
        action->setCheckable(true); action->setChecked(!hidden_columns_.contains(column));
        connect(action, &QAction::toggled, this, [this, column](bool shown) {
            if (shown) hidden_columns_.remove(column); else hidden_columns_.insert(column);
            apply_column_visibility(); persist();
        });
    }
    menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}
void AShareScreen::show_row_menu(const QPoint& pos) {
    const int row = table_->rowAt(pos.y());
    if (row < 0 || row >= visible_.size()) return;
    const auto symbol = visible_[row];
    QMenu menu(this); menu.setStyleSheet(theme::menu_stylesheet(theme_->currentIndex() == 1));
    auto* move = menu.addMenu(QStringLiteral("移动到分组"));
    for (const auto& group : groups_) {
        auto* action = move->addAction(group); action->setCheckable(true);
        action->setChecked(symbol_group_.value(symbol) == group);
        connect(action, &QAction::triggered, this, [this, symbol, group]() { symbol_group_[symbol] = group; rebuild_watchlist(); persist(); });
    }
    if (groups_.isEmpty()) move->setEnabled(false);
    auto* ungroup = menu.addAction(QStringLiteral("移出分组"));
    ungroup->setEnabled(symbol_group_.contains(symbol));
    connect(ungroup, &QAction::triggered, this, [this, symbol]() { symbol_group_.remove(symbol); rebuild_watchlist(); persist(); });
    menu.addSeparator();
    auto* remove = menu.addAction(QStringLiteral("从自选移除"));
    connect(remove, &QAction::triggered, this, [this, symbol]() {
        select_symbol(symbol); findChild<QPushButton*>("ashareRemove")->click();
    });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

void AShareScreen::rebuild_indexes() {
    while (auto* item = index_cards_->takeAt(0)) { delete item->widget(); delete item; }
    index_buttons_.clear();
    for (const auto& symbol : indexes_) {
        auto* button = new QPushButton; button->setObjectName("index_" + symbol);
        button->setProperty("ticker", true); button->setCheckable(true); button->setFont(theme::mono(9));
        index_cards_->addWidget(button); index_buttons_[symbol] = button;
        connect(button, &QPushButton::clicked, this, [this, symbol]() { select_symbol(symbol); persist(); });
    }
    index_cards_->addStretch(); render_indexes();
}
void AShareScreen::render_indexes() {
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto& item : index_catalog()) {
        auto* button = index_buttons_.value(item.symbol, nullptr); if (!button) continue;
        button->setChecked(item.symbol == selected_);
        const auto& c = theme::palette(theme_->currentIndex() == 1);
        // Single-line ticker: "上证指数 3701.00 −1.76%"; stale or failed quotes are greyed and labelled.
        QString text = item.name + QStringLiteral("  加载中…"); QColor color = c.muted;
        if (quotes_.contains(item.symbol)) {
            const auto& q = quotes_[item.symbol];
            const bool stale = q.time.date() != now.toTimeZone(QTimeZone("Asia/Shanghai")).date() ||
                (is_market_session(now) && q.time.secsTo(now) > 120);
            text = item.name + "  " + QString::number(q.last, 'f', 2) + "  " + (q.change_percent() >= 0 ? "+" : "") +
                   QString::number(q.change_percent(), 'f', 2) + "%" + (stale ? QStringLiteral(" 旧") : QString());
            color = stale ? c.muted : q.change_percent() >= 0 ? c.rise : c.fall;
            button->setToolTip(item.symbol.toUpper() + "\n" + q.time.toString("yyyy-MM-dd HH:mm:ss") +
                               (stale ? QStringLiteral("\n旧行情：非今日或超过 2 分钟未更新") : QString()));
        }
        if (errors_.contains("quote" + item.symbol)) { text += QStringLiteral(" 刷新失败"); color = c.muted; }
        button->setText(text); button->setStyleSheet("color:" + color.name() + ";");
    }
}
void AShareScreen::persist() {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
    settings.setValue("symbols", symbols_); settings.setValue("selected", selected_);
    settings.setValue("indexes", indexes_);
    settings.setValue("groups", groups_);
    QVariantMap assigned;
    for (auto it = symbol_group_.cbegin(); it != symbol_group_.cend(); ++it) assigned[it.key()] = it.value();
    settings.setValue("symbol_groups", assigned);
    settings.setValue("group_filter", group_filter_->currentData().toString());
    QVariantList hidden; for (int column : hidden_columns_) hidden.append(column);
    settings.setValue("hidden_columns", hidden);
    settings.setValue("chart_adjust", adjust_->currentData().toString());
    settings.setValue("chart_overlay", overlay_->currentIndex());
    settings.setValue("chart_oscillator", oscillator_->currentIndex());
}
QVariantMap AShareScreen::save_state() const {
    return {{"selected", selected_}, {"daily", mode_->currentIndex() == 1}, {"period", mode_->currentIndex()},
            {"view", QStringList{"stock", "market", "portfolio", "notes"}.value(view(), "stock")}};
}
void AShareScreen::restore_state(const QVariantMap& state) {
    const auto symbol = state.value("selected").toString();
    if (symbols_.contains(symbol) || indexes_.contains(symbol)) select_symbol(symbol);
    // "period" was added later; older saved layouts only carry the daily flag.
    if (state.contains("period")) select_period(state.value("period").toInt());
    else select_daily(state.value("daily").toBool());
    const auto view = state.value("view").toString();
    set_view(view == "market" ? MarketView : view == "portfolio" ? PortfolioView : view == "notes" ? NotesView : StockView);
}
bool AShareScreen::market_view() const { return stack_->currentIndex() == MarketView; }
int AShareScreen::view() const { return stack_->currentIndex(); }
void AShareScreen::set_market_view(bool market) { set_view(market ? MarketView : StockView); }
void AShareScreen::set_view(int view) {
    view = std::clamp(view, int(StockView), int(NotesView));
    stack_->setCurrentIndex(view);
    view_stock_->setChecked(view == StockView); view_market_->setChecked(view == MarketView);
    view_portfolio_->setChecked(view == PortfolioView); view_notes_->setChecked(view == NotesView);
    status_->setVisible(view == StockView); // other views show their own source / freshness line
    market_->set_active(view == MarketView);
    portfolio_->set_active(view == PortfolioView);
}
QString AShareScreen::ai_context(bool include_position) const {
    if (selected_.isEmpty() || is_index(selected_)) return QStringLiteral("（未选中个股）");
    QStringList lines;
    const auto& q = quotes_.value(selected_);
    if (quotes_.contains(selected_)) {
        lines << QStringLiteral("股票：%1 %2，行情时间 %3（北京时间，腾讯公开行情）").arg(q.name, selected_.toUpper(), q.time.toString("yyyy-MM-dd HH:mm:ss"));
        lines << QStringLiteral("最新 %1，昨收 %2，涨跌幅 %3%，今开 %4，最高 %5，最低 %6，成交额 %7 亿元")
                     .arg(q.last, 0, 'f', 2).arg(q.previous, 0, 'f', 2).arg(q.change_percent(), 0, 'f', 2).arg(q.open, 0, 'f', 2)
                     .arg(q.high, 0, 'f', 2).arg(q.low, 0, 'f', 2).arg(q.amount / 1e8, 0, 'f', 2);
        lines << QStringLiteral("量比 %1，换手率 %2%，振幅 %3%，总市值 %4 亿元，PE(TTM) %5，PB %6，涨停价 %7，跌停价 %8")
                     .arg(q.volume_ratio, 0, 'f', 2).arg(q.turnover_rate, 0, 'f', 2).arg(q.amplitude, 0, 'f', 2).arg(q.total_cap / 1e8, 0, 'f', 0)
                     .arg(q.pe_ttm > 0 ? QString::number(q.pe_ttm, 'f', 1) : QStringLiteral("亏损或无")).arg(q.pb, 0, 'f', 2)
                     .arg(q.limit_up, 0, 'f', 2).arg(q.limit_down, 0, 'f', 2);
        const double bids = std::accumulate(q.bids.begin(), q.bids.end(), 0.0, [](double s, const BookLevel& l) { return s + l.volume; });
        const double asks = std::accumulate(q.asks.begin(), q.asks.end(), 0.0, [](double s, const BookLevel& l) { return s + l.volume; });
        if (bids + asks > 0) lines << QStringLiteral("五档委比 %1%，外盘 %2 手，内盘 %3 手").arg((bids - asks) / (bids + asks) * 100, 0, 'f', 1)
                                          .arg(q.outer / 100, 0, 'f', 0).arg(q.inner / 100, 0, 'f', 0);
    } else lines << QStringLiteral("股票：%1（实时行情尚未加载）").arg(selected_.toUpper());
    const auto key = kline_key(selected_, KPeriod::Day, Adjust::Forward);
    if (candles_.contains(key)) {
        const auto& c = candles_[key];
        const auto ind = compute_indicators(c);
        const int i = c.size() - 1;
        auto f = [](double v, int d = 2) { return has_value(v) ? QString::number(v, 'f', d) : QStringLiteral("—"); };
        lines << QStringLiteral("日 K 技术指标（前复权，截至 %1）：MA5 %2，MA10 %3，MA20 %4，MA60 %5；MACD DIF %6 DEA %7 柱 %8；KDJ K %9 D %10 J %11；RSI14 %12；BOLL 上 %13 中 %14 下 %15")
                     .arg(c[i].date.toString(Qt::ISODate), f(ind.ma5[i]), f(ind.ma10[i]), f(ind.ma20[i]), f(ind.ma60[i]), f(ind.dif[i], 3), f(ind.dea[i], 3), f(ind.macd[i], 3), f(ind.k[i]))
                     .arg(f(ind.d[i]), f(ind.j[i]), f(ind.rsi[i], 1), f(ind.boll_upper[i]), f(ind.boll_mid[i]), f(ind.boll_lower[i]));
        lines << QStringLiteral("最近 30 个交易日（前复权）：日期 开 高 低 收 成交量(万股)");
        for (int k = std::max(0, int(c.size()) - 30); k < c.size(); ++k)
            lines << QStringLiteral("%1 %2 %3 %4 %5 %6").arg(c[k].date.toString("MM-dd")).arg(c[k].open, 0, 'f', 2).arg(c[k].high, 0, 'f', 2)
                         .arg(c[k].low, 0, 'f', 2).arg(c[k].close, 0, 'f', 2).arg(c[k].volume / 1e4, 0, 'f', 1);
    } else lines << QStringLiteral("日 K：未加载（若该股数据源不提供前复权序列，则此处为空）");
    const auto f10 = profile_->summary_for(selected_);
    lines << (f10.isEmpty() ? QStringLiteral("F10：未加载（打开「个股资料」页签后才会获取）") : QStringLiteral("F10 摘要（东方财富）：") + QChar('\n') + f10);
    const auto market = market_->summary();
    if (!market.isEmpty()) lines << market;
    if (include_position) lines << portfolio_->position_summary(selected_);
    return lines.join(QChar('\n'));
}
void AShareScreen::open_stock(const QString& symbol) {
    if (normalize_symbol(symbol) != symbol) return;
    if (!symbols_.contains(symbol)) {
        if (symbols_.size() >= 30) {
            set_market_view(false);
            status_->setText(QStringLiteral("自选已满 30 只，未加入 %1；请先移除一只再从大盘打开").arg(symbol.toUpper()));
            return;
        }
        symbols_.append(symbol);
    }
    set_market_view(false); rebuild_watchlist(); select_symbol(symbol); persist();
}
void AShareScreen::select_daily(bool value) { mode_->setCurrentIndex(value ? 1 : 0); }
void AShareScreen::select_period(int index) { mode_->setCurrentIndex(std::clamp(index, 0, mode_->count() - 1)); }
QVariantMap AShareScreen::diagnostics() const {
    return {{"selected", selected_}, {"period", mode_->currentText()}, {"has_quote", has_quote()},
            {"candles", chart_->candle_count()}, {"book_levels", order_book_->level_count()},
            {"trades", order_book_->trade_count()}, {"visible_rows", table_->rowCount()},
            {"kline_error", errors_.value(analysis_key())}, {"trades_error", errors_.value("trades" + selected_)},
            {"last", quotes_.value(selected_).last},
            {"last_average", quotes_.value(selected_).minutes.isEmpty() ? 0.0 : quotes_.value(selected_).minutes.last().average}};
}

QStringList AShareScreen::visible_symbols() const {
    const auto filter = group_filter_->currentData().toString();
    QStringList out;
    for (const auto& symbol : symbols_) {
        const auto group = symbol_group_.value(symbol);
        if (filter.isEmpty() || (filter == "__none__" ? group.isEmpty() : group == filter)) out.append(symbol);
    }
    if (sort_column_ > Name) {
        std::stable_sort(out.begin(), out.end(), [this](const QString& a, const QString& b) {
            const double va = quotes_.contains(a) ? column_value(quotes_[a], sort_column_) : kNoValue;
            const double vb = quotes_.contains(b) ? column_value(quotes_[b], sort_column_) : kNoValue;
            if (!has_value(va) || !has_value(vb)) return has_value(va) && !has_value(vb); // missing last
            return sort_desc_ ? va > vb : va < vb;
        });
    }
    return out;
}
void AShareScreen::rebuild_watchlist() {
    render_indexes();
    QSignalBlocker blocker(table_);
    visible_ = visible_symbols();
    table_->horizontalHeader()->setSortIndicatorShown(sort_column_ > Name);
    if (sort_column_ > Name) table_->horizontalHeader()->setSortIndicator(sort_column_, sort_desc_ ? Qt::DescendingOrder : Qt::AscendingOrder);
    table_->setRowCount(visible_.size());
    const auto now = QDateTime::currentDateTimeUtc();
    const auto& pal = theme::palette(theme_->currentIndex() == 1);
    for (int row = 0; row < visible_.size(); ++row) {
        const auto symbol = visible_[row];
        const auto quote = quotes_.value(symbol);
        const bool available = quotes_.contains(symbol);
        const bool stale = available && (quote.time.date() != now.toTimeZone(QTimeZone("Asia/Shanghai")).date() ||
                                        (is_market_session(now) && quote.time.secsTo(now) > 120));
        const auto group = symbol_group_.value(symbol);
        for (int column = 0; column < ColumnCount; ++column) {
            const QString value = column == Name
                ? (available ? quote.name : QStringLiteral("待加载")) + (stale ? QStringLiteral(" · 旧行情") : QString()) + "\n" +
                      symbol.toUpper() + (group.isEmpty() ? QString() : " · " + group)
                : available ? column_text(quote, column) : "—";
            auto* item = new QTableWidgetItem(value);
            if (column) {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                item->setFont(theme::mono(9));
                if (column == Last || column == Change) item->setForeground(quote.last >= quote.previous ? pal.rise : pal.fall);
            }
            item->setToolTip(errors_.contains("quote" + symbol) ? errors_.value("quote" + symbol) : quote.time.toString("yyyy-MM-dd HH:mm:ss"));
            if (stale || errors_.contains("quote" + symbol)) item->setForeground(pal.muted);
            table_->setItem(row, column, item);
        }
    }
    table_->selectRow(visible_.indexOf(selected_));
}
void AShareScreen::select_symbol(const QString& symbol) {
    selected_ = symbol;
    const bool index = is_index(symbol);
    if (index) {
        auto* choice = findChild<QComboBox*>("ashareIndexChoice"); choice->setCurrentIndex(choice->findData(symbol));
    }
    tabs_->setVisible(!index); order_book_->setVisible(!index);
    research_->set_symbol(index ? QString() : symbol);
    profile_->set_symbol(index ? QString() : symbol);
    ai_->set_symbol(index ? QString() : symbol, quotes_.value(symbol).name);
    stock_notes_->set_symbol(index ? QString() : symbol, quotes_.value(symbol).name);
    update_chart_controls(); render_indexes();
    { QSignalBlocker blocker(table_); table_->setCurrentCell(-1, -1); table_->selectRow(visible_.indexOf(symbol)); }
    chart_->clear(QStringLiteral("正在加载…")); order_book_->clear(QStringLiteral("正在加载盘口…")); render_selected();
    if (!symbol.isEmpty()) {
        service_->fetch_quote(symbol); fetch_selected_kline(false);
        if (!index) service_->fetch_trades(symbol);
    }
}
void AShareScreen::refresh(bool force) {
    for (const auto& symbol : indexes_) service_->fetch_quote(symbol);
    for (const auto& symbol : symbols_) service_->fetch_quote(symbol);
    fetch_selected_kline(force);
    if (!selected_.isEmpty() && !is_index(selected_)) service_->fetch_trades(selected_);
}
void AShareScreen::render_selected() {
    render_indexes();
    const bool index = is_index(selected_);
    tabs_->setVisible(!index && !selected_.isEmpty()); order_book_->setVisible(!index && !selected_.isEmpty());
    update_chart_controls();
    if (selected_.isEmpty()) {
        title_->setText(QStringLiteral("添加第一只自选股")); price_->setText("—"); details_->clear(); analysis_->clear(); chart_info_->clear();
        chart_->clear(QStringLiteral("在左侧输入股票代码，开始查看行情")); status_->setText(QStringLiteral("自选列表为空")); return;
    }
    const bool kline = !intraday();
    const auto period = current_period();
    const auto basis = index ? QStringLiteral("指数点位") : is_minute_period(period) && kline ? QStringLiteral("不复权")
                     : effective_adjust(selected_, period, current_adjust()) == Adjust::Forward ? QStringLiteral("前复权") : QStringLiteral("不复权");
    const auto now = QDateTime::currentDateTimeUtc();
    const auto today = now.toTimeZone(QTimeZone("Asia/Shanghai")).date();
    title_->setText(selected_.toUpper()); price_->setText("—"); details_->clear();
    status_->setText(QStringLiteral("正在加载腾讯公开行情…"));
    if (quotes_.contains(selected_)) {
        const auto& q = quotes_[selected_];
        title_->setText(q.name + "  " + selected_.toUpper());
        const QString sign = q.last >= q.previous ? "+" : "";
        price_->setText(QStringLiteral("%1  %2%3  %4%5%").arg(q.last, 0, 'f', 2).arg(sign).arg(q.last - q.previous, 0, 'f', 2)
                            .arg(sign).arg(q.change_percent(), 0, 'f', 2));
        const auto& pal = theme::palette(theme_->currentIndex() == 1);
        price_->setStyleSheet(QStringLiteral("color:%1;").arg((q.last >= q.previous ? pal.rise : pal.fall).name()));
        QString detail = QStringLiteral("昨收 %1    今开 %2    最高 %3    最低 %4    成交额 %5 元")
            .arg(q.previous, 0, 'f', 2).arg(q.open, 0, 'f', 2).arg(q.high, 0, 'f', 2).arg(q.low, 0, 'f', 2).arg(money(q.amount));
        if (!index && q.limit_up > 0)
            detail += QStringLiteral("    涨停 %1    跌停 %2").arg(q.limit_up, 0, 'f', 2).arg(q.limit_down, 0, 'f', 2);
        if (!index)
            detail += QStringLiteral("\n量比 %1    换手 %2    振幅 %3    总市值 %4    流通市值 %5    PE(TTM) %6    PB %7")
                .arg(column_text(q, VolumeRatio), column_text(q, Turnover), column_text(q, Amplitude), column_text(q, TotalCap),
                     q.float_cap > 0 ? money(q.float_cap) : "—", column_text(q, PE), column_text(q, PB));
        details_->setText(detail);
        if (!kline) chart_->set_quote(q);
        if (!index) order_book_->set_quote(q);
        QString state = is_market_session(now) ? QStringLiteral("交易时段 · 自动请求 15 秒") : QStringLiteral("非连续交易时段 · 自动刷新待机");
        if (q.time.date() != today) state = QStringLiteral("非今日行情 · 展示最近交易日，可能休市或数据未更新");
        else if (is_market_session(now) && q.time.secsTo(now) > 120) state = QStringLiteral("行情超过 2 分钟未更新 · 可能停牌或源延迟");
        if (pause_->isChecked()) state += QStringLiteral(" · 已暂停");
        status_->setText(QStringLiteral("腾讯公开行情  |  行情时间 %1（北京时间）  |  %2%3")
            .arg(q.time.toString("yyyy-MM-dd HH:mm:ss"), state, index ? QString() : QStringLiteral("  |  分笔：东方财富 · 约 3 秒快照")));
        if (!kline) chart_info_->setText((index ? QStringLiteral("%1 · 指数点位 · 午休压缩") : QStringLiteral("%1 · 价格蓝 / 均价黄 · 午休压缩")).arg(q.date.toString("yyyy-MM-dd")));
    }
    if (kline) chart_info_->setText(QStringLiteral("%1 · %2 · 滚轮缩放 · 鼠标查看").arg(period_name(period), basis));
    const auto key = analysis_key();
    const auto analysis_period = kline ? period : KPeriod::Day;
    const auto analysis_basis = kline ? basis : index ? QStringLiteral("指数点位")
        : effective_adjust(selected_, KPeriod::Day, current_adjust()) == Adjust::Forward ? QStringLiteral("前复权") : QStringLiteral("不复权");
    analysis_->setText(QStringLiteral("正在加载%1，以计算 MA / MACD / KDJ / RSI / BOLL…").arg(period_name(analysis_period)));
    if (candles_.contains(key)) {
        const auto& candles = candles_[key];
        if (kline) chart_->set_candles(candles);
        const auto ind = compute_indicators(candles);
        const int i = candles.size() - 1;
        const auto& last = candles[i];
        const QString when = is_minute_period(analysis_period) ? last.date.toString("yyyy-MM-dd") + " " + last.time.toString("HH:mm")
                                                               : last.date.toString("yyyy-MM-dd");
        analysis_->setToolTip(QStringLiteral("最新一根 K 线未收盘时指标随行情变化；历史不足指标窗口时显示 —。"));
        analysis_->setText(QStringLiteral("%1指标  |  MA5 %2  MA10 %3  MA20 %4  MA60 %5  |  MACD DIF %6  DEA %7  柱 %8\n"
                                          "KDJ  K %9  D %10  J %11  |  RSI14 %12  |  BOLL 上 %13  中 %14  下 %15  |  截至 %16 · %17 · %18 根")
            .arg(period_name(analysis_period), fmt(ind.ma5[i]), fmt(ind.ma10[i]), fmt(ind.ma20[i]), fmt(ind.ma60[i]),
                 fmt(ind.dif[i], 3), fmt(ind.dea[i], 3), fmt(ind.macd[i], 3))
            .arg(fmt(ind.k[i]), fmt(ind.d[i]), fmt(ind.j[i]), fmt(ind.rsi[i], 1), fmt(ind.boll_upper[i]), fmt(ind.boll_mid[i]),
                 fmt(ind.boll_lower[i]), when, analysis_basis)
            .arg(candles.size()));
    }
    const auto quote_error = errors_.value("quote" + selected_);
    const auto kline_error = errors_.value(key);
    if (!quote_error.isEmpty()) status_->setText(QStringLiteral("行情刷新失败（保留数据时间 %1）：%2")
        .arg(quotes_.contains(selected_) ? quotes_[selected_].time.toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("无"), quote_error));
    if (!kline_error.isEmpty()) analysis_->setText(QStringLiteral("%1刷新失败，已有指标未更新：%2").arg(period_name(analysis_period), kline_error));
    if (kline && !kline_error.isEmpty() && !candles_.contains(key)) chart_->clear(kline_error);
    if (!kline && !quote_error.isEmpty() && !quotes_.contains(selected_)) chart_->clear(quote_error);
}
} // namespace fincept::screens
