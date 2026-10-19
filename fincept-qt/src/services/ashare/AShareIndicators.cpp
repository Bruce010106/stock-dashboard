#include "services/ashare/AShareIndicators.h"

#include <algorithm>

namespace fincept::services::ashare {
QVector<double> sma(const QVector<double>& values, int period) {
    QVector<double> out(values.size(), kNoValue);
    double sum = 0;
    for (int i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= period)
            sum -= values[i - period];
        if (i >= period - 1)
            out[i] = sum / period;
    }
    return out;
}
QVector<double> ema(const QVector<double>& values, int period) {
    QVector<double> out(values.size(), kNoValue);
    const double alpha = 2.0 / (period + 1);
    for (int i = 0; i < values.size(); ++i)
        out[i] = i == 0 ? values[0] : alpha * values[i] + (1 - alpha) * out[i - 1];
    return out;
}

Indicators compute_indicators(const QVector<Candle>& candles) {
    Indicators r;
    const int n = candles.size();
    QVector<double> close(n), volume(n);
    for (int i = 0; i < n; ++i) {
        close[i] = candles[i].close;
        volume[i] = candles[i].volume;
    }
    r.ma5 = sma(close, 5);
    r.ma10 = sma(close, 10);
    r.ma20 = sma(close, 20);
    r.ma60 = sma(close, 60);
    r.vol_ma5 = sma(volume, 5);
    r.vol_ma10 = sma(volume, 10);

    r.boll_mid = r.ma20;
    r.boll_upper = QVector<double>(n, kNoValue);
    r.boll_lower = QVector<double>(n, kNoValue);
    for (int i = 19; i < n; ++i) {
        double squares = 0;
        for (int k = i - 19; k <= i; ++k)
            squares += (close[k] - r.boll_mid[i]) * (close[k] - r.boll_mid[i]);
        const double deviation = std::sqrt(squares / 19);
        r.boll_upper[i] = r.boll_mid[i] + 2 * deviation;
        r.boll_lower[i] = r.boll_mid[i] - 2 * deviation;
    }

    const auto fast = ema(close, 12), slow = ema(close, 26);
    r.dif = QVector<double>(n, kNoValue);
    for (int i = 0; i < n; ++i)
        r.dif[i] = fast[i] - slow[i];
    r.dea = n ? ema(r.dif, 9) : QVector<double>();
    r.macd = QVector<double>(n, kNoValue);
    for (int i = 0; i < n; ++i)
        r.macd[i] = 2 * (r.dif[i] - r.dea[i]);
    // The EMA seed makes the first bars unreliable; hide until the slow EMA has warmed up.
    for (int i = 0; i < std::min(n, 25); ++i)
        r.dif[i] = r.dea[i] = r.macd[i] = kNoValue;

    r.k = r.d = r.j = QVector<double>(n, kNoValue);
    double k = 50, d = 50;
    for (int i = 8; i < n; ++i) {
        double low = candles[i].low, high = candles[i].high;
        for (int m = i - 8; m < i; ++m) {
            low = std::min(low, candles[m].low);
            high = std::max(high, candles[m].high);
        }
        const double rsv = high > low ? (close[i] - low) / (high - low) * 100 : 50;
        k = (2 * k + rsv) / 3;
        d = (2 * d + k) / 3;
        r.k[i] = k;
        r.d[i] = d;
        r.j[i] = 3 * k - 2 * d;
    }

    r.rsi = QVector<double>(n, kNoValue);
    double gain = 0, loss = 0;
    for (int i = 1; i < n; ++i) {
        const double diff = close[i] - close[i - 1];
        if (i <= 14) {
            gain += std::max(0.0, diff) / 14;
            loss += std::max(0.0, -diff) / 14;
        } else {
            gain = (gain * 13 + std::max(0.0, diff)) / 14;
            loss = (loss * 13 + std::max(0.0, -diff)) / 14;
        }
        if (i >= 14)
            r.rsi[i] = loss > 0 ? 100 - 100 / (1 + gain / loss) : gain > 0 ? 100 : 50;
    }
    return r;
}
} // namespace fincept::services::ashare
