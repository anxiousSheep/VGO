#include "session.h"

#include <utility>

#include "advisor.h"
#include "strength.h"

namespace vgo {

AIMatchController::AIMatchController(int size, Color human_color, EngineInterface* engine,
                                     AiAdvisor* advisor, int level, double kyu_hint,
                                     const std::vector<Point>& handicap)
    : game_(handicap.empty() ? Game(size) : Game(size, 0.5, handicap)),
      human_color_(human_color),
      engine_(engine),
      advisor_(advisor),
      level_(level),
      kyu_hint_(kyu_hint) {
    if (advisor_) advisor_->setUserColor(human_color_);  // 供 LLM 定位用户执子方
    // AI opens if human is White (even game) or if this is a handicap game
    // (human holds the black handicap stones and White — the AI — moves first).
    ai_turn_ = (human_color_ == Color::White) || !handicap.empty();
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
    blocked_points_.clear();
    retry_count_ = 0;
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
    blocked_points_.clear();
    retry_count_ = 0;
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
    // 复制棋盘并屏蔽已知非法点（劫），让引擎避开这些点。
    Board board = game_.board();
    for (const Point& p : blocked_points_) {
        if (board.in_bounds(p.x, p.y) && board.is_empty(p.x, p.y)) {
            board.set(p.x, p.y, opponent(game_.current()));
        }
    }
    engine_->requestMove(board, game_.current(), strengthForLevel(level_, kyu_hint_),
                         [this, gen](EngineResult r) {
                             if (gen != generation_) return;  // stale / cancelled
                             onAiResult(std::move(r));
                         });
}

void AIMatchController::onAiResult(EngineResult r) {
    thinking_ = false;

    // AI 主动认输（KataGo allowResignation 判定明显劣势）：直接结束对局。
    if (r.move.is_resign && !game_.is_over() && !resigned_) {
        ai_turn_ = false;
        settle_proposed_ = false;
        resigned_ = true;
        if (onChanged) onChanged();  // 先刷新棋盘/状态，再提示认输（避免被 updateStatus 覆盖）
        if (onAiResign) onAiResign();
        if (onActivity) onActivity(AiActivity::Idle);
        return;
    }

    // 人类刚请求数目：AI 用这一手「评估」——虚着=同意终局，落子=拒绝并继续。
    if (settle_proposed_ && settle_proposer_ == human_color_) {
        settle_proposed_ = false;
        ai_turn_ = false;
        if (r.move.is_pass) {
            confirmSettle();
        } else if (game_.is_legal(r.move.p.x, r.move.p.y)) {
            game_.play(r.move.p.x, r.move.p.y);
            if (advisor_) advisor_->noteAnalysis(r.analysis);
            if (onAiMove) onAiMove(r.move, r.analysis);
            if (onChanged) onChanged();
        } else {
            game_.pass();
            if (onChanged) onChanged();
        }
        if (onActivity) onActivity(AiActivity::Idle);
        return;
    }

    // 常规 AI 落子。
    if (!game_.is_over()) {
        if (r.move.is_pass) {
            // AI 认为终局：改为提议，等待人类确认，不再直接双虚着结束。
            settle_proposed_ = true;
            settle_proposer_ = game_.current();  // AI 颜色
            ai_has_non_pass_ = false;
            for (const MoveCandidate& c : r.analysis.candidates) {
                if (c.p.x >= 0 && !c.is_pass) {
                    ai_non_pass_move_ = {c.p, false};
                    ai_has_non_pass_ = true;
                    break;
                }
            }
            if (onCountProposed) onCountProposed();
            if (onChanged) onChanged();
        } else if (game_.is_legal(r.move.p.x, r.move.p.y)) {
            game_.play(r.move.p.x, r.move.p.y);
            blocked_points_.clear();
            retry_count_ = 0;
            if (advisor_) advisor_->noteAnalysis(r.analysis);
            if (onAiMove) onAiMove(r.move, r.analysis);
            if (onChanged) onChanged();
        } else {
            // 引擎给出非法点（通常是劫，重建局面丢失劫态所致）：屏蔽后重试。
            blocked_points_.push_back(r.move.p);
            if (++retry_count_ < 3) {
                beginAiTurn();
                return;
            }
            blocked_points_.clear();
            retry_count_ = 0;
            game_.pass();
        }
    }

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

void AIMatchController::humanRequestCount() {
    if (!humanMayAct() || game_.is_over() || thinking_) return;
    settle_proposed_ = true;
    settle_proposer_ = human_color_;
    ai_turn_ = true;
    beginAiTurn();  // AI 评估：虚着=同意终局，落子=拒绝继续
}

void AIMatchController::humanConfirmCount() {
    if (!settle_proposed_ || settle_proposer_ != opponent(human_color_)) return;
    confirmSettle();
}

void AIMatchController::humanDeclineCount() {
    if (!settle_proposed_ || settle_proposer_ != opponent(human_color_)) return;
    settle_proposed_ = false;
    // AI 落子继续（用提议时的最佳非虚着；没有则重新思考一手）。
    if (ai_has_non_pass_ && game_.is_legal(ai_non_pass_move_.p.x, ai_non_pass_move_.p.y)) {
        game_.play(ai_non_pass_move_.p.x, ai_non_pass_move_.p.y);
        ai_has_non_pass_ = false;
        ai_turn_ = false;
        if (onAiMove) onAiMove(ai_non_pass_move_, Analysis{});
        if (onChanged) onChanged();
    } else {
        ai_has_non_pass_ = false;
        ai_turn_ = true;
        beginAiTurn();
    }
}

void AIMatchController::smartReferee() {
    if (game_.is_over()) return;
    ++generation_;
    if (engine_) engine_->cancel();
    if (advisor_) advisor_->cancel();
    thinking_ = false;
    ai_turn_ = false;
    settle_proposed_ = false;
    game_.settle();
    if (onSettled) onSettled();
    if (onChanged) onChanged();
    if (onActivity) onActivity(AiActivity::Idle);
}

void AIMatchController::confirmSettle() {
    settle_proposed_ = false;
    thinking_ = false;
    ai_turn_ = false;
    game_.settle();
    if (onSettled) onSettled();
    if (onChanged) onChanged();
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
    blocked_points_.clear();
    retry_count_ = 0;
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
