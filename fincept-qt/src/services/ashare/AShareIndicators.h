#pragma once
#include "services/ashare/AShareMarketService.h"

#include <QVector>

#include <cmath>
#include <limits>

namespace fincept::services::ashare {
// Values before an indicator's warm-up window are NaN; callers must not plot or quote them.
inline constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();
inline bool has_value(double value) {
    return std::isfinite(value);
}

QVector<double> sma(const QVector<double>& values, int period);
// Seeded with the first value, matching common Chinese charting software (TDX/THS EMA).
QVector<double> ema(const QVector<double>& values, int period);

struct Indicators {
    QVector<double> ma5{}, ma10{}, ma20{}, ma60{};
    QVector<double> boll_mid{}, boll_upper{}, boll_lower{}; // BOLL(20, 2), sample standard deviation
    QVector<double> dif{}, dea{}, macd{};                   // MACD(12, 26, 9), bar = 2 * (DIF - DEA)
    QVector<double> k{}, d{}, j{};                          // KDJ(9, 3, 3), K/D start at 50
    QVector<double> rsi{};                                  // RSI14, Wilder smoothing
    QVector<double> vol_ma5{}, vol_ma10{};
};
Indicators compute_indicators(const QVector<Candle>& candles);
} // namespace fincept::services::ashare
