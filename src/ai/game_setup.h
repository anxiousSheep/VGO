#pragma once

#include "go/go_types.h"

namespace vgo {

// What a new game is: placement (human arranges stones, AI only answers),
// human-vs-AI, review (replay an imported record), or the daily puzzle
// (re-solve one of the learner's own blunders, D3.5「每日一题」).
enum class GameMode { Placement, HumanVsAi, Review, DailyPuzzle };

// Parameters produced by the New Game dialog. Immutable once a game starts.
struct GameSetup {
    GameMode mode = GameMode::HumanVsAi;
    int size = 9;
    Color human_color = Color::Black;  // nigiri 时由 UI 先随机得出
    bool nigiri = false;
    int level = 1;  // 1..19，见 strength.h
    double kyu_hint = 20.0;  // D3.4 新手难度爬坡：连续强度（20=20k 新手，0=1d），仅 level==1 生效
    int handicap = 0;  // 让子数（2..9；0 = 平手局）。仅执黑有效，摆子为黑、白先行。
};

}  // namespace vgo
