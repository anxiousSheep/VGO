#pragma once

#include <QWidget>

class QLineEdit;
class QTextEdit;

namespace vgo {

// A chat panel: a read-only message log, an input box, and quick-question
// buttons. Emits messageSent() for both free-text and quick questions.
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void appendHuman(const QString& text);
    void appendAi(const QString& text);
    void clear();

signals:
    void messageSent(const QString& text);

private:
    QTextEdit* log_ = nullptr;
    QLineEdit* input_ = nullptr;
};

}  // namespace vgo
