#include "services/ashare/AShareAiService.h"

#include "services/llm/ModelCatalog.h"
#include "services/llm/ProviderCatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <memory>
#ifdef Q_OS_WIN
// <windows.h> must precede <wincrypt.h>; IncludeBlocks: Regroup would re-sort them.
// clang-format off
#    include <windows.h>
#    include <wincrypt.h>
// clang-format on
#endif

namespace fincept::services::ashare {
using fincept::ai_chat::ProviderCatalog;
namespace {
QSettings prefs() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, "AShareBoard", "Personal");
}
#ifdef Q_OS_WIN
QByteArray dpapi(const QByteArray& input, bool encrypt) {
    DATA_BLOB in{DWORD(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))}, out{0, nullptr};
    const BOOL ok =
        encrypt
            ? CryptProtectData(&in, L"AShareBoard AI key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
            : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok)
        return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#endif
QString key_name(const QString& provider) {
    return "ai/keys/" + provider.toLower();
}
// The two helpers below are copied from Fincept's services/llm/LlmHttpHelpers.cpp, whose
// translation unit depends on LlmService / MCP and is therefore not linked into the board.
QString server_error_message(const QByteArray& body) {
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};
    const auto ej = doc.object();
    QString msg;
    if (ej.value("message").isString())
        msg = ej.value("message").toString();
    if (msg.isEmpty() && ej.value("error").isObject()) {
        const auto eo = ej.value("error").toObject();
        if (eo.value("message").isString())
            msg = eo.value("message").toString();
        const auto md = eo.value("metadata").toObject();
        if (!md.value("raw").toString().isEmpty())
            msg += " (upstream " + md.value("provider_name").toString() + ": " + md.value("raw").toString() + ")";
    }
    if (msg.isEmpty() && ej.value("error").isString())
        msg = ej.value("error").toString();
    return msg;
}
QString strip_think(QString content) {
    static const QRegularExpression closed(QStringLiteral("<\\s*think\\s*>[\\s\\S]*?<\\s*/\\s*think\\s*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    content.remove(closed);
    static const QRegularExpression open(QStringLiteral("<\\s*think\\s*>[\\s\\S]*$"),
                                         QRegularExpression::CaseInsensitiveOption);
    content.remove(open);
    return content.trimmed();
}
} // namespace

AiSettings load_ai_settings() {
    auto s = prefs();
    AiSettings a;
    a.provider = s.value("ai/provider", a.provider).toString();
    a.model = s.value("ai/model", a.model).toString();
    a.base_url = s.value("ai/base_url").toString();
    if (!supported_ai_providers().contains(a.provider))
        a = AiSettings{};
    return a;
}
void save_ai_settings(const AiSettings& a) {
    auto s = prefs();
    s.setValue("ai/provider", a.provider);
    s.setValue("ai/model", a.model);
    s.setValue("ai/base_url", a.base_url.trimmed());
}
QStringList supported_ai_providers() {
    QStringList out;
    for (const auto& p : ProviderCatalog::known_providers())
        if (p != "gemini" && p != "fincept" && !ProviderCatalog::is_blocked(p))
            out << p;
    return out;
}
bool store_api_key(const QString& provider, const QString& key, QString& error) {
#ifdef Q_OS_WIN
    const auto cipher = dpapi(key.trimmed().toUtf8(), true);
    if (cipher.isEmpty()) {
        error = QStringLiteral("Windows 加密（DPAPI）失败，密钥未保存");
        return false;
    }
    prefs().setValue(key_name(provider), QString::fromLatin1(cipher.toBase64()));
    return true;
#else
    Q_UNUSED(provider);
    Q_UNUSED(key);
    error = QStringLiteral("当前平台没有可用的系统加密，未保存密钥");
    return false;
#endif
}
QString load_api_key(const QString& provider) {
#ifdef Q_OS_WIN
    const auto stored = prefs().value(key_name(provider)).toString();
    if (stored.isEmpty())
        return {};
    return QString::fromUtf8(dpapi(QByteArray::fromBase64(stored.toLatin1()), false));
#else
    Q_UNUSED(provider);
    return {};
#endif
}
bool has_api_key(const QString& provider) {
    return prefs().contains(key_name(provider));
}
void forget_api_key(const QString& provider) {
    prefs().remove(key_name(provider));
}

QString ai_endpoint(const AiSettings& a) {
    if (ProviderCatalog::is_blocked(a.provider, a.base_url))
        return {};
    return ProviderCatalog::chat_endpoint(a.provider, a.base_url.trimmed(), a.model);
}
int ai_max_tokens(const AiSettings& a) {
    const int cap = fincept::ai_chat::ModelCatalog::output_cap(a.provider.toLower(), a.model);
    // Replies here are short analyses; never ask for more than 4096 tokens.
    return std::min(cap > 0 ? cap : 4096, 4096);
}
QByteArray build_ai_body(const AiSettings& a, const QString& system, const QVector<AiMessage>& messages) {
    QJsonArray list;
    const bool anthropic = a.provider == "anthropic";
    if (!anthropic && !system.isEmpty())
        list.append(QJsonObject{{"role", "system"}, {"content", system}});
    for (const auto& m : messages)
        list.append(QJsonObject{{"role", m.role}, {"content", m.content}});
    QJsonObject body{{"model", a.model}, {"messages", list}, {"stream", true}};
    if (anthropic) {
        body["system"] = system;
        body["max_tokens"] = ai_max_tokens(a);
    } else if (a.provider == "openai") {
        // OpenAI's current models take max_completion_tokens and reject custom temperature on reasoning models.
        body["max_completion_tokens"] = ai_max_tokens(a);
    } else {
        body["max_tokens"] = ai_max_tokens(a);
        body["temperature"] = 0.3;
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QVector<QByteArray> SseBuffer::feed(const QByteArray& chunk) {
    pending_ += chunk;
    QVector<QByteArray> out;
    int newline;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        QByteArray line = pending_.left(newline).trimmed();
        pending_.remove(0, newline + 1);
        if (line.startsWith("data:"))
            out.append(line.mid(5).trimmed());
    }
    return out;
}
QString ai_stream_delta(const QString& provider, const QByteArray& payload, bool* done) {
    if (payload == "[DONE]") {
        if (done)
            *done = true;
        return {};
    }
    const auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return {};
    const auto o = doc.object();
    if (provider == "anthropic") {
        if (o.value("type").toString() == "message_stop" && done)
            *done = true;
        if (o.value("type").toString() == "content_block_delta") {
            const auto delta = o.value("delta").toObject();
            if (delta.value("type").toString() == "text_delta")
                return delta.value("text").toString();
        }
        return {};
    }
    const auto choices = o.value("choices").toArray();
    if (choices.isEmpty())
        return {};
    const auto choice = choices[0].toObject();
    if (!choice.value("finish_reason").isNull() && !choice.value("finish_reason").toString().isEmpty() && done)
        *done = true;
    // reasoning_content (DeepSeek-R / Kimi thinking) is deliberately not shown as answer text.
    return choice.value("delta").toObject().value("content").toString();
}
QString visible_ai_text(const QString& raw, bool* thinking) {
    // An opening <think> with no closing tag yet means the model is still reasoning.
    static const QRegularExpression pending(QStringLiteral("<\\s*think\\s*>(?![\\s\\S]*<\\s*/\\s*think\\s*>)"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (thinking)
        *thinking = pending.match(raw).hasMatch();
    return strip_think(raw);
}

AShareAiService::AShareAiService(QObject* parent) : QObject(parent), network_(this) {}
void AShareAiService::cancel() {
    if (reply_) {
        auto* r = reply_.data();
        reply_ = nullptr;
        r->abort();
        r->deleteLater();
    }
}
void AShareAiService::ask(const QString& system, const QVector<AiMessage>& messages) {
    cancel();
    const auto settings = load_ai_settings();
    const auto url = ai_endpoint(settings);
    if (url.isEmpty()) {
        emit failed(QStringLiteral("该模型服务商在独立看板中不可用，请在设置里更换"));
        return;
    }
    const auto key = load_api_key(settings.provider);
    if (key.isEmpty() && ProviderCatalog::requires_api_key(settings.provider)) {
        emit failed(QStringLiteral("尚未设置 %1 的 API Key，请点击「AI 设置」")
                        .arg(ProviderCatalog::display_name(settings.provider)));
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");
    if (settings.provider == "anthropic") {
        request.setRawHeader("x-api-key", key.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
    } else if (!key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(90000); // inactivity timeout; long streams keep it alive
    auto* reply = network_.post(request, build_ai_body(settings, system, messages));
    reply_ = reply;
    auto sse = std::make_shared<SseBuffer>();
    auto raw = std::make_shared<QString>();
    auto body = std::make_shared<QByteArray>();
    const auto provider = settings.provider;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, sse, raw, body, provider]() {
        if (reply != reply_)
            return;
        const auto chunk = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            *body += chunk;
            return;
        } // error JSON, parsed on finish
        bool changed = false;
        for (const auto& payload : sse->feed(chunk)) {
            const auto delta = ai_stream_delta(provider, payload, nullptr);
            if (!delta.isEmpty()) {
                *raw += delta;
                changed = true;
            }
        }
        if (changed) {
            bool thinking = false;
            const auto text = visible_ai_text(*raw, &thinking);
            emit progress(text, thinking);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, raw, body]() {
        if (reply != reply_) {
            reply->deleteLater();
            return;
        }
        reply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        *body += reply->readAll();
        const auto net_error = reply->error();
        const auto net_message = reply->errorString();
        reply->deleteLater();
        if (status >= 400 || (net_error != QNetworkReply::NoError && raw->isEmpty())) {
            auto message = server_error_message(*body);
            if (message.isEmpty())
                message = status ? QStringLiteral("HTTP %1").arg(status) : net_message;
            emit failed(QStringLiteral("模型服务返回错误：") + message);
            return;
        }
        const auto text = visible_ai_text(*raw, nullptr);
        if (text.isEmpty()) {
            emit failed(QStringLiteral("模型没有返回内容（可能被截断或只输出了思考过程）"));
            return;
        }
        emit finished(text);
    });
}
} // namespace fincept::services::ashare
