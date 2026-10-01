#include "session.h"

#include <utility>

#include "advisor.h"
#include "strength.h"

namespace vgo {

AIMatchController::AIMatchController(int size, Color human_color, EngineInterface* engine,
                                     AiAdvisor* advisor, int level)
    : game_(size),
      human_color_(human_color),
      engine_(engine),
      advisor_(advisor),
      level_(level) {
    if (advisor_) advisor_->setUserColor(human_color_);  // 供 LLM 定位用户执子方
    ai_turn_ = (human_color_ == Color::White);  // AI opens if human is White
}

void AIMatchController::setLevel(int l) {
    level_ = l;
    if (advisor_) advisor_->setLevel(l);
}

void AIMatchController::start() {
    if (ai_turn_ && !thinking_ && !game_.is_over()) beginAiTurn();
}

void AIMatchController::humanPlay(Point p) {
    if (!humanMayAct()) return;
    if (!game_.play(p.x, p.y)) return;
    if (onChanged) onChanged();
    if (game_.is_over()) {
        ai_turn_ = false;
        return;
    }
    ai_turn_ = true;
    beginAiTurn();
}

void AIMatchController::humanPass() {
    if (!humanMayAct()) return;
    game_.pass();
    if (onChanged) onChanged();
    if (game_.is_over()) {
        ai_turn_ = false;
        return;
    }
    ai_turn_ = true;
    beginAiTurn();
}

void AIMatchController::beginAiTurn() {
    if (thinking_ || game_.is_over()) return;
    thinking_ = true;
    if (onActivity) onActivity(AiActivity::Thinking);
    const int gen = ++generation_;
    engine_->requestMove(game_.board(), game_.current(), strengthForLevel(level_),
                         [this, gen](EngineResult r) {
                             if (gen != generation_) return;  // stale / cancelled
                             onAiResult(std::move(r));
                         });
}

void AIMatchController::onAiResult(EngineResult r) {
    thinking_ = false;
    if (!game_.is_over()) {
        if (r.move.is_pass) {
            game_.pass();
        } else if (game_.is_legal(r.move.p.x, r.move.p.y)) {
            game_.play(r.move.p.x, r.move.p.y);
        } else {
            game_.pass();  // engine suggested an illegal move (e.g. superko)
        }
    }
    if (advisor_) advisor_->noteAnalysis(r.analysis);
    if (onAiMove) onAiMove(r.move, r.analysis);
    if (onChanged) onChanged();

    if (game_.is_over()) {
        ai_turn_ = false;
        if (onActivity) onActivity(AiActivity::Idle);
        return;
    }
    ai_turn_ = false;
    // 不自动解说：避免每手都向 LLM 发一条「为什么下这里」打扰用户；
    // 用户可主动在聊天面板提问（advisor.ask）。
    if (onActivity) onActivity(AiActivity::Idle);
}

void AIMatchController::ask(const std::string& question) {
    if (!advisor_) return;
    advisor_->ask(game_, question, [this](const ConversationReply& r) {
        if (onReply) onReply(r);
    });
}

void AIMatchController::undo() {
    ++generation_;  // invalidate any pending engine callback
    if (engine_) engine_->cancel();
    if (advisor_) advisor_->cancel();

    if (thinking_) {
        thinking_ = false;
        if (game_.move_number() > 0) game_.undo();  // remove the human's just-made move
        ai_turn_ = false;
        resigned_ = false;
        if (onChanged) onChanged();
        if (onActivity) onActivity(AiActivity::Idle);
        return;
    }
    // Undo a whole move pair (the AI's reply + the human's move).
    if (game_.move_number() < 2) return;
    game_.undo();  // AI's move
    game_.undo();  // human's move
    resigned_ = false;
    ai_turn_ = false;
    if (onChanged) onChanged();
}

void AIMatchController::resign() {
    if (resigned_ || game_.is_over()) return;
    ++generation_;
    if (engine_) engine_->cancel();
    if (advisor_) advisor_->cancel();
    thinking_ = false;
    ai_turn_ = false;
    resigned_ = true;
    if (onChanged) onChanged();
}

}  // namespace vgo
