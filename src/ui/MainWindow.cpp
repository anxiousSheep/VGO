#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include "GobanWidget.h"

namespace {

QString colorName(vgo::Color c) {
    switch (c) {
        case vgo::Color::Black: return QStringLiteral("黑");
        case vgo::Color::White: return QStringLiteral("白");
        default:                return QStringLiteral("—");
    }
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("VGO — 围棋练习"));

    board_view_ = new vgo::GobanWidget(this);
    setCentralWidget(board_view_);
    connect(board_view_, &vgo::GobanWidget::pointClicked, this, &MainWindow::onPointClicked);

    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);

    auto* act9 = new QAction(QStringLiteral("9×9"), this);
    auto* act13 = new QAction(QStringLiteral("13×13"), this);
    auto* act19 = new QAction(QStringLiteral("19×19"), this);
    auto* actPass = new QAction(QStringLiteral("虚着/停一手"), this);
    auto* actUndo = new QAction(QStringLiteral("悔棋"), this);
    auto* actResign = new QAction(QStringLiteral("认输"), this);
    auto* actScore = new QAction(QStringLiteral("计分/终局"), this);
    auto* actQuit = new QAction(QStringLiteral("退出"), this);

    connect(act9, &QAction::triggered, this, &MainWindow::newGame9);
    connect(act13, &QAction::triggered, this, &MainWindow::newGame13);
    connect(act19, &QAction::triggered, this, &MainWindow::newGame19);
    connect(actPass, &QAction::triggered, this, &MainWindow::onPass);
    connect(actUndo, &QAction::triggered, this, &MainWindow::onUndo);
    connect(actResign, &QAction::triggered, this, &MainWindow::onResign);
    connect(actScore, &QAction::triggered, this, &MainWindow::onScore);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);

    QMenu* gameMenu = menuBar()->addMenu(QStringLiteral("对局"));
    QMenu* newMenu = gameMenu->addMenu(QStringLiteral("新局"));
    newMenu->addAction(act9);
    newMenu->addAction(act13);
    newMenu->addAction(act19);
    gameMenu->addAction(actPass);
    gameMenu->addAction(actUndo);
    gameMenu->addAction(actResign);
    gameMenu->addSeparator();
    gameMenu->addAction(actScore);
    gameMenu->addSeparator();
    gameMenu->addAction(actQuit);

    QToolBar* tb = addToolBar(QStringLiteral("工具"));
    tb->setMovable(false);
    tb->addAction(act9);
    tb->addAction(act13);
    tb->addAction(act19);
    tb->addSeparator();
    tb->addAction(actUndo);
    tb->addAction(actPass);
    tb->addAction(actResign);
    tb->addAction(actScore);

    startGame(9);
}

void MainWindow::startGame(int size) {
    delete game_;
    game_ = new vgo::Game(size);
    resigned_ = false;
    refresh();
}

void MainWindow::newGame9() { startGame(9); }
void MainWindow::newGame13() { startGame(13); }
void MainWindow::newGame19() { startGame(19); }

void MainWindow::refresh() {
    board_view_->setBoard(&game_->board());
    board_view_->setLastMove(game_->last_move(), game_->has_last_move());
    board_view_->setGhostColor(game_->current());
    updateStatus();
}

void MainWindow::onPointClicked(int x, int y) {
    if (!game_ || resigned_ || game_->is_over()) return;
    if (game_->play(x, y)) {
        refresh();
    } else {
        status_->setText(QStringLiteral("该位置不能落子（已占/自杀/打劫）"));
    }
}

void MainWindow::onPass() {
    if (!game_ || resigned_ || game_->is_over()) return;
    game_->pass();
    refresh();
    if (game_->is_over()) {
        onScore();
    }
}

void MainWindow::onUndo() {
    if (!game_) return;
    if (game_->undo()) {
        resigned_ = false;
        refresh();
    } else {
        status_->setText(QStringLiteral("没有可悔的棋"));
    }
}

void MainWindow::onResign() {
    if (!game_ || resigned_ || game_->is_over()) return;
    const vgo::Color resigner = game_->current();
    const vgo::Color winner = vgo::opponent(resigner);
    resigned_ = true;
    status_->setText(QStringLiteral("%1方认输，%2方获胜").arg(colorName(resigner)).arg(colorName(winner)));
    QMessageBox::information(this, QStringLiteral("认输"),
                             QStringLiteral("%1方认输，%2方获胜")
                                 .arg(colorName(resigner))
                                 .arg(colorName(winner)));
}

void MainWindow::onScore() {
    if (!game_) return;
    const auto [black, white] = game_->score_area();
    const double lead = game_->score_black();
    QString result;
    if (lead > 0.0) result = QStringLiteral("黑胜");
    else if (lead < 0.0) result = QStringLiteral("白胜");
    else result = QStringLiteral("平局");

    QMessageBox::information(
        this, QStringLiteral("计分（数子法）"),
        QStringLiteral("黑方：%1 子（含目）\n白方：%2 子（含目）\n贴目：%3\n结果：%4（黑领先 %5 目）")
            .arg(black)
            .arg(white)
            .arg(game_->komi())
            .arg(result)
            .arg(lead));
}

void MainWindow::updateStatus() {
    if (game_->is_over()) {
        const auto [black, white] = game_->score_area();
        const vgo::Color win = game_->winner();
        status_->setText(QStringLiteral("终局：黑 %1 子 vs 白 %2 子（贴 %3），%4胜")
                             .arg(black)
                             .arg(white)
                             .arg(game_->komi())
                             .arg(colorName(win)));
        return;
    }
    status_->setText(QStringLiteral("第 %1 手 · %2方落子 · 黑提 %3 白提 %4")
                         .arg(game_->move_number() + 1)
                         .arg(colorName(game_->current()))
                         .arg(game_->captures(vgo::Color::Black))
                         .arg(game_->captures(vgo::Color::White)));
}
