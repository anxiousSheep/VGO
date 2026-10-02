#pragma once

#include <QMainWindow>

#include <utility>
#include <vector>

#include "ai/game_setup.h"
#include "ai/variation.h"
#include "go/game.h"

class QLabel;
class QAction;
class QDockWidget;
class QJsonObject;

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

// Main window: holds the current game (a placement Game, or an AIMatchController
// for human-vs-AI), the board view, the chat side bar, and the menu/toolbar
// actions. Game parameters are chosen once in the New Game dialog and cannot be
// changed in-game; model settings live in a separate Settings dialog. Also hosts
// the board-demo mode (N2) and teaching aids (M3b).
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

    // 棋盘演示（N2，试下模式）
    void onDemoToggled(bool on);
    void onDemoAiMove();
    void onDemoUndo();
    void onDemoExit();
    void demoAiContinue(int plies);                       // 权威 AI 连续续下 plies 手
    void startDemoFromQuestion(const vgo::DemoRequest& dr);  // 提问触发演示（N2.2）
    // LLM 工具调用（function-calling）：模型可主动请求打开演示并落子/续下。
    std::string handleDemoTool(const QString& name, const QJsonObject& args);
    // 打开（或续用/重置/回退）试下，并落假设手；placed 输出如「黑 H6」，失败返回 false。
    bool enterDemoAndPlace(vgo::Point p, vgo::Color color, bool reset, int undo,
                           std::string* placed);
    std::vector<vgo::Move> collectAiMoves(int plies);  // 同步收集权威 AI 续下（阻塞到完成）
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
    void refresh();
    void updateStatus();
    void updateModelStatus();
    void appendAi(const QString& text);
    void handleReply(const vgo::ConversationReply& r);
    void requestAnalysis(bool sayInChat);
    void applyAnalysis(const vgo::Analysis& a, vgo::Color to_move, bool sayInChat);
    void showScoreResult(int black, int white, double komi, double black_lead);

    // 棋盘演示（N2，试下模式）
    void startDemo();
    void exitDemo();
    void onDemoPointClicked(int x, int y);
    void updateVariationBoard();
    vgo::Game demoGame() const;  // 当前试下棋盘还原成带历史的 Game，供讲解
    std::string demoMovesDescription() const;  // 变化图走法描述，如「白 H6、黑 G5」

    // 教学增强（M3b）
    void maybeWarnBlunder();
    void loadSkills();

    bool inAiMode() const { return controller_ != nullptr; }
    vgo::Game* currentGame() const;
    vgo::Color userColor() const;
    bool usingKata() const;
    bool usingHttp() const;

    vgo::GameSetup setup_;

    vgo::Game* game_ = nullptr;                     // placement mode
    vgo::AIMatchController* controller_ = nullptr;  // human-vs-AI mode
    vgo::EngineInterface* engine_ = nullptr;        // 落子引擎（难度随等级）
    vgo::EngineInterface* analysis_engine_ = nullptr;  // 形势判断专用满血引擎
    vgo::ConversationService* conversation_ = nullptr;
    vgo::AiAdvisor* advisor_ = nullptr;

    vgo::GobanWidget* board_view_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* modelStatus_ = nullptr;
    QLabel* scoreStatus_ = nullptr;
    vgo::ChatPanel* chat_ = nullptr;
    QDockWidget* dock_ = nullptr;
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
