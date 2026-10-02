#pragma once

#include <QDialog>

#include "ai/game_setup.h"

class QButtonGroup;
class QComboBox;
class QWidget;

// Modal "New Game" dialog. The mode is decided by the active tab, not here:
//   human_vs_ai == true  -> 人机对弈：棋盘大小 + 执子 + 难度
//   human_vs_ai == false -> 摆棋：仅棋盘大小
class NewGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewGameDialog(bool human_vs_ai, QWidget* parent = nullptr);

    vgo::GameSetup setup() const { return setup_; }

private:
    void accept() override;

    bool human_vs_ai_;
    vgo::GameSetup setup_;
    QButtonGroup* sizeGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QWidget* colorRow_ = nullptr;
    QWidget* levelRow_ = nullptr;
};
