#pragma once
#include "services/ashare/AShareOverviewService.h"
#include <QDateTime>
#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QTableWidget;
class QTabWidget;
class QTimer;
namespace fincept::screens {
// Sector heatmap: equal tiles ordered by change, coloured by red-up / green-down intensity.
class AShareHeatmap : public QWidget {
    Q_OBJECT
  public:
    explicit AShareHeatmap(QWidget* parent = nullptr);
    void set_sectors(const QVector<services::ashare::Sector>& sectors);
    void set_selected(const QString& code);
    void set_message(const QString& message);
    int tile_count() const { return sectors_.size(); }
    QSize sizeHint() const override { return {640, 300}; }
  signals:
    void sector_clicked(const QString& code);
  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
  private:
    QVector<QRectF> layout_tiles() const;
    QVector<services::ashare::Sector> sectors_;
    QString selected_, message_ = QStringLiteral("正在加载板块…");
    int hover_ = -1;
};

// Up/down distribution histogram from the exchange-wide change buckets.
class AShareBreadthChart : public QWidget {
    Q_OBJECT
  public:
    explicit AShareBreadthChart(QWidget* parent = nullptr);
    void set_breadth(const services::ashare::Breadth& breadth);
    void set_message(const QString& message);
  protected:
    void paintEvent(QPaintEvent*) override;
  private:
    services::ashare::Breadth breadth_;
    QString message_ = QStringLiteral("正在加载涨跌分布…");
};

// "看大盘" market overview: breadth, limit pools, sector heatmap / flows, dragon-tiger list.
class AShareMarketPanel : public QWidget {
    Q_OBJECT
  public:
    explicit AShareMarketPanel(QWidget* parent = nullptr);
    // Requests are only made while the panel is shown.
    void set_active(bool active);
    void refresh(bool force);
    void rerender(); // re-apply palette-dependent colours after a theme switch
    QVariantMap diagnostics() const;
    QString summary() const; // one-line breadth digest for the AI context; empty until loaded
  signals:
    void open_stock(const QString& symbol);
    void data_loaded();
  protected:
    void paintEvent(QPaintEvent*) override;
  private:
    void render_breadth();
    void render_sectors();
    void render_board();
    void render_pool(const QString& kind);
    void render_billboard();
    void update_status();
    services::ashare::AShareOverviewService* service_;
    bool active_ = false;
    QHash<QString, QDateTime> fetched_;
    QHash<QString, QString> errors_;
    services::ashare::Breadth breadth_; bool has_breadth_ = false;
    QHash<QString, QVector<services::ashare::Sector>> sectors_;
    QHash<QString, services::ashare::LimitPool> pools_;
    QVector<services::ashare::BillboardRow> billboard_; bool has_billboard_ = false;
    QVector<services::ashare::BoardStock> board_stocks_;
    QString selected_board_;
    QLabel *updown_, *limits_, *turnover_, *northbound_, *board_title_, *board_stats_, *status_;
    AShareBreadthChart* breadth_chart_;
    AShareHeatmap* heatmap_;
    QComboBox* board_type_;
    QTableWidget* board_table_;
    QTabWidget* tabs_;
    QHash<QString, QTableWidget*> tables_;
    QTimer* timer_;
};
} // namespace fincept::screens
