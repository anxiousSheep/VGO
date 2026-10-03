#include "MainWindow.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

#include "ChatPanel.h"
#include "GobanWidget.h"
#include "NewGameDialog.h"
#include "SettingsDialog.h"
#include "VariationPanel.h"

#include "ai/advisor.h"
#include "ai/degradation.h"
#include "ai/fallback_engine.h"
#include "ai/gtp.h"
#include "ai/session.h"
#include "ai/template_conversation.h"
#include "ai/engine_kata/kata_engine.h"
#include "ai/conversation_http/http_conversation.h"
#include "ai/evaluate.h"
#include "ai/learner/learner_model.h"
#include "ai/skill.h"
#include "ai/variation.h"
#include "model/LearnerStore.h"
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
    // D3.3：学习者画像持久化——重开应用后画像仍在。
    learner_ = std::make_unique<vgo::LearnerStore>(vgo::LearnerStore::defaultPath());
    learner_->load();

    board_view_ = new vgo::GobanWidget(this);
    connect(board_view_, &vgo::GobanWidget::pointClicked, this, &MainWindow::onPointClicked);

    // 中央区域为两页：棋盘（摆棋 / 对弈 / 已导入棋谱的复盘）与复盘空态（大按钮引导导入）。
    central_stack_ = new QStackedWidget(this);
    central_stack_->addWidget(board_view_);
    review_placeholder_ = new QWidget(this);
    {
        auto* v = new QVBoxLayout(review_placeholder_);
        v->addStretch(2);
        auto* hint = new QLabel(QStringLiteral("尚未导入棋谱"), review_placeholder_);
        QFont hintFont = hint->font();
        hintFont.setPointSize(hintFont.pointSize() + 3);
        hint->setFont(hintFont);
        hint->setAlignment(Qt::AlignCenter);
        auto* btn = new QPushButton(QStringLiteral("导入棋谱…"), review_placeholder_);
        btn->setMinimumSize(240, 64);
        QFont btnFont = btn->font();
        btnFont.setPointSize(btnFont.pointSize() + 4);
        btn->setFont(btnFont);
        connect(btn, &QPushButton::clicked, this, &MainWindow::importReview);
        auto* sub = new QLabel(QStringLiteral("支持 .vgo 棋谱，导入后可逐手回看 / 试下"), review_placeholder_);
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet(QStringLiteral("color: #6e7781;"));
        v->addWidget(hint);
        v->addSpacing(12);
        v->addWidget(btn, 0, Qt::AlignCenter);
        v->addSpacing(8);
        v->addWidget(sub);
        v->addStretch(3);
    }
    central_stack_->addWidget(review_placeholder_);
    setCentralWidget(central_stack_);

    // 三个同级标签页：摆棋 / 人机对弈 / 复盘（各自独立，不再经「新对局」选模式）。
    mode_bar_ = new QTabBar(this);
    mode_bar_->addTab(QStringLiteral("摆棋"));
    mode_bar_->addTab(QStringLiteral("人机对弈"));
    mode_bar_->addTab(QStringLiteral("复盘"));
    mode_bar_->setExpanding(false);
    connect(mode_bar_, &QTabBar::currentChanged, this, &MainWindow::onModeChanged);

    // 顶部：菜单栏在上、标签栏在下。菜单栏手动创建并随标签栏放入同一个顶部容器，
    // 经 setMenuWidget 置顶（取代默认 menuBar 的自动定位）。
    auto* menubar = new QMenuBar(this);
    auto* topWidget = new QWidget(this);
    auto* topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    topLayout->addWidget(menubar);
    topLayout->addWidget(mode_bar_);
    setMenuWidget(topWidget);

    // 尺寸微调：菜单栏（对局/视图/设置）稍小，标签栏稍大。
    {
        QFont menuFont = menubar->font();
        menuFont.setPointSize(std::max(menuFont.pointSize() - 1, 6));
        menubar->setFont(menuFont);
        QFont tabFont = mode_bar_->font();
        tabFont.setPointSize(tabFont.pointSize() + 2);
        mode_bar_->setFont(tabFont);
    }

    status_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);
    scoreStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(scoreStatus_);
    modelStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(modelStatus_);
    tierStatus_ = new QLabel(this);  // 讲解档位徽标（D5.1）
    statusBar()->addPermanentWidget(tierStatus_);

    // Chat side bar in a collapsible dock.
    chat_ = new vgo::ChatPanel(this);
    dock_ = new QDockWidget(QStringLiteral("与 AI 交流"), this);
    dock_->setObjectName(QStringLiteral("aiChatDock"));
    dock_->setWidget(chat_);
    dock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    // 保留全部特征（折叠 toggleViewAction 与拖拽调整大小依赖 Closable/Movable），
    // 浮动/关闭按钮经样式表隐藏（见下方 setStyleSheet）。
    dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                       QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock_);
    connect(chat_, &vgo::ChatPanel::messageSent, this, &MainWindow::onChatMessage);
    connect(chat_, &vgo::ChatPanel::newChatRequested, this, [this] {
        if (advisor_) advisor_->cancel();
    });

    // Menu: 对局 (new game + in-game actions), 视图 (AI panel), 设置 (models).
    actNew_ = new QAction(QStringLiteral("新对局…"), this);
    actNew_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    actPass_ = new QAction(QStringLiteral("虚着/停一手"), this);
    actCount_ = new QAction(QStringLiteral("请求数目"), this);
    actUndo_ = new QAction(QStringLiteral("悔棋"), this);
    actUndo_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    actResign_ = new QAction(QStringLiteral("认输"), this);
    actScore_ = new QAction(QStringLiteral("计分/终局"), this);
    saveAction_ = new QAction(QStringLiteral("保存棋谱"), this);
    saveAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    importAction_ = new QAction(QStringLiteral("导入棋谱"), this);
    importAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    prevAction_ = new QAction(QStringLiteral("上一步"), this);
    prevAction_->setShortcut(QKeySequence(QStringLiteral("Left")));
    nextAction_ = new QAction(QStringLiteral("下一步"), this);
    nextAction_->setShortcut(QKeySequence(QStringLiteral("Right")));
    analyzeAction_ = new QAction(QStringLiteral("形势"), this);
    analyzeAction_->setCheckable(true);  // 手动开关：开=显示并随落子刷新，关=隐藏
    analyzeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    analyzeAction_->setEnabled(false);  // 无 KataGo 时隐藏/禁用（见 createAiServices）
    auto* actQuit = new QAction(QStringLiteral("退出"), this);
    actQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    auto* actSettings = new QAction(QStringLiteral("模型设置…"), this);

    connect(actNew_, &QAction::triggered, this, &MainWindow::newGame);
    connect(actPass_, &QAction::triggered, this, &MainWindow::onPass);
    connect(actCount_, &QAction::triggered, this, &MainWindow::onRequestCount);
    connect(actUndo_, &QAction::triggered, this, &MainWindow::onUndo);
    connect(actResign_, &QAction::triggered, this, &MainWindow::onResign);
    connect(actScore_, &QAction::triggered, this, &MainWindow::onScore);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveRecord);
    connect(importAction_, &QAction::triggered, this, &MainWindow::importReview);
    connect(prevAction_, &QAction::triggered, this, [this] { stepReview(-1); });
    connect(nextAction_, &QAction::triggered, this, [this] { stepReview(1); });
    connect(analyzeAction_, &QAction::toggled, this, &MainWindow::onAnalyzeToggled);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);
    connect(actSettings, &QAction::triggered, this, &MainWindow::openModelSettings);

    QMenu* gameMenu = menubar->addMenu(QStringLiteral("对局"));
    gameMenu->addAction(actNew_);
    gameMenu->addSeparator();
    gameMenu->addAction(actUndo_);
    gameMenu->addAction(actPass_);
    gameMenu->addAction(actCount_);
    gameMenu->addAction(actResign_);
    gameMenu->addSeparator();
    gameMenu->addAction(actScore_);
    gameMenu->addAction(analyzeAction_);
    gameMenu->addSeparator();
    gameMenu->addAction(saveAction_);
    gameMenu->addAction(importAction_);
    gameMenu->addSeparator();
    gameMenu->addAction(prevAction_);
    gameMenu->addAction(nextAction_);
    gameMenu->addSeparator();
    gameMenu->addAction(actQuit);

    toggleChatAction_ = dock_->toggleViewAction();
    toggleChatAction_->setText(QStringLiteral("AI 交流面板"));
    toggleChatAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));

    QMenu* viewMenu = menubar->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(toggleChatAction_);

    QMenu* settingsMenu = menubar->addMenu(QStringLiteral("设置"));
    settingsMenu->addAction(actSettings);

    QToolBar* tb = addToolBar(QStringLiteral("工具"));
    tb->setMovable(false);
    tb->addAction(actNew_);
    tb->addSeparator();
    tb->addAction(actUndo_);
    tb->addAction(actPass_);
    tb->addAction(actCount_);
    tb->addAction(actResign_);
    tb->addAction(actScore_);
    tb->addAction(analyzeAction_);
    tb->addSeparator();
    tb->addAction(saveAction_);
    tb->addAction(importAction_);
    tb->addSeparator();
    tb->addAction(prevAction_);
    tb->addAction(nextAction_);

    // 棋盘演示（N2）：一个可开关的演示态，含独立演示面板（底部 dock）。
    demoAction_ = new QAction(QStringLiteral("演示/试下"), this);
    demoAction_->setCheckable(true);
    demoAction_->setToolTip(QStringLiteral("进入/退出棋盘演示（试下，不改变真实对局）"));
    connect(demoAction_, &QAction::toggled, this, &MainWindow::onDemoToggled);
    gameMenu->insertAction(actQuit, demoAction_);
    gameMenu->insertSeparator(actQuit);

    // 侧边工具栏（仿 IDEA 工具窗口栏，靠右）：AI 交流（上）、演示/试下（下）。
    {
        auto* sideBar = new QToolBar(QStringLiteral("侧边栏"), this);
        sideBar->setObjectName(QStringLiteral("sideBar"));
        sideBar->setMovable(false);
        sideBar->setOrientation(Qt::Vertical);
        sideBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        addToolBar(Qt::RightToolBarArea, sideBar);
        sideBar->addAction(toggleChatAction_);
        auto* spacer = new QWidget(sideBar);
        spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        sideBar->addWidget(spacer);
        sideBar->addAction(demoAction_);
    }

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
    demo_dock_ = new QDockWidget(QStringLiteral("棋盘演示 / 试下"), this);
    demo_dock_->setObjectName(QStringLiteral("demoDock"));
    demo_dock_->setWidget(variation_panel_);
    demo_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    demo_dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable);
    demo_dock_->setVisible(false);
    addDockWidget(Qt::BottomDockWidgetArea, demo_dock_);

    // 隐藏浮动/关闭按钮（保留特征以便折叠/调整大小），分隔条保持默认样式。
    setStyleSheet(QStringLiteral(
        "QDockWidget::close-button, QDockWidget::float-button { max-width: 0px; max-height: 0px; }"));

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
    updateActionsForMode();
}

MainWindow::~MainWindow() {
    teardown();
}

vgo::Game* MainWindow::currentGame() const {
    switch (app_mode_) {
        case vgo::GameMode::HumanVsAi:
            return controller_ ? &controller_->game() : nullptr;
        case vgo::GameMode::Review:
            return review_game_.get();
        case vgo::GameMode::Placement:
        default:
            return game_;
    }
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

    // 落子引擎按难度选模型：业余段（1-11）用标准模型搜索 + 人类模型选子
    // （-human-model + humanSLProfile），职业段（12-19）用标准模型 + 递增 visits。
    const vgo::StrengthParams sp = vgo::strengthForLevel(level);
    const bool useHuman = !sp.human_profile.empty() && !ks.humanModel.isEmpty() &&
                          QFileInfo::exists(ks.humanModel);
    const QString humanProfile = useHuman ? QString::fromStdString(sp.human_profile) : QString();

    engine_ = nullptr;
    const bool stdOk = ks.enabled && !ks.binary.isEmpty() && !ks.model.isEmpty() &&
                       QFileInfo::exists(ks.binary) && QFileInfo::exists(ks.model);
    if (stdOk) {
        if (useHuman) {
            auto* k = new vgo::KataGoEngine(ks.binary, ks.model, ks.config, humanProfile,
                                            ks.humanModel);
            if (k->available()) {
                engine_ = k;
                connect(k, &vgo::KataGoEngine::statusMessage, this,
                        [this](const QString& msg) { status_->setText(msg); });
            } else {
                delete k;
            }
        } else {
            auto* k = new vgo::KataGoEngine(ks.binary, ks.model, ks.config);
            if (k->available()) {
                engine_ = k;
                connect(k, &vgo::KataGoEngine::statusMessage, this,
                        [this](const QString& msg) { status_->setText(msg); });
            } else {
                delete k;
            }
        }
    }
    if (!engine_) engine_ = new vgo::FallbackEngine();

    // 形势判断引擎：始终用标准模型满血（职业9段/超人类），与落子引擎解耦，保证
    // 形势判断的正确性。职业段（落子即标准模型）时复用；业余段则另起一个。
    analysis_engine_ = nullptr;
    if (dynamic_cast<vgo::KataGoEngine*>(engine_) && !useHuman) {
        analysis_engine_ = engine_;  // 复用（每次请求各自 kata-set-param maxVisits）
    } else if (stdOk) {
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
    // 给 LLM 挂上「打开演示并模拟下棋」的工具（function-calling），让模型自己决定
    // 何时演示、演示哪一手（解析自然语言），而非靠关键词。
    if (auto* h = dynamic_cast<vgo::HttpConversationService*>(conversation_)) {
        h->setToolHandler([this](const QString& name, const QJsonObject& args) {
            return handleDemoTool(name, args);
        });
    }

    advisor_ = new vgo::AiAdvisor(engine_, conversation_, level);
    advisor_->setAnalysisEngine(analysis_engine_ ? analysis_engine_ : engine_);
    advisor_->setAnalysisLevel(vgo::ModelManager::instance().analysisLevel());
    // D3.6：把跨局教练记忆注入 prompt（隐私开关关闭或暂无数据时为空）。
    advisor_->setCoachMemoryProvider([this]() -> std::string {
        if (!learner_ || !learner_->enabled()) return {};
        return learner_->model().coachMemory();
    });

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
    updateTierStatus();
}

void MainWindow::updateTierStatus() {
    // D5.1：常驻讲解档位徽标，消除「静默降级」——用户始终知道当前拿到的质量档位。
    if (!tierStatus_) return;
    const vgo::ExplanationTier tier = vgo::resolveTier(usingKata(), usingHttp());
    const QString label = QString::fromUtf8(vgo::tierLabel(tier));
    const QString desc = QString::fromUtf8(vgo::tierDescription(tier).c_str());
    QString color;
    switch (tier) {
        case vgo::ExplanationTier::FullKataGoLlm: color = QStringLiteral("#1a7f37"); break;
        case vgo::ExplanationTier::LocalLlm: color = QStringLiteral("#9a6700"); break;
        case vgo::ExplanationTier::Template: default: color = QStringLiteral("#6e7781"); break;
    }
    tierStatus_->setText(QStringLiteral("讲解档位：%1").arg(label));
    tierStatus_->setToolTip(desc);
    tierStatus_->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(color));
}

void MainWindow::startPlacement(int size) {
    if (demo_active_) exitDemo();
    delete game_;
    game_ = nullptr;
    // AI 服务跨模式共享：仅首次创建；已有则沿用并重置语气档位。
    if (!advisor_) createAiServices(setup_.level);
    else advisor_->setLevel(setup_.level);
    game_ = new vgo::Game(size);
    resigned_ = false;
    chat_->clear();
    appendAi(QStringLiteral("摆棋模式：你可以自由摆放黑白棋子（用「虚着」切换行棋方），"
                            "并随时在右侧问我问题，比如「谁领先」「我该下哪」。"));
    refresh();
}

void MainWindow::startAi(const vgo::GameSetup& setup) {
    if (demo_active_) exitDemo();
    blunders_.clear();
    blunder_summary_shown_ = false;
    game_total_loss_ = 0.0;   // D3：重置本局学习观测
    game_blunder_count_ = 0;
    game_recorded_ = false;
    if (analyzeAction_) analyzeAction_->setChecked(false);
    delete controller_;
    controller_ = nullptr;
    teardownServices();  // 按新难度重建落子引擎与 advisor
    createAiServices(setup.level);

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
    controller_->onAiResign = [this] {
        appendAi(QStringLiteral("我认输了，你（%1方）获胜。").arg(colorName(setup_.human_color)));
        status_->setText(QStringLiteral("AI 认输，你获胜"));
        // AI 认输弹窗：附带「保存棋谱」。
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("AI 认输"));
        box.setText(QStringLiteral("AI 认输，你（%1方）获胜。").arg(colorName(setup_.human_color)));
        auto* save = box.addButton(QStringLiteral("保存棋谱"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        box.exec();
        if (box.clickedButton() == save) saveRecord();
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
    if (demo_active_) exitDemo();
    blunders_.clear();
    if (analyzeAction_) analyzeAction_->setChecked(false);
    delete controller_;
    controller_ = nullptr;
    delete game_;
    game_ = nullptr;
    review_game_.reset();
    review_record_ = vgo::GameRecord{};
    review_cursor_ = 0;
    teardownServices();
    board_view_->setHighlight({});
    board_view_->setCandidates({});
    board_view_->setOwnership({});
    if (scoreStatus_) scoreStatus_->clear();
}

void MainWindow::teardownServices() {
    delete advisor_;
    advisor_ = nullptr;
    if (analysis_engine_ && analysis_engine_ != engine_) delete analysis_engine_;
    analysis_engine_ = nullptr;
    delete engine_;
    engine_ = nullptr;
    delete conversation_;
    conversation_ = nullptr;
}

void MainWindow::newGame() {
    if (app_mode_ == vgo::GameMode::Review) {
        importReview();
        return;
    }
    const bool human = (app_mode_ == vgo::GameMode::HumanVsAi);
    NewGameDialog dlg(human, this);
    if (dlg.exec() == QDialog::Accepted) applySetup(dlg.setup());
}

void MainWindow::openModelSettings() {
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::settingsChanged, this, &MainWindow::updateModelStatus);
    dlg.exec();
    if (learner_) learner_->load();  // 设置页可能改动/清空学习数据，重载保持一致
    updateModelStatus();
}

void MainWindow::updateModelStatus() {
    // 设置变更时，立即把形势分析强度应用到当前对局的 advisor。
    if (advisor_) advisor_->setAnalysisLevel(vgo::ModelManager::instance().analysisLevel());
    const auto& km = vgo::ModelManager::instance();
    modelStatus_->setText(QStringLiteral("引擎：%1 · 对话：%2")
                              .arg(km.katagoStatusText(), km.llmStatusText()));
    updateTierStatus();
}

void MainWindow::onModeChanged(int index) {
    if (demo_active_) exitDemo();
    switch (index) {
        case 0: app_mode_ = vgo::GameMode::Placement; break;
        case 1: app_mode_ = vgo::GameMode::HumanVsAi; break;
        case 2: app_mode_ = vgo::GameMode::Review; break;
        default: return;
    }
    refresh();
    updateActionsForMode();
}

void MainWindow::loadReview(const vgo::GameRecord& r) {
    review_record_ = r;
    review_blunders_ = r.blunders;  // 供复盘试下命中检测
    review_cursor_ = 0;
    rebuildReviewGame();
    refresh();
    updateActionsForMode();
}

void MainWindow::rebuildReviewGame() {
    review_game_ =
        std::make_unique<vgo::Game>(vgo::recordToGame(review_record_, review_cursor_));
}

void MainWindow::stepReview(int delta) {
    if (app_mode_ != vgo::GameMode::Review || review_record_.moves.empty()) return;
    const int n = static_cast<int>(review_record_.moves.size());
    review_cursor_ = std::clamp(review_cursor_ + delta, 0, n);
    rebuildReviewGame();
    refresh();
    updateActionsForMode();
}

void MainWindow::importReview() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入棋谱"), QString(),
        QStringLiteral("VGO 棋谱 (*.vgo);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导入棋谱"),
                             QStringLiteral("无法打开文件：%1").arg(path));
        return;
    }
    vgo::GameRecord r;
    if (!vgo::parseRecord(QString::fromUtf8(f.readAll()).toStdString(), &r)) {
        QMessageBox::warning(this, QStringLiteral("导入棋谱"),
                             QStringLiteral("棋谱格式不正确，无法导入。"));
        return;
    }
    loadReview(r);
    appendAi(QStringLiteral("已导入棋谱，共 %1 手。用「上一步/下一步」回看，"
                            "或点「演示/试下」从当前局面继续。")
                 .arg(review_record_.moves.size()));
    // 本局恶手（结构化）：面板汇总；试下时若落在恶手上会另行提示。
    if (!r.blunders.empty()) {
        QString summary = QStringLiteral("本局恶手（共 %1 次）：").arg(r.blunders.size());
        for (const vgo::BlunderNote& b : r.blunders) {
            summary += QStringLiteral("\n  第 %1 手（%2）：%3")
                           .arg(b.move)
                           .arg(b.point.x >= 0
                                    ? QString::fromStdString(vgo::gtpPoint(b.point, r.size))
                                    : QStringLiteral("—"))
                           .arg(QString::fromStdString(b.text));
        }
        appendAi(summary);
    }
    // 其它备注（悔棋记录等）：随棋谱一并展示，便于复盘。
    if (!r.notes.empty()) {
        QString notes = QStringLiteral("复盘备注：");
        for (const std::string& n : r.notes)
            notes += QStringLiteral("\n") + QString::fromStdString(n);
        appendAi(notes);
    }
}

void MainWindow::saveRecord() {
    vgo::GameRecord r;
    if (app_mode_ == vgo::GameMode::Review) {
        r = review_record_;  // 复盘保存整张谱，不受当前光标影响
    } else {
        vgo::Game* g = currentGame();
        if (!g) return;
        r = vgo::recordFromGame(*g);
        // 人机对弈：把本局恶手（含落点）随谱保存，供复盘命中检测与展示。
        if (inAiMode()) r.blunders = blunders_;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存棋谱"), QStringLiteral("game.vgo"),
        QStringLiteral("VGO 棋谱 (*.vgo)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("保存棋谱"),
                             QStringLiteral("无法写入文件：%1").arg(path));
        return;
    }
    f.write(QByteArray::fromStdString(vgo::serializeRecord(r)));
    f.close();
    status_->setText(QStringLiteral("已保存棋谱：%1").arg(QFileInfo(path).fileName()));
}

void MainWindow::updateActionsForMode() {
    const bool placement = (app_mode_ == vgo::GameMode::Placement);
    const bool ai = (app_mode_ == vgo::GameMode::HumanVsAi);
    const bool review = (app_mode_ == vgo::GameMode::Review);
    const int n = static_cast<int>(review_record_.moves.size());

    // 菜单/工具栏从属当前标签页：只显示本模式相关动作（隐藏而非置灰）。
    actNew_->setVisible(placement || ai);    // 新对局：摆棋/对弈
    actUndo_->setVisible(placement || ai);   // 悔棋：摆棋/对弈
    actPass_->setVisible(placement || ai);   // 虚着：摆棋/对弈
    actCount_->setVisible(ai);               // 请求数目：对弈
    actResign_->setVisible(ai);              // 认输：对弈
    actScore_->setVisible(placement || ai);  // 计分/终局：摆棋/对弈
    saveAction_->setVisible(placement || ai);  // 保存棋谱：摆棋/对弈
    importAction_->setVisible(review);       // 导入棋谱：复盘
    prevAction_->setVisible(review);         // 上一步：复盘
    nextAction_->setVisible(review);         // 下一步：复盘
    if (blunderAction_) blunderAction_->setVisible(ai);  // 局中预警：对弈

    // 复盘导航随光标可用/不可用。
    prevAction_->setEnabled(review && review_cursor_ > 0);
    nextAction_->setEnabled(review && review_cursor_ < n);
}

void MainWindow::updateModeView() {
    if (!central_stack_) return;
    // 复盘且未导入棋谱：不显示棋盘（实际棋谱尺寸未知），改为导入引导大按钮。
    const bool emptyReview = (app_mode_ == vgo::GameMode::Review) && !review_game_;
    central_stack_->setCurrentWidget(emptyReview ? review_placeholder_ : board_view_);
}

void MainWindow::refresh() {
    updateModeView();  // 复盘空态 / 棋盘两页切换
    if (demo_active_) return;  // 演示态下不触碰真实对局棋盘
    vgo::Game* g = currentGame();
    if (!g) {
        // 当前模式尚无对局（如复盘未导入棋谱）：清空棋盘显示。
        board_view_->setBoard(nullptr);
        board_view_->setLastMove({-1, -1}, false);
        board_view_->setHighlight({});
        board_view_->clearOwnership();
        if (scoreStatus_) scoreStatus_->clear();
        updateStatus();
        return;
    }
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
    if (app_mode_ == vgo::GameMode::Review) {
        status_->setText(QStringLiteral("复盘模式下请点「演示/试下」才能落子"));
        return;
    }
    if (app_mode_ == vgo::GameMode::HumanVsAi) {
        if (!controller_ || !controller_->humanMayAct()) {
            if (controller_ && controller_->isAiTurn()) status_->setText(QStringLiteral("轮到 AI，请稍候…"));
            return;
        }
        if (!controller_->game().is_legal(x, y)) {
            status_->setText(QStringLiteral("该位置不能落子（已占/自杀/打劫）"));
            return;
        }
        controller_->humanPlay({x, y});
        maybeWarnBlunder();  // 预警展示 + 学习者画像采集（D3.2）
        return;
    }
    // Placement mode: the human arranges both sides.
    if (!game_ || game_->is_over()) return;
    if (game_->play(x, y)) {
        refresh();
        maybeWarnBlunder();
    } else {
        status_->setText(QStringLiteral("该位置不能落子（已占/自杀/打劫）"));
    }
}

void MainWindow::onPass() {
    if (app_mode_ == vgo::GameMode::HumanVsAi) {
        if (controller_) controller_->humanPass();
        return;
    }
    if (!game_ || game_->is_over()) return;
    game_->pass();
    refresh();
    if (game_->is_over()) onScore();
}

void MainWindow::onRequestCount() {
    if (app_mode_ == vgo::GameMode::HumanVsAi) {
        if (controller_) controller_->humanRequestCount();
        return;
    }
    // 摆棋模式无对手，直接数目。
    onScore();
}

void MainWindow::promptConfirmSettle() {
    if (!inAiMode() || !controller_) return;
    const int ret = QMessageBox::question(
        this, QStringLiteral("终局提议"),
        QStringLiteral("AI 认为棋局已结束，提议数子。是否同意？"),
        QStringLiteral("同意数子"), QStringLiteral("继续下"));
    if (ret == 0) controller_->humanConfirmCount();
    else controller_->humanDeclineCount();
}

void MainWindow::onUndo() {
    if (app_mode_ == vgo::GameMode::HumanVsAi) {
        if (!controller_) return;
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
    if (app_mode_ == vgo::GameMode::HumanVsAi) {
        if (!controller_ || controller_->resigned() || controller_->game().is_over()) return;
        const vgo::Color winner = vgo::opponent(controller_->humanColor());
        controller_->resign();
        status_->setText(QStringLiteral("你认输，%1方获胜").arg(colorName(winner)));
        // 认输弹窗：附带「保存棋谱」。
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("认输"));
        box.setText(QStringLiteral("你认输，%1方获胜").arg(colorName(winner)));
        auto* save = box.addButton(QStringLiteral("保存棋谱"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        box.exec();
        if (box.clickedButton() == save) saveRecord();
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
    // M3b.3：终局复盘——汇总本局检测到的明显恶手（如开启局中预警）。只汇总一次，
    // blunders_ 保留到 saveRecord() 以便写入棋谱末尾。
    if (!blunders_.empty() && !blunder_summary_shown_) {
        blunder_summary_shown_ = true;
        QString summary =
            QStringLiteral("终局复盘：本局检测到 %1 次明显恶手").arg(blunders_.size());
        for (const auto& b : blunders_)
            summary += QStringLiteral("（第 %1 手）").arg(b.move);
        summary += QStringLiteral("。");
        appendAi(summary);
    }

    // D3：终局把本局学习观测写入跨局画像（仅真实对局结束且未重复记录时）。
    recordGameSummary();

    QString result;
    if (black_lead > 0.0) result = QStringLiteral("黑胜");
    else if (black_lead < 0.0) result = QStringLiteral("白胜");
    else result = QStringLiteral("平局");

    // 终局计分弹窗：附带「保存棋谱」。
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("计分（数子法）"));
    box.setText(QStringLiteral("黑方：%1 子（含目）\n白方：%2 子（含目）\n贴目：%3\n结果：%4（黑领先 %5 目）")
                    .arg(black)
                    .arg(white)
                    .arg(komi)
                    .arg(result)
                    .arg(black_lead));
    auto* save = box.addButton(QStringLiteral("保存棋谱"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == save) saveRecord();
}

void MainWindow::onChatMessage(const QString& text) {
    const std::string q = text.toStdString();
    // N2.2：提问触发演示——「如果白棋在H6长出，黑棋后面几步该如何应对」。
    // 仅在无 LLM 时用关键词兜底；有 LLM 时交给模型走 function-calling，演示后还能继续讲解。
    if (usingKata() && currentGame() && !demo_busy_ && !usingHttp()) {
        const vgo::DemoRequest dr = vgo::parseDemoRequest(q, currentGame()->size());
        if (dr.valid) {
            startDemoFromQuestion(dr);
            return;
        }
    }
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
    // 试下模式下，聊天问答针对变化图当前局面（而非真实对局），并告知 LLM 已有演示。
    if (demo_active_) {
        vgo::Game dg = demoGame();
        if (advisor_) {
            advisor_->setDemoState(true, demoMovesDescription());
            advisor_->ask(dg, q, [this](const vgo::ConversationReply& r) { handleReply(r); });
        } else {
            appendAi(QStringLiteral("当前没有可用的 AI 助手。"));
        }
        return;
    }
    if (advisor_) advisor_->setDemoState(false, "");
    if (inAiMode() && controller_) {
        controller_->ask(q);
    } else if (advisor_ && currentGame()) {
        vgo::Game* g = currentGame();
        advisor_->setUserColor(vgo::Color::Empty);  // 摆棋/复盘无立场
        advisor_->ask(*g, q, [this](const vgo::ConversationReply& r) { handleReply(r); });
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
    return (inAiMode() && controller_) ? controller_->humanColor() : vgo::Color::Empty;
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
    if (on) {
        if (demo_active_) {
            // 重新展开被折叠的演示（保留现有状态）。
            demo_dock_->setVisible(true);
        } else {
            startDemo();
        }
    } else {
        // 折叠：隐藏演示面板但保留演示状态；真正退出走面板里的「结束演示」。
        if (demo_active_) demo_dock_->setVisible(false);
    }
}

void MainWindow::startDemo() {
    if (demo_active_) return;
    vgo::Game* g = currentGame();
    if (!g) {
        QSignalBlocker b(demoAction_);
        demoAction_->setChecked(false);
        return;
    }
    if (inAiMode() && controller_ && !controller_->humanMayAct()) {
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
    // 立即清掉试下局面的形势显示（热力图 + 领先文案），再由 refresh() 触发对
    // 真实对局的重新分析，避免退出试下后残留试下局面的形势结果。
    board_view_->clearOwnership();
    scoreStatus_->clear();
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

    // 复盘模式：落子命中棋谱中记录的恶手时，提示并引导思考（不直接给结论）。
    if (app_mode_ == vgo::GameMode::Review) {
        for (const vgo::BlunderNote& b : review_blunders_) {
            if (b.point.x == x && b.point.y == y) {
                appendAi(QStringLiteral("提示：这里正是你当时的恶手（第 %1 手）。想一想，这一步为什么不好？")
                             .arg(b.move));
                break;
            }
        }
    }
}

vgo::Game MainWindow::demoGame() const {
    vgo::Game g(variation_.variation().start, variation_.variation().to_move);
    for (const auto& s : variation_.variation().steps) {
        if (s.move.is_pass) g.pass();
        else g.play(s.move.p.x, s.move.p.y);
    }
    return g;
}

std::string MainWindow::demoMovesDescription() const {
    std::string out;
    const int size = variation_.variation().start.size();
    int n = 0;
    for (const auto& s : variation_.variation().steps) {
        ++n;
        if (!out.empty()) out += ' ';
        out += std::to_string(n) + '.';
        if (s.move.is_pass)
            out += "虚着";
        else
            out += std::string(s.color == vgo::Color::Black ? "黑" : "白") +
                   vgo::gtpPoint(s.move.p, size);
    }
    return out;
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
    // 同步收集中暂不刷新（避免与落子争抢同一引擎）。
    if (analyzeAction_ && analyzeAction_->isChecked() && usingKata() && !demo_busy_) {
        QTimer::singleShot(0, this, [this] { requestAnalysis(false); });
    } else if (!demo_busy_) {
        board_view_->clearOwnership();
        scoreStatus_->clear();
    }
}

void MainWindow::onDemoAiMove() {
    demoAiContinue(1);
}

void MainWindow::demoAiContinue(int plies) {
    if (!demo_active_ || plies <= 0) return;
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
                     [this, plies, req_board, to_move](vgo::EngineResult r) {
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
                         // 连续演示后续几步，短暂停顿让用户看清每一手。
                         if (plies > 1) {
                             QTimer::singleShot(450, this,
                                                [this, plies] { demoAiContinue(plies - 1); });
                         }
                     });
}

void MainWindow::startDemoFromQuestion(const vgo::DemoRequest& dr) {
    vgo::Game* g = currentGame();
    if (!g) return;
    std::string placed;
    if (!enterDemoAndPlace(dr.point, dr.color, /*reset=*/false, /*undo=*/0, &placed)) {
        status_->setText(QString::fromStdString(placed));
        return;
    }
    const QString side = (dr.color == vgo::Color::Black) ? QStringLiteral("黑") : QStringLiteral("白");
    const QString resp = (dr.color == vgo::Color::Black) ? QStringLiteral("白") : QStringLiteral("黑");
    appendAi(QStringLiteral("试下：假设%1方在 %2 落子，用权威 AI 演示%3方的应对。")
                 .arg(side)
                 .arg(QString::fromStdString(vgo::gtpPoint(dr.point, g->size())))
                 .arg(resp));
    demoAiContinue(4);  // 关键词路径：异步逐步演示（无需把坐标回传给 LLM）
}

bool MainWindow::enterDemoAndPlace(vgo::Point p, vgo::Color color, bool reset, int undo,
                                   std::string* placed) {
    vgo::Game* g = currentGame();
    if (!g) {
        if (placed) *placed = "当前没有棋局";
        return false;
    }
    if (placed) *placed = "";
    // 进入（或续用/重置）试下：默认在当前演示基础上继续，reset=true 才从头。
    if (!demo_active_) {
        variation_.start(g->board(), g->current());
        demo_active_ = true;
        demo_dock_->setVisible(true);
        if (advisor_) advisor_->noteAnalysis(vgo::Analysis{});
        QSignalBlocker b(demoAction_);
        demoAction_->setChecked(true);
    } else if (reset) {
        variation_.start(g->board(), g->current());
    }
    // else: 保留现有变化图，继续追加。
    // 回退若干步（撤销最近几步），用于回到某个局部局面后再演示。
    for (int i = 0; i < undo && variation_.back(); ++i) {}
    if (p.x < 0) {
        updateVariationBoard();
        return true;
    }
    const vgo::Color play = (color == vgo::Color::Empty) ? variation_.toMove() : color;
    const vgo::Board b = variation_.board();
    if (!b.in_bounds(p.x, p.y) || b.at(p.x, p.y) != vgo::Color::Empty) {
        if (placed) *placed = "该位置无法落子";
        status_->setText(QStringLiteral("试下：该位置无法落子"));
        return false;
    }
    vgo::Game probe(b, play);
    if (!probe.play(p.x, p.y)) {
        if (placed) *placed = "该位置不能落子（自杀/打劫）";
        status_->setText(QStringLiteral("试下：该位置不能落子（自杀/打劫）"));
        return false;
    }
    variation_.commitStep(vgo::Move{p, false}, play, false);
    updateVariationBoard();
    if (placed)
        *placed = std::string(play == vgo::Color::Black ? "黑" : "白") + " " + vgo::gtpPoint(p, g->size());
    return true;
}

bool MainWindow::requestDemoMoveSync(const vgo::Board& b, vgo::Color to_move, vgo::Move* out) {
    vgo::EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) return false;
    bool called = false;
    bool ok = false;
    QEventLoop loop;
    eng->requestMove(b, to_move,
                     vgo::strengthForLevel(vgo::ModelManager::instance().analysisLevel()),
                     [&](vgo::EngineResult r) {
                         called = true;
                         if (!r.move.is_pass) {
                             *out = r.move;
                             ok = true;
                         }
                         loop.quit();
                     });
    if (!called) loop.exec();  // KataGo 异步；Fallback 同步时已调用 quit
    return ok;
}

std::vector<vgo::Move> MainWindow::collectAiMoves(int plies) {
    std::vector<vgo::Move> moves;
    if (plies < 1) plies = 1;
    if (plies > 8) plies = 8;
    demo_busy_ = true;
    for (int i = 0; i < plies && demo_active_; ++i) {
        const vgo::Board b = variation_.board();
        const vgo::Color to_move = variation_.toMove();
        vgo::Move m;
        if (!requestDemoMoveSync(b, to_move, &m)) break;
        if (!demo_active_) break;  // 用户中途退出演示
        vgo::Game probe(b, to_move);
        if (!probe.play(m.p.x, m.p.y)) break;
        variation_.commitStep(m, to_move, true);
        moves.push_back(m);
        updateVariationBoard();
    }
    demo_busy_ = false;
    updateVariationBoard();  // 结束时刷新一次形势
    return moves;
}

std::string MainWindow::handleDemoTool(const QString& name, const QJsonObject& args) {
    if (name != QStringLiteral("demonstrate_go_moves")) return "未知工具";
    if (demo_busy_) return "正在演示中，请稍候";
    vgo::Point p{-1, -1};
    const QString pt = args["point"].toString();
    if (!pt.isEmpty() && currentGame()) {
        vgo::Point parsed;
        if (vgo::gtpParsePoint(pt.toStdString(), currentGame()->size(), &parsed)) p = parsed;
    }
    vgo::Color color = vgo::Color::Empty;
    const QString c = args["color"].toString().toLower();
    if (c == "black") color = vgo::Color::Black;
    else if (c == "white") color = vgo::Color::White;
    const int plies = args["plies"].toInt(4);
    const bool reset = args["reset"].toBool(false);
    // undo_to = 回退到第 k 手之前（保留前 k-1 手），转成「撤销几步」交给 enterDemoAndPlace。
    int undo = 0;
    const int undo_to = args["undo_to"].toInt(0);
    if (undo_to > 0) {
        const int total = static_cast<int>(variation_.variation().steps.size());
        undo = total - undo_to + 1;
        if (undo < 0) undo = 0;
    }

    std::string placed;
    if (!enterDemoAndPlace(p, color, reset, undo, &placed)) return placed;

    // 同步跑完权威 AI 续下，拿到具体坐标，再回传给 LLM，避免「没有具体坐标」。
    const std::vector<vgo::Move> moves = collectAiMoves(plies);
    const int size = currentGame() ? currentGame()->size() : 19;

    std::string summary;
    if (!placed.empty()) summary = "已在棋盘演示：" + placed + " 后，权威 AI 续下";
    else summary = "已在棋盘演示：权威 AI 续下";
    for (const vgo::Move& m : moves) summary += " " + vgo::gtpPoint(m.p, size);
    if (moves.empty()) summary += "（无更多着法，AI 建议虚着/终局）";
    summary += "。";
    return summary;
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
    const int move_number = g->move_number();
    const int size = g->size();
    // 结构化棋形（用于弱项打标签）：最近一手前后的棋盘差。
    const vgo::Color mover = vgo::opponent(g->current());
    vgo::Game before = *g;
    before.undo();
    const auto facts = vgo::extractBoardFacts(
        before.board(), g->board(), vgo::Move{p, false},
        g->captures(mover) - before.captures(mover), mover);

    // evaluateLastMove 才是「已落子」的正确评估语义（evaluateMove 会再落一手而失败）。
    advisor_->evaluateLastMove(*g, [this, move_number, size, facts, p](
                                       const vgo::MoveEvaluation& e) {
        if (!vgo::isBlunder(e)) return;
        const double lost = std::max(0.0, e.lead_before - e.lead_after);

        // D3.2：把恶手喂给学习者画像（跨局弱项向量 + 本局点数损失），仅人机对弈
        // 采集，避免摆棋试下污染画像；隐私开关关闭时不采集。
        if (inAiMode() && learner_ && learner_->enabled()) {
            learner_->model().recordBlunder(
                vgo::BlunderEvent{vgo::classifyBlunderArea(facts, move_number, size), lost});
            game_total_loss_ += lost;
            ++game_blunder_count_;
            learner_->save();
        }

        // 局中预警（仅当开关打开时展示），并记录落点供棋谱持久化 / 复盘命中检测。
        if (blunderAction_ && blunderAction_->isChecked()) {
            const std::string w = vgo::blunderWarning(e);
            if (!w.empty()) {
                blunders_.push_back(vgo::BlunderNote{move_number, p, w});
                appendAi(QString::fromStdString(w));
            }
        }
    });
}

void MainWindow::recordGameSummary() {
    if (!inAiMode() || game_recorded_) return;
    if (!learner_ || !learner_->enabled()) return;
    vgo::Game* g = currentGame();
    if (!g || !g->is_over()) return;
    game_recorded_ = true;

    vgo::GameSummary s;
    s.moves = g->move_number();
    s.won = (g->winner() == userColor());
    s.level = setup_.level;
    // 用本局累计的恶手点数损失近似「平均每手损点」（点数为纲，可喂 estimateLevel）。
    s.avg_loss_per_move = (s.moves > 0) ? game_total_loss_ / static_cast<double>(s.moves) : 0.0;
    s.winrate_deviation = 0.0;  // 暂无逐手胜率偏离观测，保留字段
    learner_->model().recordGame(s);
    learner_->save();
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
    if (!g) {
        status_->setText(app_mode_ == vgo::GameMode::Review
                             ? QStringLiteral("复盘：尚未导入棋谱")
                             : QString());
        return;
    }

    if (app_mode_ == vgo::GameMode::Review) {
        status_->setText(QStringLiteral("复盘：第 %1 / %2 手")
                             .arg(review_cursor_)
                             .arg(static_cast<int>(review_record_.moves.size())));
        return;
    }

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
    if (inAiMode() && controller_) {
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
