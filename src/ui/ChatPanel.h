#pragma once

#include <QWidget>

#include <map>
#include <vector>

#include "ai/verify/claim.h"

class QLineEdit;
class QTextBrowser;

namespace vgo {

// A chat panel: a read-only message log, an input box, quick-question buttons,
// and a "＋ 新对话" button that clears history. Emits messageSent() for both
// free-text and quick questions, and newChatRequested() when a fresh
// conversation is started. Supports streaming AI output (N3).
//
// A4：每条 AI 回复正文下方内嵌 👍/👎 反馈（作为日志内的可点击锚点，小字灰色），
// 选择后高亮被选项、置灰另一项；下一次输出自带新按钮。feedbackGiven(replyIndex, up)
// 带出该回复的序号（随 clear 归零），供上层按序回查反馈上下文落盘。
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    // 普通 AI 回复（无校验证据）。
    void appendAi(const QString& text);
    // D2.4：带校验证据的 AI 回复——正文后追加可 hover 看来源、可点击跳 PV 的证据行，
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
    struct FeedbackLine {
        int up_start = 0;     // 👍 锚点的文档字符区间
        int up_end = 0;
        int down_start = 0;   // 👎 锚点的文档字符区间
        int down_end = 0;
        int rating = 0;       // 0 = 未评价，+1 = 👍，-1 = 👎
    };

    void insertFeedbackButtons(int replyIndex);
    void recolorFeedback(int replyIndex, bool up);

    QTextBrowser* log_ = nullptr;
    QLineEdit* input_ = nullptr;
    QString stream_buffer_;
    int stream_content_pos_ = 0;
    bool streaming_ = false;
    int reply_counter_ = 0;                  // AI 回复序号（随 clear 归零）
    std::map<int, FeedbackLine> feedbacks_;  // replyIndex -> 锚点区间/评价状态
};

}  // namespace vgo
