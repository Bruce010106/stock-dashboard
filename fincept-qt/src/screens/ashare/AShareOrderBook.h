#pragma once
#include "services/ashare/AShareMarketService.h"
#include <QDateTime>
#include <QWidget>

namespace fincept::screens {
// Five-level book from the Tencent snapshot plus Eastmoney ~3 s tick snapshots.
// Painted on the same stable dark backing as the chart so numbers stay legible in both themes.
class AShareOrderBook : public QWidget {
    Q_OBJECT
  public:
    explicit AShareOrderBook(QWidget* parent = nullptr);
    void set_quote(const services::ashare::Quote& quote);
    void set_trades(const QString& symbol, const QVector<services::ashare::Trade>& trades, const QDateTime& fetched);
    void set_trades_error(const QString& error);
    void clear(const QString& message);
    int level_count() const;
    int trade_count() const { return trades_.size(); }
  protected:
    void paintEvent(QPaintEvent*) override;
  private:
    services::ashare::Quote quote_;
    QVector<services::ashare::Trade> trades_;
    QString trades_symbol_; // ticks may arrive before the first quote
    QDateTime trades_fetched_;
    QString message_ = QStringLiteral("选择个股以查看盘口"), trades_error_;
};
}
