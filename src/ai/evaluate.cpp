#include "evaluate.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "conversation.h"
#include "gtp.h"

namespace vgo {

DemoRequest parseDemoRequest(const std::string& text, int size) {
    DemoRequest dr;
    const PointMatch pm = findPointInQuestion(text, size);
    if (!pm.found) return dr;

    // Continuation intent: "应对/接应/后面/接下来/怎么办…"
    const std::string q = lower_ascii(text);
    const bool wants_continuation =
        contains(q, "应对") || contains(q, "接应") || contains(q, "后面") ||
        contains(q, "接下来") || contains(q, "怎么办") || contains(q, "应付");
    if (!wants_continuation) return dr;

    // The color of the proposed move is the nearest 黑/白 before the point.
    const std::string pt = gtpPoint(pm.p, size);
    const std::size_t pos = lower_ascii(text).find(lower_ascii(pt));
    const std::size_t white_pos = text.rfind("白", pos);
    const std::size_t black_pos = text.rfind("黑", pos);
    Color color = Color::Empty;
    if (white_pos != std::string::npos &&
        (black_pos == std::string::npos || white_pos > black_pos))
        color = Color::White;
    else if (black_pos != std::string::npos)
        color = Color::Black;
    if (color == Color::Empty) return dr;

    dr.valid = true;
    dr.point = pm.p;
    dr.color = color;
    return dr;
}

PointMatch findPointInQuestion(const std::string& text, int size) {
    PointMatch m;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        if (c < 'A' || c > 'Z' || c == 'I') continue;
        std::size_t j = i + 1;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
        if (j == i + 1) continue;  // a letter not followed by a number
        Point p;
        if (gtpParsePoint(text.substr(i, j - i), size, &p)) {
            m.found = true;
            m.p = p;
            return m;
        }
        i = j - 1;
    }
    return m;
}

MoveEvaluation computeMoveEvaluation(const MoveEvaluation& in,
                                     const Analysis& before, Color before_to_move,
                                     const Analysis& after, Color after_to_move,
                                     Color mover) {
    MoveEvaluation e = in;
    e.mover = mover;
    if (!before.valid || !after.valid) return e;  // valid stays false
    e.valid = true;
    const auto wr = [&](const Analysis& a, Color to_move) {
        return mover == to_move ? a.winrate : 1.0 - a.winrate;
    };
    const auto lead = [&](const Analysis& a, Color to_move) {
        return mover == to_move ? a.score_lead : -a.score_lead;
    };
    e.winrate_before = wr(before, before_to_move);
    e.winrate_after = wr(after, after_to_move);
    e.lead_before = lead(before, before_to_move);
    e.lead_after = lead(after, after_to_move);
    return e;
}

MoveEvaluator::MoveEvaluator(EngineInterface* engine)
    : engine_(engine), strength_(strengthForLevel(kAnalysisLevelNormal)) {}

void MoveEvaluator::evaluate(const Game& game, const Point& p, Callback cb) {
    const int gen = ++generation_;
    MoveEvaluation base;
    base.p = p;

    Game after = game;
    const Color mover = game.current();
    base.mover = mover;
    if (p.x < 0 || !after.play(p.x, p.y)) {
        base.legal = false;
        cb(base);
        return;
    }
    if (!engine_) {
        base.legal = true;  // valid stays false
        cb(base);
        return;
    }
    engine_->analyze(game.board(), mover, strength_,
                     [this, gen, after, mover, base, cb](const Analysis& before) mutable {
                         if (gen != generation_) return;
                         engine_->analyze(
                             after.board(), opponent(mover), strength_,
                             [this, gen, before, mover, base, cb](const Analysis& aft) {
                                 if (gen != generation_) return;
                                 cb(computeMoveEvaluation(base, before, mover, aft,
                                                          opponent(mover), mover));
                             });
                     });
}

void MoveEvaluator::evaluateLastMove(const Game& game, Callback cb) {
    const int gen = ++generation_;
    MoveEvaluation base;
    base.legal = game.has_last_move();
    if (!game.has_last_move()) {
        base.p = {-1, -1};
        cb(base);
        return;
    }
    base.p = game.last_move();
    const Color mover = opponent(game.current());  // the side that made the last move
    base.mover = mover;
    Game before = game;
    before.undo();
    if (!engine_) {
        base.legal = true;  // valid stays false
        cb(base);
        return;
    }
    engine_->analyze(before.board(), mover, strength_,
                     [this, gen, game, mover, base, cb](const Analysis& beforeA) mutable {
                         if (gen != generation_) return;
                         engine_->analyze(
                             game.board(), opponent(mover), strength_,
                             [this, gen, beforeA, mover, base, cb](const Analysis& afterA) {
                                 if (gen != generation_) return;
                                 cb(computeMoveEvaluation(base, beforeA, mover, afterA,
                                                          opponent(mover), mover));
                             });
                     });
}

void MoveEvaluator::cancel() {
    ++generation_;
}

bool isBlunder(const MoveEvaluation& e, double winrate_drop, double lead_drop) {
    if (!e.valid || !e.legal) return false;
    return (e.winrate_after - e.winrate_before) <= -winrate_drop ||
           (e.lead_after - e.lead_before) <= -lead_drop;
}

std::string blunderWarning(const MoveEvaluation& e) {
    if (!isBlunder(e)) return {};
    const double dw = (e.winrate_after - e.winrate_before) * 100.0;
    const double dl = e.lead_after - e.lead_before;
    std::ostringstream os;
    os << "（预警）这步棋明显欠妥：胜率下降约 " << std::fixed << std::setprecision(1)
       << std::abs(dw) << " 个百分点，领先缩水约 " << std::abs(dl) << " 目。";
    return os.str();
}

}  // namespace vgo
