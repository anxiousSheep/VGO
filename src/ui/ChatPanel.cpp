#include "ChatPanel.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "ai/gtp.h"

namespace vgo {

namespace {

// 用户消息颜色：与 AI 回复（默认深色）区分，避免连续问答混淆。
const QColor kUserColor(0x1f, 0x6f, 0xeb);  // 蓝 #1f6feb

// 固定格式 AI 提示（非 LLM 生成）的淡色。
const QColor kNoteColor(0x57, 0x60, 0x6a);  // 灰 #57606a

// 证据/trace 行的标签色。
const QColor kTraceLabelColor(0x82, 0x50, 0xdf);  // 紫 #8250df

// A4：<claims> 机器断言 JSON 原文的淡化灰。
const QColor kClaimsColor(0x8b, 0x94, 0x9e);  // 灰 #8b949e

// A4：反馈按钮高亮色——👍 绿、👎 橙（与「证据」橙一致）。
const char* const kFeedbackUpAccent = "#1a7f37";
const char* const kFeedbackDownAccent = "#9a6700";

// Render a markdown string (as produced by the LLM) into a document fragment.
QTextDocumentFragment markdownFragment(const QString& markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return QTextDocumentFragment(&doc);
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

// 把纯文本转成可安全嵌入 QLabel 富文本的 HTML（转义 + 换行转 <br>），保持多行提示
// 与迁移前的 QTextCursor::insertText 行为一致（\n 换行）。
QString plainToRich(const QString& text) {
    QString s = text.toHtmlEscaped();
    s.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    return s;
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

// A4：反馈按钮样式——未选中透明+灰边，悬停/按下有浅灰动画；选中后高亮（👍 绿 / 👎 橙）。
void applyFeedbackStyle(QPushButton* b, bool up, bool selected) {
    const QString accent =
        QString::fromLatin1(up ? kFeedbackUpAccent : kFeedbackDownAccent);
    const QString bg = selected ? accent : QStringLiteral("transparent");
    const QString border = selected ? accent : QStringLiteral("#d0d7de");
    const QString hover = selected ? accent : QStringLiteral("#f3f4f6");
    const QString pressed = selected ? accent : QStringLiteral("#e6ebf1");
    b->setStyleSheet(QStringLiteral(
                         "QPushButton {"
                         "  background-color: %1;"
                         "  border: 1px solid %2;"
                         "  border-radius: 13px;"
                         "}"
                         "QPushButton:hover { background-color: %3; }"
                         "QPushButton:pressed { background-color: %4; }")
                         .arg(bg, border, hover, pressed));
}

}  // namespace

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    messages_ = new QWidget(scroll_);
    messages_layout_ = new QVBoxLayout(messages_);
    messages_layout_->setContentsMargins(8, 8, 8, 8);
    messages_layout_->setSpacing(10);
    messages_layout_->addStretch(1);  // 消息从顶部堆叠，末尾留空
    scroll_->setWidget(messages_);

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
    v->setContentsMargins(0, 0, 0, 0);
    v->addLayout(topRow);
    v->addWidget(scroll_, 1);
    v->addLayout(quickRow);
    v->addLayout(sendRow);
}

void ChatPanel::addMessageWidget(QWidget* w) {
    // 插在末尾 stretch 之前，保持消息从上往下堆叠。
    messages_layout_->insertWidget(messages_layout_->count() - 1, w);
    scrollToBottom();
}

void ChatPanel::scrollToBottom() {
    QTimer::singleShot(0, scroll_, [this]() {
        scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
    });
}

QTextBrowser* ChatPanel::makeAiBrowser() {
    auto* b = new QTextBrowser(messages_);
    b->setReadOnly(true);
    b->setOpenLinks(false);
    b->setTextInteractionFlags(Qt::TextBrowserInteraction);
    b->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    b->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    b->setFrameShape(QFrame::NoFrame);
    b->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; }"));
    b->document()->setDocumentMargin(0);
    // 随内容高度自适应（去掉内部滚动条后必须显式撑高）。
    connect(b->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            b, [b](const QSizeF& sz) {
                b->setFixedHeight(static_cast<int>(sz.height()) + 2);
            });
    connect(b, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString s = url.toString();
        if (!s.startsWith(QStringLiteral("claim:"))) return;
        bool ok = false;
        const int idx = s.mid(6).toInt(&ok);
        if (ok) emit evidenceActivated(idx);
    });
    return b;
}

void ChatPanel::composeAi(QTextBrowser* b, const QString& body, const QString& json,
                          const std::vector<ClaimVerdict>& verdicts, int size) {
    QTextCursor cursor(b->document());
    cursor.movePosition(QTextCursor::End);

    // "AI：" 头 + markdown 正文。
    cursor.insertHtml(QStringLiteral("<b>AI：</b>"));
    if (!body.isEmpty()) cursor.insertFragment(markdownFragment(body));
    cursor.insertBlock();

    // <claims> JSON 原文淡化灰小字。
    if (!json.isEmpty()) {
        QTextBlockFormat block;
        block.setTopMargin(2);
        cursor.setBlockFormat(block);
        QTextCharFormat fmt;
        fmt.setForeground(kClaimsColor);
        fmt.setFontPointSize(qMax(9.0, b->font().pointSizeF() - 1.0));
        cursor.insertText(json, fmt);
        cursor.insertBlock();
    }

    // 证据行：每条断言一个可点击超链接，hover 看来源、点击跳 PV。
    if (!verdicts.empty()) {
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
    }
}

void ChatPanel::appendAiBubble(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                               int size, int replyIndex) {
    QString body;
    QString json;
    splitClaimsBlock(text, &body, &json);

    auto* bubble = new QWidget(messages_);
    auto* v = new QVBoxLayout(bubble);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    QTextBrowser* browser = makeAiBrowser();
    composeAi(browser, body, json, verdicts, size);
    v->addWidget(browser);

    // A4：圆形 👍/👎 小按钮，仅在正文非空时显示。
    if (!text.trimmed().isEmpty()) {
        auto* up = new QPushButton(QStringLiteral("👍"), bubble);
        up->setFixedSize(26, 26);
        up->setCursor(Qt::PointingHandCursor);
        up->setFocusPolicy(Qt::NoFocus);
        up->setToolTip(QStringLiteral("这条回答有帮助"));
        applyFeedbackStyle(up, /*up=*/true, /*selected=*/false);

        auto* down = new QPushButton(QStringLiteral("👎"), bubble);
        down->setFixedSize(26, 26);
        down->setCursor(Qt::PointingHandCursor);
        down->setFocusPolicy(Qt::NoFocus);
        down->setToolTip(QStringLiteral("这条回答不好（会记入坏样本供改进）"));
        applyFeedbackStyle(down, /*up=*/false, /*selected=*/false);

        const int idx = replyIndex;
        connect(up, &QPushButton::clicked, this, [this, idx]() { handleFeedback(idx, true); });
        connect(down, &QPushButton::clicked, this, [this, idx]() { handleFeedback(idx, false); });
        feedback_buttons_[idx] = {up, down};

        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 2, 0, 0);
        row->setSpacing(6);
        row->addWidget(up);
        row->addWidget(down);
        row->addStretch(1);
        v->addLayout(row);
    }

    addMessageWidget(bubble);
}

void ChatPanel::handleFeedback(int replyIndex, bool up) {
    const auto it = feedback_buttons_.find(replyIndex);
    if (it == feedback_buttons_.end()) return;
    QPushButton* upBtn = it->second.first;
    QPushButton* downBtn = it->second.second;
    // 高亮被选项，另一项置为不可点击（禁用即不可再点，天然防重复投票）。
    applyFeedbackStyle(upBtn, /*up=*/true, /*selected=*/up);
    applyFeedbackStyle(downBtn, /*up=*/false, /*selected=*/!up);
    upBtn->setEnabled(false);
    downBtn->setEnabled(false);
    emit feedbackGiven(replyIndex, up);
}

void ChatPanel::appendHuman(const QString& text) {
    auto* label = new QLabel(messages_);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 整条消息（标签 + 正文）都用用户蓝，与迁移前的 QTextBrowser 配色一致。
    label->setText(QStringLiteral("<span style=\"color:%1;\"><b>你：</b>%2</span>")
                       .arg(kUserColor.name(), plainToRich(text)));
    addMessageWidget(label);
}

void ChatPanel::appendAi(const QString& text) {
    appendAi(text, std::vector<ClaimVerdict>{}, 0);
}

void ChatPanel::appendNote(const QString& text) {
    auto* label = new QLabel(messages_);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 固定格式提示整条用淡灰，与迁移前的 QTextBrowser 配色一致。
    label->setText(QStringLiteral("<span style=\"color:%1;\"><b>AI：</b>%2</span>")
                       .arg(kNoteColor.name(), plainToRich(text)));
    addMessageWidget(label);
}

void ChatPanel::appendAi(const QString& text, const std::vector<ClaimVerdict>& verdicts,
                         int size) {
    appendAiBubble(text, verdicts, size, reply_counter_);
    ++reply_counter_;
}

void ChatPanel::clear() {
    stream_browser_ = nullptr;
    stream_buffer_.clear();
    feedback_buttons_.clear();
    reply_counter_ = 0;
    // 清空所有消息控件，只留末尾的 stretch。
    while (messages_layout_->count() > 1) {
        QLayoutItem* item = messages_layout_->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void ChatPanel::beginAiStream() {
    stream_buffer_.clear();
    stream_browser_ = makeAiBrowser();
    QTextCursor cursor(stream_browser_->document());
    cursor.insertHtml(QStringLiteral("<b>AI：</b>"));
    addMessageWidget(stream_browser_);
}

void ChatPanel::appendAiStream(const QString& text) {
    if (!stream_browser_) beginAiStream();
    stream_buffer_ += text;
    QTextCursor cursor(stream_browser_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    scrollToBottom();
}

void ChatPanel::endAiStream() {
    if (!stream_browser_) return;
    // 用渲染后的 markdown 替换逐字流式文本。
    QTextCursor cursor(stream_browser_->document());
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    cursor.insertHtml(QStringLiteral("<b>AI：</b>"));
    cursor.insertFragment(markdownFragment(stream_buffer_));
    stream_browser_ = nullptr;
    stream_buffer_.clear();
    scrollToBottom();
}

}  // namespace vgo
