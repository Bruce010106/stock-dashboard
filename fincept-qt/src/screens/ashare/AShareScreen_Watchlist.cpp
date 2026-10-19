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

QWidget* AShareScreen::build_watchlist() {
    auto* watch = new theme::Panel;
    auto* left = new QVBoxLayout(watch);
    left->setContentsMargins(8, 8, 8, 8);
    left->setSpacing(6);
    auto* watch_title = new QLabel(QStringLiteral("自选 WATCHLIST"));
    watch_title->setProperty("section", true);
    left->addWidget(watch_title);
    auto* add_row = new QHBoxLayout;
    input_ = new QLineEdit;
    input_->setPlaceholderText(QStringLiteral("Ctrl+K · 名称 / 代码 / 拼音首字母"));
    input_->setMaxLength(40);
    input_->setObjectName("ashareSymbolInput");
    auto* add = new QPushButton(QStringLiteral("添加"));
    add->setObjectName("ashareAdd");
    add_row->addWidget(input_, 1);
    add_row->addWidget(add);
    left->addLayout(add_row);
    search_status_ = new QLabel;
    search_status_->setWordWrap(true);
    search_status_->hide();
    left->addWidget(search_status_);
    suggestions_ = new QListWidget;
    suggestions_->setObjectName("ashareSearchResults");
    suggestions_->setMaximumHeight(150);
    suggestions_->hide();
    left->addWidget(suggestions_);
    search_timer_ = new QTimer(this);
    search_timer_->setSingleShot(true);
    search_timer_->setInterval(350);
    connect(input_, &QLineEdit::textChanged, this, [this]() {
        search_timer_->stop();
        suggestions_->clear();
        suggestions_->hide();
        search_status_->hide();
        const auto query = input_->text().trimmed();
        if (!query.isEmpty() && normalize_symbol(query).isEmpty())
            search_timer_->start();
    });
    connect(search_timer_, &QTimer::timeout, this, [this]() {
        search_status_->setText(QStringLiteral("正在搜索…"));
        search_status_->show();
        service_->search_stocks(input_->text().trimmed());
    });
    connect(service_, &AShareMarketService::search_ready, this,
            [this](const QString& query, const QVector<StockMatch>& matches, const QString& error) {
                if (query != input_->text().trimmed() || query.isEmpty())
                    return;
                suggestions_->clear();
                for (const auto& stock : matches) {
                    auto* item = new QListWidgetItem(stock.name + "  " + stock.symbol.toUpper(), suggestions_);
                    item->setData(Qt::UserRole, stock.symbol);
                }
                suggestions_->setVisible(!matches.isEmpty());
                search_status_->show();
                search_status_->setText(!error.isEmpty() ? error
                                        : matches.isEmpty() ? QStringLiteral("未找到 A 股，请补充名称或输入六位代码")
                                                            : QStringLiteral("点击候选加入自选 · 腾讯搜索"));
            });
    auto choose = [this, add](QListWidgetItem* item) {
        const auto symbol = item->data(Qt::UserRole).toString();
        input_->setText(symbol);
        add->click();
    };
    connect(suggestions_, &QListWidget::itemClicked, this, choose);
    connect(suggestions_, &QListWidget::itemActivated, this, choose);
    auto* group_row = new QHBoxLayout;
    group_filter_ = new QComboBox;
    group_filter_->setObjectName("ashareGroupFilter");
    group_filter_->setToolTip(QStringLiteral("按分组筛选自选；在列表上右键可把股票移入分组"));
    auto* new_group = new QPushButton(QStringLiteral("新建分组"));
    new_group->setObjectName("ashareNewGroup");
    delete_group_ = new QPushButton(QStringLiteral("删除分组"));
    delete_group_->setObjectName("ashareDeleteGroup");
    delete_group_->setToolTip(QStringLiteral("删除当前分组；组内股票保留在自选中，变为未分组"));
    group_row->addWidget(group_filter_, 1);
    group_row->addWidget(new_group);
    group_row->addWidget(delete_group_);
    left->addLayout(group_row);
    table_ = new QTableWidget(0, ColumnCount);
    table_->setObjectName("ashareWatchlist");
    table_->setHorizontalHeaderLabels(kColumnTitles);
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->horizontalHeader()->setToolTip(QStringLiteral("点击表头排序（降序 → 升序 → 原顺序）；右键选择显示的列"));
    table_->setColumnWidth(Name, 128);
    for (int column = 1; column < ColumnCount; ++column)
        table_->setColumnWidth(column, column == Amount || column == TotalCap ? 86 : 74);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->verticalHeader()->setDefaultSectionSize(52);
    left->addWidget(table_, 1);
    auto* remove = new QPushButton(QStringLiteral("移除选中股票"));
    remove->setObjectName("ashareRemove");
    left->addWidget(remove);
    auto* hint =
        new QLabel(QStringLiteral("红涨绿跌 · 本地保存 · 右键移入分组\n每 15 秒请求一次，实际新鲜度以行情时间为准"));
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    left->addWidget(hint);

    // ---- Watchlist actions ----
    connect(add, &QPushButton::clicked, this, [this]() {
        const auto symbol = normalize_symbol(input_->text());
        if (is_index(symbol)) {
            if (!indexes_.contains(symbol)) {
                indexes_.append(symbol);
                rebuild_indexes();
            }
            input_->clear();
            select_symbol(symbol);
            persist();
            return;
        }
        if (symbol.isEmpty()) {
            if (!input_->text().trimmed().isEmpty()) {
                search_timer_->stop();
                service_->search_stocks(input_->text().trimmed());
            }
            search_status_->setText(QStringLiteral("请从搜索结果中选择股票，或输入六位代码"));
            search_status_->show();
            return;
        }
        if (!symbols_.contains(symbol)) {
            if (symbols_.size() >= 30) {
                status_->setText(QStringLiteral("最多支持 30 只自选股"));
                return;
            }
            symbols_.append(symbol);
            // Adding while a group is shown puts the stock in that group.
            const auto group = group_filter_->currentData().toString();
            if (groups_.contains(group))
                symbol_group_[symbol] = group;
        }
        input_->clear();
        selected_ = symbol;
        rebuild_watchlist();
        select_symbol(symbol);
        persist();
    });
    connect(input_, &QLineEdit::returnPressed, add, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (is_index(selected_)) {
            status_->setText(QStringLiteral("请使用顶部的移除指数按钮，移除下拉框所选指数"));
            return;
        }
        symbols_.removeAll(selected_);
        quotes_.remove(selected_);
        symbol_group_.remove(selected_);
        selected_ = visible_symbols().value(0, symbols_.value(0));
        rebuild_watchlist();
        select_symbol(selected_);
        persist();
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && row < visible_.size()) {
            select_symbol(visible_[row]);
            persist();
        }
    });
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        if (column == Name)
            sort_column_ = -1;
        else if (sort_column_ != column) {
            sort_column_ = column;
            sort_desc_ = true;
        } else if (sort_desc_)
            sort_desc_ = false;
        else
            sort_column_ = -1;
        rebuild_watchlist();
    });
    connect(table_->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &AShareScreen::show_header_menu);
    connect(table_, &QTableWidget::customContextMenuRequested, this, &AShareScreen::show_row_menu);
    connect(new_group, &QPushButton::clicked, this, [this]() {
        if (groups_.size() >= 10) {
            status_->setText(QStringLiteral("最多 10 个分组"));
            return;
        }
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("新建分组"),
                                                QStringLiteral("分组名称（最多 8 个字）"), QLineEdit::Normal, {}, &ok)
                              .trimmed()
                              .left(8);
        if (!ok || name.isEmpty())
            return;
        if (groups_.contains(name) || name == QStringLiteral("全部自选") || name == QStringLiteral("未分组")) {
            status_->setText(QStringLiteral("分组名称已存在"));
            return;
        }
        groups_.append(name);
        rebuild_groups();
        group_filter_->setCurrentIndex(group_filter_->findData(name));
        persist();
    });
    connect(delete_group_, &QPushButton::clicked, this, [this]() {
        const auto group = group_filter_->currentData().toString();
        if (!groups_.contains(group))
            return;
        groups_.removeAll(group);
        for (auto it = symbol_group_.begin(); it != symbol_group_.end();)
            it = it.value() == group ? symbol_group_.erase(it) : std::next(it);
        rebuild_groups();
        group_filter_->setCurrentIndex(0);
        rebuild_watchlist();
        persist();
    });
    return watch;
}

void AShareScreen::rebuild_groups() {
    QSignalBlocker blocker(group_filter_);
    const auto current = group_filter_->currentData().toString();
    group_filter_->clear();
    group_filter_->addItem(QStringLiteral("全部自选"), QString());
    for (const auto& group : groups_)
        group_filter_->addItem(group, group);
    group_filter_->addItem(QStringLiteral("未分组"), QStringLiteral("__none__"));
    group_filter_->setCurrentIndex(std::max(0, group_filter_->findData(current)));
    delete_group_->setEnabled(groups_.contains(group_filter_->currentData().toString()));
}
void AShareScreen::apply_column_visibility() {
    for (int column = 0; column < ColumnCount; ++column)
        table_->setColumnHidden(column, hidden_columns_.contains(column));
}
void AShareScreen::show_header_menu(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(theme::menu_stylesheet(theme_->currentIndex() == 1));
    for (int column = VolumeRatio; column < ColumnCount; ++column) {
        auto* action = menu.addAction(kColumnTitles[column]);
        action->setCheckable(true);
        action->setChecked(!hidden_columns_.contains(column));
        connect(action, &QAction::toggled, this, [this, column](bool shown) {
            if (shown)
                hidden_columns_.remove(column);
            else
                hidden_columns_.insert(column);
            apply_column_visibility();
            persist();
        });
    }
    menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}
void AShareScreen::show_row_menu(const QPoint& pos) {
    const int row = table_->rowAt(pos.y());
    if (row < 0 || row >= visible_.size())
        return;
    const auto symbol = visible_[row];
    QMenu menu(this);
    menu.setStyleSheet(theme::menu_stylesheet(theme_->currentIndex() == 1));
    auto* move = menu.addMenu(QStringLiteral("移动到分组"));
    for (const auto& group : groups_) {
        auto* action = move->addAction(group);
        action->setCheckable(true);
        action->setChecked(symbol_group_.value(symbol) == group);
        connect(action, &QAction::triggered, this, [this, symbol, group]() {
            symbol_group_[symbol] = group;
            rebuild_watchlist();
            persist();
        });
    }
    if (groups_.isEmpty())
        move->setEnabled(false);
    auto* ungroup = menu.addAction(QStringLiteral("移出分组"));
    ungroup->setEnabled(symbol_group_.contains(symbol));
    connect(ungroup, &QAction::triggered, this, [this, symbol]() {
        symbol_group_.remove(symbol);
        rebuild_watchlist();
        persist();
    });
    menu.addSeparator();
    auto* remove = menu.addAction(QStringLiteral("从自选移除"));
    connect(remove, &QAction::triggered, this, [this, symbol]() {
        select_symbol(symbol);
        findChild<QPushButton*>("ashareRemove")->click();
    });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

void AShareScreen::rebuild_indexes() {
    while (auto* item = index_cards_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    index_buttons_.clear();
    for (const auto& symbol : indexes_) {
        auto* button = new QPushButton;
        button->setObjectName("index_" + symbol);
        button->setProperty("ticker", true);
        button->setCheckable(true);
        button->setFont(theme::mono(9));
        index_cards_->addWidget(button);
        index_buttons_[symbol] = button;
        connect(button, &QPushButton::clicked, this, [this, symbol]() {
            select_symbol(symbol);
            persist();
        });
    }
    index_cards_->addStretch();
    render_indexes();
}
void AShareScreen::render_indexes() {
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto& item : index_catalog()) {
        auto* button = index_buttons_.value(item.symbol, nullptr);
        if (!button)
            continue;
        button->setChecked(item.symbol == selected_);
        const auto& c = theme::palette(theme_->currentIndex() == 1);
        // Single-line ticker: "上证指数 3701.00 −1.76%"; stale or failed quotes are greyed and labelled.
        QString text = item.name + QStringLiteral("  加载中…");
        QColor color = c.muted;
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
        if (errors_.contains("quote" + item.symbol)) {
            text += QStringLiteral(" 刷新失败");
            color = c.muted;
        }
        button->setText(text);
        button->setStyleSheet("color:" + color.name() + ";");
    }
}

QStringList AShareScreen::visible_symbols() const {
    const auto filter = group_filter_->currentData().toString();
    QStringList out;
    for (const auto& symbol : symbols_) {
        const auto group = symbol_group_.value(symbol);
        if (filter.isEmpty() || (filter == "__none__" ? group.isEmpty() : group == filter))
            out.append(symbol);
    }
    if (sort_column_ > Name) {
        std::stable_sort(out.begin(), out.end(), [this](const QString& a, const QString& b) {
            const double va = quotes_.contains(a) ? column_value(quotes_[a], sort_column_) : kNoValue;
            const double vb = quotes_.contains(b) ? column_value(quotes_[b], sort_column_) : kNoValue;
            if (!has_value(va) || !has_value(vb))
                return has_value(va) && !has_value(vb); // missing last
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
    if (sort_column_ > Name)
        table_->horizontalHeader()->setSortIndicator(sort_column_,
                                                     sort_desc_ ? Qt::DescendingOrder : Qt::AscendingOrder);
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
            const QString value = column == Name ? (available ? quote.name : QStringLiteral("待加载")) +
                                                       (stale ? QStringLiteral(" · 旧行情") : QString()) + "\n" +
                                                       symbol.toUpper() + (group.isEmpty() ? QString() : " · " + group)
                                  : available ? column_text(quote, column)
                                              : "—";
            auto* item = new QTableWidgetItem(value);
            if (column) {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                item->setFont(theme::mono(9));
                if (column == Last || column == Change)
                    item->setForeground(quote.last >= quote.previous ? pal.rise : pal.fall);
            }
            item->setToolTip(errors_.contains("quote" + symbol) ? errors_.value("quote" + symbol)
                                                                : quote.time.toString("yyyy-MM-dd HH:mm:ss"));
            if (stale || errors_.contains("quote" + symbol))
                item->setForeground(pal.muted);
            table_->setItem(row, column, item);
        }
    }
    table_->selectRow(visible_.indexOf(selected_));
}

} // namespace fincept::screens
