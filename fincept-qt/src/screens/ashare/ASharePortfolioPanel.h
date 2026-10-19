#pragma once
#include "services/ashare/AShareLedger.h"
#include "services/ashare/AShareMarketService.h"

#include <QDateTime>
#include <QHash>
#include <QWidget>

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;
namespace fincept::screens {
// One valued holding: ledger position + latest quote.
struct ValuedHolding {
    services::ashare::Position position{};
    double price = 0, previous = 0, market_value = 0, unrealized = 0, unrealized_pct = 0, day_pnl = 0, day_pct = 0,
           weight = 0;
    bool priced = false;
};
QVector<QRectF> squarify(const QVector<double>& values, const QRectF& area);

// Treemap of holdings sized by market value; colour mode follows Fincept's
// PortfolioHeatmap (P&L / day change / weight), painted with the board palette.
class AShareHoldingsMap : public QWidget {
    Q_OBJECT
  public:
    enum class Mode { Pnl, Day, Weight };
    explicit AShareHoldingsMap(QWidget* parent = nullptr);
    void set_holdings(const QVector<ValuedHolding>& holdings);
    void set_mode(Mode mode) {
        mode_ = mode;
        update();
    }
  signals:
    void holding_clicked(const QString& symbol);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

  private:
    QVector<ValuedHolding> holdings_;
    QVector<QRectF> tiles_;
    Mode mode_ = Mode::Pnl;
};

class ASharePortfolioPanel : public QWidget {
    Q_OBJECT
  public:
    explicit ASharePortfolioPanel(QWidget* parent = nullptr);
    void set_active(bool active);
    void reload();   // re-read the ledger file
    void rerender(); // theme switch
    // Plain-text summary of the user's position in one stock (for the AI context, opt-in).
    QString position_summary(const QString& symbol) const;
    QVector<ValuedHolding> holdings() const { return valued_; }
    const QVector<services::ashare::LedgerTxn>& transactions() const { return txns_; }
    bool add_transaction(services::ashare::LedgerTxn txn, QString& error);
  signals:
    void open_stock(const QString& symbol);
    void data_loaded();

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    void recompute();
    void render();
    void fetch_quotes();
    bool persist(QString& error);
    services::ashare::AShareMarketService* quotes_service_;
    QVector<services::ashare::LedgerTxn> txns_;
    services::ashare::LedgerResult result_;
    QVector<ValuedHolding> valued_;
    QHash<QString, services::ashare::Quote> quotes_;
    QHash<QString, QString> quote_errors_;
    QString load_error_;
    bool active_ = false;
    QLabel *value_, *cost_, *unrealized_, *day_, *realized_, *status_;
    AShareHoldingsMap* map_;
    QComboBox* map_mode_;
    QTableWidget *holdings_table_, *txn_table_;
    QDateEdit* date_;
    QLineEdit *code_, *note_;
    QComboBox* type_;
    QDoubleSpinBox *qty_, *price_, *fee_;
    QLabel* form_error_;
    QTimer* timer_;
};
} // namespace fincept::screens
