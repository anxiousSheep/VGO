#pragma once

#include <QMainWindow>

#include "ai/game_setup.h"
#include "go/game.h"

class QLabel;
class QAction;
class QDockWidget;

namespace vgo {
class GobanWidget;
class ChatPanel;
class EngineInterface;
class ConversationService;
class AiAdvisor;
class AIMatchController;
struct ConversationReply;
}

// Main window: holds the current game (a placement Game, or an AIMatchController
// for human-vs-AI), the board view, the chat side bar, and the menu/toolbar
// actions. Game parameters are chosen once in the New Game dialog and cannot be
// changed in-game; model settings live in a separate Settings dialog.
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
    void onUndo();
    void onResign();
    void onScore();
    void onChatMessage(const QString& text);

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

    bool inAiMode() const { return controller_ != nullptr; }
    vgo::Game* currentGame() const;

    vgo::GameSetup setup_;

    vgo::Game* game_ = nullptr;                     // placement mode
    vgo::AIMatchController* controller_ = nullptr;  // human-vs-AI mode
    vgo::EngineInterface* engine_ = nullptr;
    vgo::ConversationService* conversation_ = nullptr;
    vgo::AiAdvisor* advisor_ = nullptr;

    vgo::GobanWidget* board_view_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* modelStatus_ = nullptr;
    vgo::ChatPanel* chat_ = nullptr;
    QDockWidget* dock_ = nullptr;
    QAction* toggleChatAction_ = nullptr;
    bool resigned_ = false;
};
