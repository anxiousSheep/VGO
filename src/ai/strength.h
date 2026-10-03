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

// D3.4 新手难度动态爬坡：带连续强度提示的 strengthForLevel。`level == 1`（新手）
// 时据 `kyu_hint`（20 = 20k 新手，0 = 1d）在细粒度 HumanSL 阶梯上选档，其余档位
// 忽略 kyu_hint。默认 kyu_hint = 20 与旧行为一致（新手 = rank_20k）。
StrengthParams strengthForLevel(int level, double kyu_hint);

// 连续 kyu 估计（20 = 20k 新手，<= 0 = 1d）→ 细粒度 HumanSL rank，形成新手爬坡
// 阶梯 rank_20k → 15k → 10k → 7k → 5k → 3k → 1k → 1d。1d 及以上由 19 级表承担。
std::string humanProfileForStrength(double kyu);

// Short Chinese rank label for a level, e.g. "新手", "业余3段", "职业1段".
std::string rankLabel(int level);

// 形势判断 / 教学演示强度档位，对应对局难度之外的职业段 visits（标准模型）。
// 形势判断与棋盘演示的「权威 AI 后续」共用此档位；用户可在设置中选择，默认「一般」= 职业5段。
constexpr int kAnalysisLevelNormal = 15;    // 职业5段：300 visits（一般）
constexpr int kAnalysisLevelStronger = 17;  // 职业7段：3000 visits（更强）
constexpr int kAnalysisLevelFull = 19;      // 职业9段：100000 visits（完全正确，超人类）

}  // namespace vgo
