#include "ChatPanel.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vgo {

namespace {

// 用户消息颜色：与 AI 回复（默认深色）区分，避免连续问答混淆。
const QColor kUserColor(0x09, 0x69, 0xda);  // 蓝 #0969da

// 每条消息之间的空隙（px）：加大段落间距，让请求/回复更易分辨。
constexpr int kMessageSpacing = 14;

// 追加内容后把视图滚到底部。QTextEdit::ensureCursorVisible 依赖「组件自身光标」
// 的位置，而我们在局部 QTextCursor 上插入时组件光标仍停留在文首，导致新消息
// 反而向上跳到历史记录；直接以滚动条最大值兜底最可靠。
void scrollToBottom(QTextEdit* log) {
    if (QScrollBar* sb = log->verticalScrollBar()) sb->setValue(sb->maximum());
}

// Render a markdown string (as produced by the LLM) into a document fragment
// using Qt's built-in GitHub-flavoured markdown parser, so the AI reply shows
// headings / bold / lists / code blocks instead of raw markdown source.
QTextDocumentFragment markdownFragment(const QString& markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return QTextDocumentFragment(&doc);
}

// Append one chat message: a bold "<label>：" header followed by the content,
// rendered as markdown when `markdown` is true and plain text otherwise. When
// `color` is valid the whole message (label + body) is tinted — used for the
// user's own messages. Each message ends on its own paragraph with extra top
// spacing so consecutive messages stay clearly separated.
void appendMessage(QTextEdit* log, const QString& label, const QString& text,
                   bool markdown, const QColor& color) {
    QTextCursor cursor(log->document());
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat block;
    block.setTopMargin(kMessageSpacing);
    cursor.setBlockFormat(block);

    cursor.beginEditBlock();
    if (color.isValid()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        cursor.insertHtml(QStringLiteral("<span style=\"color:%1;\"><b>%2：</b></span>")
                              .arg(color.name(), label.toHtmlEscaped()));
        cursor.insertText(text, fmt);
    } else {
        cursor.insertHtml(QStringLiteral("<b>%1：</b>").arg(label.toHtmlEscaped()));
        if (markdown)
            cursor.insertFragment(markdownFragment(text));
        else
            cursor.insertText(text);
    }
    cursor.insertBlock();
    cursor.endEditBlock();
    scrollToBottom(log);
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
    appendMessage(log_, QStringLiteral("你"), text, /*markdown=*/false, kUserColor);
}

void ChatPanel::appendAi(const QString& text) {
    appendMessage(log_, QStringLiteral("AI"), text, /*markdown=*/true, QColor());
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
    QTextBlockFormat block;
    block.setTopMargin(kMessageSpacing);
    cursor.setBlockFormat(block);
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
    scrollToBottom(log_);
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
    scrollToBottom(log_);
}

}  // namespace vgo
