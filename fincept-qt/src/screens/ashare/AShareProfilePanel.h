#pragma once
#include "services/ashare/AShareProfileService.h"
#include <QWidget>
class QLabel;
class QTextBrowser;
namespace fincept::screens {
class AShareProfilePanel : public QWidget {
    Q_OBJECT
  public:
    explicit AShareProfilePanel(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);
    // Requests are only made while the tab is active, to avoid hitting F10 for every selection.
    void set_active(bool active);
    void set_dark(bool dark);
    static QString render_html(const services::ashare::StockProfile& profile, bool dark);
  private:
    void load(bool force);
    void render();
    QString symbol_, loaded_;
    bool active_ = false, dark_ = false, has_profile_ = false;
    services::ashare::StockProfile profile_;
    QLabel* status_;
    QTextBrowser* text_;
    services::ashare::AShareProfileService* service_;
};
}
