#pragma once

#include <QMainWindow>

#include <memory>
#include <utility>
#include <vector>

#include "ai/game_setup.h"
#include "ai/record.h"
#include "ai/variation.h"
#include "go/game.h"

class QLabel;
class QAction;
class QDockWidget;
class QJsonObject;
class QTabBar;

namespace vgo {
class GobanWidget;
class ChatPanel;
class VariationPanel;
class EngineInterface;
class ConversationService;
class AiAdvisor;
class AIMatchController;
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

private:
    void applySetup(const vgo::GameSetup& setup);
    void startPlacement(int size);
    void startAi(const vgo::GameSetup& setup);
    void createAiServices(int level);
    void teardown();
    void teardownServices();
    void refresh();
    void updateStatus();
    void updateModelStatus();
    void updateTierStatus();
    void updateActionsForMode();
    void appendAi(const QString& text);
    void handleReply(const vgo::ConversationReply& r);
    void requestAnalysis(bool sayInChat);
    void applyAnalysis(const vgo::Analysis& a, vgo::Color to_move, bool sayInChat);
    void showScoreResult(int black, int white, double komi, double black_lead);

    // 复盘
    void loadReview(const vgo::GameRecord& r);
    void rebuildReviewGame();

    // 棋盘演示（N2，试下模式）
    void startDemo();
    void exitDemo();
    void onDemoPointClicked(int x, int y);
    void updateVariationBoard();
    vgo::Game demoGame() const;
    std::string demoMovesDescription() const;

    // 教学增强（M3b）
    void maybeWarnBlunder();
    void loadSkills();

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
    vgo::EngineInterface* analysis_engine_ = nullptr;  // 形势判断专用满血引擎
    vgo::ConversationService* conversation_ = nullptr;
    vgo::AiAdvisor* advisor_ = nullptr;

    vgo::GobanWidget* board_view_ = nullptr;
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
    std::vector<std::pair<int, std::string>> blunders_;  // 手数 + 预警文本（M3b.3）
};
