#include "screens/ashare/AShareChart.h"

#include "screens/ashare/AShareTheme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace fincept::screens {
using services::ashare::has_value;
namespace {
QString fmt(double value, int digits = 2) {
    return has_value(value) ? QString::number(value, 'f', digits) : "—";
}
} // namespace
AShareChart::AShareChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(440, 345);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void AShareChart::set_quote(const services::ashare::Quote& quote) {
    quote_ = quote;
    message_ =
        quote.minutes.isEmpty() ? QStringLiteral("该交易日暂无分时数据（盘前 / 停牌 / 数据源未更新）") : QString();
    update();
}
void AShareChart::set_candles(const QVector<services::ashare::Candle>& candles) {
    candles_ = candles;
    indicators_ = services::ashare::compute_indicators(candles_);
    message_.clear();
    update();
}
void AShareChart::set_daily(bool daily) {
    daily_ = daily;
    hover_ = -1;
    update();
}
void AShareChart::set_minute_bars(bool minute_bars) {
    minute_bars_ = minute_bars;
    update();
}
void AShareChart::set_overlay(Overlay overlay) {
    overlay_ = overlay;
    update();
}
void AShareChart::set_oscillator(Oscillator oscillator) {
    oscillator_ = oscillator;
    update();
}
void AShareChart::clear(const QString& message) {
    quote_ = {};
    candles_.clear();
    indicators_ = {};
    message_ = message;
    hover_ = -1;
    update();
}
void AShareChart::mouseMoveEvent(QMouseEvent* e) {
    hover_ = int(e->position().x());
    update();
}
void AShareChart::leaveEvent(QEvent*) {
    hover_ = -1;
    update();
}
void AShareChart::wheelEvent(QWheelEvent* e) {
    if (daily_) {
        visible_days_ = std::clamp(visible_days_ + (e->angleDelta().y() > 0 ? -15 : 15), 30, 300);
        update();
    }
    e->accept();
}

void AShareChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& pal = theme::palette(theme::dark(this));
    const QColor background = pal.chart_bg, grid = pal.grid, muted = pal.muted, text = pal.text;
    const QColor rise = pal.rise, fall = pal.fall, line = pal.price_line, average = pal.average;
    const QColor ma10_color = pal.ma10, ma60_color = pal.ma60, white_line = pal.strong;
    p.setPen(QPen(pal.border, 1));
    p.setBrush(background);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setBrush(Qt::NoBrush); // paths below are strokes only
    p.setFont(theme::mono(9));
    const bool with_osc = daily_ && oscillator_ != Oscillator::None;
    const double usable = height() - 112 - (with_osc ? 24 : 0);
    // Without a sub-chart the original 73% / 23% price-volume split is preserved.
    const QRectF plot(66, 48, width() - 140, usable * (with_osc ? 0.58 : 0.73));
    const QRectF volume(plot.left(), plot.bottom() + 24, plot.width(), usable * (with_osc ? 0.19 : 0.23));
    const QRectF osc(plot.left(), volume.bottom() + 24, plot.width(), with_osc ? usable * 0.19 : 0);
    const QRectF bottom = with_osc ? osc : volume;
    const int count = daily_ ? candles_.size() : quote_.minutes.size();
    if (count == 0) {
        p.setPen(muted);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                   message_.isEmpty() ? QStringLiteral("正在加载…") : message_);
        return;
    }
    const auto& ind = indicators_;
    int start = daily_ ? std::max(0, count - visible_days_) : 0;
    double lo = std::numeric_limits<double>::max(), hi = 0, max_volume = 1;
    auto include = [&](double value) {
        if (has_value(value) && value > 0) {
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
    };
    if (daily_) {
        for (int i = start; i < count; ++i) {
            lo = std::min(lo, candles_[i].low);
            hi = std::max(hi, candles_[i].high);
            if (overlay_ == Overlay::MA) {
                include(ind.ma5[i]);
                include(ind.ma10[i]);
                include(ind.ma20[i]);
                include(ind.ma60[i]);
            }
            if (overlay_ == Overlay::BOLL) {
                include(ind.boll_upper[i]);
                include(ind.boll_lower[i]);
            }
            max_volume = std::max(max_volume, candles_[i].volume);
        }
        const double pad = std::max(0.01, (hi - lo) * 0.08);
        lo -= pad;
        hi += pad;
    } else {
        double delta = std::max(0.01, quote_.previous * 0.003);
        for (const auto& point : quote_.minutes) {
            delta = std::max(delta, std::abs(point.price - quote_.previous));
            if (point.average > 0)
                delta = std::max(delta, std::abs(point.average - quote_.previous));
            max_volume = std::max(max_volume, point.volume);
        }
        lo = quote_.previous - delta * 1.08;
        hi = quote_.previous + delta * 1.08;
    }
    auto y = [&](double value) { return plot.bottom() - (value - lo) / (hi - lo) * plot.height(); };
    auto x = [&](int i) {
        return daily_ ? plot.left() + (i - start + 0.5) / (count - start) * plot.width()
                      : plot.left() + quote_.minutes[i].session_minute / 241.0 * plot.width();
    };
    p.setPen(grid);
    for (int i = 0; i <= 4; ++i) {
        const double yy = plot.top() + i * plot.height() / 4;
        p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
        const double value = hi - i * (hi - lo) / 4;
        p.setPen(daily_ ? muted : value >= quote_.previous ? rise : fall);
        p.drawText(QRectF(0, yy - 10, 57, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'f', 2));
        if (!daily_)
            p.drawText(QRectF(plot.right() + 8, yy - 10, 65, 20), Qt::AlignVCenter,
                       QString::number((value / quote_.previous - 1) * 100, 'f', 2) + "%");
        p.setPen(grid);
    }
    for (int i = 0; i <= 4; ++i) {
        const double xx = plot.left() + i * plot.width() / 4;
        p.drawLine(QPointF(xx, plot.top()), QPointF(xx, bottom.bottom()));
    }
    p.drawLine(volume.topLeft(), volume.topRight());
    p.setPen(muted);
    p.drawText(QRectF(0, volume.top(), 57, 20), Qt::AlignRight,
               QString::number(max_volume / 10000, 'f', 1) + QStringLiteral("万"));

    const double bar_width =
        daily_ ? std::max(1.0, plot.width() / (count - start) * 0.65) : std::max(1.0, plot.width() / 300);
    QPainterPath price_path, average_path;
    bool avg_started = false;
    for (int i = start; i < count; ++i) {
        const auto color =
            daily_ ? (candles_[i].close >= candles_[i].open ? rise : fall)
                   : (quote_.minutes[i].price >= (i ? quote_.minutes[i - 1].price : quote_.previous) ? rise : fall);
        const double v = daily_ ? candles_[i].volume : quote_.minutes[i].volume;
        p.fillRect(QRectF(x(i) - bar_width / 2, volume.bottom() - v / max_volume * volume.height(), bar_width,
                          v / max_volume * volume.height()),
                   color);
        if (daily_) {
            const auto& c = candles_[i];
            p.setPen(QPen(color, 1));
            p.drawLine(QPointF(x(i), y(c.high)), QPointF(x(i), y(c.low)));
            p.fillRect(QRectF(x(i) - bar_width / 2, std::min(y(c.open), y(c.close)), bar_width,
                              std::max(1.0, std::abs(y(c.open) - y(c.close)))),
                       color);
        } else {
            const auto point = QPointF(x(i), y(quote_.minutes[i].price));
            if (i == start)
                price_path.moveTo(point);
            else
                price_path.lineTo(point);
            if (quote_.minutes[i].average > 0) {
                const auto avg = QPointF(x(i), y(quote_.minutes[i].average));
                if (!avg_started) {
                    average_path.moveTo(avg);
                    avg_started = true;
                } else
                    average_path.lineTo(avg);
            }
        }
    }
    // Generic series painter within a panel's own value mapping.
    auto draw_series = [&](const QVector<double>& values, const QColor& color,
                           const std::function<double(double)>& map) {
        QPainterPath path;
        bool begun = false;
        for (int i = start; i < count && i < values.size(); ++i)
            if (has_value(values[i])) {
                if (!begun) {
                    path.moveTo(x(i), map(values[i]));
                    begun = true;
                } else
                    path.lineTo(x(i), map(values[i]));
            }
        p.setPen(QPen(color, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };
    int selected = count - 1;
    if (hover_ >= plot.left() && hover_ <= plot.right()) {
        double distance = std::numeric_limits<double>::max();
        for (int i = start; i < count; ++i)
            if (std::abs(x(i) - hover_) < distance) {
                distance = std::abs(x(i) - hover_);
                selected = i;
            }
    }
    auto legend = [&](const QRectF& panel, const QList<QPair<QString, QColor>>& items) {
        double left = panel.left() + 4;
        for (const auto& item : items) {
            p.setPen(item.second);
            const double w = p.fontMetrics().horizontalAdvance(item.first) + 14;
            p.drawText(QRectF(left, panel.top() - 20, w, 18), Qt::AlignVCenter, item.first);
            left += w;
        }
    };
    if (daily_) {
        auto price_map = [&](double v) { return y(v); };
        if (overlay_ == Overlay::MA) {
            draw_series(ind.ma5, average, price_map);
            draw_series(ind.ma10, ma10_color, price_map);
            draw_series(ind.ma20, line, price_map);
            draw_series(ind.ma60, ma60_color, price_map);
        } else if (overlay_ == Overlay::BOLL) {
            draw_series(ind.boll_mid, average, price_map);
            draw_series(ind.boll_upper, line, price_map);
            draw_series(ind.boll_lower, line, price_map);
        }
        // Volume moving averages share the volume panel scale.
        auto volume_map = [&](double v) { return volume.bottom() - v / max_volume * volume.height(); };
        draw_series(ind.vol_ma5, average, volume_map);
        draw_series(ind.vol_ma10, ma10_color, volume_map);
        legend(volume,
               {{QStringLiteral("成交量 · 股"), muted},
                {"MAVOL5 " + fmt(ind.vol_ma5.value(selected) / 10000, 1) + QStringLiteral("万"), average},
                {"MAVOL10 " + fmt(ind.vol_ma10.value(selected) / 10000, 1) + QStringLiteral("万"), ma10_color}});
        if (overlay_ == Overlay::MA)
            legend(QRectF(plot.left(), plot.top() + 20, 0, 0), {{"MA5 " + fmt(ind.ma5[selected]), average},
                                                                {"MA10 " + fmt(ind.ma10[selected]), ma10_color},
                                                                {"MA20 " + fmt(ind.ma20[selected]), line},
                                                                {"MA60 " + fmt(ind.ma60[selected]), ma60_color}});
        else if (overlay_ == Overlay::BOLL)
            legend(QRectF(plot.left(), plot.top() + 20, 0, 0),
                   {{"BOLL(20,2) " + QStringLiteral("中 ") + fmt(ind.boll_mid[selected]), average},
                    {QStringLiteral("上 ") + fmt(ind.boll_upper[selected]), line},
                    {QStringLiteral("下 ") + fmt(ind.boll_lower[selected]), line}});
        if (with_osc) {
            p.setPen(grid);
            p.drawLine(osc.topLeft(), osc.topRight());
            double olo = 0, ohi = 0;
            auto scan = [&](const QVector<double>& values) {
                for (int i = start; i < count && i < values.size(); ++i)
                    if (has_value(values[i])) {
                        olo = std::min(olo, values[i]);
                        ohi = std::max(ohi, values[i]);
                    }
            };
            if (oscillator_ == Oscillator::MACD) {
                scan(ind.dif);
                scan(ind.dea);
                scan(ind.macd);
            } else if (oscillator_ == Oscillator::KDJ) {
                olo = 0;
                ohi = 100;
                scan(ind.j);
            } else {
                olo = 0;
                ohi = 100;
            }
            if (ohi - olo < 1e-9) {
                ohi += 1;
                olo -= 1;
            }
            auto omap = [&](double v) { return osc.bottom() - (v - olo) / (ohi - olo) * osc.height(); };
            p.setPen(muted);
            p.drawText(QRectF(0, osc.top() - 6, 57, 16), Qt::AlignRight,
                       QString::number(ohi, 'f', oscillator_ == Oscillator::MACD ? 2 : 0));
            p.drawText(QRectF(0, osc.bottom() - 10, 57, 16), Qt::AlignRight,
                       QString::number(olo, 'f', oscillator_ == Oscillator::MACD ? 2 : 0));
            if (oscillator_ == Oscillator::MACD) {
                p.setPen(QPen(grid, 1, Qt::DashLine));
                p.drawLine(QPointF(osc.left(), omap(0)), QPointF(osc.right(), omap(0)));
                for (int i = start; i < count; ++i)
                    if (has_value(ind.macd[i])) {
                        const double top = omap(std::max(0.0, ind.macd[i])), base = omap(std::min(0.0, ind.macd[i]));
                        p.fillRect(QRectF(x(i) - std::max(1.0, bar_width * 0.5) / 2, top,
                                          std::max(1.0, bar_width * 0.5), std::max(1.0, base - top)),
                                   ind.macd[i] >= 0 ? rise : fall);
                    }
                draw_series(ind.dif, white_line, omap);
                draw_series(ind.dea, average, omap);
                legend(osc, {{"MACD(12,26,9)", muted},
                             {"DIF " + fmt(ind.dif[selected], 3), white_line},
                             {"DEA " + fmt(ind.dea[selected], 3), average},
                             {"MACD " + fmt(ind.macd[selected], 3),
                              has_value(ind.macd[selected]) && ind.macd[selected] < 0 ? fall : rise}});
            } else if (oscillator_ == Oscillator::KDJ) {
                p.setPen(QPen(grid, 1, Qt::DashLine));
                for (double level : {20.0, 80.0})
                    p.drawLine(QPointF(osc.left(), omap(level)), QPointF(osc.right(), omap(level)));
                draw_series(ind.k, white_line, omap);
                draw_series(ind.d, average, omap);
                draw_series(ind.j, ma10_color, omap);
                legend(osc, {{"KDJ(9,3,3)", muted},
                             {"K " + fmt(ind.k[selected]), white_line},
                             {"D " + fmt(ind.d[selected]), average},
                             {"J " + fmt(ind.j[selected]), ma10_color}});
            } else {
                p.setPen(QPen(grid, 1, Qt::DashLine));
                for (double level : {30.0, 70.0})
                    p.drawLine(QPointF(osc.left(), omap(level)), QPointF(osc.right(), omap(level)));
                draw_series(ind.rsi, ma10_color, omap);
                legend(osc, {{"RSI14 " + fmt(ind.rsi[selected], 1), ma10_color}});
            }
        }
    } else {
        p.setPen(QPen(line, 1.8));
        p.drawPath(price_path);
        p.setPen(QPen(average, 1.4));
        p.drawPath(average_path);
        p.setPen(muted);
        p.drawText(QRectF(plot.left(), volume.top() - 21, 190, 20), QStringLiteral("成交量 · 股"));
    }
    p.setPen(muted);
    const QStringList times{QStringLiteral("09:30"), QStringLiteral("10:30"), QStringLiteral("11:30 / 13:00"),
                            QStringLiteral("14:00"), QStringLiteral("15:00")};
    for (int i = 0; i <= 4; ++i) {
        const int idx = start + (count - start - 1) * i / 4;
        const QString label = !daily_ ? times[i]
                              : minute_bars_
                                  ? candles_[idx].date.toString("MM-dd") + " " + candles_[idx].time.toString("HH:mm")
                                  : candles_[idx].date.toString(count - start > 400 ? "yyyy-MM" : "yy-MM-dd");
        p.drawText(QRectF(plot.left() + i * plot.width() / 4 - 55, bottom.bottom() + 7, 110, 24), Qt::AlignCenter,
                   label);
    }
    if (hover_ >= plot.left() && hover_ <= plot.right()) {
        p.setPen(QPen(muted, 1, Qt::DashLine));
        p.drawLine(QPointF(x(selected), plot.top()), QPointF(x(selected), bottom.bottom()));
        const double price = daily_ ? candles_[selected].close : quote_.minutes[selected].price;
        p.drawLine(QPointF(plot.left(), y(price)), QPointF(plot.right(), y(price)));
    }
    p.setPen(text);
    QString info;
    if (daily_) {
        const auto& c = candles_[selected];
        const double prev = selected > 0 ? candles_[selected - 1].close : 0;
        const QString when = minute_bars_ ? c.date.toString("yyyy-MM-dd") + " " + c.time.toString("HH:mm")
                                          : c.date.toString("yyyy-MM-dd");
        info = QStringLiteral("%1   开 %2  高 %3  低 %4  收 %5")
                   .arg(when)
                   .arg(c.open, 0, 'f', 2)
                   .arg(c.high, 0, 'f', 2)
                   .arg(c.low, 0, 'f', 2)
                   .arg(c.close, 0, 'f', 2);
        if (prev > 0)
            info += QStringLiteral("  涨跌 %1%2%")
                        .arg(c.close >= prev ? "+" : "")
                        .arg((c.close / prev - 1) * 100, 0, 'f', 2);
    } else {
        info = QStringLiteral("%1   价格 %2    均价 %3    分钟量 %4 股")
                   .arg(quote_.minutes[selected].time)
                   .arg(quote_.minutes[selected].price, 0, 'f', 2)
                   .arg(quote_.minutes[selected].average > 0 ? QString::number(quote_.minutes[selected].average, 'f', 2)
                                                             : "—")
                   .arg(quote_.minutes[selected].volume, 0, 'f', 0);
        if (services::ashare::is_index(quote_.symbol))
            info = QStringLiteral("%1   点位 %2    分钟量 %3 股")
                       .arg(quote_.minutes[selected].time)
                       .arg(quote_.minutes[selected].price, 0, 'f', 2)
                       .arg(quote_.minutes[selected].volume, 0, 'f', 0);
    }
    p.drawText(QRectF(plot.left(), 10, width() - plot.left() - 8, 28), Qt::AlignVCenter, info);
}
} // namespace fincept::screens
