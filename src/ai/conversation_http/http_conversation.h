#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <vector>

#include "ai/agent/tool_registry.h"
#include "ai/conversation.h"
#include "ai/skill.h"

namespace vgo {

// A2：多轮会话的一个回合——只存最终 user/assistant 文本；工具环路中间消息绝不入历史。
struct SessionTurn {
    QString user;       // 原始自然语言提问（非 buildUserPrompt 全文）
    QString assistant;  // 最终回答文本
};

// OpenAI-compatible chat-completions client (works with Ollama, llama.cpp
// server, DeepSeek, Qwen, and other OpenAI-compatible endpoints). Supports
// non-streaming `answer` (with function-calling / tools) and injects matching
// domain Skills into the system prompt (N4).
//
// 传输层（L16）：在 Windows 上走 WinHTTP 直连（`winhttp_transport`），而非
// QNetworkAccessManager——因为 QNAM 每次连 socket 都会经 QNetworkConnectionMonitor
// 枚举本机网卡，在 VPN / 虚拟网卡环境下触发 interfaceNameFromIndex 空指针崩溃。
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

    // D5.5 预算守护：单次回答的 token 上限与 HTTP 超时（超限/超时熔断并明示）。
    void setMaxTokens(int n) { max_tokens_ = n; }
    void setTimeoutMs(int ms) { timeout_ms_ = ms; }
    // D1.10：工具环路墙钟预算（配合 depth 上限，保证环路有界）。
    void setMaxLoopMs(qint64 ms) { max_loop_ms_ = ms; }

    // D5.3 模型路由：可选「轻模型」（意图 / 工具选择）。配置后，工具选择回合用轻模型、
    // 最终讲解回合用强模型（model_）；为空则退回单模型（现有行为）。
    void setRouterModel(const QString& m) { router_model_ = m; }
    QString routerModel() const { return router_model_; }

    // A2：清空多轮会话记忆（新对局 / 切换模式时由 UI 调用）。cancel() 不清会话。
    void clearSession();
    // A2：会话是否已有历史（多轮追问时投机缓存旁路，避免串答）。
    bool hasHistory() const override;

private:
    void doChat(QJsonArray messages, int depth, qint64 deadline_ms, int max_tokens,
                Callback cb);
    // B3 流式版 doChat：逐轮流式，工具回合丢弃 content（必要时通知 UI abort）、
    // 最终回合把 content 增量送 deltaCb；stream+tools 遇 4xx 时降级为非流式重试。
    void doChatStream(QJsonArray messages, int depth, qint64 deadline_ms, int max_tokens,
                      DeltaCallback cb);
    // 执行单个工具调用（function-calling）并返回结果串；doChat 与 doChatStream 共用。
    std::string execToolCall(const QJsonObject& call) const;

    QString baseUrl_;
    QString apiKey_;
    QString model_;
    QString router_model_;   // D5.3：可选轻模型（工具选择）；空 = 不路由
    bool available_ = false;
    SkillRegistry skills_ = SkillRegistry::builtin();
    ToolHandler tool_handler_;
    ToolRegistry tool_registry_;  // D1：多工具（position-bound，advisor 注入）
    int max_tokens_ = 0;          // A3：单次回答 token 上限；0 = 按 level 自动分档
    int timeout_ms_ = 120000;     // D5.5：HTTP 传输超时（熔断）；远端 LLM 慢，30s 过紧
    qint64 max_loop_ms_ = 60000;  // D1.10：工具环路墙钟预算
    int generation_ = 0;          // cancel() 递增，在途请求结果据此丢弃

    // A2：多轮会话记忆（只存最终 user/assistant 回合；工具环路中间消息不入历史）。
    std::vector<SessionTurn> session_;
    QString session_summary_;          // 超窗压缩后的「前情提要」，可为空
    static constexpr int kSessionWindowTurns = 6;  // 滑动窗口：最近 6 轮
};

// Builds the follow-up messages after the model issued tool calls: an
// assistant message carrying `toolCalls`, followed by exactly one `tool`
// message per call (matching tool_call_id, in order). `exec` resolves each
// call's QJsonObject to a result string. Pure (Qt-Core-only) so it can be
// unit-tested. Every tool_call must be answered, or the endpoint rejects the
// follow-up with "assistant message with 'tool_calls' must be followed by tool
// messages responding to each 'tool_call_id'".
QJsonArray buildToolFollowup(const QJsonArray& messages, const QString& content,
                             const QJsonArray& toolCalls,
                             const std::function<std::string(const QJsonObject&)>& exec);

// B3：把流式 tool_calls 增量（SseParser::drainToolCallDeltas 的原始 QJsonObject 列表）
// 按 index 聚合成完整的 tool_calls 数组（id/type/name 只取一次，arguments 串接）。
// 纯逻辑（Qt-Core-only），供单测与 answerStream 复用。
QJsonArray buildToolCallsFromDeltas(const std::vector<QJsonObject>& deltas);

// Builds the user prompt. For `Other` questions the engine facts (winrate /
// candidates / dead groups) are omitted so the model fetches them via tools (D1.9).
// Exposed (like buildToolFollowup) for unit-testing the convergence.
QString buildUserPrompt(const ConversationRequest& req, QuestionClass cls);

// D5.3 模型路由：决定某一次环路迭代用哪个模型、是否声明工具（纯逻辑、可单测）。
//   - 无 routerModel / 本局面无工具：单模型（强模型），声明工具直到 depth 上限。
//   - 有 routerModel 且有工具：depth 0（工具选择）用轻模型并声明工具；depth>=1
//     （最终讲解）用强模型且不再声明工具——轻模型挑工具、强模型做最终讲解。
struct TurnRoute {
    QString model;         // 本次请求用的模型 id
    bool declare_tools = false;
};
TurnRoute routeTurn(const QString& strong, const QString& router, int depth,
                    bool has_tools, bool before_deadline);

// A2：组装 messages = [system] + (摘要非空 ? [前情提要] : []) + 窗口内每轮
// [user, assistant] + [本轮 user]。纯逻辑（Qt-Core-only），供单测。
QJsonArray buildConversationMessages(const QJsonObject& system,
                                     const QString& summary,
                                     const std::vector<SessionTurn>& turns,
                                     const QJsonObject& user);

// A2：滑动窗口裁剪 + 规则式摘要（不额外调 LLM）。返回时 *turns 已裁到最近 max_turns
// 轮，*summary 累积了被裁轮次的「问题句 + 结论首句」。纯逻辑，供单测。
void compressSession(std::vector<SessionTurn>* turns, QString* summary, int max_turns);

// A3：按 level 分三档的结构契约文本（语气 + 结构 + 长度约定）。纯函数，供单测。
QString buildStructureContract(int level);

// A3：按 level 分档的 max_tokens。新手/进阶/高手 = 512/1024/768。
int maxTokensForLevel(int level);

// A3：组装 system prompt（人设 + 三档结构契约 + 教练记忆 + 断言契约 + 技能注入）。
// 暴露供单测（不再含「简短」等一刀切措辞）。
QString buildSystemPrompt(const SkillRegistry& skills, QuestionClass cls,
                          const ConversationRequest& req);

}  // namespace vgo
