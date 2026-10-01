#pragma once

#include <string>

namespace vgo {

// Move-selection parameters for one strength level (1..19). HumanSL levels
// (新手 + 业余段) drive a human-style model via `human_profile`; pro levels
// drive the standard model via an increasing search budget (`max_visits`).
// `randomness` drives the built-in fallback engine.
struct StrengthParams {
    int level = 1;                 // 1..19
    std::string human_profile;     // HumanSL rank, e.g. "rank_1d"; empty => standard model
    double temperature = 1.0;      // KataGo temperature
    int max_visits = 1;            // KataGo search budget (standard model)
    double randomness = 0.95;      // fallback engine: 0 = greedy, 1 = random
};

// Clamps level to [1,19] and returns the corresponding parameters.
StrengthParams strengthForLevel(int level);

// Short Chinese rank label for a level, e.g. "新手", "业余3段", "职业1段".
std::string rankLabel(int level);

// 形势判断（analysis）固定使用满血强度：职业9段/超人类（标准模型 + max_visits=100000），
// 不随对局难度变化，保证形势判断的正确性。
constexpr int kAnalysisLevel = 19;

}  // namespace vgo
