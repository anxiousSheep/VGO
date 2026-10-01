#pragma once

#include <QMainWindow>

#include "go/game.h"

class QLabel;

namespace vgo {
class GobanWidget;
}

// Main window: holds the Game and the board view, and wires up the toolbar /
// menu actions (new game, undo, pass, resign, score).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void newGame9();
    void newGame13();
    void newGame19();
    void onPointClicked(int x, int y);
    void onPass();
    void onUndo();
    void onResign();
    void onScore();

private:
    void startGame(int size);
    void refresh();
    void updateStatus();

    vgo::Game* game_ = nullptr;
    vgo::GobanWidget* board_view_ = nullptr;
    QLabel* status_ = nullptr;
    bool resigned_ = false;
};
