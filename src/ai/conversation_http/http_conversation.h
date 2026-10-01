#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include "ai/conversation.h"

namespace vgo {

// OpenAI-compatible chat-completions client (works with Ollama, llama.cpp
// server, DeepSeek, Qwen, and other OpenAI-compatible endpoints). Non-streaming.
// Falls back to TemplateConversation when not configured.
class HttpConversationService : public QObject, public ConversationService {
    Q_OBJECT
public:
    HttpConversationService(const QString& baseUrl, const QString& apiKey,
                            const QString& model, QObject* parent = nullptr);
    ~HttpConversationService() override;

    bool available() const { return available_; }

    void answer(const ConversationRequest& req, Callback cb) override;
    void cancel() override;

private:
    QString baseUrl_;
    QString apiKey_;
    QString model_;
    bool available_ = false;
    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
};

}  // namespace vgo
