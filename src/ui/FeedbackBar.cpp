#include "FeedbackBar.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTextCursor>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace vgo {

namespace {

// 20×20 线框 SVG（Lucide thumbs-up / thumbs-down，24 viewBox 缩放到 20×20）。
// __COLOR__ 由渲染时替换成当前图标色。
const char* const kThumbUpSvg = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="__COLOR__" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7 10v12"/><path d="M15 5.88 14 10h5.83a2 2 0 0 1 1.92 2.56l-2.33 8A2 2 0 0 1 17.5 22H4a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2h2.76a2 2 0 0 0 1.79-1.11L12 2h0a3.13 3.13 0 0 1 3 3.88Z"/></svg>
)SVG";

const char* const kThumbDownSvg = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="__COLOR__" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 14V2"/><path d="M9 18.12 10 14H4.17a2 2 0 0 1-1.92-2.56l2.33-8A2 2 0 0 1 6.5 2H20a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-2.76a2 2 0 0 0-1.79 1.11L12 22h0a3.13 3.13 0 0 1-3-3.88Z"/></svg>
)SVG";

// 明暗两套对称配色。
struct FeedbackPalette {
    QColor iconIdle;
    QColor bgHover;
    QColor iconHover;
    QColor bgUp;
    QColor iconUp;
    QColor bgDown;
    QColor iconDown;
};

FeedbackPalette lightPalette() {
    return {QColor(QStringLiteral("#9CA3AF")), QColor(QStringLiteral("#F3F4F6")),
            QColor(QStringLiteral("#6B7280")), QColor(QStringLiteral("#DCFCE7")),
            QColor(QStringLiteral("#16A34A")), QColor(QStringLiteral("#FEE2E2")),
            QColor(QStringLiteral("#DC2626"))};
}

FeedbackPalette darkPalette() {
    return {QColor(QStringLiteral("#6B7280")), QColor(QStringLiteral("#374151")),
            QColor(QStringLiteral("#D1D5DB")), QColor(QStringLiteral("#14532D")),
            QColor(QStringLiteral("#4ADE80")), QColor(QStringLiteral("#7F1D1D")),
            QColor(QStringLiteral("#F87171"))};
}

bool isDarkScheme() {
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

const char* reasonSlug(int id) {
    switch (id) {
        case 0: return "inaccurate";
        case 1: return "incomplete";
        case 2: return "harmful";
        case 3: return "formatting";
        default: return "other";
    }
}

const char* reasonLabel(int id) {
    switch (id) {
        case 0: return "不准确";
        case 1: return "不完整";
        case 2: return "有害或不安全";
        case 3: return "格式混乱";
        default: return "其他";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// FeedbackIconButton
// ---------------------------------------------------------------------------

FeedbackIconButton::FeedbackIconButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent), kind_(kind) {
    setFixedSize(40, 40);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
    setCheckable(false);

    press_anim_ = new QVariantAnimation(this);
    press_anim_->setDuration(120);
    connect(press_anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        press_scale_ = v.toReal();
        update();
    });
}

QSize FeedbackIconButton::sizeHint() const {
    return QSize(40, 40);
}

void FeedbackIconButton::setSelected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    update();
}

void FeedbackIconButton::setGrayed(bool g) {
    if (grayed_ == g) return;
    grayed_ = g;
    update();
}

void FeedbackIconButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const FeedbackPalette pal = isDarkScheme() ? darkPalette() : lightPalette();

    QColor bg = Qt::transparent;
    QColor icon = pal.iconIdle;
    if (selected_) {
        bg = (kind_ == Kind::Up) ? pal.bgUp : pal.bgDown;
        icon = (kind_ == Kind::Up) ? pal.iconUp : pal.iconDown;
    } else if (!grayed_ && hovered_) {
        bg = pal.bgHover;
        icon = pal.iconHover;
    }

    // 另一方灰化：整按钮 opacity 0.4。
    p.setOpacity(grayed_ ? 0.4 : 1.0);

    // 按下缩放（以中心为基准，0.92，120ms 过渡）。
    if (!qFuzzyCompare(press_scale_, 1.0)) {
        p.translate(width() / 2.0, height() / 2.0);
        p.scale(press_scale_, press_scale_);
        p.translate(-width() / 2.0, -height() / 2.0);
    }

    // 32×32 圆角背景，居中于 40×40 命中区（两侧各留 4px → 视觉 gap 8px）。
    const qreal inset = (width() - 32) / 2.0;
    const QRectF box(inset, inset, 32, 32);
    if (bg.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, 8, 8);
    }

    // 20×20 线框图标，居中于 32×32 盒内。
    QString svg = QString::fromUtf8(kind_ == Kind::Up ? kThumbUpSvg : kThumbDownSvg);
    svg.replace(QStringLiteral("__COLOR__"), icon.name(QColor::HexRgb));
    QSvgRenderer renderer(svg.toUtf8());
    if (renderer.isValid()) {
        const qreal iconInset = inset + 6.0;
        renderer.render(&p, QRectF(iconInset, iconInset, 20, 20));
    }
}

void FeedbackIconButton::enterEvent(QEnterEvent*) {
    hovered_ = true;
    update();
}

void FeedbackIconButton::leaveEvent(QEvent*) {
    hovered_ = false;
    update();
}

void FeedbackIconButton::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        press_anim_->stop();
        press_anim_->setStartValue(press_scale_);
        press_anim_->setEndValue(0.92);
        press_anim_->start();
    }
    QAbstractButton::mousePressEvent(e);
}

void FeedbackIconButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        press_anim_->stop();
        press_anim_->setStartValue(press_scale_);
        press_anim_->setEndValue(1.0);
        press_anim_->start();
    }
    QAbstractButton::mouseReleaseEvent(e);
}

// ---------------------------------------------------------------------------
// FeedbackBar
// ---------------------------------------------------------------------------

bool FeedbackBar::debug_force_show_ = false;

FeedbackBar::FeedbackBar(QWidget* parent) : QWidget(parent) {
    buildUi();
    setState(State::Idle);

    // 系统明暗切换时刷新图标与「感谢反馈」配色。
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                up_btn_->update();
                down_btn_->update();
                thanks_->setStyleSheet(
                    QStringLiteral("QLabel { color: %1; }")
                        .arg(isDarkScheme() ? QStringLiteral("#4ADE80")
                                            : QStringLiteral("#16A34A")));
            });
}

void FeedbackBar::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 按钮行：margin-top 10px，与正文同 padding-left（0），宽度随内容收缩。
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 10, 0, 0);
    row->setSpacing(0);  // 命中区 40×40 紧贴 → 视觉 32×32 之间自然 gap 8px

    up_btn_ = new FeedbackIconButton(FeedbackIconButton::Kind::Up, this);
    down_btn_ = new FeedbackIconButton(FeedbackIconButton::Kind::Down, this);

    thanks_ = new QLabel(QStringLiteral("感谢反馈"), this);
    thanks_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }")
            .arg(isDarkScheme() ? QStringLiteral("#4ADE80") : QStringLiteral("#16A34A")));
    thanks_->hide();

    row->addWidget(up_btn_);
    row->addWidget(down_btn_);
    row->addWidget(thanks_);
    row->addStretch(1);
    root->addLayout(row);

    // 差评原因面板（选择态）。
    reason_form_ = new QWidget(this);
    auto* form = new QVBoxLayout(reason_form_);
    form->setContentsMargins(0, 8, 0, 0);
    form->setSpacing(4);

    reason_group_ = new QButtonGroup(this);
    reason_group_->setExclusive(true);
    const char* labels[] = {"不准确", "不完整", "有害或不安全", "格式混乱", "其他"};
    for (int id = 0; id < 5; ++id) {
        auto* r = new QRadioButton(QString::fromUtf8(labels[id]), reason_form_);
        reason_group_->addButton(r, id);
        form->addWidget(r);
    }

    detail_edit_ = new QPlainTextEdit(reason_form_);
    detail_edit_->setPlaceholderText(QStringLiteral("补充说明（可选，最多 200 字）"));
    detail_edit_->setFixedHeight(60);
    detail_edit_->hide();

    counter_ = new QLabel(reason_form_);
    counter_->setAlignment(Qt::AlignRight);
    counter_->hide();

    auto* actions = new QHBoxLayout;
    submit_btn_ = new QPushButton(QStringLiteral("提交"), reason_form_);
    cancel_btn_ = new QPushButton(QStringLiteral("取消"), reason_form_);
    actions->addWidget(submit_btn_);
    actions->addWidget(cancel_btn_);
    actions->addStretch(1);

    form->addWidget(detail_edit_);
    form->addWidget(counter_);
    form->addLayout(actions);
    reason_form_->hide();
    root->addWidget(reason_form_);

    // 差评已提交（只读摘要 + 撤销）。
    reason_summary_ = new QWidget(this);
    auto* summary = new QHBoxLayout(reason_summary_);
    summary->setContentsMargins(0, 8, 0, 0);
    summary_label_ = new QLabel(reason_summary_);
    undo_btn_ = new QPushButton(QStringLiteral("撤销"), reason_summary_);
    summary->addWidget(summary_label_);
    summary->addWidget(undo_btn_);
    summary->addStretch(1);
    reason_summary_->hide();
    root->addWidget(reason_summary_);

    connect(up_btn_, &QAbstractButton::clicked, this, [this] { onUp(); });
    connect(down_btn_, &QAbstractButton::clicked, this, [this] { onDown(); });
    connect(reason_group_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (!checked) return;
        selected_reason_id_ = id;
        onReasonToggled(id);
    });
    connect(detail_edit_, &QPlainTextEdit::textChanged, this, [this] { updateCounter(); });
    connect(submit_btn_, &QPushButton::clicked, this, [this] { onSubmit(); });
    connect(cancel_btn_, &QPushButton::clicked, this, [this] { onCancel(); });
    connect(undo_btn_, &QPushButton::clicked, this, [this] { onUndo(); });

    updateSubmitEnabled();
}

void FeedbackBar::setState(State s) {
    state_ = s;
    switch (s) {
        case State::Idle:
            up_btn_->setSelected(false);
            up_btn_->setGrayed(false);
            down_btn_->setSelected(false);
            down_btn_->setGrayed(false);
            reason_form_->hide();
            reason_summary_->hide();
            break;
        case State::Upvoted:
            up_btn_->setSelected(true);
            up_btn_->setGrayed(false);
            down_btn_->setSelected(false);
            down_btn_->setGrayed(true);
            reason_form_->hide();
            reason_summary_->hide();
            break;
        case State::Downvoting:
            up_btn_->setSelected(false);
            up_btn_->setGrayed(true);
            down_btn_->setSelected(true);
            down_btn_->setGrayed(false);
            reason_summary_->hide();
            reason_form_->show();
            break;
        case State::Downvoted:
            up_btn_->setSelected(false);
            up_btn_->setGrayed(true);
            down_btn_->setSelected(true);
            down_btn_->setGrayed(false);
            reason_form_->hide();
            reason_summary_->show();
            break;
    }
}

void FeedbackBar::onUp() {
    if (state_ != State::Idle) return;
    setState(State::Upvoted);
    emit feedbackGiven(true, QString(), QString());
    showThanks();
}

void FeedbackBar::onDown() {
    if (state_ != State::Idle) return;
    resetReasonForm();
    setState(State::Downvoting);
}

void FeedbackBar::onSubmit() {
    if (state_ != State::Downvoting || selected_reason_id_ < 0) return;
    submitted_reason_ = QString::fromLatin1(reasonSlug(selected_reason_id_));
    submitted_detail_ =
        (selected_reason_id_ == 4) ? detail_edit_->toPlainText().trimmed() : QString();

    QString summary = QString::fromUtf8(reasonLabel(selected_reason_id_));
    if (!submitted_detail_.isEmpty()) summary += QStringLiteral("：") + submitted_detail_;
    summary_label_->setText(summary);

    setState(State::Downvoted);
    emit feedbackGiven(false, submitted_reason_, submitted_detail_);
}

void FeedbackBar::onCancel() {
    if (state_ != State::Downvoting) return;
    resetReasonForm();
    setState(State::Idle);
}

void FeedbackBar::onUndo() {
    if (state_ != State::Downvoted) return;
    resetReasonForm();
    setState(State::Idle);
    emit feedbackUndone();
}

void FeedbackBar::onReasonToggled(int id) {
    const bool other = (id == 4);
    detail_edit_->setVisible(other);
    counter_->setVisible(other);
    updateSubmitEnabled();
}

void FeedbackBar::updateSubmitEnabled() {
    submit_btn_->setEnabled(selected_reason_id_ >= 0);
}

void FeedbackBar::updateCounter() {
    constexpr int kMax = 200;
    const QString t = detail_edit_->toPlainText();
    if (t.size() > kMax) {
        QTextCursor c = detail_edit_->textCursor();
        const int pos = c.position();
        detail_edit_->blockSignals(true);
        detail_edit_->setPlainText(t.left(kMax));
        detail_edit_->blockSignals(false);
        c.setPosition(qMin(pos, kMax));
        detail_edit_->setTextCursor(c);
    }
    counter_->setText(QStringLiteral("%1 / %2").arg(detail_edit_->toPlainText().size()).arg(kMax));
}

void FeedbackBar::resetReasonForm() {
    reason_group_->setExclusive(false);
    for (QAbstractButton* b : reason_group_->buttons()) b->setChecked(false);
    reason_group_->setExclusive(true);
    detail_edit_->clear();
    detail_edit_->hide();
    counter_->hide();
    selected_reason_id_ = -1;
    updateSubmitEnabled();
}

void FeedbackBar::showThanks() {
    // 右侧淡入，2 秒后淡出收起。
    auto* effect = new QGraphicsOpacityEffect(thanks_);
    thanks_->setGraphicsEffect(effect);
    effect->setOpacity(0.0);
    thanks_->show();

    auto* fadeIn = new QPropertyAnimation(effect, "opacity", this);
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(2000, this, [this]() {
        auto* eff = qobject_cast<QGraphicsOpacityEffect*>(thanks_->graphicsEffect());
        auto* fadeOut = new QPropertyAnimation(eff, "opacity", this);
        fadeOut->setDuration(200);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        connect(fadeOut, &QPropertyAnimation::finished, this, [this]() {
            thanks_->setGraphicsEffect(nullptr);
            thanks_->hide();
        });
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

}  // namespace vgo
