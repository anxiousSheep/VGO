#pragma once

#include <QWidget>

#include <map>
#include <utility>
#include <vector>

#include "ai/verify/claim.h"

class QLineEdit;
class QTextBrowser;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace vgo {

// A chat panel: a scrollable message list, an input box, quick-question buttons,
// and a "＋ 新对话" button that clears history. Emits messageSent() for both
// free-text and quick questions, and newChatRequested() when a fresh
// conversation is started. Supports streaming AI output (N3).
//
// A4：每条 AI 回复下方内嵌一对圆形 👍/👎 反馈按钮（真实 QPushButton，带悬停/按下
// 动画）；选择后高亮被选项（👍 绿 / 👎 橙）、另一项置为不可点击；下一次输出自带
// 新的一组按钮。feedbackGiven(replyIndex, up) 带出该回复序号（随 clear 归零），
// 供上层按序回查反馈上下文落盘。一组按钮严格对应当前这一次问答的 AI 回复。
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    // 普通 AI 回复（无校验证据）。
    void appendAi(const QString& text);
    // D2.4：带校验证据的 AI 回复——正文后追加可 hover 看来源、可点击跳 PV 的证据行；
    // 末尾内嵌 👍/👎 反馈按钮；<claims> 的 JSON 原文以淡化灰呈现。
    void appendAi(const QString& text, const std::vector<ClaimVerdict>& verdicts, int size);
    // 固定格式的 AI 提示（非 LLM 生成），淡色呈现，不带反馈按钮。
    void appendNote(const QString& text);
    void clear();

    // Streaming output: begin a new AI line, append deltas, then end it.
    void beginAiStream();
    void appendAiStream(const QString& text);
    void endAiStream();

signals:
    void messageSent(const QString& text);
    void newChatRequested();
    // D2.4：用户点击某条证据（索引为 verdicts 的下标）。
    void evidenceActivated(int index);
    // A4：用户对第 replyIndex 条 AI 回复给出反馈（true=👍，false=👎）。
    void feedbackGiven(int replyIndex, bool up);

private:
    void addMessageWidget(QWidget* w);
    void scrollToBottom();
    QTextBrowser* makeAiBrowser();
    void composeAi(QTextBrowser* b, const QString& body, const QString& json,
                   const std::vector<ClaimVerdict>& verdicts, int size);
    void appendAiBubble(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                        int size, int replyIndex);
    void handleFeedback(int replyIndex, bool up);

    QScrollArea* scroll_ = nullptr;
    QWidget* messages_ = nullptr;
    QVBoxLayout* messages_layout_ = nullptr;
    QLineEdit* input_ = nullptr;

    // Streaming state（N3；当前 answerStream 回退到 answer，非流式路径未启用）。
    QTextBrowser* stream_browser_ = nullptr;
    QString stream_buffer_;

    int reply_counter_ = 0;  // AI 回复序号（随 clear 归零）
    std::map<int, std::pair<QPushButton*, QPushButton*>> feedback_buttons_;
};

}  // namespace vgo
