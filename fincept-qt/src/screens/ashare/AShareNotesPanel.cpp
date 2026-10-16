#include "screens/ashare/AShareNotesPanel.h"
#include "screens/ashare/AShareTheme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace fincept::screens {
using namespace services::ashare;

AShareNotesPanel::AShareNotesPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("ashareNotesPanel");
    auto* root = new QHBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter; split->setChildrenCollapsible(false); root->addWidget(split);

    auto* side = new theme::Panel; auto* left = new QVBoxLayout(side); left->setContentsMargins(8, 8, 8, 8); left->setSpacing(6);
    auto* head = new QLabel(QStringLiteral("笔记 NOTES")); head->setProperty("section", true); left->addWidget(head);
    auto* new_note = new QPushButton(QStringLiteral("新建笔记")); new_note->setObjectName("ashareNoteNew"); left->addWidget(new_note);
    search_ = new QLineEdit; search_->setPlaceholderText(QStringLiteral("搜索标题、正文、标签、代码")); search_->setObjectName("ashareNoteSearch");
    left->addWidget(search_);
    auto* filters = new QHBoxLayout;
    category_filter_ = new QComboBox; category_filter_->addItem(QStringLiteral("全部分类"), QString());
    for (const auto& id : note_categories()) category_filter_->addItem(note_category_label(id), id);
    favorites_only_ = new QCheckBox(QStringLiteral("仅收藏"));
    filters->addWidget(category_filter_, 1); filters->addWidget(favorites_only_); left->addLayout(filters);
    auto* ticker_row = new QHBoxLayout;
    ticker_label_ = new QLabel; ticker_label_->setProperty("muted", true);
    clear_ticker_ = new QPushButton(QStringLiteral("显示全部")); clear_ticker_->hide();
    ticker_row->addWidget(ticker_label_, 1); ticker_row->addWidget(clear_ticker_); left->addLayout(ticker_row);
    list_ = new QListWidget; list_->setObjectName("ashareNoteList"); left->addWidget(list_, 1);
    split->addWidget(side);

    auto* editor = new theme::Panel; auto* right = new QVBoxLayout(editor); right->setContentsMargins(10, 8, 10, 8); right->setSpacing(6);
    title_ = new QLineEdit; title_->setPlaceholderText(QStringLiteral("标题")); title_->setObjectName("ashareNoteTitle");
    title_->setStyleSheet("font-size:15px;font-weight:600;"); right->addWidget(title_);
    auto* meta = new QHBoxLayout;
    category_ = new QComboBox; for (const auto& id : note_categories()) category_->addItem(note_category_label(id), id);
    sentiment_ = new QComboBox;
    for (const auto* id : {"neutral", "bullish", "bearish"}) sentiment_->addItem(note_sentiment_label(id), QString(id));
    tickers_ = new QLineEdit; tickers_->setPlaceholderText(QStringLiteral("相关股票代码，空格分隔")); tickers_->setObjectName("ashareNoteTickers");
    tags_ = new QLineEdit; tags_->setPlaceholderText(QStringLiteral("标签，逗号分隔"));
    favorite_ = new QCheckBox(QStringLiteral("收藏"));
    meta->addWidget(category_); meta->addWidget(sentiment_); meta->addWidget(tickers_, 2); meta->addWidget(tags_, 2); meta->addWidget(favorite_);
    right->addLayout(meta);
    content_ = new QPlainTextEdit; content_->setObjectName("ashareNoteContent");
    content_->setPlaceholderText(QStringLiteral("支持 Markdown。记录买入理由、关键价位、复盘结论……"));
    preview_ = new QTextBrowser; preview_->hide();
    right->addWidget(content_, 1); right->addWidget(preview_, 1);
    auto* actions = new QHBoxLayout;
    status_ = new QLabel; status_->setProperty("muted", true); actions->addWidget(status_, 1);
    open_stock_ = new QPushButton(QStringLiteral("打开个股"));
    preview_toggle_ = new QPushButton(QStringLiteral("预览")); preview_toggle_->setCheckable(true);
    auto* export_md = new QPushButton(QStringLiteral("导出 Markdown"));
    delete_ = new QPushButton(QStringLiteral("删除")); delete_->setObjectName("ashareNoteDelete");
    save_ = new QPushButton(QStringLiteral("保存  Ctrl+S")); save_->setObjectName("ashareNoteSave"); save_->setShortcut(QKeySequence::Save);
    for (auto* b : {open_stock_, preview_toggle_, export_md, delete_, save_}) actions->addWidget(b);
    right->addLayout(actions);
    split->addWidget(editor); split->setSizes({320, 860});

    connect(new_note, &QPushButton::clicked, this, [this]() { new_note_for(ticker_filter_, {}); });
    connect(search_, &QLineEdit::textChanged, this, &AShareNotesPanel::refresh_list);
    connect(category_filter_, &QComboBox::currentIndexChanged, this, &AShareNotesPanel::refresh_list);
    connect(favorites_only_, &QCheckBox::toggled, this, &AShareNotesPanel::refresh_list);
    connect(clear_ticker_, &QPushButton::clicked, this, [this]() { filter_ticker({}); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !list_->item(row)) return;
        const auto id = list_->item(row)->data(Qt::UserRole).toString();
        if (id == current_id_) return;
        if (dirty_) { QString error; if (!save_current(error)) { status_->setText(error); return; } }
        show_note(id);
    });
    for (auto* edit : {title_, tickers_, tags_}) connect(edit, &QLineEdit::textEdited, this, [this]() { set_dirty(true); });
    connect(content_, &QPlainTextEdit::textChanged, this, [this]() { set_dirty(true); });
    for (auto* combo : {category_, sentiment_}) connect(combo, &QComboBox::currentIndexChanged, this, [this]() { set_dirty(true); });
    connect(favorite_, &QCheckBox::toggled, this, [this]() { set_dirty(true); });
    connect(save_, &QPushButton::clicked, this, [this]() { QString e; status_->setText(save_current(e) ? QStringLiteral("已保存") : e); });
    connect(delete_, &QPushButton::clicked, this, [this]() {
        if (current_id_.isEmpty()) return;
        const auto backup = notes_;
        notes_.erase(std::remove_if(notes_.begin(), notes_.end(), [this](const Note& n) { return n.id == current_id_; }), notes_.end());
        QString error;
        if (!persist(error)) { notes_ = backup; status_->setText(error); return; }
        current_id_.clear(); dirty_ = false; refresh_list(); show_note(notes_.isEmpty() ? QString() : list_->count() ? list_->item(0)->data(Qt::UserRole).toString() : QString());
        status_->setText(QStringLiteral("已删除")); emit notes_changed();
    });
    connect(preview_toggle_, &QPushButton::toggled, this, [this](bool on) {
        // Notes are the user's own text; still render Markdown without raw HTML.
        if (on) preview_->document()->setMarkdown(content_->toPlainText(), QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML));
        preview_->setVisible(on); content_->setVisible(!on);
    });
    connect(export_md, &QPushButton::clicked, this, [this]() {
        const auto note = editor_note();
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 Markdown"), (note.title.isEmpty() ? "note" : note.title) + ".md",
                                                       QStringLiteral("Markdown (*.md)"));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) { file.write(note_to_markdown(note).toUtf8()); file.commit(); status_->setText(QStringLiteral("已导出")); }
        else status_->setText(QStringLiteral("导出失败：") + file.errorString());
    });
    connect(open_stock_, &QPushButton::clicked, this, [this]() {
        const auto tickers = parse_tickers(tickers_->text());
        if (!tickers.isEmpty()) emit open_stock(tickers.first());
    });
    reload();
}
void AShareNotesPanel::paintEvent(QPaintEvent*) { theme::paint_background(this); }
void AShareNotesPanel::rerender() {
    const bool dark = theme::dark(this);
    preview_->setStyleSheet(dark ? "background:#0f1319;color:#e6eaf0;border:1px solid #232a35;padding:6px;"
                                 : "background:#ffffff;color:#14171c;border:1px solid #dadfe6;padding:6px;");
    refresh_list();
}
void AShareNotesPanel::reload() {
    QString error;
    if (!load_notes(notes_path(), notes_, error)) load_error_ = error; else load_error_.clear();
    refresh_list();
    show_note(list_->count() ? list_->item(0)->data(Qt::UserRole).toString() : QString());
    if (!load_error_.isEmpty()) status_->setText(load_error_);
}
bool AShareNotesPanel::persist(QString& error) {
    if (!load_error_.isEmpty()) { error = QStringLiteral("笔记文件读取失败，为避免覆盖原文件暂不保存：") + load_error_; return false; }
    return save_notes(notes_path(), notes_, error);
}
void AShareNotesPanel::filter_ticker(const QString& symbol) {
    ticker_filter_ = symbol;
    ticker_label_->setText(symbol.isEmpty() ? QString() : QStringLiteral("只看 %1 的笔记").arg(symbol.toUpper()));
    clear_ticker_->setVisible(!symbol.isEmpty());
    refresh_list();
    if (list_->count()) show_note(list_->item(0)->data(Qt::UserRole).toString());
}
void AShareNotesPanel::new_note_for(const QString& symbol, const QString& name) {
    if (dirty_) { QString error; if (!save_current(error)) { status_->setText(error); return; } }
    Note n; n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    n.created = n.updated = QDateTime::currentDateTimeUtc();
    if (!symbol.isEmpty()) { n.tickers << symbol; n.title = (name.isEmpty() ? symbol.toUpper() : name) + QStringLiteral(" 研究笔记"); }
    notes_.prepend(n);
    QString error;
    if (!persist(error)) { notes_.removeFirst(); status_->setText(error); return; }
    refresh_list(); show_note(n.id); title_->setFocus(); title_->selectAll();
    emit notes_changed();
}
QVector<Note> AShareNotesPanel::notes_for(const QString& symbol) const {
    QVector<Note> out;
    for (const auto& n : notes_) if (n.tickers.contains(symbol)) out << n;
    return out;
}
Note AShareNotesPanel::editor_note() const {
    Note n;
    for (const auto& existing : notes_) if (existing.id == current_id_) n = existing;
    n.title = title_->text().trimmed(); n.content = content_->toPlainText();
    n.category = category_->currentData().toString(); n.sentiment = sentiment_->currentData().toString();
    n.tickers = parse_tickers(tickers_->text());
    n.tags.clear();
    for (const auto& t : tags_->text().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts)) if (!t.trimmed().isEmpty()) n.tags << t.trimmed();
    n.favorite = favorite_->isChecked();
    return n;
}
bool AShareNotesPanel::save_current(QString& error) {
    if (current_id_.isEmpty()) { error = QStringLiteral("没有选中的笔记"); return false; }
    auto n = editor_note(); n.updated = QDateTime::currentDateTimeUtc();
    const auto backup = notes_;
    for (auto& existing : notes_) if (existing.id == current_id_) existing = n;
    if (!persist(error)) { notes_ = backup; return false; }
    // Keep only valid codes visible so the user sees what was actually linked.
    loading_ = true; tickers_->setText(n.tickers.join(' ').toUpper()); loading_ = false;
    set_dirty(false); refresh_list(); emit notes_changed();
    return true;
}
void AShareNotesPanel::set_dirty(bool dirty) {
    if (loading_) return;
    dirty_ = dirty && !current_id_.isEmpty();
    save_->setText(dirty_ ? QStringLiteral("保存 *  Ctrl+S") : QStringLiteral("保存  Ctrl+S"));
}
void AShareNotesPanel::refresh_list() {
    const auto& c = theme::palette(theme::dark(this));
    QSignalBlocker blocker(list_);
    list_->clear();
    auto sorted = notes_;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Note& a, const Note& b) {
        return a.favorite != b.favorite ? a.favorite : a.updated > b.updated;
    });
    for (const auto& n : sorted) {
        if (!note_matches(n, search_->text(), category_filter_->currentData().toString(), ticker_filter_, favorites_only_->isChecked())) continue;
        const QString line2 = QStringLiteral("%1 · %2 · %3%4").arg(note_category_label(n.category), note_sentiment_label(n.sentiment),
            n.updated.toLocalTime().toString("MM-dd HH:mm"), n.tickers.isEmpty() ? QString() : "  " + n.tickers.join(' ').toUpper());
        auto* item = new QListWidgetItem((n.favorite ? QStringLiteral("★ ") : QString()) +
                                         (n.title.isEmpty() ? QStringLiteral("未命名笔记") : n.title) + "\n" + line2, list_);
        item->setData(Qt::UserRole, n.id);
        item->setForeground(n.sentiment == "bullish" ? c.rise : n.sentiment == "bearish" ? c.fall : c.text);
        if (n.id == current_id_) list_->setCurrentItem(item);
    }
    status_->setText(load_error_.isEmpty() ? QStringLiteral("%1 条笔记 · 本地文件 %2").arg(notes_.size()).arg(notes_path()) : load_error_);
}
void AShareNotesPanel::show_note(const QString& id) {
    loading_ = true;
    Note n; bool found = false;
    for (const auto& existing : notes_) if (existing.id == id) { n = existing; found = true; }
    current_id_ = found ? id : QString();
    title_->setText(n.title); content_->setPlainText(n.content);
    category_->setCurrentIndex(std::max(0, category_->findData(n.category)));
    sentiment_->setCurrentIndex(std::max(0, sentiment_->findData(n.sentiment)));
    tickers_->setText(n.tickers.join(' ').toUpper()); tags_->setText(n.tags.join(", ")); favorite_->setChecked(n.favorite);
    for (auto* w : std::initializer_list<QWidget*>{title_, content_, category_, sentiment_, tickers_, tags_, favorite_, save_, delete_, open_stock_})
        w->setEnabled(found);
    preview_toggle_->setChecked(false);
    loading_ = false; dirty_ = false; set_dirty(false);
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString() == id) { QSignalBlocker b(list_); list_->setCurrentRow(i); }
}

// ---------------- Stock tab ----------------
AShareStockNotes::AShareStockNotes(AShareNotesPanel* notes, QWidget* parent) : QWidget(parent), notes_(notes) {
    setObjectName("ashareStockNotes");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    summary_ = new QLabel; summary_->setProperty("muted", true); row->addWidget(summary_, 1);
    auto* create = new QPushButton(QStringLiteral("为本股新建笔记")); auto* all = new QPushButton(QStringLiteral("在笔记中查看"));
    row->addWidget(create); row->addWidget(all); layout->addLayout(row);
    list_ = new QListWidget; list_->setMinimumHeight(90); layout->addWidget(list_, 1);
    connect(create, &QPushButton::clicked, this, [this]() { if (!symbol_.isEmpty()) emit this->create(symbol_, name_); });
    connect(all, &QPushButton::clicked, this, [this]() { if (!symbol_.isEmpty()) emit show_all(symbol_); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { if (!symbol_.isEmpty()) emit show_all(symbol_); });
    connect(notes_, &AShareNotesPanel::notes_changed, this, &AShareStockNotes::refresh);
}
void AShareStockNotes::set_symbol(const QString& symbol, const QString& name) { symbol_ = symbol; name_ = name; refresh(); }
void AShareStockNotes::refresh() {
    list_->clear();
    const auto notes = notes_->notes_for(symbol_);
    summary_->setText(symbol_.isEmpty() ? QString() : QStringLiteral("%1 的笔记 %2 条").arg(symbol_.toUpper()).arg(notes.size()));
    for (const auto& n : notes)
        new QListWidgetItem(QStringLiteral("%1  ·  %2 · %3").arg(n.title.isEmpty() ? QStringLiteral("未命名笔记") : n.title,
            note_sentiment_label(n.sentiment), n.updated.toLocalTime().toString("yyyy-MM-dd")), list_);
}
} // namespace fincept::screens
