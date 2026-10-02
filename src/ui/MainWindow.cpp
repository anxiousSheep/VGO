#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

#include "ChatPanel.h"
#include "GobanWidget.h"
#include "NewGameDialog.h"
#include "SettingsDialog.h"
#include "VariationPanel.h"

#include "ai/advisor.h"
#include "ai/fallback_engine.h"
#include "ai/gtp.h"
#include "ai/session.h"
#include "ai/template_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/conversation_http/http_conversation.h"
#include "ai/evaluate.h"
#include "ai/skill.h"
#include "ai/variation.h"
#include "model/ModelManager.h"

namespace {

QString colorName(vgo::Color c) {
    switch (c) {
        case vgo::Color::Black: return QStringLiteral("黑");
        case vgo::Color::White: return QStringLiteral("白");
        default:                return QStringLiteral("—");
    }
}

// 智能裁判触发手数：随棋盘大小而定，19路≈350手（盘面总点数 - 11）。
int smartRefereeThreshold(int size) {
    return size * size - 11;
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
    scoreStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(scoreStatus_);
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
    auto* actCount = new QAction(QStringLiteral("请求数目"), this);
    auto* actUndo = new QAction(QStringLiteral("悔棋"), this);
    actUndo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    auto* actResign = new QAction(QStringLiteral("认输"), this);
    auto* actScore = new QAction(QStringLiteral("计分/终局"), this);
    analyzeAction_ = new QAction(QStringLiteral("形势"), this);
    analyzeAction_->setCheckable(true);  // 手动开关：开=显示并随落子刷新，关=隐藏
    analyzeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    analyzeAction_->setEnabled(false);  // 无 KataGo 时隐藏/禁用（见 createAiServices）
    auto* actQuit = new QAction(QStringLiteral("退出"), this);
    actQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    auto* actSettings = new QAction(QStringLiteral("模型设置…"), this);

    connect(actNew, &QAction::triggered, this, &MainWindow::newGame);
    connect(actPass, &QAction::triggered, this, &MainWindow::onPass);
    connect(actCount, &QAction::triggered, this, &MainWindow::onRequestCount);
    connect(actUndo, &QAction::triggered, this, &MainWindow::onUndo);
    connect(actResign, &QAction::triggered, this, &MainWindow::onResign);
    connect(actScore, &QAction::triggered, this, &MainWindow::onScore);
    connect(analyzeAction_, &QAction::toggled, this, &MainWindow::onAnalyzeToggled);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);
    connect(actSettings, &QAction::triggered, this, &MainWindow::openModelSettings);

    QMenu* gameMenu = menuBar()->addMenu(QStringLiteral("对局"));
    gameMenu->addAction(actNew);
    gameMenu->addSeparator();
    gameMenu->addAction(actUndo);
    gameMenu->addAction(actPass);
    gameMenu->addAction(actCount);
    gameMenu->addAction(actResign);
    gameMenu->addSeparator();
    gameMenu->addAction(actScore);
    gameMenu->addAction(analyzeAction_);
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
    tb->addAction(actCount);
    tb->addAction(actResign);
    tb->addAction(actScore);
    tb->addAction(analyzeAction_);
    tb->addSeparator();
    tb->addAction(toggleChatAction_);

    // 棋盘演示（N2）：一个可开关的演示态，含独立演示面板（底部 dock）。
    demoAction_ = new QAction(QStringLiteral("演示"), this);
    demoAction_->setCheckable(true);
    demoAction_->setToolTip(QStringLiteral("进入/退出棋盘演示（不改变真实对局）"));
    connect(demoAction_, &QAction::toggled, this, &MainWindow::onDemoToggled);
    gameMenu->insertAction(actQuit, demoAction_);
    gameMenu->insertSeparator(actQuit);
    tb->addAction(demoAction_);

    // 教学增强（M3b.2）：局中预警开关。
    blunderAction_ = new QAction(QStringLiteral("局中预警"), this);
    blunderAction_->setCheckable(true);
    blunderAction_->setChecked(true);
    blunderAction_->setToolTip(QStringLiteral("明显恶手时自动提醒（需 KataGo）"));
    connect(blunderAction_, &QAction::toggled, this, &MainWindow::onBlunderToggled);
    gameMenu->insertAction(actQuit, blunderAction_);

    // 教学增强（N4.2）：技能知识热更。
    auto* actReloadSkills = new QAction(QStringLiteral("重载技能知识"), this);
    connect(actReloadSkills, &QAction::triggered, this, &MainWindow::onReloadSkills);
    settingsMenu->addAction(actReloadSkills);

    variation_panel_ = new vgo::VariationPanel(this);
    connect(variation_panel_, &vgo::VariationPanel::aiMoveRequested, this,
            &MainWindow::onDemoAiMove);
    connect(variation_panel_, &vgo::VariationPanel::undoRequested, this,
            &MainWindow::onDemoUndo);
    connect(variation_panel_, &vgo::VariationPanel::exitRequested, this,
            &MainWindow::onDemoExit);
    demo_dock_ = new QDockWidget(QStringLiteral("棋盘演示"), this);
    demo_dock_->setObjectName(QStringLiteral("demoDock"));
    demo_dock_->setWidget(variation_panel_);
    demo_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    demo_dock_->setVisible(false);
    addDockWidget(Qt::BottomDockWidgetArea, demo_dock_);

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

    // 落子引擎按难度选模型：业余段（1-11）用人类风格模型 + humanSLProfile，
    // 职业段（12-19）用标准模型 + 递增 visits（M3a.10 标定前置）。
    const vgo::StrengthParams sp = vgo::strengthForLevel(level);
    const bool useHuman = !sp.human_profile.empty() && !ks.humanModel.isEmpty() &&
                          QFileInfo::exists(ks.humanModel);
    const QString moveModel = useHuman ? ks.humanModel : ks.model;
    const QString humanProfile = useHuman ? QString::fromStdString(sp.human_profile) : QString();

    engine_ = nullptr;
    if (ks.enabled && !ks.binary.isEmpty() && !moveModel.isEmpty() &&
        QFileInfo::exists(ks.binary) && QFileInfo::exists(moveModel)) {
        auto* k = new vgo::KataGoEngine(ks.binary, moveModel, ks.config, humanProfile);
        if (k->available()) {
            engine_ = k;
            connect(k, &vgo::KataGoEngine::statusMessage, this,
                    [this](const QString& msg) { status_->setText(msg); });
        } else {
            delete k;
        }
    }
    if (!engine_) engine_ = new vgo::FallbackEngine();

    // 形势判断引擎：始终用标准模型满血（职业9段/超人类），与落子引擎解耦，保证
    // 形势判断的正确性。落子已是标准模型时复用；业余段落子用了人类模型则另起一个。
    analysis_engine_ = nullptr;
    if (dynamic_cast<vgo::KataGoEngine*>(engine_) && moveModel == ks.model) {
        analysis_engine_ = engine_;  // 复用（每次请求各自 kata-set-param maxVisits）
    } else if (ks.enabled && !ks.binary.isEmpty() && !ks.model.isEmpty() &&
               QFileInfo::exists(ks.binary) && QFileInfo::exists(ks.model)) {
        auto* ak = new vgo::KataGoEngine(ks.binary, ks.model, ks.config);
        if (ak->available()) {
            analysis_engine_ = ak;
            connect(ak, &vgo::KataGoEngine::statusMessage, this,
                    [this](const QString& msg) { status_->setText(msg); });
        } else {
            delete ak;
        }
    }

    const vgo::LlmSettings& ls = vgo::ModelManager::instance().llm();
    conversation_ = nullptr;
    if (!ls.baseUrl.isEmpty() && !ls.model.isEmpty()) {
        auto* h = new vgo::HttpConversationService(ls.baseUrl,
                                                   vgo::ModelManager::instance().apiKey(),
                                                   ls.model);
        if (h->available()) conversation_ = h; else delete h;
    }
    if (!conversation_) conversation_ = new vgo::TemplateConversation();
    loadSkills();  // N4：按问题类型注入围棋知识包（可从 skills/ 目录热更）

    advisor_ = new vgo::AiAdvisor(engine_, conversation_, level);
    advisor_->setAnalysisEngine(analysis_engine_ ? analysis_engine_ : engine_);
    advisor_->setAnalysisLevel(vgo::ModelManager::instance().analysisLevel());

    const bool usingKata =
        (dynamic_cast<vgo::KataGoEngine*>(analysis_engine_) != nullptr);
    const bool usingHttp =
        (dynamic_cast<vgo::HttpConversationService*>(conversation_) != nullptr);
    // 形势显示依赖 KataGo 的 ownership；未接 KataGo 时隐藏该功能（M6 验收）。
    analyzeAction_->setEnabled(usingKata);
    if (!usingKata) {
        scoreStatus_->clear();
        board_view_->setOwnership({});
    }
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
    controller_->onCountProposed = [this] { promptConfirmSettle(); };
    controller_->onSettled = [this] { onScore(); };

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
    // 新对局：退出演示态、清空复盘记录。
    if (demo_active_) exitDemo();
    blunders_.clear();
    // 新对局重置形势开关为关（toggled(false) 会清空显示并取消在途分析）。
    if (analyzeAction_) analyzeAction_->setChecked(false);
    delete controller_;
    controller_ = nullptr;
    delete advisor_;
    advisor_ = nullptr;
    if (analysis_engine_ && analysis_engine_ != engine_) delete analysis_engine_;
    analysis_engine_ = nullptr;
    delete engine_;
    engine_ = nullptr;
    delete conversation_;
    conversation_ = nullptr;
    delete game_;
    game_ = nullptr;
    board_view_->setHighlight({});
    board_view_->setCandidates({});
    board_view_->setOwnership({});
    if (scoreStatus_) scoreStatus_->clear();
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
    // 设置变更时，立即把形势分析强度应用到当前对局的 advisor。
    if (advisor_) advisor_->setAnalysisLevel(vgo::ModelManager::instance().analysisLevel());
    const auto& km = vgo::ModelManager::instance();
    modelStatus_->setText(QStringLiteral("引擎：%1 · 对话：%2")
                              .arg(km.katagoStatusText(), km.llmStatusText()));
}

void MainWindow::refresh() {
    if (demo_active_) return;  // 演示态下不触碰真实对局棋盘
    vgo::Game* g = currentGame();
    if (!g) return;
    board_view_->setBoard(&g->board());
    board_view_->setLastMove(g->last_move(), g->has_last_move());
    board_view_->setGhostColor(g->current());
    board_view_->setHighlight({});
    updateStatus();
    // 智能裁判：达到手数阈值（随棋盘大小）后由模型自动判定胜负。
    if (inAiMode() && controller_ && !controller_->game().is_over() &&
        controller_->game().move_number() >= smartRefereeThreshold(setup_.size)) {
        controller_->smartReferee();
    }
    // 形势开关打开时，随落子/悔棋/虚着自动刷新归属图。用 singleShot(0) 延后到本次
    // 事件处理结束，避免人机对弈中人类落子后与 AI 的 genmove 争抢单飞引擎（若先发
    // analyze 会让 genmove 被判忙而停一手）。AI 落子后 refresh 会再触发一次并成功。
    if (analyzeAction_ && analyzeAction_->isChecked()) {
        QTimer::singleShot(0, this, [this] { requestAnalysis(false); });
    }
}

void MainWindow::onPointClicked(int x, int y) {
    if (demo_active_) {
        onDemoPointClicked(x, y);
        return;
    }
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
        if (blunderAction_ && blunderAction_->isChecked()) maybeWarnBlunder();
        return;
    }
    // Placement mode: the human arranges both sides.
    if (!game_ || game_->is_over()) return;
    if (game_->play(x, y)) {
        refresh();
        if (blunderAction_ && blunderAction_->isChecked()) maybeWarnBlunder();
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

void MainWindow::onRequestCount() {
    if (inAiMode()) {
        controller_->humanRequestCount();
        return;
    }
    // 摆棋模式无对手，直接数目。
    onScore();
}

void MainWindow::promptConfirmSettle() {
    if (!inAiMode()) return;
    const int ret = QMessageBox::question(
        this, QStringLiteral("终局提议"),
        QStringLiteral("AI 认为棋局已结束，提议数子。是否同意？"),
        QStringLiteral("同意数子"), QStringLiteral("继续下"));
    if (ret == 0) controller_->humanConfirmCount();
    else controller_->humanDeclineCount();
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
    const double komi = g->komi();
    // 本地计数作为回退（无 KataGo 或分析失败时用）。
    const auto [local_black, local_white] = g->score_area();
    const double local_lead = g->score_black();

    // 优先用 KataGo 判断（ownership 正确处理死子与未定型领地，且黑+白=盘面总点数）。
    if (usingKata() && advisor_) {
        const vgo::Color to_move = g->current();
        advisor_->analyzePosition(*g, [this, to_move, komi, local_black, local_white,
                                       local_lead](const vgo::Analysis& a) {
            if (!a.valid || a.ownership.empty()) {
                showScoreResult(local_black, local_white, komi, local_lead);
                return;
            }
            int black = 0;
            int white = 0;
            for (double v : a.ownership.value) {
                if (v > 0.0) ++black;
                else if (v < 0.0) ++white;
            }
            // rootInfo.scoreLead 是 to_move 视角（含贴目），转成黑方领先。
            double black_lead = a.score_lead;
            if (to_move == vgo::Color::White) black_lead = -a.score_lead;
            showScoreResult(black, white, komi, black_lead);
        });
        return;
    }
    showScoreResult(local_black, local_white, komi, local_lead);
}

void MainWindow::showScoreResult(int black, int white, double komi, double black_lead) {
    // M3b.3：终局复盘——汇总本局检测到的明显恶手（如开启局中预警）。
    if (!blunders_.empty()) {
        QString summary =
            QStringLiteral("终局复盘：本局检测到 %1 次明显恶手").arg(blunders_.size());
        for (const auto& b : blunders_)
            summary += QStringLiteral("（第 %1 手）").arg(b.first);
        summary += QStringLiteral("。");
        appendAi(summary);
        blunders_.clear();
    }

    QString result;
    if (black_lead > 0.0) result = QStringLiteral("黑胜");
    else if (black_lead < 0.0) result = QStringLiteral("白胜");
    else result = QStringLiteral("平局");

    QMessageBox::information(
        this, QStringLiteral("计分（数子法）"),
        QStringLiteral("黑方：%1 子（含目）\n白方：%2 子（含目）\n贴目：%3\n结果：%4（黑领先 %5 目）")
            .arg(black)
            .arg(white)
            .arg(komi)
            .arg(result)
            .arg(black_lead));
}

void MainWindow::onChatMessage(const QString& text) {
    const std::string q = text.toStdString();
    // 形势类提问（谁领先/形势）走 analyzePosition：一次引擎调用同时产出
    // 领地热力 + 领先目数，避免与文本问答争抢单飞引擎。同时打开形势开关，
    // 让热力图继续随落子自动刷新。
    if (usingKata() && vgo::classifyQuestion(q) == vgo::QuestionClass::WhoIsWinning) {
        {
            QSignalBlocker blocker(analyzeAction_);  // 避免 toggled 再触发一次分析
            analyzeAction_->setChecked(true);
        }
        requestAnalysis(/*sayInChat=*/true);
        return;
    }
    // 试下模式下，聊天问答针对变化图当前局面（而非真实对局）。
    if (demo_active_) {
        vgo::Game dg = demoGame();
        if (advisor_) {
            advisor_->ask(dg, q, [this](const vgo::ConversationReply& r) { handleReply(r); });
        } else {
            appendAi(QStringLiteral("当前没有可用的 AI 助手。"));
        }
        return;
    }
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

bool MainWindow::usingKata() const {
    // 形势显示依赖形势判断引擎（标准模型满血），而非落子引擎。
    return dynamic_cast<vgo::KataGoEngine*>(analysis_engine_) != nullptr;
}

bool MainWindow::usingHttp() const {
    return dynamic_cast<vgo::HttpConversationService*>(conversation_) != nullptr;
}

vgo::Color MainWindow::userColor() const {
    return inAiMode() ? controller_->humanColor() : vgo::Color::Empty;
}

void MainWindow::onAnalyzeToggled(bool on) {
    if (on) {
        requestAnalysis(false);  // 打开：立即刷新一次
    } else {
        // 关闭：清空归属图与领先显示，并取消在途分析。
        if (advisor_) advisor_->cancel();
        board_view_->clearOwnership();
        scoreStatus_->clear();
    }
}

void MainWindow::requestAnalysis(bool sayInChat) {
    if (!usingKata()) return;
    // 仅当形势开关打开时才刷新；防止关闭后残留的延后 singleShot 再次触发。
    if (!analyzeAction_ || !analyzeAction_->isChecked()) return;
    if (!advisor_) return;
    // 试下模式：形势针对变化图当前局面。
    if (demo_active_) {
        vgo::Game dg = demoGame();
        const vgo::Color to_move = dg.current();
        advisor_->analyzePosition(dg, [this, to_move, sayInChat](const vgo::Analysis& a) {
            if (!demo_active_) return;
            applyAnalysis(a, to_move, sayInChat);
        });
        return;
    }
    vgo::Game* g = currentGame();
    if (!g) return;
    const vgo::Color to_move = g->current();
    advisor_->analyzePosition(*g, [this, to_move, sayInChat](const vgo::Analysis& a) {
        applyAnalysis(a, to_move, sayInChat);
    });
}

void MainWindow::applyAnalysis(const vgo::Analysis& a, vgo::Color to_move,
                               bool sayInChat) {
    if (!a.valid || a.ownership.empty()) {
        // 引擎忙（如 AI 落子时）或失败：保留上次形势图，仅提示，稍后随落子自动重试。
        if (sayInChat) appendAi(QStringLiteral("当前形势暂时无法计算（引擎忙或未接入 KataGo）。"));
        scoreStatus_->setText(QStringLiteral("形势：引擎忙，稍后自动重试"));
        return;
    }
    board_view_->setOwnership(a.ownership);

    // Analysis.winrate/score_lead 是 to_move 视角；转成黑方视角显示。
    double black_lead = a.score_lead;
    double black_winrate = a.winrate;
    if (to_move == vgo::Color::White) {
        black_lead = -a.score_lead;
        black_winrate = 1.0 - a.winrate;
    }

    // 领先文案：有人机执子方时转成「我方」视角，否则用黑白双方视角。
    QString lead;
    const vgo::Color uc = userColor();
    if (uc == vgo::Color::Empty) {
        if (black_lead > 0.05)
            lead = QStringLiteral("黑领先 %1 目").arg(black_lead, 0, 'f', 1);
        else if (black_lead < -0.05)
            lead = QStringLiteral("白领先 %1 目").arg(-black_lead, 0, 'f', 1);
        else
            lead = QStringLiteral("双方均势");
    } else {
        const double mine = (uc == vgo::Color::Black) ? black_lead : -black_lead;
        if (mine > 0.05)
            lead = QStringLiteral("我方领先 %1 目").arg(mine, 0, 'f', 1);
        else if (mine < -0.05)
            lead = QStringLiteral("我方落后 %1 目").arg(-mine, 0, 'f', 1);
        else
            lead = QStringLiteral("双方均势");
    }
    const int wpct = static_cast<int>(black_winrate * 100.0 + 0.5);
    scoreStatus_->setText(QStringLiteral("%1 · 黑方胜率 %2%").arg(lead).arg(wpct));

    if (sayInChat) {
        appendAi(QStringLiteral("当前形势：%1 · 黑方胜率 %2%。").arg(lead).arg(wpct));
    }
}

void MainWindow::appendAi(const QString& text) {
    chat_->appendAi(text);
}

// ---------------------------------------------------------------------------
// 棋盘演示（N2，试下模式）：独立变化图，自由试下/回退/退出，权威 AI 后续。
// ---------------------------------------------------------------------------
void MainWindow::onDemoToggled(bool on) {
    if (on) startDemo();
    else exitDemo();
}

void MainWindow::startDemo() {
    if (demo_active_) return;
    vgo::Game* g = currentGame();
    if (!g) {
        QSignalBlocker b(demoAction_);
        demoAction_->setChecked(false);
        return;
    }
    if (inAiMode() && !controller_->humanMayAct()) {
        status_->setText(QStringLiteral("请等 AI 落子后再进入演示"));
        QSignalBlocker b(demoAction_);
        demoAction_->setChecked(false);
        return;
    }
    variation_.start(g->board(), g->current());
    demo_active_ = true;
    variation_board_ = g->board();
    variation_numbers_.clear();
    // 试下模式的讲解以变化图当前局面为准，先清掉真实对局缓存的形势。
    if (advisor_) advisor_->noteAnalysis(vgo::Analysis{});
    updateVariationBoard();
    demo_dock_->setVisible(true);
    appendAi(QStringLiteral("已进入试下模式：棋盘为独立变化图，点棋盘试下，"
                            "或让「AI 演示一手」续下；用「回退」撤销，不影响真实对局。"));
    status_->setText(QStringLiteral("试下模式中"));
}

void MainWindow::exitDemo() {
    if (!demo_active_) return;
    demo_active_ = false;
    variation_.exit();
    demo_dock_->setVisible(false);
    board_view_->setMoveNumbers({});
    board_view_->clearOwnership();
    refresh();
    appendAi(QStringLiteral("已退出试下模式，回到真实对局。"));
}

void MainWindow::onDemoPointClicked(int x, int y) {
    const vgo::Board b = variation_.board();
    if (!b.in_bounds(x, y) || b.at(x, y) != vgo::Color::Empty) {
        status_->setText(QStringLiteral("该位置已有棋子"));
        return;
    }
    vgo::Game probe(b, variation_.toMove());
    if (!probe.play(x, y)) {
        status_->setText(QStringLiteral("该位置不能落子（自杀/打劫）"));
        return;
    }
    variation_.commitStep(vgo::Move{{x, y}, false}, variation_.toMove(), false);
    updateVariationBoard();
}

vgo::Game MainWindow::demoGame() const {
    vgo::Game g(variation_.variation().start, variation_.variation().to_move);
    for (const auto& s : variation_.variation().steps) {
        if (s.move.is_pass) g.pass();
        else g.play(s.move.p.x, s.move.p.y);
    }
    return g;
}

void MainWindow::updateVariationBoard() {
    variation_board_ = variation_.board();
    variation_numbers_.clear();
    const auto& steps = variation_.variation().steps;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (!steps[i].move.is_pass)
            variation_numbers_.push_back({steps[i].move.p, static_cast<int>(i) + 1});
    }
    board_view_->setBoard(&variation_board_);
    board_view_->setMoveNumbers(variation_numbers_);
    board_view_->setGhostColor(variation_.toMove());
    board_view_->setLastMove({-1, -1}, false);
    board_view_->setHighlight({});
    variation_panel_->setCanUndo(!steps.empty());

    // 形势：打开时随试下/回退针对变化图当前局面自动刷新，不再被清空。
    if (analyzeAction_ && analyzeAction_->isChecked() && usingKata()) {
        QTimer::singleShot(0, this, [this] { requestAnalysis(false); });
    } else {
        board_view_->clearOwnership();
        scoreStatus_->clear();
    }
}

void MainWindow::onDemoAiMove() {
    if (!demo_active_) return;
    vgo::EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) {
        status_->setText(QStringLiteral("未接入 KataGo，无法 AI 演示"));
        return;
    }
    const vgo::Board req_board = variation_.board();
    const vgo::Color to_move = variation_.toMove();
    // 与形势判断同源：同一引擎 + 同一强度档位（设置里「形势分析 / 教学演示强度」）。
    const int demo_level = vgo::ModelManager::instance().analysisLevel();
    eng->requestMove(req_board, to_move, vgo::strengthForLevel(demo_level),
                     [this, req_board, to_move](vgo::EngineResult r) {
                         if (!demo_active_) return;
                         if (r.move.is_pass) {
                             variation_.commitStep(vgo::Move{{-1, -1}, true}, to_move, true);
                         } else {
                             vgo::Game probe(req_board, to_move);
                             if (!probe.play(r.move.p.x, r.move.p.y)) {
                                 status_->setText(QStringLiteral("AI 演示：无合适落点"));
                                 return;
                             }
                             variation_.commitStep(r.move, to_move, true);
                         }
                         updateVariationBoard();
                     });
}

void MainWindow::onDemoUndo() {
    if (variation_.back()) updateVariationBoard();
}

void MainWindow::onDemoExit() {
    QSignalBlocker b(demoAction_);
    demoAction_->setChecked(false);
    exitDemo();
}

// ---------------------------------------------------------------------------
// 教学增强（M3b）：局中预警 + 终局复盘 + 技能热更。
// ---------------------------------------------------------------------------
void MainWindow::onBlunderToggled(bool on) {
    status_->setText(on ? QStringLiteral("局中预警已开启")
                        : QStringLiteral("局中预警已关闭"));
}

void MainWindow::maybeWarnBlunder() {
    if (!usingKata() || !advisor_) return;
    vgo::Game* g = currentGame();
    if (!g || g->move_number() == 0 || !g->has_last_move()) return;
    const vgo::Point p = g->last_move();
    advisor_->evaluateMove(*g, p, [this](const vgo::MoveEvaluation& e) {
        const std::string w = vgo::blunderWarning(e);
        if (w.empty()) return;
        blunders_.push_back({currentGame() ? currentGame()->move_number() : 0, w});
        appendAi(QString::fromStdString(w));
    });
}

void MainWindow::onReloadSkills() {
    loadSkills();
    appendAi(QStringLiteral("已重新加载技能知识。"));
}

void MainWindow::loadSkills() {
    auto* http = dynamic_cast<vgo::HttpConversationService*>(conversation_);
    if (!http) return;
    vgo::SkillRegistry reg = vgo::SkillRegistry::builtin();
    const QStringList dirs = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/skills"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/skills"),
        vgo::ModelManager::instance().dataDir() + QStringLiteral("/skills"),
    };
    for (const QString& dir : dirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList files =
            d.entryList({QStringLiteral("*.md"), QStringLiteral("*.skill")}, QDir::Files);
        for (const QString& f : files) {
            QFile file(d.filePath(f));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QByteArray bytes = file.readAll();
            vgo::Skill s;
            if (vgo::SkillRegistry::fromText(QString::fromUtf8(bytes).toStdString(), &s))
                reg.upsert(s);
        }
    }
    http->setSkills(std::move(reg));
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
