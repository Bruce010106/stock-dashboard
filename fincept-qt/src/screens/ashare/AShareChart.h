#pragma once
#include "services/ashare/AShareIndicators.h"
#include "services/ashare/AShareMarketService.h"

#include <QWidget>

namespace fincept::screens {
class AShareChart : public QWidget {
    Q_OBJECT
  public:
    enum class Overlay { MA, BOLL, None };
    enum class Oscillator { MACD, KDJ, RSI, None };
    explicit AShareChart(QWidget* parent = nullptr);
    void set_quote(const services::ashare::Quote& quote);
    void set_candles(const QVector<services::ashare::Candle>& candles);
    void set_daily(bool daily); // true = K-line mode (any period)
    void set_minute_bars(bool minute_bars);
    void set_overlay(Overlay overlay);
    void set_oscillator(Oscillator oscillator);
    void clear(const QString& message);
    int candle_count() const { return candles_.size(); }

  protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;

  private:
    services::ashare::Quote quote_;
    QVector<services::ashare::Candle> candles_;
    services::ashare::Indicators indicators_;
    QString message_ = QStringLiteral("选择自选股以加载行情");
    bool daily_ = false, minute_bars_ = false;
    Overlay overlay_ = Overlay::MA;
    Oscillator oscillator_ = Oscillator::MACD;
    int hover_ = -1, visible_days_ = 90;
};
} // namespace fincept::screens
