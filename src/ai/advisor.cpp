#include "advisor.h"

#include <utility>

#include "lifedeath.h"
#include "strength.h"

namespace vgo {

AiAdvisor::AiAdvisor(EngineInterface* engine, ConversationService* conversation, int level)
    : engine_(engine), conversation_(conversation), level_(level) {}

void AiAdvisor::ask(const Game& game, const std::string& question, ReplyCb cb) {
    if (!conversation_) return;
    const QuestionClass cls = classifyQuestion(question);
    // 讲解增强（N1）：落点质量走 what-if，死活走 ownership 判死，二者异步查引擎。
    if (cls == QuestionClass::MoveQuality) {
        handleMoveQuality(game, question, cb);
        return;
    }
    if (cls == QuestionClass::LifeDeath) {
        handleLifeDeath(game, question, cb);
        return;
    }
    // 「我该下哪」为用户执子方建议；「谁领先」用当前局面分析。二者都查引擎拿真实分析。
    const bool needEngine = (cls == QuestionClass::WhereToPlay || cls == QuestionClass::WhoIsWinning);

    if (needEngine && !game.is_over() && engine_) {
        const Color to_move = (user_color_ != Color::Empty && cls == QuestionClass::WhereToPlay)
                                  ? user_color_
                                  : game.current();
        const int gen = ++generation_;
        const Game snapshot = game;  // stable copy for the async callback
        engine_->requestMove(game.board(), to_move, strengthForLevel(level_),
                             [this, gen, question, snapshot, cb](EngineResult r) {
                                 if (gen != generation_) return;  // stale / cancelled
                                 GroundedFacts f = currentFacts(snapshot);
                                 if (r.analysis.valid) {
                                     f.analysis = r.analysis;  // 用引擎完整分析
                                 } else if (!r.move.is_pass) {
                                     MoveCandidate c;
                                     c.p = r.move.p;
                                     f.analysis.valid = true;
                                     f.analysis.best = c;
                                     f.analysis.candidates.push_back(c);
                                 }
                                 deliverAnswer(snapshot, question, std::move(f), cb);
                             });
        return;
    }
    deliverAnswer(game, question, currentFacts(game), cb);
}

void AiAdvisor::explainLastMove(const Game& game, ReplyCb cb) {
    deliverAnswer(game, "为什么下这里", currentFacts(game), cb);
}

void AiAdvisor::analyzePosition(const Game& game, AnalysisCb cb) {
    // 形势判断用专用满血引擎（analysis_engine_），未设置时回退到落子引擎。
    EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) {
        cb(Analysis{});
        return;
    }
    const int gen = ++generation_;
    const Color to_move = game.current();
    const Game snapshot = game;  // stable copy for the async callback
    // 形势判断用独立强度档位（analysis_level_，默认职业7段），与对局难度解耦，
    // 保证判断正确；落子仍按 level_ 的难度。
    eng->analyze(game.board(), to_move, strengthForLevel(analysis_level_),
                 [this, gen, cb](const Analysis& a) {
                     if (gen != generation_) return;  // stale / cancelled
                     last_analysis_ = a;              // cache as grounding
                     cb(a);
                 });
}

void AiAdvisor::cancel() {
    ++generation_;
    if (conversation_) conversation_->cancel();
    if (move_evaluator_) move_evaluator_->cancel();
}

MoveEvaluator* AiAdvisor::evaluator() {
    if (!move_evaluator_) {
        move_evaluator_ =
            std::make_unique<MoveEvaluator>(analysis_engine_ ? analysis_engine_ : engine_);
    }
    return move_evaluator_.get();
}

void AiAdvisor::evaluateMove(const Game& game, const Point& p, MoveEvaluator::Callback cb) {
    const int gen = ++generation_;
    evaluator()->evaluate(game, p, [this, gen, cb](const MoveEvaluation& e) {
        if (gen != generation_) return;
        cb(e);
    });
}

void AiAdvisor::handleMoveQuality(const Game& game, const std::string& question, ReplyCb cb) {
    const Game snapshot = game;
    const int gen = ++generation_;
    const auto deliver = [this, gen, snapshot, question, cb](const MoveEvaluation& e) {
        if (gen != generation_) return;
        GroundedFacts f = currentFacts(snapshot);
        f.move_eval = e;
        deliverAnswer(snapshot, question, std::move(f), cb);
    };
    const PointMatch m = findPointInQuestion(question, game.size());
    if (m.found) {
        // 提问中明确给了落点：作为「假想手」在副本上试下再评估。
        evaluator()->evaluate(snapshot, m.p, deliver);
        return;
    }
    if (game.has_last_move()) {
        // 无落点：评估「最近一手」（已在棋盘上，撤掉再比较前后）。
        evaluator()->evaluateLastMove(snapshot, deliver);
        return;
    }
    deliverAnswer(snapshot, question, currentFacts(snapshot), cb);
}

void AiAdvisor::handleLifeDeath(const Game& game, const std::string& question, ReplyCb cb) {
    EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) {
        deliverAnswer(game, question, currentFacts(game), cb);
        return;
    }
    const Game snapshot = game;
    const Color to_move = game.current();
    const int gen = ++generation_;
    eng->analyze(game.board(), to_move, strengthForLevel(analysis_level_),
                 [this, gen, snapshot, question, cb](const Analysis& a) {
                     if (gen != generation_) return;
                     GroundedFacts f = currentFacts(snapshot);
                     f.analysis = a;
                     if (!a.ownership.empty()) {
                         f.dead_groups = deadGroups(snapshot.board(), a.ownership);
                         f.life_death = analyzeLifeDeath(snapshot.board(), a.ownership);
                     }
                     deliverAnswer(snapshot, question, std::move(f), cb);
                 });
}

GroundedFacts AiAdvisor::currentFacts(const Game& game) const {
    GroundedFacts f;
    f.analysis = last_analysis_;
    if (!last_analysis_.ownership.empty()) {
        f.dead_groups = deadGroups(game.board(), last_analysis_.ownership);
    }
    if (game.move_number() > 0 && game.has_last_move()) {
        const Color mover = opponent(game.current());
        Game before = game;
        before.undo();
        f.facts = extractBoardFacts(before.board(), game.board(),
                                    Move{game.last_move(), false},
                                    game.captures(mover) - before.captures(mover),
                                    mover);
    }
    return f;
}

void AiAdvisor::deliverAnswer(const Game& game, const std::string& question,
                              GroundedFacts facts, ReplyCb cb) {
    ConversationRequest req;
    req.question = question;
    req.facts = std::move(facts);
    req.board_context = boardToAscii(game.board());
    req.score_black = game.score_black();
    req.move_number = game.move_number();
    req.size = game.size();
    req.to_move = game.current();
    req.last_mover = opponent(game.current());
    req.user_color = user_color_;
    req.last_move = game.last_move();
    req.has_last_move = game.has_last_move();
    req.hint = isHintRequest(question);
    req.in_demo = demo_active_ctx_;
    req.demo_moves = demo_moves_;
    req.profile = PlayerProfile{level_, true};
    const int gen = ++generation_;
    conversation_->answer(req, [this, gen, cb](ConversationReply r) {
        if (gen != generation_) return;  // stale / cancelled
        cb(std::move(r));
    });
}

}  // namespace vgo
