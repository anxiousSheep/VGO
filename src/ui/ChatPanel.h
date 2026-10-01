#pragma once

#include <QWidget>

class QLineEdit;
class QTextEdit;

namespace vgo {

// A chat panel: a read-only message log, an input box, quick-question buttons,
// and a "＋ 新对话" button that clears history. Emits messageSent() for both
// free-text and quick questions, and newChatRequested() when a fresh
// conversation is started.
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    void appendAi(const QString& text);
    void clear();

signals:
    void messageSent(const QString& text);
    void newChatRequested();

private:
    QTextEdit* log_ = nullptr;
    QLineEdit* input_ = nullptr;
};

}  // namespace vgo
