#pragma once

#include <QWidget>

#include <vector>

#include "ai/verify/claim.h"

class QLineEdit;
class QTextBrowser;

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

private:
    QTextBrowser* log_ = nullptr;
    QLineEdit* input_ = nullptr;
    QString stream_buffer_;
    int stream_content_pos_ = 0;
    bool streaming_ = false;
};

}  // namespace vgo
