#include "advisor.h"

#include <utility>

#include "strength.h"

namespace vgo {

AiAdvisor::AiAdvisor(EngineInterface* engine, ConversationService* conversation, int level)
    : engine_(engine), conversation_(conversation), level_(level) {}

void AiAdvisor::ask(const Game& game, const std::string& question, ReplyCb cb) {
    if (!conversation_) return;
    const QuestionClass cls = classifyQuestion(question);
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
}

GroundedFacts AiAdvisor::currentFacts(const Game& game) const {
    GroundedFacts f;
    f.analysis = last_analysis_;
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
    req.profile = PlayerProfile{level_, true};
    const int gen = ++generation_;
    conversation_->answer(req, [this, gen, cb](ConversationReply r) {
        if (gen != generation_) return;  // stale / cancelled
        cb(std::move(r));
    });
}

}  // namespace vgo
