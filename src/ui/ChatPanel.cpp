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

// A4：<claims> 机器断言 JSON 原文的淡化灰——保留可溯源，但不抢正文。
const QColor kClaimsColor(0x8b, 0x94, 0x9e);  // 灰 #8b949e

// A4：反馈按钮配色——未评价/未选中为浅灰，选中高亮（👍 绿、👎 红）。
const QColor kFeedbackInactive(0xb0, 0xb6, 0xbc);  // #b0b6bc
const QColor kFeedbackUp(0x1a, 0x7f, 0x37);        // 绿 #1a7f37
const QColor kFeedbackDown(0xd1, 0x24, 0x2f);      // 红 #d1242f

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

// A4：剥离 <claims>…</claims> 机器断言块——正文与 JSON 原文分开，JSON 供淡化灰展示。
void splitClaimsBlock(const QString& text, QString* body, QString* json) {
    const QString openTag = QStringLiteral("<claims>");
    const QString closeTag = QStringLiteral("</claims>");
    const int open = text.indexOf(openTag);
    if (open < 0) {
        *body = text;
        json->clear();
        return;
    }
    const int close = text.indexOf(closeTag, open);
    if (close < 0) {
        *body = text;
        json->clear();
        return;
    }
    *body = text.left(open);
    *json = text.mid(open + openTag.size(), close - open - openTag.size()).trimmed();
    *body += text.mid(close + closeTag.size());
}

// A4：把 <claims> 的 JSON 原文以淡化灰小字渲染（保留可溯源，但不抢正文）。
void appendClaimsJson(QTextEdit* log, const QString& json) {
    QTextCursor cursor(log->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat block;
    block.setTopMargin(2);
    cursor.setBlockFormat(block);
    QTextCharFormat fmt;
    fmt.setForeground(kClaimsColor);
    fmt.setFontPointSize(qMax(9.0, log->font().pointSizeF() - 1.0));
    cursor.insertText(json, fmt);
    cursor.insertBlock();
    scrollToBottom(log);
}

}  // namespace

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    log_ = new QTextBrowser(this);
    log_->setReadOnly(true);
    log_->setOpenLinks(false);  // 不自动跳转，仅靠 anchorClicked 处理内部「claim:/feedback:」链接
    // 证据行是超链接（hover 看来源、点击跳 PV），需要 TextBrowser 交互；只读 + 可选文本。
    log_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(log_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString s = url.toString();
        if (s.startsWith(QStringLiteral("claim:"))) {
            bool ok = false;
            const int idx = s.mid(6).toInt(&ok);
            if (ok) emit evidenceActivated(idx);
            return;
        }
        if (s.startsWith(QStringLiteral("feedback:"))) {
            // "feedback:up:3" / "feedback:down:3"
            const QString rest = s.mid(QStringLiteral("feedback:").size());
            const int colon = rest.lastIndexOf(QLatin1Char(':'));
            if (colon <= 0) return;
            const bool up = rest.left(colon) == QStringLiteral("up");
            const int idx = rest.mid(colon + 1).toInt();
            auto it = feedbacks_.find(idx);
            if (it == feedbacks_.end() || it->second.rating != 0) return;  // 已评价，忽略
            it->second.rating = up ? 1 : -1;
            recolorFeedback(idx, up);
            emit feedbackGiven(idx, up);
        }
    });

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
    appendAi(text, std::vector<ClaimVerdict>{}, 0);
}

void ChatPanel::appendNote(const QString& text) {
    // 固定格式提示用淡灰，正文不渲染 markdown（这些是程序拼接的纯文本）。
    appendMessage(log_, QStringLiteral("AI"), text, /*markdown=*/false, kNoteColor);
}

void ChatPanel::appendAi(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                         int size) {
    QString body;
    QString json;
    splitClaimsBlock(text, &body, &json);
    appendMessage(log_, QStringLiteral("AI"), body, /*markdown=*/true, QColor());
    if (!json.isEmpty()) appendClaimsJson(log_, json);

    // 证据行：每条断言一个可点击超链接，hover 显示来源与校验说明，点击跳 PV。
    if (!verdicts.empty()) {
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

    // A4：每条 AI 回复下方内嵌 👍/👎（小字、灰色），选择后高亮被选、置灰另一项。
    if (!text.trimmed().isEmpty()) {
        insertFeedbackButtons(reply_counter_);
        ++reply_counter_;
    }
}

void ChatPanel::insertFeedbackButtons(int replyIndex) {
    QTextCursor cursor(log_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat block;
    block.setTopMargin(2);
    cursor.setBlockFormat(block);

    const qreal pt = qMax(9.0, log_->font().pointSizeF() - 2.0);

    QTextCharFormat upFmt;
    upFmt.setForeground(kFeedbackInactive);
    upFmt.setFontPointSize(pt);
    upFmt.setAnchor(true);
    upFmt.setAnchorHref(QStringLiteral("feedback:up:%1").arg(replyIndex));

    FeedbackLine line;
    line.up_start = cursor.position();
    cursor.insertText(QStringLiteral("👍"), upFmt);
    line.up_end = cursor.position();

    QTextCharFormat sepFmt;
    sepFmt.setFontPointSize(pt);
    cursor.insertText(QStringLiteral("  "), sepFmt);

    QTextCharFormat downFmt = upFmt;
    downFmt.setAnchorHref(QStringLiteral("feedback:down:%1").arg(replyIndex));
    line.down_start = cursor.position();
    cursor.insertText(QStringLiteral("👎"), downFmt);
    line.down_end = cursor.position();

    cursor.insertBlock();
    scrollToBottom(log_);
    feedbacks_[replyIndex] = line;
}

void ChatPanel::recolorFeedback(int replyIndex, bool up) {
    const auto it = feedbacks_.find(replyIndex);
    if (it == feedbacks_.end()) return;
    const FeedbackLine& line = it->second;
    const qreal pt = qMax(9.0, log_->font().pointSizeF() - 2.0);

    QTextCursor cursor(log_->document());
    // 👍：选中（up）时高亮绿，否则置灰；去掉锚点使其不可再点。
    QTextCharFormat upFmt;
    upFmt.setForeground(up ? kFeedbackUp : kFeedbackInactive);
    upFmt.setFontPointSize(pt);
    cursor.setPosition(line.up_start);
    cursor.setPosition(line.up_end, QTextCursor::KeepAnchor);
    cursor.setCharFormat(upFmt);

    // 👎：选中（!up）时高亮红，否则置灰。
    QTextCharFormat downFmt;
    downFmt.setForeground(up ? kFeedbackInactive : kFeedbackDown);
    downFmt.setFontPointSize(pt);
    cursor.setPosition(line.down_start);
    cursor.setPosition(line.down_end, QTextCursor::KeepAnchor);
    cursor.setCharFormat(downFmt);
}

void ChatPanel::clear() {
    log_->clear();
    stream_buffer_.clear();
    streaming_ = false;
    feedbacks_.clear();
    reply_counter_ = 0;
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
