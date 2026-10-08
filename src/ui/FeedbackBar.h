#pragma once

#include <QAbstractButton>

class QLabel;
class QPushButton;
class QRadioButton;
class QPlainTextEdit;
class QVariantAnimation;
class QButtonGroup;

namespace vgo {

// 单个反馈图标按钮：视觉 28×28 圆角线框 SVG（命中区 40×40），按下缩放到 0.92
// （120ms 过渡）。视觉态由 FeedbackBar 通过 selected/grayed 驱动，hover/press 自理。
// 复用 QAbstractButton 的 clicked 信号，无需自增信号/属性。
class FeedbackIconButton : public QAbstractButton {
public:
    enum class Kind { Up, Down };

    FeedbackIconButton(Kind kind, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void setSelected(bool s);  // 高亮（👍 绿 / 👎 红）
    void setGrayed(bool g);    // 另一方灰化（opacity 0.4）

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    Kind kind_;
    bool selected_ = false;
    bool grayed_ = false;
    bool hovered_ = false;
    qreal press_scale_ = 1.0;
    QVariantAnimation* press_anim_ = nullptr;
};

// A4：内嵌于每条 AI 回复下方的反馈条。自带状态机：
//   Idle（未反馈）→ Upvoted（👍 定格，右侧淡入「感谢反馈」2 秒后收起）
//                → Downvoting（选原因）→ Downvoted（只读摘要 + 撤销）
//   撤销回到 Idle。不感知 replyIndex——由 ChatPanel 在信号里补序号。
class FeedbackBar : public QWidget {
    Q_OBJECT
public:
    explicit FeedbackBar(QWidget* parent = nullptr);

    // 调试开关：强制显示反馈条（即使正文过短等自动条件不满足），便于测试。
    static void setDebugForceShow(bool on) { debug_force_show_ = on; }
    static bool debugForceShow() { return debug_force_show_; }

signals:
    // up=true 点赞；up=false 差评。reason（slug）/ detail（「其他」自由文本）仅差评时有值。
    void feedbackGiven(bool up, const QString& reason, const QString& detail);
    // 撤销已定格的差评（回到未反馈态）。
    void feedbackUndone();

private:
    enum class State { Idle, Upvoted, Downvoting, Downvoted };

    void buildUi();
    void setState(State s);
    void onUp();
    void onDown();
    void onSubmit();
    void onCancel();
    void onUndo();
    void onReasonToggled(int id);
    void updateSubmitEnabled();
    void updateCounter();
    void resetReasonForm();
    void showThanks();

    static bool debug_force_show_;

    State state_ = State::Idle;

    FeedbackIconButton* up_btn_ = nullptr;
    FeedbackIconButton* down_btn_ = nullptr;
    QLabel* prompt_ = nullptr;  // 按钮左侧的提示文字「对本次回答满意吗」
    QLabel* thanks_ = nullptr;

    // 差评原因面板（选择态）：单选 + 「其他」文本框 + 提交/取消。
    QWidget* reason_form_ = nullptr;
    QButtonGroup* reason_group_ = nullptr;
    QPlainTextEdit* detail_edit_ = nullptr;
    QLabel* counter_ = nullptr;
    QPushButton* submit_btn_ = nullptr;
    QPushButton* cancel_btn_ = nullptr;

    // 差评已提交（只读摘要 + 撤销）。
    QWidget* reason_summary_ = nullptr;
    QLabel* summary_label_ = nullptr;
    QPushButton* undo_btn_ = nullptr;

    int selected_reason_id_ = -1;   // 当前选中的原因（ButtonGroup id）
    QString submitted_reason_;      // 提交后的原因 slug
    QString submitted_detail_;      // 提交后的补充文本
};

}  // namespace vgo
