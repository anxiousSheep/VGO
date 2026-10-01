#include "fallback_engine.h"

#include <algorithm>
#include <array>
#include <utility>

#include "go/game.h"

namespace vgo {

namespace {

constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Heuristic score for a legal move (higher is better). Uses a Game copy.
double scoreMove(const Board& board, Color me, const Point& p) {
    Game sim(board, me);
    if (!sim.play(p.x, p.y)) return -1e9;  // should not happen (already legal)
    double score = 0.0;
    score += sim.captures(me) * 10.0;
    const int myLibs = static_cast<int>(sim.board().liberties(p.x, p.y).size());
    if (myLibs == 1) score -= 5.0;  // self-atari
    const Color opp = opponent(me);
    for (const auto [dx, dy] : kDirs) {
        const int nx = p.x + dx;
        const int ny = p.y + dy;
        if (sim.board().in_bounds(nx, ny) && sim.board().at(nx, ny) == opp &&
            sim.board().liberties(nx, ny).size() == 1) {
            score += 4.0;  // puts opponent in atari
        }
    }
    return score;
}

}  // namespace

FallbackEngine::FallbackEngine(unsigned seed) : state_(seed) {}

void FallbackEngine::requestMove(const Board& board, Color to_move,
                                 const StrengthParams& params, Callback cb) {
    Game probe(board, to_move);
    const auto legal = probe.legal_moves();

    EngineResult r;
    if (legal.empty()) {
        r.move.is_pass = true;
    } else {
        // Simple LCG for reproducibility.
        state_ = state_ * 1103515245u + 12345u;
        const unsigned roll = state_;
        const double randomness = std::clamp(params.randomness, 0.0, 1.0);
        const bool random =
            (roll % 1000u) < static_cast<unsigned>(randomness * 1000.0);
        if (random) {
            r.move = {legal[static_cast<std::size_t>((roll >> 8) % legal.size())], false};
        } else {
            std::vector<std::pair<Point, double>> scored;
            scored.reserve(legal.size());
            for (const Point& p : legal) scored.emplace_back(p, scoreMove(board, to_move, p));
            const auto best = std::max_element(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            r.move = {best->first, false};
        }
    }

    // The fallback engine provides no positional analysis.
    cb(r);
}

}  // namespace vgo
