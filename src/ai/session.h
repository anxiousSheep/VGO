#pragma once

#include <functional>
#include <string>

#include "analysis.h"
#include "conversation.h"
#include "engine.h"
#include "go/game.h"

namespace vgo {

class AiAdvisor;

enum class AiActivity { Idle, Thinking, Explaining };

// Coordinates a human-vs-AI game. Owns the Game; applies human moves, drives
// the AI turn, handles undo (whole move pairs) and questions (delegated to an
// AiAdvisor). UI-agnostic: the owner wires std::function callbacks and receives
// asynchronous results through them.
class AIMatchController {
public:
    using AiMoveCb = std::function<void(const Move&, const Analysis&)>;
    using ReplyCb = std::function<void(const ConversationReply&)>;
    using ActivityCb = std::function<void(AiActivity)>;

    std::function<void()> onChanged;   // board/status changed -> refresh
    AiMoveCb onAiMove;                 // AI played a move
    ReplyCb onReply;                   // an answer/commentary is ready
    ActivityCb onActivity;             // Thinking/Explaining/Idle

    AIMatchController(int size, Color human_color, EngineInterface* engine,
                      AiAdvisor* advisor, int level = 5);

    Game& game() { return game_; }
    const Game& game() const { return game_; }
    Color humanColor() const { return human_color_; }
    int level() const { return level_; }
    void setLevel(int l);

    bool isAiTurn() const { return ai_turn_; }
    bool isThinking() const { return thinking_; }
    bool resigned() const { return resigned_; }
    bool humanMayAct() const { return !ai_turn_ && !game_.is_over() && !resigned_; }

    // Begin play. Must be called after the owner has wired the callbacks.
    void start();

    void humanPlay(Point p);
    void humanPass();
    void undo();
    void resign();
    void ask(const std::string& question);

private:
    void beginAiTurn();
    void onAiResult(EngineResult r);

    Game game_;
    Color human_color_;
    EngineInterface* engine_;
    AiAdvisor* advisor_;
    int level_ = 5;
    bool ai_turn_ = false;
    bool thinking_ = false;
    bool resigned_ = false;
    int generation_ = 0;   // bumped to invalidate stale async callbacks
};

}  // namespace vgo
