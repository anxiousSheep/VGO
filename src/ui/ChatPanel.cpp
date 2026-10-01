#include "ChatPanel.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vgo {

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    log_ = new QTextEdit(this);
    log_->setReadOnly(true);

    input_ = new QLineEdit(this);
    input_->setPlaceholderText(QStringLiteral("问 AI：为什么下这里 / 我该下哪 / 谁领先…"));
    auto* send = new QPushButton(QStringLiteral("发送"), this);
    connect(send, &QPushButton::clicked, this, [this]() {
        const QString t = input_->text().trimmed();
        if (t.isEmpty()) return;
        appendHuman(t);
        input_->clear();
        emit messageSent(t);
    });
    connect(input_, &QLineEdit::returnPressed, send, &QPushButton::click);

    auto* newChat = new QPushButton(QStringLiteral("＋ 新对话"), this);
    connect(newChat, &QPushButton::clicked, this, [this]() {
        clear();
        emit newChatRequested();
    });

    auto* quickRow = new QHBoxLayout;
    for (const char* q : {"为什么下这里", "我该下哪", "谁领先"}) {
        auto* b = new QPushButton(QString::fromUtf8(q), this);
        connect(b, &QPushButton::clicked, this, [this, q]() {
            appendHuman(QString::fromUtf8(q));
            emit messageSent(QString::fromUtf8(q));
        });
        quickRow->addWidget(b);
    }

    auto* sendRow = new QHBoxLayout;
    sendRow->addWidget(input_, 1);
    sendRow->addWidget(send);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(newChat);
    topRow->addStretch(1);

    auto* v = new QVBoxLayout(this);
    v->addLayout(topRow);
    v->addWidget(log_, 1);
    v->addLayout(quickRow);
    v->addLayout(sendRow);
}

void ChatPanel::appendHuman(const QString& text) {
    log_->append(QStringLiteral("<b>你：</b>") + text.toHtmlEscaped());
}

void ChatPanel::appendAi(const QString& text) {
    log_->append(QStringLiteral("<b>AI：</b>") + text.toHtmlEscaped());
}

void ChatPanel::clear() {
    log_->clear();
}

}  // namespace vgo
