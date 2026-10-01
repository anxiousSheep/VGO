#pragma once

#include <string>

namespace vgo {

// Move-selection parameters for one strength level. KataGo fields are used by
// the KataGo engine; `randomness` drives the built-in fallback engine.
struct StrengthParams {
    int level = 5;                 // 1..10
    std::string human_profile;     // KataGo humanSLProfile, e.g. "preaz_10k"
    double temperature = 1.0;      // KataGo temperature
    int max_visits = 1;            // KataGo search budget
    double randomness = 0.6;       // fallback engine: 0 = greedy, 1 = random
};

// Clamps level to [1,10] and returns the corresponding parameters.
StrengthParams strengthForLevel(int level);

// Short Chinese label for a level, e.g. "中级 (5)".
std::string strengthLabel(int level);

}  // namespace vgo
