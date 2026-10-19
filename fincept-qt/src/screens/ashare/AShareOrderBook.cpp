#include "screens/ashare/AShareOrderBook.h"

#include "screens/ashare/AShareTheme.h"

#include <QPainter>
#include <QTimeZone>

#include <algorithm>

namespace fincept::screens {
namespace {
QString lots(double shares) {
    const double value = shares / 100;
    return value >= 1e4 ? QString::number(value / 1e4, 'f', 1) + QStringLiteral("万") : QString::number(value, 'f', 0);
}
} // namespace
AShareOrderBook::AShareOrderBook(QWidget* parent) : QWidget(parent) {
    setObjectName("ashareOrderBook");
    setFixedWidth(258);
    setMinimumHeight(345);
}
void AShareOrderBook::set_quote(const services::ashare::Quote& quote) {
    if (quote.symbol != trades_symbol_) {
        trades_.clear();
        trades_error_.clear();
        trades_fetched_ = {};
        trades_symbol_.clear();
    }
    quote_ = quote;
    message_.clear();
    update();
}
void AShareOrderBook::set_trades(const QString& symbol, const QVector<services::ashare::Trade>& trades,
                                 const QDateTime& fetched) {
    trades_symbol_ = symbol;
    trades_ = trades;
    trades_fetched_ = fetched;
    trades_error_.clear();
    update();
}
void AShareOrderBook::set_trades_error(const QString& error) {
    trades_error_ = error;
    update();
}
void AShareOrderBook::clear(const QString& message) {
    quote_ = {};
    trades_.clear();
    trades_symbol_.clear();
    trades_fetched_ = {};
    trades_error_.clear();
    message_ = message;
    update();
}
int AShareOrderBook::level_count() const {
    int count = 0;
    for (const auto& level : quote_.bids + quote_.asks)
        if (level.price > 0)
            ++count;
    return count;
}

void AShareOrderBook::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& pal = theme::palette(theme::dark(this));
    const QColor background = pal.chart_bg, grid = pal.grid, muted = pal.muted, text = pal.text;
    const QColor rise = pal.rise, fall = pal.fall, bar_color = pal.depth_bar;
    p.setPen(QPen(pal.border, 1));
    p.setBrush(background);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setBrush(Qt::NoBrush);
    p.setFont(theme::mono(9));
    const double left = 10, right = width() - 10, row = 18;
    if (!message_.isEmpty() || quote_.symbol.isEmpty()) {
        p.setPen(muted);
        p.drawText(rect().adjusted(14, 14, -14, -14), Qt::AlignCenter | Qt::TextWordWrap,
                   message_.isEmpty() ? QStringLiteral("正在加载盘口…") : message_);
        return;
    }
    auto price_color = [&](double price) {
        return price <= 0 ? muted : price > quote_.previous ? rise : price < quote_.previous ? fall : text;
    };
    double y = 8;
    double bid_total = 0, ask_total = 0, max_level = 1;
    for (const auto& level : quote_.bids) {
        bid_total += level.volume;
        max_level = std::max(max_level, level.volume);
    }
    for (const auto& level : quote_.asks) {
        ask_total += level.volume;
        max_level = std::max(max_level, level.volume);
    }
    // 委比 = (委买 - 委卖) / (委买 + 委卖)
    const double ratio = bid_total + ask_total > 0 ? (bid_total - ask_total) / (bid_total + ask_total) * 100 : 0;
    p.setPen(text);
    p.drawText(QRectF(left, y, 120, row), Qt::AlignVCenter, QStringLiteral("五档盘口 · 手"));
    p.setPen(ratio >= 0 ? rise : fall);
    p.drawText(QRectF(left, y, right - left, row), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("委比 %1%2%").arg(ratio >= 0 ? "+" : "").arg(ratio, 0, 'f', 2));
    y += row + 2;
    auto draw_level = [&](const QString& label, const services::ashare::BookLevel& level) {
        const double w = level.volume / max_level * (right - left) * 0.45;
        p.fillRect(QRectF(right - w, y + 3, w, row - 6), bar_color);
        p.setPen(muted);
        p.drawText(QRectF(left, y, 40, row), Qt::AlignVCenter, label);
        p.setPen(price_color(level.price));
        p.drawText(QRectF(left + 40, y, 90, row), Qt::AlignVCenter,
                   level.price > 0 ? QString::number(level.price, 'f', 2) : "—");
        p.setPen(text);
        // Tencent truncates to whole lots; STAR odd-lot remainders show as 0.
        const auto volume = level.price <= 0   ? QStringLiteral("—")
                            : level.volume > 0 ? lots(level.volume)
                                               : QStringLiteral("<1");
        p.drawText(QRectF(left, y, right - left, row), Qt::AlignRight | Qt::AlignVCenter, volume);
        y += row;
    };
    for (int i = std::min<int>(4, quote_.asks.size() - 1); i >= 0; --i)
        draw_level(QStringLiteral("卖%1").arg(i + 1), quote_.asks[i]);
    p.setPen(grid);
    p.drawLine(QPointF(left, y + 2), QPointF(right, y + 2));
    y += 5;
    for (int i = 0; i < std::min<int>(5, quote_.bids.size()); ++i)
        draw_level(QStringLiteral("买%1").arg(i + 1), quote_.bids[i]);
    y += 4;
    p.setPen(grid);
    p.drawLine(QPointF(left, y), QPointF(right, y));
    y += 4;

    auto pair = [&](const QString& a, const QString& av, const QColor& ac, const QString& b, const QString& bv,
                    const QColor& bc) {
        const double mid = (left + right) / 2;
        p.setPen(muted);
        p.drawText(QRectF(left, y, 40, row), Qt::AlignVCenter, a);
        p.setPen(ac);
        p.drawText(QRectF(left, y, mid - left - 8, row), Qt::AlignRight | Qt::AlignVCenter, av);
        p.setPen(muted);
        p.drawText(QRectF(mid + 8, y, 40, row), Qt::AlignVCenter, b);
        p.setPen(bc);
        p.drawText(QRectF(mid + 8, y, right - mid - 8, row), Qt::AlignRight | Qt::AlignVCenter, bv);
        y += row;
    };
    pair(QStringLiteral("外盘"), quote_.outer > 0 ? lots(quote_.outer) : "—", rise, QStringLiteral("内盘"),
         quote_.inner > 0 ? lots(quote_.inner) : "—", fall);
    y += 4;
    p.setPen(grid);
    p.drawLine(QPointF(left, y), QPointF(right, y));
    y += 4;

    p.setPen(text);
    p.drawText(QRectF(left, y, right - left, row), Qt::AlignVCenter, QStringLiteral("分笔成交 · 约 3 秒快照"));
    p.setPen(muted);
    p.drawText(QRectF(left, y, right - left, row), Qt::AlignRight | Qt::AlignVCenter,
               trades_fetched_.isValid() ? trades_fetched_.toTimeZone(QTimeZone("Asia/Shanghai")).toString("HH:mm:ss") +
                                               QStringLiteral(" 获取")
                                         : QString());
    y += row;
    if (!trades_error_.isEmpty()) {
        p.setPen(muted);
        const QRectF box(left, y, right - left, row * 2);
        p.drawText(box, Qt::AlignLeft | Qt::TextWordWrap,
                   (trades_.isEmpty() ? QString() : QStringLiteral("刷新失败，保留旧数据：")) + trades_error_);
        y += row * 2;
    }
    if (trades_.isEmpty() && trades_error_.isEmpty()) {
        p.setPen(muted);
        p.drawText(QRectF(left, y, right - left, row), Qt::AlignVCenter,
                   trades_fetched_.isValid() ? QStringLiteral("暂无成交（盘前 / 停牌）") : QStringLiteral("正在加载…"));
        return;
    }
    // Newest first; show only as many rows as fit.
    for (int i = trades_.size() - 1; i >= 0 && y + row <= height() - 6; --i) {
        const auto& t = trades_[i];
        p.setPen(muted);
        p.drawText(QRectF(left, y, 70, row), Qt::AlignVCenter, t.time);
        p.setPen(price_color(t.price));
        p.drawText(QRectF(left + 64, y, 70, row), Qt::AlignVCenter, QString::number(t.price, 'f', 2));
        p.setPen(text);
        p.drawText(QRectF(left, y, right - left - 22, row), Qt::AlignRight | Qt::AlignVCenter, lots(t.volume));
        p.setPen(t.side > 0 ? rise : t.side < 0 ? fall : muted);
        p.drawText(QRectF(left, y, right - left, row), Qt::AlignRight | Qt::AlignVCenter,
                   t.side > 0   ? "B"
                   : t.side < 0 ? "S"
                                : "·");
        y += row;
    }
}
} // namespace fincept::screens
