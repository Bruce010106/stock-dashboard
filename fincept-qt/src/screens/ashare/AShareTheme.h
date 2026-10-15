#pragma once
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QWidget>

// Direction B (2026-10-15, user choice): flat, dense "pro terminal" look.
// Dark is the default; a light variant keeps the same structure. Red = up, green = down.
namespace fincept::screens::theme {
struct Palette {
    QColor bg, panel, panel_alt, header, border, text, muted, accent, selection;
    QColor chart_bg, grid, rise, fall, flat, price_line, average, ma10, ma60, strong, depth_bar;
};
inline const Palette& palette(bool night) {
    static const Palette dark{
        "#0b0e13", "#12161d", "#151a22", "#171c24", "#232a35", "#e6eaf0", "#8b95a5", "#f0a43a", "#2a2418",
        "#0f1319", "#1e2631", "#ff5c66", "#2ec98f", "#c3cad5", "#4ea3ff", "#f4c76b", "#d98cff", "#c9ced6", "#e8edf5",
        QColor(78, 163, 255, 46)};
    static const Palette light{
        "#eef0f3", "#ffffff", "#f7f8fa", "#f3f4f6", "#dadfe6", "#14171c", "#5e6673", "#c77700", "#fff1dc",
        "#ffffff", "#e8ebf0", "#e0303c", "#0e9f6e", "#3d4450", "#1e6fd9", "#c98a00", "#9b4dd6", "#6b7280", "#1f2937",
        QColor(30, 111, 217, 36)};
    return night ? dark : light;
}
inline bool dark(const QWidget* widget) {
    for (auto* current = widget; current; current = current->parentWidget())
        if (current->objectName() == "ashareBoard") return current->property("darkTheme").toBool();
    return true; // standalone widgets (tests) follow the default dark theme
}
// Tabular figures: Cascadia Mono / Consolas ship with Windows; CJK falls back to YaHei.
inline QFont mono(double point_size = 9, bool bold = false) {
    QFont font;
    font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Microsoft YaHei UI")});
    font.setPointSizeF(point_size);
    font.setBold(bold);
    return font;
}
inline QString css_color(const QColor& color) { return color.name(QColor::HexArgb); }

inline QString stylesheet(bool night) {
    const auto& c = palette(night);
    QString css = QStringLiteral(
        "QWidget#ashareBoard{color:@text;}"
        "QWidget#ashareBoard QLabel,QWidget#ashareBoard QPushButton,QWidget#ashareBoard QComboBox,"
        "QWidget#ashareBoard QLineEdit,QWidget#ashareBoard QListWidget,QWidget#ashareBoard QTextBrowser,"
        "QWidget#ashareBoard QTabBar{font-family:'Segoe UI','Microsoft YaHei UI';font-size:13px;}"
        "QWidget#ashareBoard QLabel{color:@text;background:transparent;}"
        "QWidget#ashareBoard QLabel#ashareBrand{color:@accent;font-weight:600;font-size:14px;padding-right:6px;}"
        "QWidget#ashareBoard QLabel[muted=\"true\"]{color:@muted;font-size:12px;}"
        "QWidget#ashareBoard QLabel[section=\"true\"]{color:@muted;font-size:11px;letter-spacing:1px;}"
        "QWidget#ashareBoard QLineEdit,QWidget#ashareBoard QComboBox{background:@panelAlt;color:@text;border:1px solid @border;border-radius:4px;padding:5px 8px;min-height:18px;}"
        "QWidget#ashareBoard QLineEdit:focus,QWidget#ashareBoard QComboBox:focus{border:1px solid @accent;}"
        "QWidget#ashareBoard QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:top right;width:18px;border:0;}"
        "QWidget#ashareBoard QComboBox QAbstractItemView{background:@panel;color:@text;selection-background-color:@selection;selection-color:@text;border:1px solid @border;}"
        "QWidget#ashareBoard QPushButton{background:@panelAlt;color:@text;border:1px solid @border;border-radius:4px;padding:5px 10px;}"
        "QWidget#ashareBoard QPushButton:hover{border-color:@accent;}"
        "QWidget#ashareBoard QPushButton:checked{background:@selection;border-color:@accent;}"
        "QWidget#ashareBoard QPushButton:pressed{background:@selection;}"
        "QWidget#ashareBoard QPushButton:focus{border:1px solid @accent;}"
        "QWidget#ashareBoard QPushButton[ticker=\"true\"]{background:transparent;border:1px solid transparent;border-radius:4px;padding:4px 10px;text-align:left;}"
        "QWidget#ashareBoard QPushButton[ticker=\"true\"]:hover{border-color:@border;}"
        "QWidget#ashareBoard QPushButton[ticker=\"true\"]:checked{background:@selection;border-color:@accent;}"
        "QWidget#ashareBoard QTableWidget{background:@panel;alternate-background-color:@panelAlt;color:@text;border:0;gridline-color:@border;"
        "selection-background-color:@selection;selection-color:@text;outline:0;}"
        "QWidget#ashareBoard QHeaderView::section{background:@header;color:@muted;border:0;border-bottom:1px solid @border;padding:6px;font-size:12px;}"
        "QWidget#ashareBoard QHeaderView::section:hover{color:@text;}"
        "QWidget#ashareBoard QTabWidget::pane{border:0;}"
        "QWidget#ashareBoard QTabBar::tab{background:transparent;color:@muted;border:0;border-bottom:2px solid transparent;padding:6px 12px;margin-right:4px;}"
        "QWidget#ashareBoard QTabBar::tab:selected{color:@text;border-bottom:2px solid @accent;}"
        "QWidget#ashareBoard QTabBar::tab:hover{color:@text;}"
        "QWidget#ashareBoard QComboBox:disabled{color:@muted;}"
        "QWidget#ashareBoard QSplitter::handle{background:@bg;}"
        "QWidget#ashareBoard QSplitter::handle:hover{background:@border;}"
        "QWidget#ashareBoard QScrollBar:vertical{background:transparent;width:8px;}"
        "QWidget#ashareBoard QScrollBar:horizontal{background:transparent;height:8px;}"
        "QWidget#ashareBoard QScrollBar::handle{background:@border;border-radius:3px;min-height:24px;min-width:24px;}"
        "QWidget#ashareBoard QScrollBar::handle:hover{background:@muted;}"
        "QWidget#ashareBoard QScrollBar::add-line,QWidget#ashareBoard QScrollBar::sub-line{width:0;height:0;}"
        "QWidget#ashareBoard QScrollBar::add-page,QWidget#ashareBoard QScrollBar::sub-page{background:transparent;}"
        "QWidget#ashareBoard QListWidget{background:@panel;color:@text;border:1px solid @border;border-radius:4px;}"
        "QWidget#ashareBoard QListWidget::item:selected,QWidget#ashareBoard QListWidget::item:hover{background:@selection;}"
        "QWidget#ashareBoard QToolTip{background:@panel;color:@text;border:1px solid @border;}");
    const QList<QPair<QString, QColor>> tokens{{"@panelAlt", c.panel_alt}, {"@panel", c.panel}, {"@header", c.header},
        {"@border", c.border}, {"@text", c.text}, {"@muted", c.muted}, {"@accent", c.accent},
        {"@selection", c.selection}, {"@bg", c.bg}};
    for (const auto& token : tokens) css.replace(token.first, token.second.name());
    return css;
}
// Popup menus are top-level windows, so they cannot inherit the board stylesheet.
inline QString menu_stylesheet(bool night) {
    const auto& c = palette(night);
    return QStringLiteral("QMenu{background:%1;color:%2;border:1px solid %3;padding:4px;}"
                          "QMenu::item{padding:5px 18px;}QMenu::item:selected{background:%4;}"
                          "QMenu::item:disabled{color:%5;}QMenu::separator{height:1px;background:%3;margin:4px 6px;}")
        .arg(c.panel.name(), c.text.name(), c.border.name(), c.selection.name(), c.muted.name());
}
inline void paint_background(QWidget* widget) {
    QPainter p(widget);
    p.fillRect(widget->rect(), palette(dark(widget)).bg);
}
// Flat terminal panel: solid fill, 1px hairline, small radius. No gradients or glass.
class Panel : public QWidget {
  public:
    explicit Panel(QWidget* parent = nullptr) : QWidget(parent) { setAutoFillBackground(false); }
  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::palette(theme::dark(this));
        p.setPen(QPen(c.border, 1)); p.setBrush(c.panel);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    }
};
} // namespace fincept::screens::theme
