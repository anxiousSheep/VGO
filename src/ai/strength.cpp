#include "strength.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vgo {

namespace {

// One row of the 19-level difficulty table (see docs/archive/技术路线-选型与任务计划.md
// §2). 新手+业余段 use the HumanSL model (`rank_*` profiles); 职业段 use the
// standard model with an increasing search budget. `randomness` keeps the
// fallback engine monotonic too.
struct Row {
    int level;
    const char* human_profile;  // empty => standard model
    double temperature;
    int max_visits;
    double randomness;
};

const Row& rowFor(int level) {
    static const Row kRows[19] = {
        // 新手 + 业余段（HumanSL 模型）
        {1,  "rank_20k", 1.20, 1,        0.95},
        {2,  "rank_1d",  1.05, 1,        0.85},
        {3,  "rank_1d",  1.00, 1,        0.80},
        {4,  "rank_3d",  0.95, 1,        0.72},
        {5,  "rank_3d",  0.90, 1,        0.65},
        {6,  "rank_5d",  0.85, 1,        0.55},
        {7,  "rank_5d",  0.82, 1,        0.48},
        {8,  "rank_7d",  0.80, 1,        0.40},
        {9,  "rank_7d",  0.78, 1,        0.32},
        {10, "rank_9d",  0.75, 1,        0.25},
        // 职业段（标准模型 + 递增 visits）
        {11, "rank_9d",  0.70, 1,        0.18},
        {12, "",         0.50, 8,        0.13},
        {13, "",         0.45, 30,       0.09},
        {14, "",         0.40, 100,      0.06},
        {15, "",         0.35, 300,      0.045},
        {16, "",         0.30, 1000,     0.03},
        {17, "",         0.25, 3000,     0.02},
        {18, "",         0.20, 10000,    0.01},
        {19, "",         0.15, 100000,   0.005},
    };
    return kRows[level - 1];
}

}  // namespace

StrengthParams strengthForLevel(int level) {
    return strengthForLevel(level, 20.0);
}

StrengthParams strengthForLevel(int level, double kyu_hint) {
    level = std::max(1, std::min(19, level));
    const Row& r = rowFor(level);
    StrengthParams p;
    p.level = level;
    p.human_profile = r.human_profile;
    if (level == 1) {
        // D3.4 新手难度动态爬坡：据连续 kyu 估计在细粒度阶梯上选档，取代固定 20k。
        p.human_profile = humanProfileForStrength(kyu_hint);
    }
    p.temperature = r.temperature;
    p.max_visits = r.max_visits;
    p.randomness = r.randomness;
    return p;
}

std::string humanProfileForStrength(double kyu) {
    // 新手带阶梯：20k → 15k → 10k → 7k → 5k → 3k → 1k → 1d。
    // 1d 及以上（kyu <= 0）由 19 级表的业余/职业段承担，本函数只覆盖新手带。
    if (kyu <= 0.0) return "rank_1d";
    if (kyu >= 20.0) return "rank_20k";
    static const struct Rung { double kyu; const char* profile; } kLadder[] = {
        {20.0, "rank_20k"}, {15.0, "rank_15k"}, {10.0, "rank_10k"},
        {7.0, "rank_7k"},   {5.0, "rank_5k"},   {3.0, "rank_3k"},
        {1.0, "rank_1k"},   {0.0, "rank_1d"},
    };
    const char* best = "rank_20k";
    double best_d = 1e18;
    for (const Rung& r : kLadder) {
        const double d = std::abs(kyu - r.kyu);
        if (d < best_d) {
            best_d = d;
            best = r.profile;
        }
    }
    return best;
}

std::string rankLabel(int level) {
    level = std::max(1, std::min(19, level));
    if (level == 1) return "新手";
    if (level <= 10) return std::string("业余") + std::to_string(level - 1) + "段";
    return std::string("职业") + std::to_string(level - 10) + "段";
}

}  // namespace vgo
