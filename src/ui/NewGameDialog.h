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
                           QWidget* parent = nullptr);

    vgo::GameSetup setup() const { return setup_; }

private:
    void accept() override;

    bool human_vs_ai_;
    int suggested_level_ = 1;
    double suggested_kyu_ = 20.0;
    vgo::GameSetup setup_;
    QButtonGroup* sizeGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    QCheckBox* autoLevel_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QWidget* colorRow_ = nullptr;
    QWidget* levelRow_ = nullptr;
};
