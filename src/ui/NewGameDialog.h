#pragma once

#include <QDialog>

#include "ai/game_setup.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QWidget;

// Modal "New Game" dialog. The mode is decided by the active tab, not here:
//   human_vs_ai == true  -> 人机对弈：棋盘大小 + 执子 + 难度
//   human_vs_ai == false -> 摆棋：仅棋盘大小
// D3.4：难度带「自动难度」开关——开启时据学习者画像的估计自动标定（含新手难度
// 动态爬坡），关闭时手动选择（新手 = 固定 20k）。
class NewGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewGameDialog(bool human_vs_ai, int suggested_level, double suggested_kyu,
                           int analysis_level, QWidget* parent = nullptr);

    vgo::GameSetup setup() const { return setup_; }

private:
    void accept() override;

    bool human_vs_ai_;
    int suggested_level_ = 1;
    double suggested_kyu_ = 20.0;
    int analysis_level_ = 15;  // 炸鱼惩罚时匹配的职业 AI 段位（形势判断强度档位）
    vgo::GameSetup setup_;
    QButtonGroup* sizeGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    QCheckBox* autoLevel_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QComboBox* handicapCombo_ = nullptr;  // L6：让子数（仅执黑时可用）
    QWidget* colorRow_ = nullptr;
    QWidget* levelRow_ = nullptr;
    QWidget* handicapRow_ = nullptr;
};
