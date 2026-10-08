#include "ChatPanel.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QVBoxLayout>

#include "ai/gtp.h"

namespace vgo {

namespace {

// 用户消息颜色：与 AI 回复（默认深色）区分，避免连续问答混淆。调亮一些以更醒目。
const QColor kUserColor(0x1f, 0x6f, 0xeb);  // 蓝 #1f6feb

// 固定格式 AI 提示（非 LLM 生成）的淡色：让「系统播报」后退，突出正文答复。
const QColor kNoteColor(0x57, 0x60, 0x6a);  // 灰 #57606a

// 证据/trace 行的标签色：区分于正文与系统提示，提示「这是溯源信息」。
const QColor kTraceLabelColor(0x82, 0x50, 0xdf);  // 紫 #8250df

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

// A one-line human-readable label for a verified claim (D2.4 evidence footer).
QString claimLabel(const Claim& c, int size) {
    const QString side = (c.color == Color::Black)   ? QStringLiteral("黑")
                         : (c.color == Color::White) ? QStringLiteral("白")
                                                     : QStringLiteral("—");
    switch (c.type) {
        case ClaimType::ScoreLead:
            return QStringLiteral("%1领先 %2 目").arg(side).arg(c.value, 0, 'f', 1);
        case ClaimType::Winrate:
            return QStringLiteral("%1胜率 %2%")
                .arg(side)
                .arg(static_cast<int>(c.value * 100.0 + 0.5));
        case ClaimType::GroupLife:
            return QStringLiteral("%1 %2")
                .arg(QString::fromStdString(gtpPoint(c.point, size)),
                     c.alive ? QStringLiteral("活") : QStringLiteral("死"));
    }
    return {};
}

}  // namespace

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    log_ = new QTextBrowser(this);
    log_->setReadOnly(true);
    log_->setOpenLinks(false);  // 不自动跳转，仅靠 anchorClicked 处理内部「claim:」链接
    // 证据行是超链接（hover 看来源、点击跳 PV），需要 TextBrowser 交互；只读 + 可选文本。
    log_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(log_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString s = url.toString();
        if (!s.startsWith(QStringLiteral("claim:"))) return;
        bool ok = false;
        const int idx = s.mid(6).toInt(&ok);
        if (ok) emit evidenceActivated(idx);
    });

    input_ = new QLineEdit(this);
    input_->setPlaceholderText(QStringLiteral("问 AI：为什么下这里 / 我该下哪 / 谁领先…"));
    auto* send = new QPushButton(QStringLiteral("发送"), this);
    connect(send, &QPushButton::clicked, this, [this]() {
        const QString t = input_->text().trimmed();
        if (t.isEmpty()) return;
        appendHuman(t);
        input_->clear();
        setFeedbackEnabled(false);  // A4：发送新问题后置灰，防止反馈错绑到旧回复
        emit messageSent(t);
    });
    connect(input_, &QLineEdit::returnPressed, send, &QPushButton::click);

    auto* newChat = new QPushButton(QStringLiteral("＋ 新对话"), this);
    connect(newChat, &QPushButton::clicked, this, [this]() {
        clear();
        setFeedbackEnabled(false);
        emit newChatRequested();
    });

    auto* quickRow = new QHBoxLayout;
    for (const char* q : {"为什么下这里", "我该下哪", "谁领先", "求指点", "这步怎么样"}) {
        auto* b = new QPushButton(QString::fromUtf8(q), this);
        connect(b, &QPushButton::clicked, this, [this, q]() {
            appendHuman(QString::fromUtf8(q));
            setFeedbackEnabled(false);
            emit messageSent(QString::fromUtf8(q));
        });
        quickRow->addWidget(b);
    }

    // A4：「上一条回复反馈」——👍/👎 作用于最近一条 AI 回复（气泡化按钮迁 L15）。
    upvote_ = new QPushButton(QStringLiteral("👍"), this);
    upvote_->setToolTip(QStringLiteral("这条回答有帮助"));
    downvote_ = new QPushButton(QStringLiteral("👎"), this);
    downvote_->setToolTip(QStringLiteral("这条回答不好（会记入坏样本供改进）"));
    upvote_->setEnabled(false);
    downvote_->setEnabled(false);
    connect(upvote_, &QPushButton::clicked, this, [this]() {
        setFeedbackEnabled(false);
        emit feedbackGiven(true);
    });
    connect(downvote_, &QPushButton::clicked, this, [this]() {
        setFeedbackEnabled(false);
        emit feedbackGiven(false);
    });

    auto* sendRow = new QHBoxLayout;
    sendRow->addWidget(input_, 1);
    sendRow->addWidget(send);
    sendRow->addWidget(upvote_);
    sendRow->addWidget(downvote_);

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
    appendAi(text, std::vector<ClaimVerdict>{}, 0);
}

void ChatPanel::appendNote(const QString& text) {
    // 固定格式提示用淡灰，正文不渲染 markdown（这些是程序拼接的纯文本）。
    appendMessage(log_, QStringLiteral("AI"), text, /*markdown=*/false, kNoteColor);
}

void ChatPanel::appendAi(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                         int size) {
    appendMessage(log_, QStringLiteral("AI"), text, /*markdown=*/true, QColor());
    if (verdicts.empty()) return;

    // 证据行：每条断言一个可点击超链接，hover 显示来源与校验说明，点击跳 PV。
    QTextCursor cursor(log_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat block;
    block.setTopMargin(2);
    cursor.setBlockFormat(block);

    const QTextCharFormat plain;
    QTextCharFormat traceLabel = plain;
    traceLabel.setForeground(kTraceLabelColor);
    traceLabel.setFontWeight(QFont::Bold);
    cursor.insertText(QStringLiteral("证据："), traceLabel);
    for (std::size_t i = 0; i < verdicts.size(); ++i) {
        if (i > 0) cursor.insertText(QStringLiteral("  "), plain);
        const ClaimVerdict& v = verdicts[i];
        QColor color;
        switch (v.disposition) {
            case ClaimDisposition::Corrected:    color = QColor(0x9a, 0x67, 0x00); break;  // 橙
            case ClaimDisposition::Unverifiable: color = QColor(0x6e, 0x77, 0x81); break;  // 灰
            default:                             color = QColor(0x1a, 0x7f, 0x37); break;  // 绿
        }
        QTextCharFormat link;
        link.setAnchor(true);
        link.setAnchorHref(QStringLiteral("claim:%1").arg(i));
        link.setForeground(color);
        link.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        link.setToolTip(
            QStringLiteral("来源：%1%2")
                .arg(QString::fromStdString(v.claim.source),
                     v.note.empty() ? QString()
                                    : QStringLiteral(" · ") + QString::fromStdString(v.note)));
        cursor.insertText(claimLabel(v.claim, size), link);
    }
    cursor.insertBlock();
    scrollToBottom(log_);
}

void ChatPanel::clear() {
    log_->clear();
    stream_buffer_.clear();
    streaming_ = false;
}

void ChatPanel::setFeedbackEnabled(bool on) {
    if (upvote_) upvote_->setEnabled(on);
    if (downvote_) downvote_->setEnabled(on);
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
