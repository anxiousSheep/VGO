#pragma once

#include <QMainWindow>

#include <memory>
#include <utility>
#include <vector>

#include "ai/game_setup.h"
#include "ai/learner/learner_model.h"
#include "ai/record.h"
#include "ai/variation.h"
#include "go/game.h"

class QLabel;
class QAction;
class QDockWidget;
class QJsonObject;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace vgo {
class GobanWidget;
class ChatPanel;
class VariationPanel;
class EngineInterface;
class ConversationService;
class AiAdvisor;
class AIMatchController;
class LearnerStore;
class TraceStore;
class BadSampleStore;
struct ConversationReply;
struct Analysis;
struct MoveEvaluation;
struct GroundedFacts;
struct DemoRequest;
}

// Main window: hosts a mode tab bar (摆棋 / 人机对弈 / 复盘) above a shared
// board view. Each mode keeps its own state — a placement Game, an
// AIMatchController, or an imported record under review (with a cursor).
// Game parameters are chosen once per game and cannot be changed in-game; the
// board-demo (试下) mode and teaching aids (M3b) work from any mode.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void newGame();
    void openModelSettings();
    void onPointClicked(int x, int y);
    void onPass();
    void onRequestCount();
    void promptConfirmSettle();
    void onUndo();
    void onResign();
    void onScore();
    void onAnalyzeToggled(bool on);
    void onChatMessage(const QString& text);

    // 模式标签页 / 复盘 / 棋谱保存
    void onModeChanged(int index);
    void importReview();
    void stepReview(int delta);
    void saveRecord();

    // 棋盘演示（N2，试下模式）
    void onDemoToggled(bool on);
    void onDemoAiMove();
    void onDemoUndo();
    void onDemoExit();
    void demoAiContinue(int plies);                       // 权威 AI 连续续下 plies 手
    void startDemoFromQuestion(const vgo::DemoRequest& dr);  // 提问触发演示（N2.2）
    // LLM 工具调用（function-calling）：模型可主动请求打开演示并落子/续下。
    std::string handleDemoTool(const QString& name, const QJsonObject& args);
    bool enterDemoAndPlace(vgo::Point p, vgo::Color color, bool reset, int undo,
                           std::string* placed);
    std::vector<vgo::Move> collectAiMoves(int plies);
    bool requestDemoMoveSync(const vgo::Board& b, vgo::Color to_move, vgo::Move* out);

    // 教学增强（M3b）
    void onBlunderToggled(bool on);
    void onReloadSkills();

    // 可观测（D5.6）：查看运行 trace 面板
    void showTracePanel();

    // 学习者画像（D3.5）：进入「每日一题」标签页 / 判题 / 换一题
    void enterDailyPuzzle();
    void onDailyAnswer(int x, int y);
    void skipDailyProblem();

private:
    void applySetup(const vgo::GameSetup& setup);
    void startPlacement(int size);
    void startAi(const vgo::GameSetup& setup);
    void createAiServices(int level, double kyu_hint = 20.0);
    void teardown();
    void teardownServices();
    void refresh();
    void updateStatus();
    void updateModelStatus();
    void updateTierStatus();
    void updateActionsForMode();
    void appendAi(const QString& text);
    void appendNote(const QString& text);
    void handleReply(const vgo::ConversationReply& r);
    void requestAnalysis(bool sayInChat);
    void applyAnalysis(const vgo::Analysis& a, vgo::Color to_move, bool sayInChat);
    void showScoreResult(int black, int white, double komi, double black_lead);
    void updateModeView();  // 复盘空态（未导入棋谱）时切换为导入引导

    // 复盘
    void loadReview(const vgo::GameRecord& r);
    void rebuildReviewGame();

    // 棋盘演示（N2，试下模式）
    void startDemo();
    void exitDemo();
    void onDemoPointClicked(int x, int y);
    void setDemoChecked(bool on);  // 菜单动作与侧边按钮的演示勾选同步（互不触发）
    void updateVariationBoard();
    vgo::Game demoGame() const;
    std::string demoMovesDescription() const;

    // 教学增强（M3b）
    void maybeWarnBlunder();
    void loadSkills();

    // 学习者画像（D3）：恶手采集 + 每局摘要落盘
    void recordGameSummary();

    bool inAiMode() const { return app_mode_ == vgo::GameMode::HumanVsAi; }
    vgo::Game* currentGame() const;
    vgo::Color userColor() const;
    bool usingKata() const;
    bool usingHttp() const;

    vgo::GameMode app_mode_ = vgo::GameMode::Placement;
    vgo::GameSetup setup_;

    vgo::Game* game_ = nullptr;                     // 摆棋模式
    vgo::AIMatchController* controller_ = nullptr;  // 人机对弈模式
    // 复盘模式：导入的棋谱 + 光标 + 重建的当前局面。
    vgo::GameRecord review_record_;
    int review_cursor_ = 0;
    std::unique_ptr<vgo::Game> review_game_;

    vgo::EngineInterface* engine_ = nullptr;        // 落子引擎（难度随等级）
    vgo::EngineInterface* analysis_engine_ = nullptr;  // 形势判断专用满血引擎（D5.4：并发池）
    bool analysis_kata_ = false;                    // 形势判断是否由 KataGo 提供（D5.4）
    vgo::ConversationService* conversation_ = nullptr;
    vgo::AiAdvisor* advisor_ = nullptr;

    vgo::GobanWidget* board_view_ = nullptr;
    QStackedWidget* central_stack_ = nullptr;      // 棋盘 / 复盘空态 两页
    QWidget* review_placeholder_ = nullptr;         // 复盘空态：大按钮引导导入
    QLabel* status_ = nullptr;
    QLabel* modelStatus_ = nullptr;
    QLabel* scoreStatus_ = nullptr;
    QLabel* tierStatus_ = nullptr;  // 讲解档位徽标（D5.1 明示降级）
    vgo::ChatPanel* chat_ = nullptr;
    QDockWidget* dock_ = nullptr;
    QTabBar* mode_bar_ = nullptr;

    QAction* actNew_ = nullptr;
    QAction* actUndo_ = nullptr;
    QAction* actPass_ = nullptr;
    QAction* actCount_ = nullptr;
    QAction* actResign_ = nullptr;
    QAction* actScore_ = nullptr;
    QAction* saveAction_ = nullptr;    // 保存棋谱（摆棋 / 对弈终局弹窗内）
    QAction* importAction_ = nullptr;  // 导入棋谱（复盘）
    QAction* prevAction_ = nullptr;    // 上一步（复盘）
    QAction* nextAction_ = nullptr;    // 下一步（复盘）
    QAction* toggleChatAction_ = nullptr;
    QAction* analyzeAction_ = nullptr;
    bool resigned_ = false;

    // 棋盘演示（N2）
    bool demo_active_ = false;
    bool demo_busy_ = false;  // 同步收集中，抑制形势自动刷新，避免与落子争抢引擎
    vgo::VariationController variation_;
    vgo::Board variation_board_{9};
    std::vector<std::pair<vgo::Point, int>> variation_numbers_;
    vgo::VariationPanel* variation_panel_ = nullptr;
    QDockWidget* demo_dock_ = nullptr;
    QAction* demoAction_ = nullptr;

    // 教学增强（M3b）
    QAction* blunderAction_ = nullptr;
    std::vector<vgo::BlunderNote> blunders_;  // 本局恶手（手数 + 落点 + 说明）
    bool blunder_summary_shown_ = false;  // 终局复盘汇总只提示一次（blunders_ 保留给棋谱）
    std::vector<vgo::BlunderNote> review_blunders_;  // 导入棋谱中的恶手（复盘命中检测）

    // 学习者画像（D3）：跨局持久化 + 恶手采集 + 每局摘要
    std::unique_ptr<vgo::LearnerStore> learner_;
    double game_total_loss_ = 0.0;   // 本局累计恶手点数损失（喂 avg_loss_per_move）
    int game_blunder_count_ = 0;
    bool game_recorded_ = false;

    // 每日一题（D3.5）：当天恶手复盘题 + 判题状态
    std::unique_ptr<vgo::Game> daily_game_;   // 由题目快照重建的局面
    vgo::ReviewItem daily_problem_;           // 当前题目（含快照 / 领先 / 恶手说明）
    bool daily_ready_ = false;                // 当天有题可做
    bool daily_solved_ = false;               // 已过关（不再判题）
    QWidget* daily_placeholder_ = nullptr;    // 无题空态引导
    QAction* skipDailyAction_ = nullptr;      // 「换一题」

    // 人机对弈空态：未开始对局时显示「新对局」大按钮（取代空棋盘）。
    QWidget* newgame_placeholder_ = nullptr;
    // 侧边栏纵向「演示」按钮（与菜单里的演示动作互为镜像）。
    QToolButton* sideDemoBtn_ = nullptr;

    // 可观测（D5.6）：trace 落盘 + 最近一次回复的主变化（供证据点击跳 PV）
    std::unique_ptr<vgo::TraceStore> trace_store_;
    std::unique_ptr<vgo::BadSampleStore> bad_sample_store_;  // D2.5 坏样本集
    std::vector<vgo::Point> last_pv_;
};
