#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include <functional>

#include "ai/agent/tool_registry.h"
#include "ai/conversation.h"
#include "ai/skill.h"

namespace vgo {

// OpenAI-compatible chat-completions client (works with Ollama, llama.cpp
// server, DeepSeek, Qwen, and other OpenAI-compatible endpoints). Supports
// non-streaming `answer` (with function-calling / tools), SSE streaming
// `answerStream`, and injects matching domain Skills into the system prompt
// (N4). Falls back to TemplateConversation when not configured (by the caller).
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

    // A tool handler for function-calling: invoked when the model requests a
    // tool (e.g. demonstrate_go_moves). Returns a result string fed back to the
    // model. Set by the UI layer, which owns the board/demo state.
    using ToolHandler = std::function<std::string(const QString& name, const QJsonObject& args)>;
    void setToolHandler(ToolHandler h) { tool_handler_ = std::move(h); }

    // D1: inject the position-bound tool registry (built by the advisor from the
    // current board + engine facts). Its schemas are declared alongside the demo
    // tool, and its invoke() runs when the model calls one of them.
    void setTools(ToolRegistry tools) override { tool_registry_ = std::move(tools); }

private:
    void doChat(QJsonArray messages, int depth, Callback cb);

    QString baseUrl_;
    QString apiKey_;
    QString model_;
    bool available_ = false;
    SkillRegistry skills_ = SkillRegistry::builtin();
    ToolHandler tool_handler_;
    ToolRegistry tool_registry_;  // D1：多工具（position-bound，advisor 注入）
    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QByteArray stream_buffer_;  // SSE partial-line accumulation
};

}  // namespace vgo
