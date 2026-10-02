#include "variation.h"

#include <algorithm>
#include <utility>

namespace vgo {

Board Variation::boardAt(int reveal) const {
    Game g(start, to_move);
    const int n = std::min(reveal, static_cast<int>(steps.size()));
    for (int i = 0; i < n; ++i) {
        const VariationStep& s = steps[static_cast<std::size_t>(i)];
        if (s.move.is_pass) g.pass();
        else g.play(s.move.p.x, s.move.p.y);
    }
    return g.board();
}

void VariationController::start(const Board& start, Color to_move) {
    v_.start = start;
    v_.to_move = to_move;
    v_.steps.clear();
    active_ = true;
}

void VariationController::commitStep(const Move& m, Color color, bool by_ai) {
    if (!active_) return;
    VariationStep s;
    s.move = m;
    s.color = color;
    s.by_ai = by_ai;
    v_.steps.push_back(std::move(s));
}

bool VariationController::back() {
    if (!active_ || v_.steps.empty()) return false;
    v_.steps.pop_back();
    return true;
}

void VariationController::exit() {
    active_ = false;
}

Color VariationController::toMove() const {
    if (v_.steps.empty()) return v_.to_move;
    return opponent(v_.steps.back().color);
}

const VariationStep* VariationController::lastStep() const {
    if (v_.steps.empty()) return nullptr;
    return &v_.steps.back();
}

}  // namespace vgo
