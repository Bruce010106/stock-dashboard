#include "screens/ashare/AShareAiPanel.h"

#include "screens/ashare/AShareTheme.h"
#include "services/llm/ProviderCatalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace fincept::screens {
using namespace services::ashare;
using fincept::ai_chat::ProviderCatalog;

// ---------------- Settings dialog ----------------
AShareAiSettingsDialog::AShareAiSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("AI 设置"));
    setMinimumWidth(460);
    auto* form = new QFormLayout(this);
    provider_ = new QComboBox;
    provider_->setObjectName("ashareAiProvider");
    for (const auto& p : supported_ai_providers())
        provider_->addItem(ProviderCatalog::display_name(p), p);
    model_ = new QComboBox;
    model_->setEditable(true);
    model_->setObjectName("ashareAiModel");
    base_url_ = new QLineEdit;
    base_url_->setObjectName("ashareAiBaseUrl");
    key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    key_->setObjectName("ashareAiKey");
    key_state_ = new QLabel;
    key_state_->setWordWrap(true);
    form->addRow(QStringLiteral("服务商"), provider_);
    form->addRow(QStringLiteral("模型"), model_);
    form->addRow(QStringLiteral("自定义地址"), base_url_);
    form->addRow(QStringLiteral("API Key"), key_);
    form->addRow(QString(), key_state_);
    auto* note =
        new QLabel(QStringLiteral("密钥用 Windows DPAPI 按当前系统用户加密后保存在本机，不写入代码或日志。\n"
                                  "提问时会把当前个股的公开行情、指标和已加载的 F10 摘要发送给所选服务商；"
                                  "只有勾选「附带我的持仓」时才会附带你的持仓数量与成本。\n"
                                  "通义千问等 OpenAI 兼容服务：服务商选 OpenAI，自定义地址填其兼容接口地址。"));
    note->setWordWrap(true);
    note->setStyleSheet("color:#8b95a5;font-size:11px;");
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* forget = buttons->addButton(QStringLiteral("清除此服务商密钥"), QDialogButtonBox::ResetRole);
    form->addRow(buttons);
    const auto current = load_ai_settings();
    provider_->setCurrentIndex(std::max(0, provider_->findData(current.provider)));
    load_provider(current.provider);
    model_->setCurrentText(current.model);
    base_url_->setText(current.base_url);
    connect(provider_, &QComboBox::currentIndexChanged, this, [this]() {
        load_provider(provider_->currentData().toString());
        model_->setCurrentIndex(0);
        base_url_->clear();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &AShareAiSettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(forget, &QPushButton::clicked, this, [this]() {
        forget_api_key(provider_->currentData().toString());
        load_provider(provider_->currentData().toString());
    });
}
void AShareAiSettingsDialog::load_provider(const QString& provider) {
    model_->clear();
    model_->addItems(ProviderCatalog::fallback_models(provider));
    const auto def = ProviderCatalog::default_base_url(provider);
    base_url_->setPlaceholderText(def.isEmpty() ? QStringLiteral("留空使用官方地址") : def);
    key_->clear();
    key_->setPlaceholderText(has_api_key(provider) ? QStringLiteral("已保存（加密）；留空保持不变")
                                                   : QStringLiteral("粘贴 API Key"));
    key_state_->setText(!ProviderCatalog::requires_api_key(provider)
                            ? QStringLiteral("该服务商无需 API Key（本地服务）")
                        : has_api_key(provider) ? QStringLiteral("已保存密钥")
                                                : QStringLiteral("尚未保存密钥"));
}
void AShareAiSettingsDialog::save() {
    AiSettings s{provider_->currentData().toString(), model_->currentText().trimmed(), base_url_->text().trimmed()};
    if (s.model.isEmpty()) {
        key_state_->setText(QStringLiteral("请填写模型名称"));
        return;
    }
    if (ProviderCatalog::is_blocked(s.provider, s.base_url)) {
        key_state_->setText(QStringLiteral("该地址不可用"));
        return;
    }
    if (!key_->text().trimmed().isEmpty()) {
        QString error;
        if (!store_api_key(s.provider, key_->text(), error)) {
            key_state_->setText(error);
            return;
        }
    }
    key_->clear();
    save_ai_settings(s);
    accept();
}

// ---------------- Panel ----------------
QString AShareAiPanel::system_prompt() {
    return QStringLiteral(
        "你是 A 股行情分析助手。只依据用户消息中提供的数据作答：数据里没有的数字、新闻或公告不要编造，"
        "缺失的信息要直接说明“数据未提供”。注意区分前复权价格与实际成交价。"
        "回答用简体中文，条理清楚、简洁，优先使用小标题和要点。"
        "不要给出确定性的买卖指令或收益承诺；结尾单独一行注明：以上基于公开数据的分析，不构成投资建议。");
}
AShareAiPanel::AShareAiPanel(QWidget* parent) : QWidget(parent), service_(new AShareAiService(this)) {
    setObjectName("ashareAiPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* top = new QHBoxLayout;
    header_ = new QLabel;
    header_->setProperty("muted", true);
    header_->setWordWrap(true);
    top->addWidget(header_, 1);
    auto* settings = new QPushButton(QStringLiteral("AI 设置"));
    settings->setObjectName("ashareAiSettings");
    auto* clear = new QPushButton(QStringLiteral("清空对话"));
    top->addWidget(settings);
    top->addWidget(clear);
    layout->addLayout(top);
    auto* presets = new QHBoxLayout;
    const QList<QPair<QString, QString>> preset_questions{
        {QStringLiteral("综合解读"),
         QStringLiteral("请综合走势、技术指标和基本面（如有），对这只股票当前状态做一个解读。")},
        {QStringLiteral("技术面"), QStringLiteral("请只从技术面分析：趋势、均线、MACD/KDJ/RSI、成交量与关键价位。")},
        {QStringLiteral("风险点"), QStringLiteral("请列出从这些数据中能看到的主要风险点和需要继续观察的信号。")}};
    for (const auto& preset : preset_questions) {
        auto* button = new QPushButton(preset.first);
        connect(button, &QPushButton::clicked, this, [this, q = preset.second]() { ask(q); });
        presets->addWidget(button);
    }
    include_position_ = new QCheckBox(QStringLiteral("附带我的持仓"));
    include_position_->setObjectName("ashareAiIncludePosition");
    include_position_->setToolTip(QStringLiteral("勾选后会把你在「持仓」里记录的该股数量与成本一起发送给模型"));
    presets->addStretch();
    presets->addWidget(include_position_);
    layout->addLayout(presets);
    text_ = new QTextBrowser;
    text_->setObjectName("ashareAiText");
    text_->setOpenExternalLinks(false);
    text_->setMinimumHeight(110);
    layout->addWidget(text_, 1);
    auto* ask_row = new QHBoxLayout;
    input_ = new QLineEdit;
    input_->setObjectName("ashareAiInput");
    input_->setPlaceholderText(QStringLiteral("追问，例如：近期放量意味着什么？"));
    send_ = new QPushButton(QStringLiteral("发送"));
    stop_ = new QPushButton(QStringLiteral("停止"));
    stop_->setEnabled(false);
    ask_row->addWidget(input_, 1);
    ask_row->addWidget(send_);
    ask_row->addWidget(stop_);
    layout->addLayout(ask_row);
    connect(settings, &QPushButton::clicked, this, [this]() {
        AShareAiSettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            update_header();
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        service_->cancel();
        history_.clear();
        streaming_.clear();
        render();
    });
    connect(send_, &QPushButton::clicked, this, [this]() {
        if (!input_->text().trimmed().isEmpty()) {
            ask(input_->text().trimmed());
            input_->clear();
        }
    });
    connect(input_, &QLineEdit::returnPressed, send_, &QPushButton::click);
    connect(stop_, &QPushButton::clicked, this, [this]() {
        service_->cancel();
        if (!streaming_.isEmpty())
            history_.append({"assistant", streaming_ + QStringLiteral("\n\n（已停止）")});
        streaming_.clear();
        send_->setEnabled(true);
        stop_->setEnabled(false);
        render();
    });
    connect(service_, &AShareAiService::progress, this, [this](const QString& text, bool thinking) {
        streaming_ = text;
        thinking_ = thinking;
        render();
    });
    connect(service_, &AShareAiService::finished, this, [this](const QString& text) {
        history_.append({"assistant", text});
        streaming_.clear();
        thinking_ = false;
        send_->setEnabled(true);
        stop_->setEnabled(false);
        render();
    });
    connect(service_, &AShareAiService::failed, this, [this](const QString& error) {
        // Drop the unanswered question so a retry does not send it twice.
        if (!history_.isEmpty() && history_.last().role == "user")
            history_.removeLast();
        streaming_ = QStringLiteral("**未完成：** ") + error;
        thinking_ = false;
        send_->setEnabled(true);
        stop_->setEnabled(false);
        render();
        streaming_.clear();
    });
    update_header();
    render();
}
void AShareAiPanel::set_symbol(const QString& symbol, const QString& name) {
    if (symbol == symbol_) {
        name_ = name;
        update_header();
        return;
    }
    service_->cancel();
    symbol_ = symbol;
    name_ = name;
    history_.clear();
    streaming_.clear();
    send_->setEnabled(true);
    stop_->setEnabled(false);
    update_header();
    render();
}
void AShareAiPanel::set_dark(bool dark) {
    dark_ = dark;
    text_->setStyleSheet(
        dark ? "background:#0f1319;color:#e6eaf0;border:1px solid #232a35;border-radius:4px;padding:6px;"
             : "background:#ffffff;color:#14171c;border:1px solid #dadfe6;border-radius:4px;padding:6px;");
    render();
}
void AShareAiPanel::update_header() {
    const auto s = load_ai_settings();
    const bool ready = !ProviderCatalog::requires_api_key(s.provider) || has_api_key(s.provider);
    header_->setText(QStringLiteral("%1 · %2%3  |  AI 生成内容可能有误，不构成投资建议")
                         .arg(ProviderCatalog::display_name(s.provider), s.model,
                              ready ? QString() : QStringLiteral(" · 未设置 API Key")));
}
void AShareAiPanel::ask(const QString& question) {
    if (symbol_.isEmpty()) {
        streaming_ = QStringLiteral("请先在自选中选择一只股票。");
        render();
        streaming_.clear();
        return;
    }
    if (service_->busy())
        return;
    QVector<AiMessage> messages;
    if (history_.isEmpty()) {
        // The first turn carries the data snapshot; follow-ups reuse it via the history.
        last_context_ = context_ ? context_(include_position_->isChecked()) : QString();
        history_.append({"user", QStringLiteral("以下是当前数据快照：\n%1\n\n问题：%2").arg(last_context_, question)});
    } else
        history_.append({"user", question});
    streaming_.clear();
    thinking_ = true;
    send_->setEnabled(false);
    stop_->setEnabled(true);
    render();
    service_->ask(system_prompt(), history_);
}
void AShareAiPanel::render() {
    QString md;
    for (int i = 0; i < history_.size(); ++i) {
        const auto& m = history_[i];
        if (m.role == "user") {
            // Show only the question, not the full data snapshot.
            const auto q = i == 0 ? m.content.section(QStringLiteral("问题："), -1) : m.content;
            md += QStringLiteral("**问：** ") + q + "\n\n";
        } else
            md += m.content + "\n\n---\n\n";
    }
    if (!streaming_.isEmpty())
        md += streaming_ + "\n\n";
    if (thinking_ && service_->busy())
        md += QStringLiteral("*模型思考 / 生成中…*\n\n");
    if (md.isEmpty())
        md = QStringLiteral(
            "选择一只股票后点击「综合解读 / 技术面 / 风险点」，或直接输入问题。\n\n"
            "发送的内容：该股实时行情、日 K 近 30 根、技术指标、已加载的 F10 摘要，以及（如已加载）大盘涨跌家数。");
    const bool at_bottom = text_->verticalScrollBar()->value() >= text_->verticalScrollBar()->maximum() - 4;
    // Model output is untrusted: render Markdown but never raw HTML (no images / local resources).
    text_->document()->setMarkdown(
        md, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML));
    if (at_bottom)
        text_->verticalScrollBar()->setValue(text_->verticalScrollBar()->maximum());
}
} // namespace fincept::screens
