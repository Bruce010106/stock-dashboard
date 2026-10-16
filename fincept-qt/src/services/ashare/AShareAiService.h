#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QVector>


namespace fincept::services::ashare {
// AI assistant transport. Provider metadata, output caps and response parsing are
// reused from Fincept's LLM layer (ProviderCatalog / ModelCatalog, plus two helpers copied from LlmHttpHelpers);
// the MCP tool loop, accounts and DataHub are deliberately not pulled in.
struct AiSettings {
    QString provider = QStringLiteral("deepseek");
    QString model = QStringLiteral("deepseek-chat");
    QString base_url; // empty = provider default
};
struct AiMessage { QString role, content; }; // role: "user" or "assistant"

AiSettings load_ai_settings();
void save_ai_settings(const AiSettings& settings);
// Providers usable here: OpenAI-compatible ones plus Anthropic. Gemini (native
// body) and Fincept's own cloud models are not supported in the standalone board.
QStringList supported_ai_providers();
// API keys are encrypted with Windows DPAPI for the current user before they are
// written to QSettings; on other platforms storing a key is refused.
bool store_api_key(const QString& provider, const QString& key, QString& error);
QString load_api_key(const QString& provider);
bool has_api_key(const QString& provider);
void forget_api_key(const QString& provider);

// Pure helpers (unit-tested).
QString ai_endpoint(const AiSettings& settings);
int ai_max_tokens(const AiSettings& settings);
QByteArray build_ai_body(const AiSettings& settings, const QString& system, const QVector<AiMessage>& messages);
// Splits an SSE byte stream into complete "data:" JSON payloads; keeps partial lines buffered.
class SseBuffer {
  public:
    QVector<QByteArray> feed(const QByteArray& chunk);
  private:
    QByteArray pending_;
};
// Returns the text delta carried by one streamed event (empty if none); sets *done on end markers.
QString ai_stream_delta(const QString& provider, const QByteArray& payload, bool* done);
// Removes complete <think>…</think> blocks and hides an unfinished trailing one.
QString visible_ai_text(const QString& raw, bool* thinking);

class AShareAiService : public QObject {
    Q_OBJECT
  public:
    explicit AShareAiService(QObject* parent = nullptr);
    void ask(const QString& system, const QVector<AiMessage>& messages);
    void cancel();
    bool busy() const { return reply_ != nullptr; }
  signals:
    void progress(const QString& visible_text, bool thinking);
    void finished(const QString& text);
    void failed(const QString& error);
  private:
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
};
} // namespace fincept::services::ashare
