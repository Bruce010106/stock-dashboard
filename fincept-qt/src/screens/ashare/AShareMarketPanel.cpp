#include "screens/ashare/AShareMarketPanel.h"

#include "screens/ashare/AShareTheme.h"
#include "services/ashare/AShareMarketService.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {
using namespace services::ashare;
namespace {
QString money(double value) {
    const double a = std::abs(value);
    const QString sign = value < 0 ? "-" : "";
    if (a >= 1e12)
        return sign + QString::number(a / 1e12, 'f', 2) + QStringLiteral("万亿");
    if (a >= 1e8)
        return sign + QString::number(a / 1e8, 'f', 2) + QStringLiteral("亿");
    return sign + QString::number(a / 1e4, 'f', 0) + QStringLiteral("万");
}
QString pct(double value) {
    return (value > 0 ? "+" : "") + QString::number(value, 'f', 2) + "%";
}
QColor tone(double value, const theme::Palette& c) {
    return value > 0 ? c.rise : value < 0 ? c.fall : c.flat;
}
struct Cell {
    QString text;
    QColor color;
    bool number = true;
};
QTableWidget* make_table(const QStringList& headers, const QString& name) {
    auto* t = new QTableWidget(0, headers.size());
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setDefaultSectionSize(24);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setToolTip(QStringLiteral("双击个股：加入自选并切换到个股视图"));
    return t;
}
void fill(QTableWidget* t, const QVector<QVector<Cell>>& rows, const QStringList& symbols, const theme::Palette& c) {
    t->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int col = 0; col < rows[r].size() && col < t->columnCount(); ++col) {
            const auto& cell = rows[r][col];
            auto* item = new QTableWidgetItem(cell.text);
            item->setData(Qt::UserRole, symbols.value(r));
            if (cell.number) {
                item->setFont(theme::mono(9));
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            item->setForeground(cell.color.isValid() ? cell.color : c.text);
            if (!cell.number)
                item->setToolTip(cell.text);
            t->setItem(r, col, item);
        }
}
} // namespace

// ---------------- Heatmap ----------------
AShareHeatmap::AShareHeatmap(QWidget* parent) : QWidget(parent) {
    setObjectName("ashareHeatmap");
    setMouseTracking(true);
    setMinimumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void AShareHeatmap::set_sectors(const QVector<Sector>& sectors) {
    sectors_ = sectors;
    std::stable_sort(sectors_.begin(), sectors_.end(),
                     [](const Sector& a, const Sector& b) { return a.change > b.change; });
    message_.clear();
    hover_ = -1;
    update();
}
void AShareHeatmap::set_selected(const QString& code) {
    selected_ = code;
    update();
}
void AShareHeatmap::set_message(const QString& message) {
    if (sectors_.isEmpty()) {
        message_ = message;
        update();
    }
}
QVector<QRectF> AShareHeatmap::layout_tiles() const {
    QVector<QRectF> tiles;
    if (sectors_.isEmpty())
        return tiles;
    const double gap = 3, area_w = width() - 2, area_h = height() - 2;
    // Pick the column count whose tiles are closest to a 2.2 : 1 aspect ratio.
    int best = 1;
    double best_score = 1e9;
    for (int cols = 1; cols <= 16; ++cols) {
        const int rows = (sectors_.size() + cols - 1) / cols;
        const double w = area_w / cols, h = area_h / rows;
        const double score = std::abs(std::log((w / h) / 2.2));
        if (w >= 74 && score < best_score) {
            best = cols;
            best_score = score;
        }
    }
    const int rows = (sectors_.size() + best - 1) / best;
    const double w = area_w / best, h = area_h / rows;
    for (int i = 0; i < sectors_.size(); ++i)
        tiles.append(QRectF(1 + (i % best) * w, 1 + (i / best) * h, w - gap, h - gap));
    return tiles;
}
void AShareHeatmap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::palette(theme::dark(this));
    p.fillRect(rect(), c.chart_bg);
    if (sectors_.isEmpty()) {
        p.setPen(c.muted);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }
    const auto tiles = layout_tiles();
    for (int i = 0; i < sectors_.size(); ++i) {
        const auto& s = sectors_[i];
        const auto& r = tiles[i];
        // Intensity saturates at ±3%; flat boards stay neutral.
        const double k = std::min(1.0, std::abs(s.change) / 3.0);
        const QColor base = c.panel_alt, hue = s.change > 0 ? c.rise : s.change < 0 ? c.fall : c.panel_alt;
        const double mix = s.change == 0 ? 0 : 0.18 + 0.62 * k;
        const QColor fillc(int(base.red() + (hue.red() - base.red()) * mix),
                           int(base.green() + (hue.green() - base.green()) * mix),
                           int(base.blue() + (hue.blue() - base.blue()) * mix));
        p.setPen(s.code == selected_ ? QPen(c.accent, 2) : i == hover_ ? QPen(c.text, 1) : Qt::NoPen);
        p.setBrush(fillc);
        p.drawRoundedRect(r, 3, 3);
        const QColor ink = mix > 0.5 ? QColor("#ffffff") : c.text;
        p.setPen(ink);
        QFont name_font = font();
        name_font.setPixelSize(r.height() > 46 ? 12 : 11);
        p.setFont(name_font);
        const auto inner = r.adjusted(6, 3, -6, -3);
        p.drawText(inner, Qt::AlignLeft | Qt::AlignTop,
                   p.fontMetrics().elidedText(s.name, Qt::ElideRight, int(inner.width())));
        p.setFont(theme::mono(r.height() > 46 ? 10 : 9, true));
        p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, pct(s.change));
        if (r.height() > 46 && s.total > 0) {
            p.setFont(theme::mono(8));
            p.drawText(inner, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1/%2").arg(s.up).arg(s.total));
        }
    }
}
void AShareHeatmap::mousePressEvent(QMouseEvent* e) {
    const auto tiles = layout_tiles();
    for (int i = 0; i < tiles.size(); ++i)
        if (tiles[i].contains(e->position())) {
            selected_ = sectors_[i].code;
            update();
            emit sector_clicked(selected_);
            return;
        }
}
void AShareHeatmap::mouseMoveEvent(QMouseEvent* e) {
    const auto tiles = layout_tiles();
    int hover = -1;
    for (int i = 0; i < tiles.size(); ++i)
        if (tiles[i].contains(e->position())) {
            hover = i;
            break;
        }
    if (hover != hover_) {
        hover_ = hover;
        update();
        if (hover >= 0) {
            const auto& s = sectors_[hover];
            QToolTip::showText(e->globalPosition().toPoint(),
                               QStringLiteral("%1  %2\n上涨 %3 / %4 家\n主力净流入 %5\n成交额 %6\n领涨 %7 %8")
                                   .arg(s.name, pct(s.change))
                                   .arg(s.up)
                                   .arg(s.total)
                                   .arg(money(s.main_net), money(s.turnover), s.leader_name,
                                        s.leader_name.isEmpty() ? QString() : pct(s.leader_change)),
                               this);
        } else
            QToolTip::hideText();
    }
}
void AShareHeatmap::leaveEvent(QEvent*) {
    hover_ = -1;
    update();
}

// ---------------- Breadth histogram ----------------
AShareBreadthChart::AShareBreadthChart(QWidget* parent) : QWidget(parent) {
    setObjectName("ashareBreadthChart");
    setMinimumHeight(118);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
void AShareBreadthChart::set_breadth(const Breadth& breadth) {
    breadth_ = breadth;
    message_.clear();
    update();
}
void AShareBreadthChart::set_message(const QString& message) {
    if (breadth_.buckets.isEmpty()) {
        message_ = message;
        update();
    }
}
void AShareBreadthChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::palette(theme::dark(this));
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.chart_bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    if (breadth_.buckets.isEmpty()) {
        p.setPen(c.muted);
        p.drawText(rect(), Qt::AlignCenter, message_);
        return;
    }
    // Fixed −11…11 axis so the shape is comparable day to day.
    QMap<int, int> counts;
    for (const auto& b : breadth_.buckets)
        counts[b.first] = b.second;
    int peak = 1;
    for (int v : counts)
        peak = std::max(peak, v);
    const QRectF plot = QRectF(rect()).adjusted(10, 22, -10, -20);
    const double slot = plot.width() / 23.0;
    p.setFont(theme::mono(8));
    for (int key = -11; key <= 11; ++key) {
        const int value = counts.value(key);
        const double h = value / double(peak) * (plot.height() - 14);
        const QRectF bar(plot.left() + (key + 11) * slot + slot * 0.18, plot.bottom() - h, slot * 0.64, h);
        const QColor color = key > 0 ? c.rise : key < 0 ? c.fall : c.flat;
        p.fillRect(bar, color);
        p.setPen(c.text);
        p.drawText(QRectF(bar.left() - 8, bar.top() - 14, bar.width() + 16, 12), Qt::AlignCenter,
                   QString::number(value));
        p.setPen(c.muted);
        const QString label = key == -11  ? QStringLiteral("跌停")
                              : key == 11 ? QStringLiteral("涨停")
                              : key == 0  ? QStringLiteral("平")
                                          : QString::number(key);
        p.drawText(QRectF(plot.left() + (key + 11) * slot - 4, plot.bottom() + 3, slot + 8, 14), Qt::AlignCenter,
                   label);
    }
    p.setPen(c.muted);
    p.drawText(QRectF(10, 4, width() - 20, 16), Qt::AlignLeft,
               QStringLiteral("涨跌分布 · %1 · 区间 k 表示涨跌幅约在 (k−1, k]% 内，±11 为涨跌停")
                   .arg(breadth_.date.toString("yyyy-MM-dd")));
}

// ---------------- Market panel ----------------
AShareMarketPanel::AShareMarketPanel(QWidget* parent) : QWidget(parent), service_(new AShareOverviewService(this)) {
    setObjectName("ashareMarketPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto kpi = [&](const QString& title, QLabel*& value, const QString& object_name) {
        auto* panel = new theme::Panel;
        auto* box = new QVBoxLayout(panel);
        box->setContentsMargins(10, 6, 10, 6);
        box->setSpacing(2);
        auto* label = new QLabel(title);
        label->setProperty("section", true);
        box->addWidget(label);
        value = new QLabel(QStringLiteral("—"));
        value->setObjectName(object_name);
        value->setFont(theme::mono(11, true));
        value->setWordWrap(true);
        value->setTextFormat(Qt::RichText);
        box->addWidget(value);
        return panel;
    };
    auto* kpis = new QHBoxLayout;
    kpis->setSpacing(6);
    kpis->addWidget(kpi(QStringLiteral("涨跌家数 · 沪深京 A 股"), updown_, "ashareMarketUpDown"), 3);
    kpis->addWidget(kpi(QStringLiteral("涨停 / 跌停 / 炸板"), limits_, "ashareMarketLimits"), 3);
    kpis->addWidget(kpi(QStringLiteral("全 A 成交额 · 申万一级行业合计"), turnover_, "ashareMarketTurnover"), 2);
    kpis->addWidget(kpi(QStringLiteral("北向资金"), northbound_, "ashareMarketNorthbound"), 2);
    northbound_->setFont(font());
    northbound_->setText(QStringLiteral("盘中实时净买入已停止披露<br><span style='font-size:11px'>沪深港通自 "
                                        "2024-08-19 起不再盘中公布北向净买入；本看板不以 0 或估算值代替</span>"));
    root->addLayout(kpis);
    breadth_chart_ = new AShareBreadthChart;
    root->addWidget(breadth_chart_);

    auto* middle = new QSplitter;
    middle->setObjectName("ashareMarketSplit");
    middle->setChildrenCollapsible(false);
    auto* heat_panel = new theme::Panel;
    auto* heat_box = new QVBoxLayout(heat_panel);
    heat_box->setContentsMargins(8, 6, 8, 8);
    auto* heat_head = new QHBoxLayout;
    auto* heat_title = new QLabel(QStringLiteral("板块热力图 · 红涨绿跌，颜色越深涨跌越大（±3% 封顶）"));
    heat_title->setProperty("section", true);
    heat_head->addWidget(heat_title, 1);
    board_type_ = new QComboBox;
    board_type_->setObjectName("ashareBoardType");
    board_type_->addItem(QStringLiteral("申万一级行业"), "hy");
    board_type_->addItem(QStringLiteral("申万二级行业"), "hy2");
    board_type_->addItem(QStringLiteral("概念 · 最强 / 最弱各 40"), "gn");
    heat_head->addWidget(board_type_);
    heat_box->addLayout(heat_head);
    heatmap_ = new AShareHeatmap;
    heat_box->addWidget(heatmap_, 1);
    middle->addWidget(heat_panel);
    auto* board_panel = new theme::Panel;
    auto* board_box = new QVBoxLayout(board_panel);
    board_box->setContentsMargins(8, 6, 8, 8);
    board_title_ = new QLabel(QStringLiteral("点击热力图中的板块查看成分股"));
    board_title_->setStyleSheet("font-weight:600;");
    board_stats_ = new QLabel;
    board_stats_->setProperty("muted", true);
    board_stats_->setWordWrap(true);
    board_box->addWidget(board_title_);
    board_box->addWidget(board_stats_);
    board_table_ = make_table({QStringLiteral("股票"), QStringLiteral("最新"), QStringLiteral("涨跌幅"),
                               QStringLiteral("换手"), QStringLiteral("成交额")},
                              "ashareBoardStocks");
    board_box->addWidget(board_table_, 1);
    middle->addWidget(board_panel);
    middle->setStretchFactor(0, 3);
    middle->setStretchFactor(1, 2);
    middle->setSizes({760, 420});
    middle->setMinimumHeight(300);
    root->addWidget(middle, 3);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("ashareMarketTabs");
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setDrawBase(false);
    tables_["zt"] =
        make_table({QStringLiteral("股票"), QStringLiteral("最新"), QStringLiteral("涨跌幅"), QStringLiteral("连板"),
                    QStringLiteral("首次封板"), QStringLiteral("最后封板"), QStringLiteral("炸板"),
                    QStringLiteral("封单"), QStringLiteral("成交额"), QStringLiteral("行业")},
                   "ashareZtPool");
    tables_["zb"] = make_table({QStringLiteral("股票"), QStringLiteral("最新"), QStringLiteral("涨跌幅"),
                                QStringLiteral("首次封板"), QStringLiteral("炸板次数"), QStringLiteral("换手"),
                                QStringLiteral("成交额"), QStringLiteral("行业")},
                               "ashareZbPool");
    tables_["dt"] = make_table({QStringLiteral("股票"), QStringLiteral("最新"), QStringLiteral("涨跌幅"),
                                QStringLiteral("连续跌停"), QStringLiteral("最后封板"), QStringLiteral("开板"),
                                QStringLiteral("封单"), QStringLiteral("成交额"), QStringLiteral("行业")},
                               "ashareDtPool");
    tables_["lhb"] = make_table({QStringLiteral("股票"), QStringLiteral("收盘"), QStringLiteral("涨跌幅"),
                                 QStringLiteral("龙虎榜净买额"), QStringLiteral("买入额"), QStringLiteral("卖出额"),
                                 QStringLiteral("上榜原因")},
                                "ashareBillboard");
    tables_["flow"] = make_table({QStringLiteral("板块"), QStringLiteral("涨跌幅"), QStringLiteral("主力净流入"),
                                  QStringLiteral("成交额"), QStringLiteral("上涨 / 成分"), QStringLiteral("领涨股")},
                                 "ashareSectorFlow");
    tabs_->addTab(tables_["zt"], QStringLiteral("涨停池"));
    tabs_->addTab(tables_["zb"], QStringLiteral("炸板池"));
    tabs_->addTab(tables_["dt"], QStringLiteral("跌停池"));
    tabs_->addTab(tables_["lhb"], QStringLiteral("龙虎榜"));
    tabs_->addTab(tables_["flow"], QStringLiteral("板块资金流"));
    tabs_->setMinimumHeight(220);
    root->addWidget(tabs_, 2);
    status_ = new QLabel;
    status_->setProperty("muted", true);
    status_->setWordWrap(true);
    root->addWidget(status_);
    for (auto* table : tables_) {
        connect(table, &QTableWidget::cellDoubleClicked, this, [this, table](int row, int) {
            const auto symbol = table->item(row, 0) ? table->item(row, 0)->data(Qt::UserRole).toString() : QString();
            if (!symbol.isEmpty())
                emit open_stock(symbol);
        });
    }
    connect(board_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto symbol =
            board_table_->item(row, 0) ? board_table_->item(row, 0)->data(Qt::UserRole).toString() : QString();
        if (!symbol.isEmpty())
            emit open_stock(symbol);
    });

    connect(board_type_, &QComboBox::currentIndexChanged, this, [this]() {
        heatmap_->set_sectors({});
        heatmap_->set_message(QStringLiteral("正在加载板块…"));
        render_sectors();
        service_->fetch_sectors(board_type_->currentData().toString());
    });
    connect(heatmap_, &AShareHeatmap::sector_clicked, this, [this](const QString& code) {
        selected_board_ = code;
        board_stocks_.clear();
        render_board();
        service_->fetch_board_stocks(code);
    });
    connect(service_, &AShareOverviewService::breadth_ready, this, [this](const Breadth& b) {
        breadth_ = b;
        has_breadth_ = true;
        fetched_["breadth"] = QDateTime::currentDateTimeUtc();
        errors_.remove("breadth");
        render_breadth();
        update_status();
        emit data_loaded();
    });
    connect(service_, &AShareOverviewService::sectors_ready, this,
            [this](const QString& type, const QVector<Sector>& sectors) {
                sectors_[type] = sectors;
                fetched_["sectors:" + type] = QDateTime::currentDateTimeUtc();
                errors_.remove("sectors");
                render_sectors();
                render_breadth();
                update_status();
                emit data_loaded();
            });
    connect(service_, &AShareOverviewService::board_stocks_ready, this,
            [this](const QString& code, const QVector<BoardStock>& stocks) {
                if (code != selected_board_)
                    return;
                board_stocks_ = stocks;
                errors_.remove("board");
                render_board();
                emit data_loaded();
            });
    connect(service_, &AShareOverviewService::pool_ready, this, [this](const QString& kind, const LimitPool& pool) {
        pools_[kind] = pool;
        fetched_["pool:" + kind] = QDateTime::currentDateTimeUtc();
        errors_.remove("pool:" + kind);
        render_pool(kind);
        render_breadth();
        update_status();
        emit data_loaded();
    });
    connect(service_, &AShareOverviewService::billboard_ready, this, [this](const QVector<BillboardRow>& rows) {
        billboard_ = rows;
        has_billboard_ = true;
        fetched_["billboard"] = QDateTime::currentDateTimeUtc();
        errors_.remove("billboard");
        render_billboard();
        update_status();
        emit data_loaded();
    });
    connect(service_, &AShareOverviewService::failed, this, [this](const QString& kind, const QString& error) {
        errors_[kind] = error;
        if (kind == "breadth")
            breadth_chart_->set_message(error);
        if (kind == "sectors")
            heatmap_->set_message(error);
        if (kind == "board")
            board_stats_->setText(error);
        update_status();
        emit data_loaded();
    });
    timer_ = new QTimer(this);
    timer_->setInterval(60000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (active_ && isVisible() && is_market_session(QDateTime::currentDateTimeUtc()))
            refresh(false);
    });
    timer_->start();
    update_status();
}

// Bare areas (tab strip, status line) sit directly on this widget, so paint the theme background.
void AShareMarketPanel::paintEvent(QPaintEvent*) {
    theme::paint_background(this);
}
void AShareMarketPanel::set_active(bool active) {
    active_ = active;
    if (active_)
        refresh(false);
}
void AShareMarketPanel::refresh(bool force) {
    const auto now = QDateTime::currentDateTimeUtc();
    auto stale = [&](const QString& key, int seconds) {
        return force || !fetched_.contains(key) || fetched_[key].secsTo(now) >= seconds;
    };
    if (stale("breadth", 55))
        service_->fetch_breadth();
    const auto type = board_type_->currentData().toString();
    if (stale("sectors:" + type, 55))
        service_->fetch_sectors(type);
    // The level-1 list also provides the whole-market turnover tile.
    if (type != "hy" && stale("sectors:hy", 55))
        service_->fetch_sectors("hy");
    for (const auto* kind : {"zt", "zb", "dt"})
        if (stale(QString("pool:") + kind, 55))
            service_->fetch_pool(kind);
    // Dragon-tiger data is published after the close; refresh at most every 30 minutes.
    if (stale("billboard", 1800))
        service_->fetch_billboard();
    if (!selected_board_.isEmpty() && force)
        service_->fetch_board_stocks(selected_board_);
}
void AShareMarketPanel::rerender() {
    render_breadth();
    render_sectors();
    render_board();
    for (const auto* kind : {"zt", "zb", "dt"})
        render_pool(kind);
    render_billboard();
    update();
}

void AShareMarketPanel::render_breadth() {
    const auto& c = theme::palette(theme::dark(this));
    auto span = [](const QColor& color, const QString& text) {
        return QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(), text);
    };
    if (has_breadth_) {
        breadth_chart_->set_breadth(breadth_);
        const double total = std::max(1, breadth_.total());
        updown_->setText(span(c.rise, QStringLiteral("涨 %1").arg(breadth_.up)) + "  " +
                         span(c.flat, QStringLiteral("平 %1").arg(breadth_.flat)) + "  " +
                         span(c.fall, QStringLiteral("跌 %1").arg(breadth_.down)) +
                         QStringLiteral("<br><span style='font-size:11px;color:%1'>上涨占比 %2% · 共 %3 只</span>")
                             .arg(c.muted.name())
                             .arg(breadth_.up / total * 100, 0, 'f', 1)
                             .arg(breadth_.total()));
    }
    const int zt = pools_.contains("zt") ? pools_["zt"].total : has_breadth_ ? breadth_.limit_up : -1;
    const int dt = pools_.contains("dt") ? pools_["dt"].total : has_breadth_ ? breadth_.limit_down : -1;
    const int zb = pools_.contains("zb") ? pools_["zb"].total : -1;
    if (zt >= 0 || dt >= 0) {
        QString text = span(c.rise, QStringLiteral("涨停 %1").arg(std::max(zt, 0))) + "  " +
                       span(c.fall, QStringLiteral("跌停 %1").arg(std::max(dt, 0)));
        if (zb >= 0) {
            text += "  " + span(c.flat, QStringLiteral("炸板 %1").arg(zb));
            // 炸板率 = 炸板 / (涨停 + 炸板): share of stocks that touched the limit but did not hold it.
            const int touched = std::max(zt, 0) + zb;
            text += QStringLiteral("<br><span style='font-size:11px;color:%1'>炸板率 %2%</span>")
                        .arg(c.muted.name())
                        .arg(touched ? zb * 100.0 / touched : 0, 0, 'f', 1);
        }
        limits_->setText(text);
    }
    if (sectors_.contains("hy")) {
        double sum = 0;
        for (const auto& s : sectors_["hy"])
            sum += s.turnover;
        turnover_->setText(
            money(sum) +
            QStringLiteral("<br><span style='font-size:11px;color:%1'>腾讯申万一级行业成交额求和，约等于全 A</span>")
                .arg(c.muted.name()));
    }
}
void AShareMarketPanel::render_sectors() {
    const auto type = board_type_->currentData().toString();
    if (sectors_.contains(type))
        heatmap_->set_sectors(sectors_[type]);
    heatmap_->set_selected(selected_board_);
    const auto& c = theme::palette(theme::dark(this));
    auto flows = sectors_.value(type);
    std::stable_sort(flows.begin(), flows.end(),
                     [](const Sector& a, const Sector& b) { return a.main_net > b.main_net; });
    QVector<QVector<Cell>> rows;
    QStringList symbols;
    for (const auto& s : flows) {
        rows.append({{s.name, {}, false},
                     {pct(s.change), tone(s.change, c)},
                     {money(s.main_net), tone(s.main_net, c)},
                     {money(s.turnover), {}},
                     {QStringLiteral("%1 / %2").arg(s.up).arg(s.total), {}},
                     {s.leader_name.isEmpty() ? "—" : s.leader_name + " " + pct(s.leader_change),
                      tone(s.leader_change, c), false}});
        symbols.append(s.leader_code);
    }
    fill(tables_["flow"], rows, symbols, c);
    tabs_->setTabText(
        4, QStringLiteral("板块资金流 · %1").arg(board_type_->currentText().section(QStringLiteral(" · "), 0, 0)));
}
void AShareMarketPanel::render_board() {
    const auto& c = theme::palette(theme::dark(this));
    Sector sector;
    for (const auto& list : sectors_)
        for (const auto& s : list)
            if (s.code == selected_board_)
                sector = s;
    if (selected_board_.isEmpty()) {
        board_table_->setRowCount(0);
        return;
    }
    board_title_->setText(sector.name.isEmpty() ? selected_board_ : sector.name + "  " + pct(sector.change));
    board_title_->setStyleSheet(QStringLiteral("font-weight:600;color:%1;").arg(tone(sector.change, c).name()));
    board_stats_->setText(sector.name.isEmpty()
                              ? QString()
                              : QStringLiteral("上涨 %1 / %2 家 · 主力净流入 %3 · 成交额 %4 · 按涨幅列出前 40 只")
                                    .arg(sector.up)
                                    .arg(sector.total)
                                    .arg(money(sector.main_net), money(sector.turnover)));
    QVector<QVector<Cell>> rows;
    QStringList symbols;
    for (const auto& s : board_stocks_) {
        rows.append({{s.name + "  " + s.symbol.mid(2), {}, false},
                     {QString::number(s.price, 'f', 2), tone(s.change, c)},
                     {pct(s.change), tone(s.change, c)},
                     {QString::number(s.turnover_rate, 'f', 2) + "%", {}},
                     {money(s.amount), {}}});
        symbols.append(s.symbol);
    }
    fill(board_table_, rows, symbols, c);
}
void AShareMarketPanel::render_pool(const QString& kind) {
    if (!pools_.contains(kind))
        return;
    const auto& c = theme::palette(theme::dark(this));
    const auto& pool = pools_[kind];
    QVector<QVector<Cell>> rows;
    QStringList symbols;
    for (const auto& s : pool.rows) {
        const Cell name{s.name + "  " + s.symbol.mid(2), {}, false},
            price{QString::number(s.price, 'f', 2), tone(s.change, c)};
        const Cell change{pct(s.change), tone(s.change, c)}, industry{s.industry, c.muted, false};
        if (kind == "zt")
            rows.append({name,
                         price,
                         change,
                         {s.boards > 1 ? QStringLiteral("%1 连板").arg(s.boards) : QStringLiteral("首板"),
                          s.boards > 1 ? c.accent : c.text},
                         {s.first_time, {}},
                         {s.last_time, {}},
                         {QString::number(s.breaks), s.breaks ? c.flat : c.muted},
                         {money(s.seal_fund), {}},
                         {money(s.amount), {}},
                         industry});
        else if (kind == "zb")
            rows.append({name,
                         price,
                         change,
                         {s.first_time, {}},
                         {QString::number(s.breaks), {}},
                         {QString::number(s.turnover_rate, 'f', 2) + "%", {}},
                         {money(s.amount), {}},
                         industry});
        else
            rows.append({name,
                         price,
                         change,
                         {QString::number(s.boards), {}},
                         {s.last_time, {}},
                         {QString::number(s.breaks), {}},
                         {money(s.seal_fund), {}},
                         {money(s.amount), {}},
                         industry});
        symbols.append(s.symbol);
    }
    fill(tables_[kind], rows, symbols, c);
    const int index = kind == "zt" ? 0 : kind == "zb" ? 1 : 2;
    const QString title = kind == "zt"   ? QStringLiteral("涨停池")
                          : kind == "zb" ? QStringLiteral("炸板池")
                                         : QStringLiteral("跌停池");
    tabs_->setTabText(index, QStringLiteral("%1 %2").arg(title).arg(pool.total));
}
void AShareMarketPanel::render_billboard() {
    if (!has_billboard_)
        return;
    const auto& c = theme::palette(theme::dark(this));
    QVector<QVector<Cell>> rows;
    QStringList symbols;
    for (const auto& r : billboard_) {
        rows.append({{r.name + "  " + r.symbol.mid(2), {}, false},
                     {QString::number(r.close, 'f', 2), tone(r.change, c)},
                     {pct(r.change), tone(r.change, c)},
                     {money(r.net), tone(r.net, c)},
                     {money(r.buy), {}},
                     {money(r.sell), {}},
                     {r.reason, c.muted, false}});
        symbols.append(r.symbol);
    }
    fill(tables_["lhb"], rows, symbols, c);
    tabs_->setTabText(3, billboard_.isEmpty()
                             ? QStringLiteral("龙虎榜")
                             : QStringLiteral("龙虎榜 %1").arg(billboard_.first().date.toString("MM-dd")));
}
void AShareMarketPanel::update_status() {
    QStringList parts{
        QStringLiteral("腾讯板块排行 / 成分股 · 东方财富涨跌分布、涨跌停池、龙虎榜（公开网页接口，无 SLA）")};
    if (fetched_.contains("breadth"))
        parts << QStringLiteral("更新于 %1")
                     .arg(fetched_["breadth"].toTimeZone(QTimeZone("Asia/Shanghai")).toString("HH:mm:ss"));
    parts << (is_market_session(QDateTime::currentDateTimeUtc()) ? QStringLiteral("交易时段每 60 秒刷新")
                                                                 : QStringLiteral("非交易时段不自动刷新"));
    QStringList problems;
    for (auto it = errors_.cbegin(); it != errors_.cend(); ++it)
        problems << it.value();
    problems.removeDuplicates();
    status_->setText(
        parts.join(QStringLiteral("  |  ")) +
        (problems.isEmpty() ? QString() : QStringLiteral("\n未取得：") + problems.join(QStringLiteral("；"))));
}
QString AShareMarketPanel::summary() const {
    if (!has_breadth_)
        return {};
    QString text = QStringLiteral("全市场（%1）：上涨 %2 家、下跌 %3 家、平盘 %4 家")
                       .arg(breadth_.date.toString(Qt::ISODate))
                       .arg(breadth_.up)
                       .arg(breadth_.down)
                       .arg(breadth_.flat);
    if (pools_.contains("zt"))
        text += QStringLiteral("；涨停 %1 家").arg(pools_["zt"].total);
    if (pools_.contains("dt"))
        text += QStringLiteral("；跌停 %1 家").arg(pools_["dt"].total);
    return text;
}
QVariantMap AShareMarketPanel::diagnostics() const {
    return {{"breadth_total", has_breadth_ ? breadth_.total() : 0},
            {"up", breadth_.up},
            {"down", breadth_.down},
            {"heat_tiles", heatmap_->tile_count()},
            {"board_stocks", board_stocks_.size()},
            {"zt", pools_.value("zt").rows.size()},
            {"zb", pools_.value("zb").rows.size()},
            {"dt", pools_.value("dt").rows.size()},
            {"billboard", billboard_.size()},
            {"billboard_date", billboard_.isEmpty() ? QString() : billboard_.first().date.toString(Qt::ISODate)},
            {"hy_sectors", sectors_.value("hy").size()},
            {"errors", QStringList(errors_.values())}};
}
} // namespace fincept::screens
