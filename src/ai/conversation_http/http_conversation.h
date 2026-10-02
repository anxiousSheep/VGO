#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include "ai/conversation.h"
#include "ai/skill.h"

namespace vgo {

// OpenAI-compatible chat-completions client (works with Ollama, llama.cpp
// server, DeepSeek, Qwen, and other OpenAI-compatible endpoints). Supports
// non-streaming `answer` and SSE streaming `answerStream`. Injects matching
// domain Skills into the system prompt (N4). Falls back to TemplateConversation
// when not configured (handled by the caller).
class HttpConversationService : public QObject, public ConversationService {
    Q_OBJECT
public:
    HttpConversationService(const QString& baseUrl, const QString& apiKey,
                            const QString& model, QObject* parent = nullptr);
    ~HttpConversationService() override;

    bool available() const { return available_; }

    void answer(const ConversationRequest& req, Callback cb) override;
    void answerStream(const ConversationRequest& req, DeltaCallback cb) override;
    bool supportsStreaming() const override { return true; }
    void cancel() override;

    // Replace the built-in skill set (e.g. loaded from hot-reloadable files).
    void setSkills(SkillRegistry skills) { skills_ = std::move(skills); }

private:
    QString baseUrl_;
    QString apiKey_;
    QString model_;
    bool available_ = false;
    SkillRegistry skills_ = SkillRegistry::builtin();
    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QByteArray stream_buffer_;  // SSE partial-line accumulation
};

}  // namespace vgo
