#pragma once
#include "screens/common/IStatefulScreen.h"
#include "services/ashare/AShareMarketService.h"
#include <QHash>
#include <QSet>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QComboBox;
class QTimer;
class QListWidget;
class QHBoxLayout;
class QTabWidget;
namespace fincept::screens {
class AShareChart;
class AShareOrderBook;
class AShareProfilePanel;
class AShareResearchPanel;
class AShareScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit AShareScreen(QWidget* parent = nullptr);
    QVariantMap save_state() const override;
    void restore_state(const QVariantMap& state) override;
    QString state_key() const override { return "ashare"; }
    bool has_quote() const { return quotes_.contains(selected_); }
    bool has_daily() const;
    void select_daily(bool value);
    void select_period(int index); // index into the chart mode combo; 0 = intraday
    QVariantMap diagnostics() const;
  signals:
    void data_loaded();
  protected:
    void paintEvent(QPaintEvent*) override;
  private:
    bool intraday() const;
    services::ashare::KPeriod current_period() const;
    services::ashare::Adjust current_adjust() const;
    QString analysis_key() const; // intraday mode summarises daily candles
    void fetch_selected_kline(bool force);
    QStringList visible_symbols() const;
    void rebuild_watchlist();
    void rebuild_groups();
    void apply_column_visibility();
    void show_row_menu(const QPoint& pos);
    void show_header_menu(const QPoint& pos);
    void update_chart_controls();
    void select_symbol(const QString& symbol);
    void refresh(bool force = false);
    void render_selected();
    void persist();
    void rebuild_indexes();
    void render_indexes();
    void apply_theme(bool dark);
    services::ashare::AShareMarketService* service_;
    QHash<QString, services::ashare::Quote> quotes_;
    QHash<QString, QVector<services::ashare::Candle>> candles_;   // keyed by kline_key()
    QHash<QString, QDateTime> kline_fetched_;
    QHash<QString, QString> errors_;
    QStringList symbols_;
    QStringList visible_;
    QStringList groups_;
    QHash<QString, QString> symbol_group_;
    QSet<int> hidden_columns_;
    int sort_column_ = -1;
    bool sort_desc_ = true;
    QStringList indexes_;
    QHBoxLayout* index_cards_;
    QHash<QString, QPushButton*> index_buttons_;
    QString selected_;
    QTableWidget* table_;
    QComboBox* group_filter_;
    QPushButton* delete_group_;
    QLineEdit* input_;
    QListWidget* suggestions_;
    QLabel* search_status_;
    QTimer* search_timer_;
    QComboBox *mode_, *adjust_, *overlay_, *oscillator_;
    QComboBox* theme_;
    QLabel *title_, *price_, *details_, *status_, *chart_info_, *analysis_;
    QPushButton* pause_;
    AShareChart* chart_;
    AShareOrderBook* order_book_;
    QTabWidget* tabs_;
    AShareResearchPanel* research_;
    AShareProfilePanel* profile_;
    QTimer* timer_;
};
}
