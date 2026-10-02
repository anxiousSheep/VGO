#pragma once

#include "go/go_types.h"

namespace vgo {

// What a new game is: placement (human arranges stones, AI only answers),
// human-vs-AI, or review (replay an imported record).
enum class GameMode { Placement, HumanVsAi, Review };

// Parameters produced by the New Game dialog. Immutable once a game starts.
struct GameSetup {
    GameMode mode = GameMode::HumanVsAi;
    int size = 9;
    Color human_color = Color::Black;  // nigiri 时由 UI 先随机得出
    bool nigiri = false;
    int level = 1;  // 1..19，见 strength.h
};

}  // namespace vgo
