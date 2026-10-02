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

// 形势判断 / 教学演示强度档位，对应对局难度之外的职业段 visits（标准模型）。
// 形势判断与棋盘演示的「权威 AI 后续」共用此档位；用户可在设置中选择，默认「一般」= 职业5段。
constexpr int kAnalysisLevelNormal = 15;    // 职业5段：300 visits（一般）
constexpr int kAnalysisLevelStronger = 17;  // 职业7段：3000 visits（更强）
constexpr int kAnalysisLevelFull = 19;      // 职业9段：100000 visits（完全正确，超人类）

}  // namespace vgo
