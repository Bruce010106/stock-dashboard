#pragma once
// Shared helpers for the AShareScreen_*.cpp translation units (Fincept *_internal.h pattern).
#include "services/ashare/AShareIndicators.h"
#include "services/ashare/AShareMarketService.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace fincept::screens::ashare_detail {
using namespace services::ashare;
inline QString money(double value) {
    return value >= 1e8 ? QString::number(value / 1e8, 'f', 2) + QStringLiteral("亿")
                        : QString::number(value / 1e4, 'f', 1) + QStringLiteral("万");
}
// Watchlist columns. 0-2 are always shown; the rest can be toggled from the header menu.
enum Column { Name, Last, Change, VolumeRatio, Turnover, Amplitude, Amount, TotalCap, PE, PB, ColumnCount };
inline const QStringList kColumnTitles{QStringLiteral("股票"),   QStringLiteral("最新价"), QStringLiteral("涨跌幅"),
                                       QStringLiteral("量比"),   QStringLiteral("换手率"), QStringLiteral("振幅"),
                                       QStringLiteral("成交额"), QStringLiteral("总市值"), QStringLiteral("PE(TTM)"),
                                       QStringLiteral("PB")};
inline const QList<KPeriod> kPeriods{KPeriod::Day,  KPeriod::Day,   KPeriod::Week,  KPeriod::Month,
                                     KPeriod::Min5, KPeriod::Min15, KPeriod::Min30, KPeriod::Min60};
// NaN = not available, sorted last. Non-positive PE is a TTM loss, not a cheap valuation.
inline double column_value(const Quote& q, int column) {
    switch (column) {
        case Last:
            return q.last;
        case Change:
            return q.change_percent();
        case VolumeRatio:
            return q.volume_ratio > 0 ? q.volume_ratio : kNoValue;
        case Turnover:
            return q.turnover_rate > 0 ? q.turnover_rate : kNoValue;
        case Amplitude:
            return q.amplitude > 0 ? q.amplitude : kNoValue;
        case Amount:
            return q.amount > 0 ? q.amount : kNoValue;
        case TotalCap:
            return q.total_cap > 0 ? q.total_cap : kNoValue;
        case PE:
            return q.pe_ttm > 0 ? q.pe_ttm : kNoValue;
        case PB:
            return q.pb > 0 ? q.pb : kNoValue;
    }
    return kNoValue;
}
inline QString column_text(const Quote& q, int column) {
    const double v = column_value(q, column);
    switch (column) {
        case Last:
            return QString::number(q.last, 'f', 2);
        case Change:
            return (v >= 0 ? "+" : "") + QString::number(v, 'f', 2) + "%";
        case PE:
            return has_value(v) ? QString::number(v, 'f', 1) : q.pe_ttm < 0 ? QStringLiteral("亏损") : "—";
        case Amount:
        case TotalCap:
            return has_value(v) ? money(v) : "—";
        case Turnover:
        case Amplitude:
            return has_value(v) ? QString::number(v, 'f', 2) + "%" : "—";
        default:
            return has_value(v) ? QString::number(v, 'f', 2) : "—";
    }
}
inline QString fmt(double value, int digits = 2) {
    return has_value(value) ? QString::number(value, 'f', digits) : "—";
}
} // namespace fincept::screens::ashare_detail
