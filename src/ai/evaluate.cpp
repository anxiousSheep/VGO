#include "evaluate.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "gtp.h"

namespace vgo {

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
