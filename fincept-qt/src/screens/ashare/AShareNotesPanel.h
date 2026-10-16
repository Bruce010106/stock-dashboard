#pragma once
#include "services/ashare/AShareNotes.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
namespace fincept::screens {
// Notes view adapted from Fincept's NotesScreen: filterable list on the left,
// editor with Markdown preview on the right, notes linked to A-share tickers.
class AShareNotesPanel : public QWidget {
    Q_OBJECT
  public:
    explicit AShareNotesPanel(QWidget* parent = nullptr);
    void reload();
    void filter_ticker(const QString& symbol);              // "" clears the filter
    void new_note_for(const QString& symbol, const QString& name);
    QVector<services::ashare::Note> notes_for(const QString& symbol) const;
    int note_count() const { return notes_.size(); }
    bool save_current(QString& error);
    void rerender();
  signals:
    void notes_changed();
    void open_stock(const QString& symbol);
  protected:
    void paintEvent(QPaintEvent*) override;
  private:
    void refresh_list();
    void show_note(const QString& id);
    void set_dirty(bool dirty);
    services::ashare::Note editor_note() const;
    bool persist(QString& error);
    QVector<services::ashare::Note> notes_;
    QString current_id_, ticker_filter_, load_error_;
    bool dirty_ = false, loading_ = false;
    QLineEdit* search_;
    QComboBox* category_filter_;
    QCheckBox* favorites_only_;
    QLabel *ticker_label_, *status_;
    QPushButton* clear_ticker_;
    QListWidget* list_;
    QLineEdit *title_, *tickers_, *tags_;
    QComboBox *category_, *sentiment_;
    QCheckBox* favorite_;
    QPlainTextEdit* content_;
    QTextBrowser* preview_;
    QPushButton *preview_toggle_, *save_, *delete_, *open_stock_;
};

// Compact "本股笔记" tab for the stock view.
class AShareStockNotes : public QWidget {
    Q_OBJECT
  public:
    explicit AShareStockNotes(AShareNotesPanel* notes, QWidget* parent = nullptr);
    void set_symbol(const QString& symbol, const QString& name);
    void refresh();
  signals:
    void show_all(const QString& symbol);
    void create(const QString& symbol, const QString& name);
  private:
    AShareNotesPanel* notes_;
    QString symbol_, name_;
    QLabel* summary_;
    QListWidget* list_;
};
} // namespace fincept::screens
