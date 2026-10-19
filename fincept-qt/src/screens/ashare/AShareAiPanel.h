#pragma once
#include "services/ashare/AShareAiService.h"

#include <QDialog>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
namespace fincept::screens {
// Provider / model / key settings for the AI assistant. Keys are encrypted with DPAPI.
class AShareAiSettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit AShareAiSettingsDialog(QWidget* parent = nullptr);

  private:
    void load_provider(const QString& provider);
    void save();
    QComboBox *provider_, *model_;
    QLineEdit *base_url_, *key_;
    QLabel* key_state_;
};

// "AI 解读" tab: sends the selected stock's public market data (and, only if the
// user ticks the box, their own position) to the configured model and streams the reply.
class AShareAiPanel : public QWidget {
    Q_OBJECT
  public:
    using ContextBuilder = std::function<QString(bool include_position)>;
    explicit AShareAiPanel(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol, const QString& name);
    void set_context_builder(ContextBuilder builder) { context_ = std::move(builder); }
    void set_dark(bool dark);
    QString last_context() const { return last_context_; }
    static QString system_prompt();

  private:
    void ask(const QString& question);
    void render();
    void update_header();
    QString symbol_, name_, last_context_;
    QVector<services::ashare::AiMessage> history_;
    QString streaming_;
    bool dark_ = true, thinking_ = false;
    ContextBuilder context_;
    services::ashare::AShareAiService* service_;
    QLabel* header_;
    QTextBrowser* text_;
    QLineEdit* input_;
    QCheckBox* include_position_;
    QPushButton *send_, *stop_;
};
} // namespace fincept::screens
