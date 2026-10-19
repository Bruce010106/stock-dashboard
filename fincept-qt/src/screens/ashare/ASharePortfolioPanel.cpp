#include "screens/ashare/ASharePortfolioPanel.h"

#include "screens/ashare/AShareTheme.h"
#include "screens/portfolio/PortfolioCsv.h" // reused from Fincept: RFC 4180 + formula-injection guard

#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fincept::screens {
using namespace services::ashare;
namespace {
QString money(double v) {
    const double a = std::abs(v);
    const QString s = v < 0 ? "-" : "";
    if (a >= 1e8)
        return s + QString::number(a / 1e8, 'f', 2) + QStringLiteral("亿");
    if (a >= 1e4)
        return s + QString::number(a / 1e4, 'f', 2) + QStringLiteral("万");
    return s + QString::number(a, 'f', 2);
}
QString signed_money(double v) {
    return (v > 0 ? "+" : "") + money(v);
}
QString pct(double v) {
    return (v > 0 ? "+" : "") + QString::number(v, 'f', 2) + "%";
}
QColor tone(double v, const theme::Palette& c) {
    return v > 0 ? c.rise : v < 0 ? c.fall : c.flat;
}
QString type_label(const QString& type) {
    return type == "BUY" ? QStringLiteral("买入") : type == "SELL" ? QStringLiteral("卖出") : QStringLiteral("分红");
}
QTableWidgetItem* cell(const QString& text, const QColor& color, bool number, const QString& symbol = {}) {
    auto* item = new QTableWidgetItem(text);
    item->setForeground(color);
    item->setData(Qt::UserRole, symbol);
    if (number) {
        item->setFont(theme::mono(9));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    return item;
}
double worst(const QVector<double>& row, double side) {
    const double sum = std::accumulate(row.begin(), row.end(), 0.0);
    double w = 0;
    for (double r : row)
        w = std::max({w, side * side * r / (sum * sum), (sum * sum) / (side * side * r)});
    return w;
}
} // namespace

// Squarified treemap (Bruls, Huizing, van Wijk 2000). Returns one rect per input, input order.
QVector<QRectF> squarify(const QVector<double>& values, const QRectF& area) {
    QVector<QRectF> out(values.size());
    const double total = std::accumulate(values.begin(), values.end(), 0.0);
    if (values.isEmpty() || total <= 0 || area.isEmpty())
        return out;
    QVector<int> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return values[a] > values[b]; });
    const double scale = area.width() * area.height() / total;
    QRectF rest = area;
    int i = 0;
    while (i < order.size()) {
        const double side = std::min(rest.width(), rest.height());
        QVector<double> row{std::max(values[order[i]], 1e-9) * scale};
        QVector<int> members{order[i]};
        ++i;
        while (i < order.size()) {
            auto trial = row;
            trial.append(std::max(values[order[i]], 1e-9) * scale);
            if (worst(trial, side) > worst(row, side))
                break;
            row = trial;
            members.append(order[i]);
            ++i;
        }
        const double sum = std::accumulate(row.begin(), row.end(), 0.0);
        if (rest.width() >= rest.height()) { // column on the left
            const double w = sum / rest.height();
            double y = rest.top();
            for (int k = 0; k < members.size(); ++k) {
                const double h = row[k] / w;
                out[members[k]] = QRectF(rest.left(), y, w, h);
                y += h;
            }
            rest.setLeft(rest.left() + w);
        } else { // row on top
            const double h = sum / rest.width();
            double x = rest.left();
            for (int k = 0; k < members.size(); ++k) {
                const double w = row[k] / h;
                out[members[k]] = QRectF(x, rest.top(), w, h);
                x += w;
            }
            rest.setTop(rest.top() + h);
        }
    }
    return out;
}

// ---------------- Treemap ----------------
AShareHoldingsMap::AShareHoldingsMap(QWidget* parent) : QWidget(parent) {
    setObjectName("ashareHoldingsMap");
    setMinimumSize(260, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void AShareHoldingsMap::set_holdings(const QVector<ValuedHolding>& holdings) {
    holdings_ = holdings;
    update();
}
void AShareHoldingsMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::palette(theme::dark(this));
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.chart_bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    QVector<double> values;
    for (const auto& h : holdings_)
        values << std::max(h.market_value, h.position.cost);
    tiles_ = squarify(values, QRectF(rect()).adjusted(3, 3, -3, -3));
    if (holdings_.isEmpty()) {
        p.setPen(c.muted);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("还没有持仓\n在下方录入第一笔买入"));
        return;
    }
    for (int i = 0; i < holdings_.size(); ++i) {
        const auto& h = holdings_[i];
        const auto r = tiles_[i].adjusted(1.5, 1.5, -1.5, -1.5);
        const double value = mode_ == Mode::Pnl ? h.unrealized_pct : mode_ == Mode::Day ? h.day_pct : 0;
        const double k = mode_ == Mode::Weight ? std::min(1.0, h.weight / 40.0)
                                               : std::min(1.0, std::abs(value) / (mode_ == Mode::Day ? 5.0 : 20.0));
        const QColor hue = mode_ == Mode::Weight ? c.price_line : value > 0 ? c.rise : value < 0 ? c.fall : c.panel_alt;
        const double mix = (mode_ != Mode::Weight && value == 0) || !h.priced ? 0 : 0.2 + 0.6 * k;
        const QColor fill(int(c.panel_alt.red() + (hue.red() - c.panel_alt.red()) * mix),
                          int(c.panel_alt.green() + (hue.green() - c.panel_alt.green()) * mix),
                          int(c.panel_alt.blue() + (hue.blue() - c.panel_alt.blue()) * mix));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 3, 3);
        p.setPen(mix > 0.5 ? QColor("#ffffff") : c.text);
        const auto inner = r.adjusted(6, 4, -6, -4);
        if (inner.height() < 14 || inner.width() < 30)
            continue;
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.drawText(inner, Qt::AlignLeft | Qt::AlignTop,
                   p.fontMetrics().elidedText(h.position.name.isEmpty() ? h.position.symbol : h.position.name,
                                              Qt::ElideRight, int(inner.width())));
        if (inner.height() > 34) {
            p.setFont(theme::mono(9));
            const QString line = !h.priced            ? QStringLiteral("无报价")
                                 : mode_ == Mode::Pnl ? pct(h.unrealized_pct)
                                 : mode_ == Mode::Day ? pct(h.day_pct)
                                                      : QString::number(h.weight, 'f', 1) + "%";
            p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, line);
        }
    }
}
void AShareHoldingsMap::mousePressEvent(QMouseEvent* e) {
    for (int i = 0; i < tiles_.size() && i < holdings_.size(); ++i)
        if (tiles_[i].contains(e->position())) {
            emit holding_clicked(holdings_[i].position.symbol);
            return;
        }
}

// ---------------- Panel ----------------
ASharePortfolioPanel::ASharePortfolioPanel(QWidget* parent)
    : QWidget(parent), quotes_service_(new AShareMarketService(this)) {
    setObjectName("asharePortfolioPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    auto* kpis = new QHBoxLayout;
    kpis->setSpacing(6);
    auto kpi = [&](const QString& title, QLabel*& value, const char* name) {
        auto* panel = new theme::Panel;
        auto* box = new QVBoxLayout(panel);
        box->setContentsMargins(10, 6, 10, 6);
        box->setSpacing(2);
        auto* label = new QLabel(title);
        label->setProperty("section", true);
        box->addWidget(label);
        value = new QLabel("—");
        value->setObjectName(name);
        value->setFont(theme::mono(12, true));
        box->addWidget(value);
        kpis->addWidget(panel, 1);
    };
    kpi(QStringLiteral("总市值"), value_, "asharePfValue");
    kpi(QStringLiteral("持仓成本（含费用）"), cost_, "asharePfCost");
    kpi(QStringLiteral("浮动盈亏"), unrealized_, "asharePfUnrealized");
    kpi(QStringLiteral("当日盈亏（估）"), day_, "asharePfDay");
    kpi(QStringLiteral("已实现盈亏 + 分红"), realized_, "asharePfRealized");
    root->addLayout(kpis);

    auto* middle = new QSplitter;
    middle->setChildrenCollapsible(false);
    auto* map_panel = new theme::Panel;
    auto* map_box = new QVBoxLayout(map_panel);
    map_box->setContentsMargins(8, 6, 8, 8);
    auto* map_head = new QHBoxLayout;
    auto* map_title = new QLabel(QStringLiteral("持仓分布 · 面积 = 市值"));
    map_title->setProperty("section", true);
    map_head->addWidget(map_title, 1);
    map_mode_ = new QComboBox;
    map_mode_->addItems(
        {QStringLiteral("颜色：浮动盈亏"), QStringLiteral("颜色：当日涨跌"), QStringLiteral("颜色：仓位")});
    map_head->addWidget(map_mode_);
    map_box->addLayout(map_head);
    map_ = new AShareHoldingsMap;
    map_box->addWidget(map_, 1);
    middle->addWidget(map_panel);
    auto* hold_panel = new theme::Panel;
    auto* hold_box = new QVBoxLayout(hold_panel);
    hold_box->setContentsMargins(8, 6, 8, 8);
    auto* hold_title = new QLabel(QStringLiteral("当前持仓 · 双击查看个股"));
    hold_title->setProperty("section", true);
    hold_box->addWidget(hold_title);
    holdings_table_ = new QTableWidget(0, 9);
    holdings_table_->setObjectName("asharePfHoldings");
    holdings_table_->setHorizontalHeaderLabels(
        {QStringLiteral("股票"), QStringLiteral("持仓股数"), QStringLiteral("成本价"), QStringLiteral("现价"),
         QStringLiteral("市值"), QStringLiteral("浮动盈亏"), QStringLiteral("盈亏%"), QStringLiteral("当日"),
         QStringLiteral("仓位")});
    hold_box->addWidget(holdings_table_, 1);
    middle->addWidget(hold_panel);
    middle->setSizes({420, 760});
    root->addWidget(middle, 3);

    auto* txn_panel = new theme::Panel;
    auto* txn_box = new QVBoxLayout(txn_panel);
    txn_box->setContentsMargins(8, 6, 8, 8);
    auto* txn_head = new QHBoxLayout;
    auto* txn_title = new QLabel(QStringLiteral("交易记录 · 加权平均成本法 · 费用请按券商交割单填写"));
    txn_title->setProperty("section", true);
    txn_head->addWidget(txn_title, 1);
    auto* export_txn = new QPushButton(QStringLiteral("导出 CSV"));
    auto* delete_txn = new QPushButton(QStringLiteral("删除选中记录"));
    delete_txn->setObjectName("asharePfDelete");
    txn_head->addWidget(export_txn);
    txn_head->addWidget(delete_txn);
    txn_box->addLayout(txn_head);
    auto* form = new QHBoxLayout;
    form->setSpacing(4);
    date_ = new QDateEdit(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date());
    date_->setCalendarPopup(true);
    date_->setDisplayFormat("yyyy-MM-dd");
    code_ = new QLineEdit;
    code_->setPlaceholderText(QStringLiteral("代码，如 600519"));
    code_->setObjectName("asharePfCode");
    type_ = new QComboBox;
    type_->addItem(QStringLiteral("买入"), "BUY");
    type_->addItem(QStringLiteral("卖出"), "SELL");
    type_->addItem(QStringLiteral("分红"), "DIVIDEND");
    auto spin = [](double max, int decimals, const QString& suffix) {
        auto* s = new QDoubleSpinBox;
        s->setRange(0, max);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setFont(theme::mono(9));
        return s;
    };
    qty_ = spin(1e9, 0, QStringLiteral(" 股"));
    qty_->setSingleStep(100);
    price_ = spin(1e7, 3, QString());
    fee_ = spin(1e7, 2, QStringLiteral(" 费用"));
    note_ = new QLineEdit;
    note_->setPlaceholderText(QStringLiteral("备注（可选）"));
    auto* add = new QPushButton(QStringLiteral("记一笔"));
    add->setObjectName("asharePfAdd");
    for (auto* w : std::initializer_list<QWidget*>{date_, code_, type_, qty_, price_, fee_})
        form->addWidget(w);
    form->addWidget(note_, 1);
    form->addWidget(add);
    txn_box->addLayout(form);
    form_error_ = new QLabel;
    form_error_->setProperty("muted", true);
    form_error_->setWordWrap(true);
    txn_box->addWidget(form_error_);
    txn_table_ = new QTableWidget(0, 8);
    txn_table_->setObjectName("asharePfTxns");
    txn_table_->setHorizontalHeaderLabels({QStringLiteral("日期"), QStringLiteral("股票"), QStringLiteral("类型"),
                                           QStringLiteral("数量"), QStringLiteral("价格 / 金额"),
                                           QStringLiteral("费用"), QStringLiteral("成交额"), QStringLiteral("备注")});
    txn_box->addWidget(txn_table_, 1);
    root->addWidget(txn_panel, 2);
    status_ = new QLabel;
    status_->setProperty("muted", true);
    status_->setWordWrap(true);
    root->addWidget(status_);
    for (auto* t : {holdings_table_, txn_table_}) {
        t->verticalHeader()->hide();
        t->setShowGrid(false);
        t->setAlternatingRowColors(true);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->verticalHeader()->setDefaultSectionSize(24);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        t->horizontalHeader()->setStretchLastSection(true);
    }
    connect(type_, &QComboBox::currentIndexChanged, this, [this]() {
        const bool dividend = type_->currentData().toString() == "DIVIDEND";
        qty_->setEnabled(!dividend);
        price_->setPrefix(dividend ? QStringLiteral("到账 ") : QString());
    });
    connect(add, &QPushButton::clicked, this, [this]() {
        LedgerTxn t;
        t.symbol = normalize_symbol(code_->text());
        t.type = type_->currentData().toString();
        t.date = date_->date();
        t.quantity = t.type == "DIVIDEND" ? 0 : qty_->value();
        t.price = price_->value();
        t.fee = fee_->value();
        t.note = note_->text().trimmed();
        if (quotes_.contains(t.symbol))
            t.name = quotes_[t.symbol].name;
        QString error;
        if (!add_transaction(t, error)) {
            form_error_->setText(error);
            return;
        }
        form_error_->setText(QStringLiteral("已记录。"));
        code_->clear();
        note_->clear();
        qty_->setValue(0);
        price_->setValue(0);
        fee_->setValue(0);
    });
    connect(delete_txn, &QPushButton::clicked, this, [this]() {
        const int row = txn_table_->currentRow();
        if (row < 0 || !txn_table_->item(row, 0)) {
            form_error_->setText(QStringLiteral("请先在交易记录中选中一行"));
            return;
        }
        const auto id = txn_table_->item(row, 0)->data(Qt::UserRole).toString();
        const auto backup = txns_;
        txns_.erase(std::remove_if(txns_.begin(), txns_.end(), [&](const LedgerTxn& t) { return t.id == id; }),
                    txns_.end());
        QString error;
        if (!persist(error)) {
            txns_ = backup;
            form_error_->setText(error);
            return;
        }
        form_error_->setText(QStringLiteral("已删除一条记录。"));
        recompute();
    });
    connect(export_txn, &QPushButton::clicked, this, [this]() {
        QVector<QStringList> rows;
        for (const auto& t : txns_)
            rows.append({t.date.toString(Qt::ISODate), t.symbol, t.name, t.type, portfolio_csv::num(t.quantity, 0),
                         portfolio_csv::num(t.price, 3), portfolio_csv::num(t.fee, 2), t.note});
        portfolio_csv::save(this, "ashare-transactions.csv",
                            {"date", "symbol", "name", "type", "quantity", "price_or_amount", "fee", "note"}, rows);
    });
    connect(map_mode_, &QComboBox::currentIndexChanged, this,
            [this](int i) { map_->set_mode(AShareHoldingsMap::Mode(i)); });
    connect(map_, &AShareHoldingsMap::holding_clicked, this, &ASharePortfolioPanel::open_stock);
    connect(holdings_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* item = holdings_table_->item(row, 0))
            emit open_stock(item->data(Qt::UserRole).toString());
    });
    connect(quotes_service_, &AShareMarketService::quote_ready, this, [this](const Quote& q) {
        quotes_[q.symbol] = q;
        quote_errors_.remove(q.symbol);
        recompute();
        emit data_loaded();
    });
    connect(quotes_service_, &AShareMarketService::failed, this,
            [this](const QString& symbol, const QString& kind, const QString& error) {
                if (kind == "quote") {
                    quote_errors_[symbol] = error;
                    render();
                }
            });
    timer_ = new QTimer(this);
    timer_->setInterval(30000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (active_ && isVisible() && is_market_session(QDateTime::currentDateTimeUtc()))
            fetch_quotes();
    });
    timer_->start();
    reload();
}
void ASharePortfolioPanel::paintEvent(QPaintEvent*) {
    theme::paint_background(this);
}
void ASharePortfolioPanel::set_active(bool active) {
    active_ = active;
    if (active)
        fetch_quotes();
}
void ASharePortfolioPanel::reload() {
    QString error;
    if (!load_ledger(ledger_path(), txns_, error))
        load_error_ = error;
    else
        load_error_.clear();
    recompute();
}
bool ASharePortfolioPanel::persist(QString& error) {
    if (!load_error_.isEmpty()) {
        error = QStringLiteral("持仓文件读取失败，为避免覆盖原文件暂不保存：") + load_error_;
        return false;
    }
    return save_ledger(ledger_path(), txns_, error);
}
bool ASharePortfolioPanel::add_transaction(LedgerTxn txn, QString& error) {
    if (txn.id.isEmpty())
        txn.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validate_txn(txn, error))
        return false;
    if (txn.type == "SELL") {
        // Reject selling more than is held on that date instead of silently clamping.
        auto before = txns_;
        before.erase(
            std::remove_if(before.begin(), before.end(), [&](const LedgerTxn& t) { return t.date > txn.date; }),
            before.end());
        double held = 0;
        for (const auto& p : compute_ledger(before).open)
            if (p.symbol == txn.symbol)
                held = p.quantity;
        if (txn.quantity > held + 1e-9) {
            error = QStringLiteral("卖出 %1 股超过该日持仓 %2 股").arg(txn.quantity).arg(held);
            return false;
        }
    }
    txns_.append(txn);
    if (!persist(error)) {
        txns_.removeLast();
        return false;
    }
    recompute();
    fetch_quotes();
    return true;
}
void ASharePortfolioPanel::fetch_quotes() {
    for (const auto& p : compute_ledger(txns_).open)
        quotes_service_->fetch_quote(p.symbol);
}
void ASharePortfolioPanel::recompute() {
    result_ = compute_ledger(txns_);
    valued_.clear();
    double total = 0;
    for (const auto& p : result_.open) {
        ValuedHolding h;
        h.position = p;
        if (quotes_.contains(p.symbol)) {
            const auto& q = quotes_[p.symbol];
            h.priced = true;
            h.price = q.last;
            h.previous = q.previous;
            if (h.position.name.isEmpty())
                h.position.name = q.name;
            h.market_value = p.quantity * q.last;
            h.unrealized = h.market_value - p.cost;
            h.unrealized_pct = p.cost > 0 ? h.unrealized / p.cost * 100 : 0;
            // Approximation: today's change applied to the whole current position (intraday trades not separated).
            h.day_pnl = p.quantity * (q.last - q.previous);
            h.day_pct = q.change_percent();
        } else
            h.market_value = p.cost; // unpriced: weight by cost until a quote arrives
        total += h.market_value;
        valued_.append(h);
    }
    for (auto& h : valued_)
        h.weight = total > 0 ? h.market_value / total * 100 : 0;
    std::stable_sort(valued_.begin(), valued_.end(),
                     [](const ValuedHolding& a, const ValuedHolding& b) { return a.market_value > b.market_value; });
    render();
}
void ASharePortfolioPanel::rerender() {
    render();
}
void ASharePortfolioPanel::render() {
    const auto& c = theme::palette(theme::dark(this));
    double value = 0, cost = 0, unrealized = 0, day = 0;
    bool all_priced = true;
    for (const auto& h : valued_) {
        cost += h.position.cost;
        if (h.priced) {
            value += h.market_value;
            unrealized += h.unrealized;
            day += h.day_pnl;
        } else
            all_priced = false;
    }
    auto colored = [&](QLabel* label, const QString& text, double v) {
        label->setText(text);
        label->setStyleSheet(QStringLiteral("color:%1;").arg(tone(v, c).name()));
    };
    value_->setText(valued_.isEmpty() ? "—" : money(value) + (all_priced ? QString() : QStringLiteral(" *")));
    cost_->setText(valued_.isEmpty() ? "—" : money(cost));
    colored(unrealized_,
            valued_.isEmpty() ? "—" : signed_money(unrealized) + "  " + pct(cost > 0 ? unrealized / cost * 100 : 0),
            unrealized);
    colored(day_, valued_.isEmpty() ? "—" : signed_money(day), day);
    colored(realized_, signed_money(result_.realized + result_.dividends), result_.realized + result_.dividends);
    map_->set_holdings(valued_);
    holdings_table_->setRowCount(valued_.size());
    for (int r = 0; r < valued_.size(); ++r) {
        const auto& h = valued_[r];
        const auto& p = h.position;
        const auto s = p.symbol;
        holdings_table_->setItem(r, 0,
                                 cell((p.name.isEmpty() ? s.toUpper() : p.name) + "  " + s.mid(2), c.text, false, s));
        holdings_table_->setItem(r, 1, cell(QString::number(p.quantity, 'f', 0), c.text, true, s));
        holdings_table_->setItem(r, 2, cell(QString::number(p.avg_cost(), 'f', 3), c.text, true, s));
        holdings_table_->setItem(r, 3,
                                 cell(h.priced ? QString::number(h.price, 'f', 2) : QStringLiteral("无报价"),
                                      h.priced ? tone(h.day_pct, c) : c.muted, true, s));
        holdings_table_->setItem(r, 4, cell(h.priced ? money(h.market_value) : "—", c.text, true, s));
        holdings_table_->setItem(r, 5,
                                 cell(h.priced ? signed_money(h.unrealized) : "—", tone(h.unrealized, c), true, s));
        holdings_table_->setItem(r, 6, cell(h.priced ? pct(h.unrealized_pct) : "—", tone(h.unrealized, c), true, s));
        holdings_table_->setItem(r, 7, cell(h.priced ? signed_money(h.day_pnl) : "—", tone(h.day_pnl, c), true, s));
        holdings_table_->setItem(r, 8, cell(QString::number(h.weight, 'f', 1) + "%", c.text, true, s));
    }
    auto sorted = txns_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const LedgerTxn& a, const LedgerTxn& b) { return a.date > b.date; });
    txn_table_->setRowCount(sorted.size());
    for (int r = 0; r < sorted.size(); ++r) {
        const auto& t = sorted[r];
        const QColor tc = t.type == "BUY" ? c.rise : t.type == "SELL" ? c.fall : c.accent;
        auto* first = cell(t.date.toString(Qt::ISODate), c.text, true, t.symbol);
        first->setData(Qt::UserRole, t.id);
        txn_table_->setItem(r, 0, first);
        txn_table_->setItem(
            r, 1,
            cell((t.name.isEmpty() ? t.symbol.toUpper() : t.name) + "  " + t.symbol.mid(2), c.text, false, t.symbol));
        txn_table_->setItem(r, 2, cell(type_label(t.type), tc, false, t.symbol));
        txn_table_->setItem(
            r, 3, cell(t.type == "DIVIDEND" ? "—" : QString::number(t.quantity, 'f', 0), c.text, true, t.symbol));
        txn_table_->setItem(r, 4,
                            cell(QString::number(t.price, 'f', t.type == "DIVIDEND" ? 2 : 3), c.text, true, t.symbol));
        txn_table_->setItem(r, 5, cell(QString::number(t.fee, 'f', 2), c.muted, true, t.symbol));
        txn_table_->setItem(r, 6,
                            cell(money(t.type == "DIVIDEND" ? t.price : t.quantity * t.price), c.text, true, t.symbol));
        txn_table_->setItem(r, 7, cell(t.note, c.muted, false, t.symbol));
    }
    QStringList notes{QStringLiteral("本地文件：%1").arg(ledger_path()),
                      QStringLiteral("行情：腾讯公开快照，交易时段每 30 秒刷新"),
                      QStringLiteral("当日盈亏按当前持仓 × 今日涨跌估算，未区分当日买卖")};
    if (!all_priced)
        notes << QStringLiteral("* 有持仓尚无报价，总市值未计入");
    if (!load_error_.isEmpty())
        notes << load_error_;
    notes << result_.warnings;
    for (auto it = quote_errors_.cbegin(); it != quote_errors_.cend(); ++it)
        notes << it.key().toUpper() + "：" + it.value();
    status_->setText(notes.join(QStringLiteral("  |  ")));
}
QString ASharePortfolioPanel::position_summary(const QString& symbol) const {
    for (const auto& h : valued_)
        if (h.position.symbol == symbol)
            return QStringLiteral("我的持仓：%1 股，加权平均成本 %2 元（含费用），%3已实现盈亏 %4 元。")
                .arg(h.position.quantity, 0, 'f', 0)
                .arg(h.position.avg_cost(), 0, 'f', 3)
                .arg(h.priced ? QStringLiteral("浮动盈亏 %1 元（%2），")
                                    .arg(h.unrealized, 0, 'f', 2)
                                    .arg(pct(h.unrealized_pct))
                              : QString())
                .arg(h.position.realized, 0, 'f', 2);
    for (const auto& p : result_.closed)
        if (p.symbol == symbol)
            return QStringLiteral("我的持仓：已清仓，已实现盈亏 %1 元。").arg(p.realized, 0, 'f', 2);
    return QStringLiteral("我的持仓：未持有该股。");
}
} // namespace fincept::screens
