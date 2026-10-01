#include "session.h"

#include <utility>

#include "strength.h"

namespace vgo {

AIMatchController::AIMatchController(int size, Color human_color, EngineInterface* engine,
                                     ConversationService* conversation, int level)
    : game_(size),
      human_color_(human_color),
      engine_(engine),
      conversation_(conversation),
      level_(level) {
    ai_turn_ = (human_color_ == Color::White);  // AI opens if human is White
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
    last_analysis_ = r.analysis;
    if (onAiMove) onAiMove(r.move, r.analysis);
    if (onChanged) onChanged();

    if (game_.is_over()) {
        ai_turn_ = false;
        if (onActivity) onActivity(AiActivity::Idle);
        return;
    }
    ai_turn_ = false;
    commentOnMove();
    if (onActivity) onActivity(AiActivity::Idle);
}

void AIMatchController::commentOnMove() {
    if (!conversation_) return;
    deliverAnswer("为什么下这里", currentFacts());
}

GroundedFacts AIMatchController::currentFacts() const {
    GroundedFacts f;
    f.analysis = last_analysis_;
    if (game_.move_number() > 0 && game_.has_last_move()) {
        const Color mover = opponent(game_.current());
        Game before = game_;
        before.undo();
        f.facts = extractBoardFacts(before.board(), game_.board(),
                                    Move{game_.last_move(), false},
                                    game_.captures(mover) - before.captures(mover),
                                    mover);
    }
    return f;
}

void AIMatchController::deliverAnswer(const std::string& question, GroundedFacts facts) {
    ConversationRequest req;
    req.question = question;
    req.facts = std::move(facts);
    req.board_context = boardToAscii(game_.board());
    req.score_black = game_.score_black();
    req.move_number = game_.move_number();
    req.size = game_.size();
    req.to_move = game_.current();
    req.last_mover = opponent(game_.current());
    req.profile = PlayerProfile{level_, true};
    const int gen = ++generation_;
    conversation_->answer(req, [this, gen](ConversationReply r) {
        if (gen != generation_) return;
        if (onReply) onReply(std::move(r));
    });
}

void AIMatchController::ask(const std::string& question) {
    if (!conversation_) return;
    if (classifyQuestion(question) == QuestionClass::WhereToPlay && !game_.is_over() &&
        engine_) {
        const Color human = human_color_;
        const int gen = ++generation_;
        engine_->requestMove(game_.board(), human, strengthForLevel(level_),
                             [this, gen, question](EngineResult r) {
                                 if (gen != generation_) return;
                                 GroundedFacts f = currentFacts();
                                 if (!r.move.is_pass) {
                                     MoveCandidate c;
                                     c.p = r.move.p;
                                     f.analysis.valid = true;
                                     f.analysis.best = c;
                                     f.analysis.candidates.push_back(c);
                                 }
                                 deliverAnswer(question, std::move(f));
                             });
        return;
    }
    deliverAnswer(question, currentFacts());
}

void AIMatchController::undo() {
    ++generation_;  // invalidate any pending engine/LLM callback
    if (engine_) engine_->cancel();
    if (conversation_) conversation_->cancel();

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
    if (conversation_) conversation_->cancel();
    thinking_ = false;
    ai_turn_ = false;
    resigned_ = true;
    if (onChanged) onChanged();
}

}  // namespace vgo
