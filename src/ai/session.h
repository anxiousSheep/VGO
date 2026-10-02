#pragma once

#include <functional>
#include <string>
#include <vector>

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
    std::function<void()> onCountProposed;  // AI 提议终局（需人类确认）
    std::function<void()> onSettled;        // 双方同意终局 / 智能裁判，可数子
    std::function<void()> onAiResign;       // AI 主动认输（中盘明显劣势）

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
    bool humanMayAct() const {
        return !ai_turn_ && !game_.is_over() && !resigned_ && !settle_proposed_;
    }

    // Begin play. Must be called after the owner has wired the callbacks.
    void start();

    void humanPlay(Point p);
    void humanPass();
    void undo();
    void resign();
    void ask(const std::string& question);

    // 终局协商：任一方可发起，另一方确认后数子；也可由智能裁判强制判定。
    bool settleProposed() const { return settle_proposed_; }
    Color settleProposer() const { return settle_proposer_; }
    void humanRequestCount();   // 人类发起数目（AI 评估后同意或落子拒绝）
    void humanConfirmCount();   // 人类确认 AI 的终局提议
    void humanDeclineCount();   // 人类拒绝 AI 的终局提议（AI 落子继续）
    void smartReferee();        // 智能裁判：达到手数阈值后由模型判定胜负

private:
    void beginAiTurn();
    void onAiResult(EngineResult r);
    void confirmSettle();

    Game game_;
    Color human_color_;
    EngineInterface* engine_;
    AiAdvisor* advisor_;
    int level_ = 5;
    bool ai_turn_ = false;
    bool thinking_ = false;
    bool resigned_ = false;
    int generation_ = 0;   // bumped to invalidate stale async callbacks
    std::vector<Point> blocked_points_;  // 引擎给出的非法点（劫），重试时屏蔽
    int retry_count_ = 0;
    bool settle_proposed_ = false;       // 已发起终局，等待另一方确认
    Color settle_proposer_ = Color::Empty;  // 提议方颜色
    Move ai_non_pass_move_{{ -1, -1 }, false};  // AI 提议时的最佳非虚着（拒绝后落子）
    bool ai_has_non_pass_ = false;
};

}  // namespace vgo
