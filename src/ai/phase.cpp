#include "phase.h"

#include <algorithm>
#include <cmath>

#include "gtp.h"

namespace vgo {

GamePhase classifyPhase(int move_number, int size, const OwnershipMap& ownership) {
    const int fuseki_threshold = 2 * size;
    const int endgame_move_threshold = size * size / 2;

    if (!ownership.empty()) {
        // 主信号：领地大面积定型（|ownership| > 0.8 的点占比 > 0.55）。
        int total = 0;
        int solidified = 0;
        for (double v : ownership.value) {
            ++total;
            if (std::abs(v) > 0.8) ++solidified;
        }
        if (total > 0 &&
            static_cast<double>(solidified) / static_cast<double>(total) > 0.55)
            return GamePhase::Endgame;
    }

    if (move_number < fuseki_threshold) return GamePhase::Fuseki;
    if (move_number > endgame_move_threshold) return GamePhase::Endgame;
    return GamePhase::MiddleGame;
}

const char* phaseLabel(GamePhase p) {
    switch (p) {
        case GamePhase::Fuseki:     return "布局";
        case GamePhase::MiddleGame: return "中盘";
        case GamePhase::Endgame:    return "官子";
    }
    return "中盘";
}

std::string buildMoveSequence(const std::vector<std::pair<Color, Move>>& moves, int size,
                              int max_moves) {
    if (moves.empty() || max_moves <= 0) return {};
    const int start = std::max(0, static_cast<int>(moves.size()) - max_moves);
    std::string out;
    for (int i = start; i < static_cast<int>(moves.size()); ++i) {
        if (i > start) out += "、";
        out += (moves[i].first == Color::Black) ? "黑 " : "白 ";
        if (moves[i].second.is_pass) out += "虚着";
        else out += gtpPoint(moves[i].second.p, size);
    }
    return out;
}

}  // namespace vgo
