#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "ChatPanel.h"
#include "GobanWidget.h"
#include "NewGameDialog.h"
#include "SettingsDialog.h"

#include "ai/advisor.h"
#include "ai/fallback_engine.h"
#include "ai/gtp.h"
#include "ai/session.h"
#include "ai/template_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/conversation_http/http_conversation.h"
#include "model/ModelManager.h"

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
    vgo::ModelManager::instance().load();

    board_view_ = new vgo::GobanWidget(this);
    setCentralWidget(board_view_);
    connect(board_view_, &vgo::GobanWidget::pointClicked, this, &MainWindow::onPointClicked);

    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);
    modelStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(modelStatus_);

    // Chat side bar in a collapsible dock.
    chat_ = new vgo::ChatPanel(this);
    dock_ = new QDockWidget(QStringLiteral("与 AI 交流"), this);
    dock_->setObjectName(QStringLiteral("aiChatDock"));
    dock_->setWidget(chat_);
    dock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock_);
    connect(chat_, &vgo::ChatPanel::messageSent, this, &MainWindow::onChatMessage);
    connect(chat_, &vgo::ChatPanel::newChatRequested, this, [this] {
        if (advisor_) advisor_->cancel();
    });

    // Menu: 对局 (new game + in-game actions), 视图 (AI panel), 设置 (models).
    auto* actNew = new QAction(QStringLiteral("新对局…"), this);
    actNew->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    auto* actPass = new QAction(QStringLiteral("虚着/停一手"), this);
    auto* actUndo = new QAction(QStringLiteral("悔棋"), this);
    actUndo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    auto* actResign = new QAction(QStringLiteral("认输"), this);
    auto* actScore = new QAction(QStringLiteral("计分/终局"), this);
    auto* actQuit = new QAction(QStringLiteral("退出"), this);
    actQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    auto* actSettings = new QAction(QStringLiteral("模型设置…"), this);

    connect(actNew, &QAction::triggered, this, &MainWindow::newGame);
    connect(actPass, &QAction::triggered, this, &MainWindow::onPass);
    connect(actUndo, &QAction::triggered, this, &MainWindow::onUndo);
    connect(actResign, &QAction::triggered, this, &MainWindow::onResign);
    connect(actScore, &QAction::triggered, this, &MainWindow::onScore);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);
    connect(actSettings, &QAction::triggered, this, &MainWindow::openModelSettings);

    QMenu* gameMenu = menuBar()->addMenu(QStringLiteral("对局"));
    gameMenu->addAction(actNew);
    gameMenu->addSeparator();
    gameMenu->addAction(actUndo);
    gameMenu->addAction(actPass);
    gameMenu->addAction(actResign);
    gameMenu->addSeparator();
    gameMenu->addAction(actScore);
    gameMenu->addSeparator();
    gameMenu->addAction(actQuit);

    toggleChatAction_ = dock_->toggleViewAction();
    toggleChatAction_->setText(QStringLiteral("AI 交流面板"));
    toggleChatAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(toggleChatAction_);

    QMenu* settingsMenu = menuBar()->addMenu(QStringLiteral("设置"));
    settingsMenu->addAction(actSettings);

    QToolBar* tb = addToolBar(QStringLiteral("工具"));
    tb->setMovable(false);
    tb->addAction(actNew);
    tb->addSeparator();
    tb->addAction(actUndo);
    tb->addAction(actPass);
    tb->addAction(actResign);
    tb->addAction(actScore);
    tb->addSeparator();
    tb->addAction(toggleChatAction_);

    // 首次运行：未配置任何模型时，自动打开设置引导。
    if (!vgo::ModelManager::instance().onboardingDone() &&
        !vgo::ModelManager::instance().katagoReady() &&
        vgo::ModelManager::instance().llm().baseUrl.isEmpty()) {
        QTimer::singleShot(0, this, &MainWindow::openModelSettings);
    }

    vgo::GameSetup initial;
    initial.mode = vgo::GameMode::Placement;
    initial.size = 9;
    applySetup(initial);
}

MainWindow::~MainWindow() {
    teardown();
}

vgo::Game* MainWindow::currentGame() const {
    return inAiMode() ? &controller_->game() : game_;
}

void MainWindow::applySetup(const vgo::GameSetup& setup) {
    setup_ = setup;
    if (setup.mode == vgo::GameMode::HumanVsAi) {
        startAi(setup);
    } else {
        startPlacement(setup.size);
    }
}

void MainWindow::createAiServices(int level) {
    const vgo::KataGoSettings& ks = vgo::ModelManager::instance().katago();

    // 选模型：业余段（1–11）优先用人类风格模型，否则标准模型。
    QString modelPath = ks.model;
    if (level >= 1 && level <= 11 && !ks.humanModel.isEmpty() &&
        QFileInfo::exists(ks.humanModel)) {
        modelPath = ks.humanModel;
    }

    engine_ = nullptr;
    if (ks.enabled && !ks.binary.isEmpty() && !modelPath.isEmpty() &&
        QFileInfo::exists(ks.binary) && QFileInfo::exists(modelPath)) {
        auto* k = new vgo::KataGoEngine(ks.binary, modelPath, ks.config);
        if (k->available()) engine_ = k; else delete k;
    }
    if (!engine_) engine_ = new vgo::FallbackEngine();

    const vgo::LlmSettings& ls = vgo::ModelManager::instance().llm();
    conversation_ = nullptr;
    if (!ls.baseUrl.isEmpty() && !ls.model.isEmpty()) {
        auto* h = new vgo::HttpConversationService(ls.baseUrl,
                                                   vgo::ModelManager::instance().apiKey(),
                                                   ls.model);
        if (h->available()) conversation_ = h; else delete h;
    }
    if (!conversation_) conversation_ = new vgo::TemplateConversation();

    advisor_ = new vgo::AiAdvisor(engine_, conversation_, level);

    const bool usingKata = (dynamic_cast<vgo::KataGoEngine*>(engine_) != nullptr);
    const bool usingHttp =
        (dynamic_cast<vgo::HttpConversationService*>(conversation_) != nullptr);
    modelStatus_->setText(
        QStringLiteral("引擎：%1 · 对话：%2")
            .arg(usingKata ? QStringLiteral("KataGo") : QStringLiteral("离线"),
                 usingHttp ? (ls.provider.isEmpty() ? QStringLiteral("HTTP") : ls.provider)
                           : QStringLiteral("本地模板")));
}

void MainWindow::startPlacement(int size) {
    teardown();
    createAiServices(setup_.level);
    game_ = new vgo::Game(size);
    resigned_ = false;
    chat_->clear();
    appendAi(QStringLiteral("摆棋模式：你可以自由摆放黑白棋子（用「虚着」切换行棋方），"
                            "并随时在右侧问我问题，比如「谁领先」「我该下哪」。"));
    refresh();
}

void MainWindow::startAi(const vgo::GameSetup& setup) {
    teardown();
    createAiServices(setup.level);
    game_ = nullptr;

    controller_ = new vgo::AIMatchController(setup.size, setup.human_color, engine_,
                                             advisor_, setup.level);
    controller_->onChanged = [this] { refresh(); };
    controller_->onAiMove = [this](const vgo::Move& m, const vgo::Analysis&) {
        if (m.is_pass) {
            appendAi(QStringLiteral("（虚着，停一手）"));
        } else {
            appendAi(QStringLiteral("我下在 %1。")
                         .arg(QString::fromStdString(vgo::gtpPoint(m.p, setup_.size))));
        }
    };
    controller_->onReply = [this](const vgo::ConversationReply& r) { handleReply(r); };
    controller_->onActivity = [this](vgo::AiActivity a) {
        if (a == vgo::AiActivity::Thinking) status_->setText(QStringLiteral("AI 思考中…"));
    };

    chat_->clear();
    if (setup.nigiri) {
        appendAi(setup.human_color == vgo::Color::Black
                     ? QStringLiteral("猜先结果：你执黑，请先行。")
                     : QStringLiteral("猜先结果：你执白，AI 执黑先行。"));
    } else if (setup.human_color == vgo::Color::Black) {
        appendAi(QStringLiteral("你好！你执黑先行。下棋时随时可以在右侧问我问题。"));
    } else {
        appendAi(QStringLiteral("你好！我执黑先行。下棋时随时可以在右侧问我问题。"));
    }
    controller_->start();
    refresh();
}

void MainWindow::teardown() {
    delete controller_;
    controller_ = nullptr;
    delete advisor_;
    advisor_ = nullptr;
    delete engine_;
    engine_ = nullptr;
    delete conversation_;
    conversation_ = nullptr;
    delete game_;
    game_ = nullptr;
    board_view_->setHighlight({});
    board_view_->setCandidates({});
}

void MainWindow::newGame() {
    NewGameDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) applySetup(dlg.setup());
}

void MainWindow::openModelSettings() {
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::settingsChanged, this, &MainWindow::updateModelStatus);
    dlg.exec();
    updateModelStatus();
}

void MainWindow::updateModelStatus() {
    // 粗略显示当前配置状态；精确的「实际在用」在 createAiServices 里再刷新。
    const auto& km = vgo::ModelManager::instance();
    modelStatus_->setText(QStringLiteral("引擎：%1 · 对话：%2")
                              .arg(km.katagoStatusText(), km.llmStatusText()));
}

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
    // Placement mode: the human arranges both sides.
    if (!game_ || game_->is_over()) return;
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
    if (!game_ || game_->is_over()) return;
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
    status_->setText(QStringLiteral("摆棋模式下没有对手，无需认输。"));
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
    const std::string q = text.toStdString();
    if (inAiMode()) {
        controller_->ask(q);
    } else if (advisor_) {
        advisor_->ask(*game_, q, [this](const vgo::ConversationReply& r) { handleReply(r); });
    } else {
        appendAi(QStringLiteral("当前没有可用的 AI 助手。"));
    }
}

void MainWindow::handleReply(const vgo::ConversationReply& r) {
    appendAi(QString::fromStdString(r.text));
    board_view_->setHighlight(r.highlight);
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
