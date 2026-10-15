#pragma once
#include "services/ashare/AShareResearchService.h"
#include <QWidget>
class QComboBox;
class QTextBrowser;
class QLabel;
namespace fincept::screens {
class AShareResearchPanel : public QWidget {
    Q_OBJECT
  public:
    explicit AShareResearchPanel(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);
    void set_dark(bool dark);
  private:
    void load();
    QString symbol_;
    bool dark_ = false;
    QComboBox* kind_;
    QTextBrowser* text_;
    QLabel* status_;
    services::ashare::AShareResearchService* service_;
};
}
