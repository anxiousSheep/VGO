#pragma once

#include <QWidget>

#include <vector>

#include "ai/verify/claim.h"

class QLineEdit;
class QTextBrowser;
class QPushButton;

namespace vgo {

// A chat panel: a read-only message log, an input box, quick-question buttons,
// and a "＋ 新对话" button that clears history. Emits messageSent() for both
// free-text and quick questions, and newChatRequested() when a fresh
// conversation is started. Supports streaming AI output (N3).
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    // 普通 AI 回复（无校验证据）。
    void appendAi(const QString& text);
    // D2.4：带校验证据的 AI 回复——正文后追加可 hover 看来源、可点击跳 PV 的证据行。
    void appendAi(const QString& text, const std::vector<ClaimVerdict>& verdicts, int size);
    // 固定格式的 AI 提示（非 LLM 生成：落子播报 / 导入棋谱 / 终局复盘等系统消息），
    // 以淡色呈现，与 LLM 的正文答复区分开。
    void appendNote(const QString& text);
    void clear();

    // Streaming output: begin a new AI line, append deltas, then end it.
    void beginAiStream();
    void appendAiStream(const QString& text);
    void endAiStream();

    // A4：上一条回复的反馈按钮（👍/👎）。收到新回复后启用，发送新问题后置灰。
    void setFeedbackEnabled(bool on);

signals:
    void messageSent(const QString& text);
    void newChatRequested();
    // D2.4：用户点击某条证据（索引为 verdicts 的下标）。
    void evidenceActivated(int index);
    // A4：用户对最近一条 AI 回复给出反馈（true=👍，false=👎）。
    void feedbackGiven(bool up);

private:
    QTextBrowser* log_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* upvote_ = nullptr;
    QPushButton* downvote_ = nullptr;
    QString stream_buffer_;
    int stream_content_pos_ = 0;
    bool streaming_ = false;
};

}  // namespace vgo
