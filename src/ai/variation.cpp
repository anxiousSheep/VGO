#include "variation.h"

#include <algorithm>
#include <utility>

namespace vgo {

Board Variation::boardAt(int reveal) const {
    Game g(start, to_move);
    const int n = std::min(reveal, static_cast<int>(steps.size()));
    for (int i = 0; i < n; ++i) {
        const VariationStep& s = steps[static_cast<std::size_t>(i)];
        if (s.move.is_pass) {
            g.pass();
        } else {
            g.play(s.move.p.x, s.move.p.y);
        }
    }
    return g.board();
}

void VariationController::start(const Board& start, Color to_move) {
    v_.start = start;
    v_.to_move = to_move;
    v_.steps.clear();
    v_.cursor = 0;
    state_ = VariationState::WaitingForMove;
}

void VariationController::commitStep(const Move& m, Color color, bool by_ai) {
    if (state_ == VariationState::Finished) return;
    // Truncate any branch beyond the cursor (a back() followed by a new move).
    if (v_.cursor < static_cast<int>(v_.steps.size()))
        v_.steps.resize(static_cast<std::size_t>(v_.cursor));
    VariationStep s;
    s.move = m;
    s.color = color;
    s.by_ai = by_ai;
    v_.steps.push_back(std::move(s));
    v_.cursor = static_cast<int>(v_.steps.size());
    state_ = VariationState::Explaining;
}

void VariationController::appendExplanation(const std::string& delta) {
    if (v_.steps.empty()) return;
    v_.steps.back().explanation += delta;
}

void VariationController::setExplanation(const std::string& text) {
    if (v_.steps.empty()) return;
    v_.steps.back().explanation = text;
}

void VariationController::finishExplanation() {
    if (state_ == VariationState::Explaining) state_ = VariationState::WaitingForConfirm;
}

bool VariationController::confirmNext() {
    if (state_ == VariationState::Finished) return false;
    state_ = VariationState::WaitingForMove;
    return true;
}

bool VariationController::back() {
    if (state_ == VariationState::Finished) return false;
    if (v_.cursor <= 0) return false;
    --v_.cursor;
    state_ = VariationState::WaitingForMove;
    return true;
}

void VariationController::replay() {
    if (state_ == VariationState::Finished) return;
    v_.cursor = 0;
    state_ = VariationState::WaitingForMove;
}

void VariationController::exit() {
    state_ = VariationState::Finished;
}

Color VariationController::toMove() const {
    if (v_.steps.empty()) return v_.to_move;
    return opponent(v_.steps.back().color);
}

const VariationStep* VariationController::currentStep() const {
    if (v_.steps.empty()) return nullptr;
    return &v_.steps.back();
}

}  // namespace vgo
