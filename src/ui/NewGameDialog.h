#pragma once

#include <QDialog>

#include "ai/game_setup.h"

class QButtonGroup;
class QLabel;
class QSlider;
class QWidget;

// Modal "New Game" dialog: picks mode / board size / stone color (AI mode only)
// / difficulty (19 levels), and produces a GameSetup. 猜先 (nigiri) is resolved
// here by a random roll, so the caller stays deterministic and testable.
class NewGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewGameDialog(QWidget* parent = nullptr);

    vgo::GameSetup setup() const { return setup_; }

private:
    void accept() override;
    void onModeChanged(int mode);
    void refreshLevelLabel();

    vgo::GameSetup setup_;

    QButtonGroup* modeGroup_ = nullptr;
    QButtonGroup* sizeGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    QSlider* levelSlider_ = nullptr;
    QLabel* levelLabel_ = nullptr;
    QWidget* colorRow_ = nullptr;
    QWidget* levelRow_ = nullptr;
};
