#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include "ChatPanel.h"
#include "GobanWidget.h"

#include "ai/fallback_engine.h"
#include "ai/gtp.h"
#include "ai/session.h"
#include "ai/strength.h"
#include "ai/template_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/conversation_http/http_conversation.h"

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
    config_ = loadAppConfig(QCoreApplication::applicationDirPath());

    board_view_ = new vgo::GobanWidget(this);
    setCentralWidget(board_view_);
    connect(board_view_, &vgo::GobanWidget::pointClicked, this, &MainWindow::onPointClicked);

    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);

    // Chat panel in a right-hand dock.
    chat_ = new vgo::ChatPanel(this);
    auto* dock = new QDockWidget(QStringLiteral("与 AI 交流"), this);
    dock->setWidget(chat_);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(chat_, &vgo::ChatPanel::messageSent, this, &MainWindow::onChatMessage);

    auto* act9 = new QAction(QStringLiteral("9×9"), this);
    auto* act13 = new QAction(QStringLiteral("13×13"), this);
    auto* act19 = new QAction(QStringLiteral("19×19"), this);
    auto* actAiBlack = new QAction(QStringLiteral("人机对弈 · 执黑"), this);
    auto* actAiWhite = new QAction(QStringLiteral("人机对弈 · 执白"), this);
    auto* actPass = new QAction(QStringLiteral("虚着/停一手"), this);
    auto* actUndo = new QAction(QStringLiteral("悔棋"), this);
    auto* actResign = new QAction(QStringLiteral("认输"), this);
    auto* actScore = new QAction(QStringLiteral("计分/终局"), this);
    auto* actQuit = new QAction(QStringLiteral("退出"), this);

    connect(act9, &QAction::triggered, this, &MainWindow::newGame9);
    connect(act13, &QAction::triggered, this, &MainWindow::newGame13);
    connect(act19, &QAction::triggered, this, &MainWindow::newGame19);
    connect(actAiBlack, &QAction::triggered, this,
            [this] { startAi(size_, vgo::Color::Black); });
    connect(actAiWhite, &QAction::triggered, this,
            [this] { startAi(size_, vgo::Color::White); });
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
    newMenu->addSeparator();
    newMenu->addAction(actAiBlack);
    newMenu->addAction(actAiWhite);
    gameMenu->addAction(actPass);
    gameMenu->addAction(actUndo);
    gameMenu->addAction(actResign);
    gameMenu->addSeparator();
    gameMenu->addAction(actScore);
    gameMenu->addSeparator();
    gameMenu->addAction(actQuit);

    // Difficulty menu (affects the AI).
    QMenu* levelMenu = menuBar()->addMenu(QStringLiteral("难度"));
    for (int lv = 1; lv <= 10; ++lv) {
        auto* a = levelMenu->addAction(QString::fromStdString(vgo::strengthLabel(lv)));
        a->setCheckable(true);
        a->setChecked(lv == ai_level_);
        connect(a, &QAction::triggered, this, [this, lv, a, levelMenu] {
            ai_level_ = lv;
            if (controller_) controller_->setLevel(lv);
            for (QAction* other : levelMenu->actions()) other->setChecked(other == a);
        });
    }

    QToolBar* tb = addToolBar(QStringLiteral("工具"));
    tb->setMovable(false);
    tb->addAction(act9);
    tb->addAction(act13);
    tb->addAction(act19);
    tb->addSeparator();
    tb->addAction(actAiBlack);
    tb->addAction(actAiWhite);
    tb->addSeparator();
    tb->addAction(actUndo);
    tb->addAction(actPass);
    tb->addAction(actResign);
    tb->addAction(actScore);

    startLocal(9);
}

MainWindow::~MainWindow() {
    teardownAi();
    delete game_;
}

vgo::Game* MainWindow::currentGame() const {
    return inAiMode() ? &controller_->game() : game_;
}

void MainWindow::startLocal(int size) {
    teardownAi();
    delete game_;
    game_ = new vgo::Game(size);
    size_ = size;
    resigned_ = false;
    chat_->clear();
    appendAi(QStringLiteral("本地双人对弈模式。"));
    refresh();
}

void MainWindow::startAi(int size, vgo::Color humanColor) {
    teardownAi();
    delete game_;
    game_ = nullptr;
    size_ = size;
    resigned_ = false;

    // Engine: KataGo if configured and starts, else the built-in fallback.
    engine_ = nullptr;
    if (config_.katago.enabled && !config_.katago.binary.isEmpty() &&
        !config_.katago.model.isEmpty()) {
        auto* k = new vgo::KataGoEngine(config_.katago.binary, config_.katago.model,
                                        config_.katago.config);
        if (k->available()) engine_ = k; else delete k;
    }
    if (!engine_) engine_ = new vgo::FallbackEngine();

    // Conversation: HTTP if configured, else template.
    conversation_ = nullptr;
    if (config_.llm.enabled && !config_.llm.baseUrl.isEmpty()) {
        auto* h = new vgo::HttpConversationService(config_.llm.baseUrl, config_.llm.apiKey,
                                                   config_.llm.model);
        if (h->available()) conversation_ = h; else delete h;
    }
    if (!conversation_) conversation_ = new vgo::TemplateConversation();

    controller_ =
        new vgo::AIMatchController(size, humanColor, engine_, conversation_, ai_level_);
    controller_->onChanged = [this] { refresh(); };
    controller_->onAiMove = [this](const vgo::Move& m, const vgo::Analysis&) {
        if (m.is_pass) {
            appendAi(QStringLiteral("（虚着，停一手）"));
        } else {
            appendAi(QStringLiteral("我下在 %1。")
                         .arg(QString::fromStdString(vgo::gtpPoint(m.p, size_))));
        }
    };
    controller_->onReply = [this](const vgo::ConversationReply& r) {
        appendAi(QString::fromStdString(r.text));
        board_view_->setHighlight(r.highlight);
    };
    controller_->onActivity = [this](vgo::AiActivity a) {
        if (a == vgo::AiActivity::Thinking) status_->setText(QStringLiteral("AI 思考中…"));
    };

    chat_->clear();
    appendAi(humanColor == vgo::Color::Black
                 ? QStringLiteral("你好！你执黑先行。下棋时随时可以在右侧问我问题。")
                 : QStringLiteral("你好！我执黑先行。下棋时随时可以在右侧问我问题。"));
    controller_->start();
    refresh();
}

void MainWindow::teardownAi() {
    delete controller_;
    controller_ = nullptr;
    delete engine_;
    engine_ = nullptr;
    delete conversation_;
    conversation_ = nullptr;
    board_view_->setHighlight({});
    board_view_->setCandidates({});
}

void MainWindow::newGame9() { startLocal(9); }
void MainWindow::newGame13() { startLocal(13); }
void MainWindow::newGame19() { startLocal(19); }

void MainWindow::refresh() {
    vgo::Game* g = currentGame();
    if (!g) return;
    board_view_->setBoard(&g->board());
    board_view_->setLastMove(g->last_move(), g->has_last_move());
    board_view_->setGhostColor(g->current());
    board_view_->setHighlight({});
    updateStatus();
}

void MainWindow::onPointClicked(int x, int y) {
    if (inAiMode()) {
        if (!controller_->humanMayAct()) {
            if (controller_->isAiTurn()) status_->setText(QStringLiteral("轮到 AI，请稍候…"));
            return;
        }
        if (!controller_->game().is_legal(x, y)) {
            status_->setText(QStringLiteral("该位置不能落子（已占/自杀/打劫）"));
            return;
        }
        controller_->humanPlay({x, y});
        return;
    }
    if (!game_ || resigned_ || game_->is_over()) return;
    if (game_->play(x, y)) {
        refresh();
    } else {
        status_->setText(QStringLiteral("该位置不能落子（已占/自杀/打劫）"));
    }
}

void MainWindow::onPass() {
    if (inAiMode()) {
        controller_->humanPass();
        return;
    }
    if (!game_ || resigned_ || game_->is_over()) return;
    game_->pass();
    refresh();
    if (game_->is_over()) onScore();
}

void MainWindow::onUndo() {
    if (inAiMode()) {
        if (controller_->game().move_number() < 2 && !controller_->isThinking()) {
            status_->setText(QStringLiteral("没有可悔的棋"));
            return;
        }
        controller_->undo();
        return;
    }
    if (!game_) return;
    if (game_->undo()) {
        resigned_ = false;
        refresh();
    } else {
        status_->setText(QStringLiteral("没有可悔的棋"));
    }
}

void MainWindow::onResign() {
    if (inAiMode()) {
        if (controller_->resigned() || controller_->game().is_over()) return;
        const vgo::Color winner = vgo::opponent(controller_->humanColor());
        controller_->resign();
        status_->setText(QStringLiteral("你认输，%1方获胜").arg(colorName(winner)));
        QMessageBox::information(this, QStringLiteral("认输"),
                                 QStringLiteral("你认输，%1方获胜").arg(colorName(winner)));
        return;
    }
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
    vgo::Game* g = currentGame();
    if (!g) return;
    const auto [black, white] = g->score_area();
    const double lead = g->score_black();
    QString result;
    if (lead > 0.0) result = QStringLiteral("黑胜");
    else if (lead < 0.0) result = QStringLiteral("白胜");
    else result = QStringLiteral("平局");

    QMessageBox::information(
        this, QStringLiteral("计分（数子法）"),
        QStringLiteral("黑方：%1 子（含目）\n白方：%2 子（含目）\n贴目：%3\n结果：%4（黑领先 %5 目）")
            .arg(black)
            .arg(white)
            .arg(g->komi())
            .arg(result)
            .arg(lead));
}

void MainWindow::onChatMessage(const QString& text) {
    if (inAiMode()) {
        controller_->ask(text.toStdString());
    } else {
        appendAi(QStringLiteral("当前是双人对弈模式，没有 AI。请在「新局」里选择人机对弈。"));
    }
}

void MainWindow::appendAi(const QString& text) {
    chat_->appendAi(text);
}

void MainWindow::updateStatus() {
    vgo::Game* g = currentGame();
    if (!g) return;

    if (g->is_over()) {
        const auto [black, white] = g->score_area();
        const vgo::Color win = g->winner();
        status_->setText(QStringLiteral("终局：黑 %1 子 vs 白 %2 子（贴 %3），%4胜")
                             .arg(black)
                             .arg(white)
                             .arg(g->komi())
                             .arg(colorName(win)));
        return;
    }

    QString mover;
    if (inAiMode()) {
        mover = (g->current() == controller_->humanColor()) ? QStringLiteral("你") : QStringLiteral("AI");
    } else {
        mover = colorName(g->current()) + QStringLiteral("方");
    }
    status_->setText(QStringLiteral("第 %1 手 · %2（%3）· 黑提 %4 白提 %5")
                         .arg(g->move_number() + 1)
                         .arg(mover)
                         .arg(colorName(g->current()))
                         .arg(g->captures(vgo::Color::Black))
                         .arg(g->captures(vgo::Color::White)));
}
