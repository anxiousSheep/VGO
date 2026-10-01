#pragma once

#include <QMainWindow>

#include "AppConfig.h"
#include "go/game.h"

class QLabel;

namespace vgo {
class GobanWidget;
class ChatPanel;
class EngineInterface;
class ConversationService;
class AIMatchController;
}

// Main window: holds the local two-player Game (or an AIMatchController for
// human-vs-AI), the board view, the chat panel, and the menu/toolbar actions.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void newGame9();
    void newGame13();
    void newGame19();
    void onPointClicked(int x, int y);
    void onPass();
    void onUndo();
    void onResign();
    void onScore();
    void onChatMessage(const QString& text);

private:
    void startLocal(int size);
    void startAi(int size, vgo::Color humanColor);
    void teardownAi();
    void refresh();
    void updateStatus();
    void appendAi(const QString& text);

    bool inAiMode() const { return controller_ != nullptr; }
    vgo::Game* currentGame() const;

    int size_ = 9;
    int ai_level_ = 5;
    AppConfig config_;

    vgo::Game* game_ = nullptr;                 // local two-player mode
    vgo::AIMatchController* controller_ = nullptr;  // human-vs-AI mode
    vgo::EngineInterface* engine_ = nullptr;
    vgo::ConversationService* conversation_ = nullptr;

    vgo::GobanWidget* board_view_ = nullptr;
    QLabel* status_ = nullptr;
    vgo::ChatPanel* chat_ = nullptr;
    bool resigned_ = false;
};
