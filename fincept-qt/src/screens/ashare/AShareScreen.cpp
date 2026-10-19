#include "screens/ashare/AShareScreen.h"

#include "screens/ashare/AShareAiPanel.h"
#include "screens/ashare/AShareChart.h"
#include "screens/ashare/AShareMarketPanel.h"
#include "screens/ashare/AShareNotesPanel.h"
#include "screens/ashare/AShareOrderBook.h"
#include "screens/ashare/ASharePortfolioPanel.h"
#include "screens/ashare/AShareProfilePanel.h"
#include "screens/ashare/AShareResearchPanel.h"
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
    root->setContentsMargins(10, 8, 10, 6);
    root->setSpacing(6);
    root->addWidget(build_top_bar());
    setMinimumSize(1000, 520);

    auto* splitter = new QSplitter;
    splitter->setHandleWidth(6);
    splitter->addWidget(build_watchlist());
    splitter->addWidget(build_stock_content());
    splitter->setSizes({440, 940});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // Keep the chart readable without forcing the entire native window to be tall.
    auto* body_scroll = new QScrollArea;
    body_scroll->setObjectName("ashareBodyScroll");
    body_scroll->setWidgetResizable(true);
    body_scroll->setFrameShape(QFrame::NoFrame);
    body_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    body_scroll->viewport()->setAutoFillBackground(false);
    body_scroll->setWidget(splitter);
    splitter->setAutoFillBackground(false);
    market_ = new AShareMarketPanel;
    auto* market_scroll = new QScrollArea;
    market_scroll->setObjectName("ashareMarketScroll");
    market_scroll->setWidgetResizable(true);
    market_scroll->setFrameShape(QFrame::NoFrame);
    market_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    market_scroll->viewport()->setAutoFillBackground(false);
    market_scroll->setWidget(market_);
    stack_ = new QStackedWidget;
    stack_->setObjectName("ashareViews");
    auto* portfolio_scroll = new QScrollArea;
    portfolio_scroll->setObjectName("asharePortfolioScroll");
    portfolio_scroll->setWidgetResizable(true);
    portfolio_scroll->setFrameShape(QFrame::NoFrame);
    portfolio_scroll->setStyleSheet("QScrollArea{background:transparent;border:0;}");
    portfolio_scroll->viewport()->setAutoFillBackground(false);
    portfolio_scroll->setWidget(portfolio_);
    stack_->addWidget(body_scroll);
    stack_->addWidget(market_scroll);
    stack_->addWidget(portfolio_scroll);
    stack_->addWidget(notes_);
    root->addWidget(stack_, 1);
    status_ = new QLabel(QStringLiteral("正在连接行情源…"));
    status_->setWordWrap(true);
    root->addWidget(status_);
    connect(view_stock_, &QPushButton::clicked, this, [this]() { set_market_view(false); });
    connect(view_market_, &QPushButton::clicked, this, [this]() { set_market_view(true); });
    connect(view_portfolio_, &QPushButton::clicked, this, [this]() { set_view(PortfolioView); });
    connect(view_notes_, &QPushButton::clicked, this, [this]() { set_view(NotesView); });
    connect(portfolio_, &ASharePortfolioPanel::open_stock, this, &AShareScreen::open_stock);
    connect(notes_, &AShareNotesPanel::open_stock, this, &AShareScreen::open_stock);
    connect(stock_notes_, &AShareStockNotes::show_all, this, [this](const QString& symbol) {
        notes_->filter_ticker(symbol);
        set_view(NotesView);
    });
    connect(stock_notes_, &AShareStockNotes::create, this, [this](const QString& symbol, const QString& name) {
        set_view(NotesView);
        notes_->filter_ticker(symbol);
        notes_->new_note_for(symbol, name);
    });
    connect(market_, &AShareMarketPanel::open_stock, this, &AShareScreen::open_stock);

    load_preferences();
    connect_preference_signals();
    connect_data();
    install_hotkeys();
    update_chart_controls();
    timer_->start();
    rebuild_watchlist();
    render_selected();
    QTimer::singleShot(0, this, [this]() { refresh(true); });
}

QWidget* AShareScreen::build_top_bar() {
    // One compact terminal bar: brand, index ticker strip, index picker, hotkey hint, global controls.
    auto* top_bar = new theme::Panel;
    top_bar->setObjectName("ashareTopBar");
    auto* header = new QHBoxLayout(top_bar);
    header->setContentsMargins(10, 5, 8, 5);
    header->setSpacing(6);
    auto* heading = new QLabel(QStringLiteral("A股终端"));
    heading->setObjectName("ashareBrand");
    header->addWidget(heading);
    // View switch: single stock vs whole-market overview.
    view_stock_ = new QPushButton(QStringLiteral("个股"));
    view_stock_->setObjectName("ashareViewStock");
    view_market_ = new QPushButton(QStringLiteral("大盘"));
    view_market_->setObjectName("ashareViewMarket");
    auto* views = new QButtonGroup(this);
    views->setExclusive(true);
    view_portfolio_ = new QPushButton(QStringLiteral("持仓"));
    view_portfolio_->setObjectName("ashareViewPortfolio");
    view_notes_ = new QPushButton(QStringLiteral("笔记"));
    view_notes_->setObjectName("ashareViewNotes");
    // No focus ring: the checked state alone marks the active view (they also have hotkeys).
    for (auto* button : {view_stock_, view_market_, view_portfolio_, view_notes_}) {
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        views->addButton(button);
        header->addWidget(button);
    }
    view_stock_->setChecked(true);
    view_market_->setToolTip(QStringLiteral("市场全景：涨跌分布、涨跌停、板块热力图、龙虎榜（Ctrl+M）"));
    header->addSpacing(6);
    auto* index_scroll = new QScrollArea;
    index_scroll->setWidgetResizable(true);
    index_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    index_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    index_scroll->setFrameShape(QFrame::NoFrame);
    index_scroll->setFixedHeight(40);
    index_scroll->setStyleSheet("QScrollArea{border:0;background:transparent;}");
    index_scroll->viewport()->setAutoFillBackground(false);
    auto* cards = new QWidget;
    cards->setObjectName("ashareIndexCards");
    cards->setStyleSheet("QWidget#ashareIndexCards{background:transparent;}");
    index_cards_ = new QHBoxLayout(cards);
    index_cards_->setContentsMargins(0, 0, 0, 0);
    index_cards_->setSpacing(2);
    index_scroll->setWidget(cards);
    header->addWidget(index_scroll, 1);
    auto* index_choice = new QComboBox;
    index_choice->setObjectName("ashareIndexChoice");
    for (const auto& item : index_catalog())
        index_choice->addItem(item.name, item.symbol);
    index_choice->setToolTip(QStringLiteral("选择指数后添加到行情条，或移除"));
    header->addWidget(index_choice);
    auto* add_index = new QPushButton(QStringLiteral("+"));
    add_index->setObjectName("ashareAddIndex");
    add_index->setToolTip(QStringLiteral("添加指数"));
    add_index->setFixedWidth(30);
    auto* remove_index = new QPushButton(QStringLiteral("−"));
    remove_index->setObjectName("ashareRemoveIndex");
    remove_index->setToolTip(QStringLiteral("移除下拉框所选指数"));
    remove_index->setFixedWidth(30);
    header->addWidget(add_index);
    header->addWidget(remove_index);
    // Hotkey list lives in a tooltip so the index ticker keeps the horizontal space.
    auto* hotkeys = new QLabel(QStringLiteral("快捷键"));
    hotkeys->setToolTip(QStringLiteral(
        "Ctrl+K 搜索股票\nCtrl+M 切换个股 / 大盘\nCtrl+1–8 分时、日、周、月、5/15/30/60 分钟\nF5 立即刷新"));
    hotkeys->setProperty("muted", true);
    header->addSpacing(8);
    header->addWidget(hotkeys);
    theme_ = new QComboBox;
    theme_->setObjectName("ashareTheme");
    theme_->addItem(QStringLiteral("日间"), false);
    theme_->addItem(QStringLiteral("夜间"), true);
    theme_->setToolTip(QStringLiteral("切换日间 / 夜间主题，图表、盘口与表格同步切换"));
    header->addWidget(theme_);
    pause_ = new QPushButton(QStringLiteral("暂停刷新"));
    pause_->setCheckable(true);
    header->addWidget(pause_);
    auto* refresh_button = new QPushButton(QStringLiteral("立即刷新"));
    header->addWidget(refresh_button);
    connect(add_index, &QPushButton::clicked, this, [this, index_choice]() {
        const auto symbol = index_choice->currentData().toString();
        if (!indexes_.contains(symbol)) {
            indexes_.append(symbol);
            rebuild_indexes();
        }
        select_symbol(symbol);
        persist();
    });
    connect(remove_index, &QPushButton::clicked, this, [this, index_choice]() {
        const auto symbol = index_choice->currentData().toString();
        indexes_.removeAll(symbol);
        rebuild_indexes();
        if (selected_ == symbol)
            select_symbol(indexes_.isEmpty() ? symbols_.value(0) : indexes_.first());
        persist();
    });
    connect(refresh_button, &QPushButton::clicked, this, [this]() { refresh(true); });
    connect(pause_, &QPushButton::toggled, this, [this](bool paused) {
        pause_->setText(paused ? QStringLiteral("恢复刷新") : QStringLiteral("暂停刷新"));
        if (!paused)
            refresh(true);
        else
            status_->setText(QStringLiteral("自动刷新已暂停 · 可点击立即刷新"));
    });
    return top_bar;
}

void AShareScreen::load_preferences() {
    // ---- Persisted preferences ----
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
    // Dark terminal is the default since direction B; an explicit saved choice is respected.
    theme_->setCurrentIndex(settings.value("appearance", "dark").toString() == "light" ? 0 : 1);
    apply_theme(theme_->currentIndex() == 1);
    symbols_ = settings.value("symbols", QStringList{"sh600519", "sz000001", "sz300750", "sh601318"}).toStringList();
    symbols_.erase(std::remove_if(symbols_.begin(), symbols_.end(),
                                  [](const QString& s) { return normalize_symbol(s) != s || is_index(s); }),
                   symbols_.end());
    symbols_.removeDuplicates();
    symbols_ = symbols_.mid(0, 30);
    indexes_ = settings.value("indexes", QStringList{"sh000001", "sz399001", "sz399006"}).toStringList();
    indexes_.erase(std::remove_if(indexes_.begin(), indexes_.end(), [](const QString& s) { return !is_index(s); }),
                   indexes_.end());
    indexes_.removeDuplicates();
    rebuild_indexes();
    groups_ =
        settings.value("groups", QStringList{QStringLiteral("持仓"), QStringLiteral("观察"), QStringLiteral("短线")})
            .toStringList();
    groups_.removeAll(QString());
    groups_.removeDuplicates();
    groups_ = groups_.mid(0, 10);
    const auto assigned = settings.value("symbol_groups").toMap();
    for (auto it = assigned.cbegin(); it != assigned.cend(); ++it)
        if (symbols_.contains(it.key()) && groups_.contains(it.value().toString()))
            symbol_group_[it.key()] = it.value().toString();
    for (const auto& column : settings.value("hidden_columns", QVariantList{Amplitude, Amount, PB}).toList())
        if (column.toInt() > Change && column.toInt() < ColumnCount)
            hidden_columns_.insert(column.toInt());
    rebuild_groups();
    group_filter_->setCurrentIndex(std::max(0, group_filter_->findData(settings.value("group_filter").toString())));
    apply_column_visibility();
    adjust_->setCurrentIndex(settings.value("chart_adjust", "qfq").toString() == "raw" ? 1 : 0);
    overlay_->setCurrentIndex(std::clamp(settings.value("chart_overlay", 0).toInt(), 0, 2));
    oscillator_->setCurrentIndex(std::clamp(settings.value("chart_oscillator", 0).toInt(), 0, 3));
    chart_->set_overlay(AShareChart::Overlay(overlay_->currentIndex()));
    chart_->set_oscillator(AShareChart::Oscillator(oscillator_->currentIndex()));
    selected_ = settings.value("selected").toString();
    if (!symbols_.contains(selected_) && !indexes_.contains(selected_))
        selected_ = symbols_.isEmpty() ? indexes_.value(0) : symbols_.value(0);
    research_->set_symbol(selected_);
    profile_->set_symbol(is_index(selected_) ? QString() : selected_);
}

// Value-change signals are connected only after load_preferences(), so restoring
// saved values never triggers persist() with half-loaded state.
void AShareScreen::connect_preference_signals() {
    connect(theme_, &QComboBox::currentIndexChanged, this, [this](int index) {
        apply_theme(index == 1);
        QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
        preferences.setValue("appearance", index == 1 ? "dark" : "light");
    });
    connect(group_filter_, &QComboBox::currentIndexChanged, this, [this]() {
        delete_group_->setEnabled(groups_.contains(group_filter_->currentData().toString()));
        rebuild_watchlist();
        persist();
    });
    // ---- Chart controls ----
    connect(mode_, &QComboBox::currentIndexChanged, this, [this]() {
        chart_->clear(QStringLiteral("正在加载…"));
        chart_->set_daily(!intraday());
        chart_->set_minute_bars(!intraday() && is_minute_period(current_period()));
        update_chart_controls();
        render_selected();
        fetch_selected_kline(false);
    });
    connect(adjust_, &QComboBox::currentIndexChanged, this, [this]() {
        chart_->clear(QStringLiteral("正在加载…"));
        render_selected();
        fetch_selected_kline(false);
        persist();
    });
    connect(overlay_, &QComboBox::currentIndexChanged, this, [this](int index) {
        chart_->set_overlay(AShareChart::Overlay(index));
        persist();
    });
    connect(oscillator_, &QComboBox::currentIndexChanged, this, [this](int index) {
        chart_->set_oscillator(AShareChart::Oscillator(index));
        persist();
    });
}

void AShareScreen::connect_data() {
    // ---- Data ----
    connect(service_, &AShareMarketService::quote_ready, this, [this](const Quote& quote) {
        if (!symbols_.contains(quote.symbol) && !indexes_.contains(quote.symbol))
            return;
        quotes_[quote.symbol] = quote;
        errors_.remove("quote" + quote.symbol);
        rebuild_watchlist();
        if (quote.symbol == selected_) {
            render_selected();
            emit data_loaded();
        }
    });
    connect(service_, &AShareMarketService::kline_ready, this,
            [this](const QString& key, const QString& symbol, const QVector<Candle>& candles) {
                if (!symbols_.contains(symbol) && !indexes_.contains(symbol))
                    return;
                candles_[key] = candles;
                kline_fetched_[key] = QDateTime::currentDateTimeUtc();
                errors_.remove(key);
                if (symbol == selected_) {
                    render_selected();
                    emit data_loaded();
                }
            });
    connect(service_, &AShareMarketService::trades_ready, this,
            [this](const QString& symbol, const QVector<Trade>& trades) {
                if (symbol != selected_)
                    return;
                errors_.remove("trades" + symbol);
                order_book_->set_trades(symbol, trades, QDateTime::currentDateTimeUtc());
                emit data_loaded();
            });
    connect(service_, &AShareMarketService::failed, this,
            [this](const QString& symbol, const QString& kind, const QString& error) {
                if (!symbols_.contains(symbol) && !indexes_.contains(symbol))
                    return;
                errors_[kind.contains('|') ? kind : kind + symbol] = error;
                if (kind == "trades") {
                    if (symbol == selected_)
                        order_book_->set_trades_error(error);
                    return;
                }
                rebuild_watchlist();
                if (symbol == selected_)
                    render_selected();
            });
    timer_ = new QTimer(this);
    timer_->setInterval(15000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (!isVisible() || pause_->isChecked())
            return;
        if (is_market_session(QDateTime::currentDateTimeUtc()))
            refresh();
        else
            render_selected();
    });
}

void AShareScreen::install_hotkeys() {
    // Terminal hotkeys, scoped to this board so they do not hijack other workbench panels.
    auto hotkey = [this](const QKeySequence& keys, auto handler) {
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    hotkey(QKeySequence(Qt::CTRL | Qt::Key_K), [this]() {
        input_->setFocus(Qt::ShortcutFocusReason);
        input_->selectAll();
    });
    hotkey(QKeySequence(Qt::Key_F5), [this]() {
        if (market_view())
            market_->refresh(true);
        else
            refresh(true);
    });
    hotkey(QKeySequence(Qt::CTRL | Qt::Key_M), [this]() { set_market_view(!market_view()); });
    for (int i = 0; i < 8; ++i)
        hotkey(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), [this, i]() { select_period(i); });
}

void AShareScreen::paintEvent(QPaintEvent*) {
    theme::paint_background(this);
}
void AShareScreen::apply_theme(bool dark) {
    setProperty("darkTheme", dark);
    setStyleSheet(theme::stylesheet(dark));
    const auto& c = theme::palette(dark);
    analysis_->setStyleSheet(
        QStringLiteral("color:%1;padding:8px 10px;background:%2;border:1px solid %3;border-radius:4px;")
            .arg(c.muted.name(), c.panel_alt.name(), c.border.name()));
    research_->set_dark(dark);
    profile_->set_dark(dark);
    // Item colours come from the palette, so rebuild the market views for the new theme.
    if (table_) {
        rebuild_watchlist();
        render_selected();
    }
    if (market_)
        market_->rerender();
    if (ai_)
        ai_->set_dark(dark);
    if (portfolio_)
        portfolio_->rerender();
    if (notes_)
        notes_->rerender();
    for (auto* widget : findChildren<QWidget*>())
        widget->update();
    update();
}

void AShareScreen::persist() {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
    settings.setValue("symbols", symbols_);
    settings.setValue("selected", selected_);
    settings.setValue("indexes", indexes_);
    settings.setValue("groups", groups_);
    QVariantMap assigned;
    for (auto it = symbol_group_.cbegin(); it != symbol_group_.cend(); ++it)
        assigned[it.key()] = it.value();
    settings.setValue("symbol_groups", assigned);
    settings.setValue("group_filter", group_filter_->currentData().toString());
    QVariantList hidden;
    for (int column : hidden_columns_)
        hidden.append(column);
    settings.setValue("hidden_columns", hidden);
    settings.setValue("chart_adjust", adjust_->currentData().toString());
    settings.setValue("chart_overlay", overlay_->currentIndex());
    settings.setValue("chart_oscillator", oscillator_->currentIndex());
}
QVariantMap AShareScreen::save_state() const {
    return {{"selected", selected_},
            {"daily", mode_->currentIndex() == 1},
            {"period", mode_->currentIndex()},
            {"view", QStringList{"stock", "market", "portfolio", "notes"}.value(view(), "stock")}};
}
void AShareScreen::restore_state(const QVariantMap& state) {
    const auto symbol = state.value("selected").toString();
    if (symbols_.contains(symbol) || indexes_.contains(symbol))
        select_symbol(symbol);
    // "period" was added later; older saved layouts only carry the daily flag.
    if (state.contains("period"))
        select_period(state.value("period").toInt());
    else
        select_daily(state.value("daily").toBool());
    const auto view = state.value("view").toString();
    set_view(view == "market"      ? MarketView
             : view == "portfolio" ? PortfolioView
             : view == "notes"     ? NotesView
                                   : StockView);
}
bool AShareScreen::market_view() const {
    return stack_->currentIndex() == MarketView;
}
int AShareScreen::view() const {
    return stack_->currentIndex();
}
void AShareScreen::set_market_view(bool market) {
    set_view(market ? MarketView : StockView);
}
void AShareScreen::set_view(int view) {
    view = std::clamp(view, int(StockView), int(NotesView));
    stack_->setCurrentIndex(view);
    view_stock_->setChecked(view == StockView);
    view_market_->setChecked(view == MarketView);
    view_portfolio_->setChecked(view == PortfolioView);
    view_notes_->setChecked(view == NotesView);
    status_->setVisible(view == StockView); // other views show their own source / freshness line
    market_->set_active(view == MarketView);
    portfolio_->set_active(view == PortfolioView);
}

void AShareScreen::open_stock(const QString& symbol) {
    if (normalize_symbol(symbol) != symbol)
        return;
    if (!symbols_.contains(symbol)) {
        if (symbols_.size() >= 30) {
            set_market_view(false);
            status_->setText(
                QStringLiteral("自选已满 30 只，未加入 %1；请先移除一只再从大盘打开").arg(symbol.toUpper()));
            return;
        }
        symbols_.append(symbol);
    }
    set_market_view(false);
    rebuild_watchlist();
    select_symbol(symbol);
    persist();
}
void AShareScreen::select_daily(bool value) {
    mode_->setCurrentIndex(value ? 1 : 0);
}
void AShareScreen::select_period(int index) {
    mode_->setCurrentIndex(std::clamp(index, 0, mode_->count() - 1));
}
QVariantMap AShareScreen::diagnostics() const {
    return {{"selected", selected_},
            {"period", mode_->currentText()},
            {"has_quote", has_quote()},
            {"candles", chart_->candle_count()},
            {"book_levels", order_book_->level_count()},
            {"trades", order_book_->trade_count()},
            {"visible_rows", table_->rowCount()},
            {"kline_error", errors_.value(analysis_key())},
            {"trades_error", errors_.value("trades" + selected_)},
            {"last", quotes_.value(selected_).last},
            {"last_average",
             quotes_.value(selected_).minutes.isEmpty() ? 0.0 : quotes_.value(selected_).minutes.last().average}};
}

} // namespace fincept::screens
