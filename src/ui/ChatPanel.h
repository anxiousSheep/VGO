#pragma once

#include <QWidget>

class QLineEdit;
class QTextEdit;

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
    void appendAi(const QString& text);
    void clear();

    // Streaming output: begin a new AI line, append deltas, then end it.
    void beginAiStream();
    void appendAiStream(const QString& text);
    void endAiStream();

signals:
    void messageSent(const QString& text);
    void newChatRequested();

private:
    QTextEdit* log_ = nullptr;
    QLineEdit* input_ = nullptr;
    QString stream_buffer_;
    int stream_content_pos_ = 0;
    bool streaming_ = false;
};

}  // namespace vgo
