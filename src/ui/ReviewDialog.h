#pragma once

#include <QDialog>

class QLabel;
class QVBoxLayout;

namespace vgo {
class LearnerStore;
}

// D3.5「今日一题」：把学习者画像里到期的 SM-2 复习项呈现出来，用户按记忆质量
// 自评（又忘了 / 勉强 / 轻松），据此推进间隔重复调度并回写画像。数据与调度逻辑
// 全在 ai/learner/learner_model.*，本类只负责展示 + 触发保存。
class ReviewDialog : public QDialog {
    Q_OBJECT

public:
    // `today` 为「日序号」（如 QDate::currentDate().toJulianDay()），用于到期判定。
    ReviewDialog(vgo::LearnerStore* learner, int today, QWidget* parent = nullptr);

private:
    void rebuild();

    vgo::LearnerStore* learner_ = nullptr;
    int today_ = 0;
    QLabel* summary_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
};
