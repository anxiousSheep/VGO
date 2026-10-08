#pragma once

#include <QWidget>

#include <vector>

#include "ai/verify/claim.h"

class QLineEdit;
class QTextBrowser;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace vgo {

class FeedbackBar;

// A chat panel: a scrollable message list, an input box, quick-question buttons,
// and a "＋ 新对话" button that clears history. Emits messageSent() for both
// free-text and quick questions, and newChatRequested() when a fresh
// conversation is started. Supports streaming AI output (N3).
//
// A4：每条 AI 回复下方内嵌一个 FeedbackBar（👍/👎 线框图标 + 差评原因面板），
// 仅当回复已完结、正文足够长、无报错且未反馈过时才渲染（FeedbackBar 自带状态机）。
// feedbackGiven(replyIndex, up, reason, detail) 带出该回复序号（随 clear 归零），
// 供上层按序回查反馈上下文落盘；feedbackUndone(replyIndex) 表示撤销差评。
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    // 普通 AI 回复（无校验证据）。
    void appendAi(const QString& text);
    // D2.4：带校验证据的 AI 回复——正文后追加可 hover 看来源、可点击跳 PV 的证据行；
    // 末尾内嵌反馈条；<claims> 的 JSON 原文以淡化灰呈现。
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
    // A4：用户对第 replyIndex 条 AI 回复给出反馈（up=true 点赞；false 差评）。
    // reason/detail 仅在差评时有值。
    void feedbackGiven(int replyIndex, bool up, const QString& reason, const QString& detail);
    // A4：撤销第 replyIndex 条回复的差评。
    void feedbackUndone(int replyIndex);

private:
    void addMessageWidget(QWidget* w);
    void scrollToBottom();
    // 正文统一字号：在默认字号基础上调大一号（AI 回复与用户提问共用）。
    QFont messageFont() const;
    QTextBrowser* makeAiBrowser();
    void composeAi(QTextBrowser* b, const QString& body, const QString& json,
                   const std::vector<ClaimVerdict>& verdicts, int size);
    void appendAiBubble(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                        int size, int replyIndex);
    bool shouldShowFeedback(const QString& body) const;

    QScrollArea* scroll_ = nullptr;
    QWidget* messages_ = nullptr;
    QVBoxLayout* messages_layout_ = nullptr;
    QLineEdit* input_ = nullptr;

    // Streaming state（N3；当前 answerStream 回退到 answer，非流式路径未启用）。
    QTextBrowser* stream_browser_ = nullptr;
    QString stream_buffer_;

    int reply_counter_ = 0;  // AI 回复序号（随 clear 归零）
};

}  // namespace vgo
