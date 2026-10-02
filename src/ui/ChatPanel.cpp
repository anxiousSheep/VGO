#include "ChatPanel.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vgo {

namespace {

// Render a markdown string (as produced by the LLM) into a document fragment
// using Qt's built-in GitHub-flavoured markdown parser, so the AI reply shows
// headings / bold / lists / code blocks instead of raw markdown source.
QTextDocumentFragment markdownFragment(const QString& markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return QTextDocumentFragment(&doc);
}

// Append one chat message: a bold "<label>：" header followed by the content,
// rendered as markdown when `markdown` is true and plain text otherwise. Each
// message ends on its own paragraph so consecutive messages stay separated.
void appendMessage(QTextEdit* log, const QString& label, const QString& text,
                   bool markdown) {
    QTextCursor cursor(log->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertHtml(QStringLiteral("<b>%1：</b>").arg(label.toHtmlEscaped()));
    if (markdown)
        cursor.insertFragment(markdownFragment(text));
    else
        cursor.insertText(text);
    cursor.insertBlock();
    cursor.endEditBlock();
    log->ensureCursorVisible();
}

}  // namespace

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
    for (const char* q : {"为什么下这里", "我该下哪", "谁领先", "求指点", "这步怎么样"}) {
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
    appendMessage(log_, QStringLiteral("你"), text, /*markdown=*/false);
}

void ChatPanel::appendAi(const QString& text) {
    appendMessage(log_, QStringLiteral("AI"), text, /*markdown=*/true);
}

void ChatPanel::clear() {
    log_->clear();
    stream_buffer_.clear();
    streaming_ = false;
}

void ChatPanel::beginAiStream() {
    stream_buffer_.clear();
    QTextCursor cursor(log_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(QStringLiteral("<b>AI：</b>"));
    stream_content_pos_ = cursor.position();
    streaming_ = true;
}

void ChatPanel::appendAiStream(const QString& text) {
    if (!streaming_) beginAiStream();
    stream_buffer_ += text;
    // Show raw text as it arrives; it is replaced with rendered markdown in endAiStream().
    QTextCursor cursor(log_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    log_->ensureCursorVisible();
}

void ChatPanel::endAiStream() {
    if (!streaming_) return;
    QTextCursor cursor(log_->document());
    cursor.setPosition(stream_content_pos_);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.insertFragment(markdownFragment(stream_buffer_));
    cursor.insertBlock();
    streaming_ = false;
    stream_buffer_.clear();
}

}  // namespace vgo
