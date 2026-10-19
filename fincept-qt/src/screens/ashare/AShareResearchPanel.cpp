#include "AShareResearchPanel.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimeZone>
#include <QVBoxLayout>

namespace fincept::screens {
using namespace services::ashare;
AShareResearchPanel::AShareResearchPanel(QWidget* parent) : QWidget(parent), service_(new AShareResearchService(this)) {
    setObjectName("ashareResearchPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* controls = new QHBoxLayout;
    kind_ = new QComboBox;
    kind_->setObjectName("ashareResearchKind");
    kind_->addItem(QStringLiteral("资讯 · 按需加载"), "");
    kind_->addItem(QStringLiteral("个股研报"), "reports");
    kind_->addItem(QStringLiteral("个股新闻"), "news");
    kind_->addItem(QStringLiteral("A 股快讯 · 全市场"), "lives");
    controls->addWidget(kind_);
    status_ = new QLabel(QStringLiteral("a-stock-data 接口适配 · 免费公开源"));
    status_->setWordWrap(true);
    status_->setStyleSheet("color:#8da4bf;font-size:11px;");
    controls->addWidget(status_, 1);
    auto* refresh = new QPushButton(QStringLiteral("刷新资讯"));
    controls->addWidget(refresh);
    layout->addLayout(controls);
    text_ = new QTextBrowser;
    text_->setObjectName("ashareResearchText");
    text_->setOpenLinks(false);
    text_->setOpenExternalLinks(false);
    text_->setStyleSheet("background:#111c2a;color:#dce7f5;border:1px solid #253447;padding:8px;");
    text_->setMinimumHeight(90);
    text_->setMaximumHeight(130);
    text_->setPlainText(QStringLiteral("选择资讯类型查看当前自选股的研报、新闻或全市场快讯。点击标题打开原文。"));
    layout->addWidget(text_);
    connect(text_, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        if (url.scheme() == "https" || url.scheme() == "http")
            QDesktopServices::openUrl(url);
    });
    connect(kind_, &QComboBox::currentIndexChanged, this, [this]() { load(); });
    connect(refresh, &QPushButton::clicked, this, [this]() { load(); });
    connect(service_, &AShareResearchService::ready, this,
            [this](const QString& symbol, const QString& kind, const ResearchFeed& feed) {
                if (symbol != symbol_ || kind != kind_->currentData().toString())
                    return;
                status_->setText(feed.source + QStringLiteral(" · 获取于 ") +
                                 feed.fetched.toTimeZone(QTimeZone("Asia/Shanghai")).toString("HH:mm:ss") +
                                 QStringLiteral(" · 60 秒缓存"));
                QString html;
                for (const auto& item : feed.items) {
                    const auto title = item.title.toHtmlEscaped();
                    html += "<p><span style='color:#8da4bf'>" + item.time.toHtmlEscaped() + " · " +
                            item.source.toHtmlEscaped() + "</span><br>";
                    html += item.url.isEmpty() ? title
                                               : "<a style='color:#7dc4ff' href='" +
                                                     item.url.toString().toHtmlEscaped() + "'>" + title + "</a>";
                    if (!item.summary.isEmpty())
                        html += "<br>" + item.summary.toHtmlEscaped();
                    html += "</p>";
                }
                if (!dark_) {
                    html.replace("#8da4bf", "#636a76");
                    html.replace("#7dc4ff", "#0065cc");
                }
                text_->setHtml(html.isEmpty() ? QStringLiteral("接口本次返回空列表。") : html);
            });
    connect(service_, &AShareResearchService::failed, this,
            [this](const QString& symbol, const QString& kind, const QString& error) {
                if (symbol != symbol_ || kind != kind_->currentData().toString())
                    return;
                status_->setText(QStringLiteral("数据源暂不可用"));
                text_->setPlainText(error);
            });
}
void AShareResearchPanel::set_symbol(const QString& symbol) {
    if (symbol_ == symbol)
        return;
    symbol_ = symbol;
    load();
}
void AShareResearchPanel::set_dark(bool dark) {
    dark_ = dark;
    auto html = text_->toHtml();
    if (dark) {
        html.replace("#636a76", "#8da4bf");
        html.replace("#0065cc", "#7dc4ff");
    } else {
        html.replace("#8da4bf", "#636a76");
        html.replace("#7dc4ff", "#0065cc");
    }
    text_->setStyleSheet(
        dark ? "background:#0f1319;color:#e6eaf0;border:1px solid #232a35;border-radius:4px;padding:6px;"
             : "background:#ffffff;color:#14171c;border:1px solid #dadfe6;border-radius:4px;padding:6px;");
    text_->setHtml(html);
    status_->setStyleSheet(dark ? "color:#8b95a5;font-size:11px;" : "color:#5e6673;font-size:11px;");
}
void AShareResearchPanel::load() {
    const auto kind = kind_->currentData().toString();
    if (symbol_.isEmpty() || kind.isEmpty()) {
        status_->setText(QStringLiteral("a-stock-data 接口适配 · 免费公开源"));
        text_->setPlainText(QStringLiteral("选择自选股和资讯类型后加载。"));
        return;
    }
    status_->setText((kind == "lives" ? QStringLiteral("全市场") : symbol_.toUpper()) + QStringLiteral(" · 正在加载…"));
    text_->setPlainText(QStringLiteral("正在请求公开数据源…"));
    service_->fetch(symbol_, kind);
}
} // namespace fincept::screens
