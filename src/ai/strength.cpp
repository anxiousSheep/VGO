#include "strength.h"

#include <algorithm>
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
    level = std::max(1, std::min(19, level));
    const Row& r = rowFor(level);
    StrengthParams p;
    p.level = level;
    p.human_profile = r.human_profile;
    p.temperature = r.temperature;
    p.max_visits = r.max_visits;
    p.randomness = r.randomness;
    return p;
}

std::string rankLabel(int level) {
    level = std::max(1, std::min(19, level));
    if (level == 1) return "新手";
    if (level <= 10) return std::string("业余") + std::to_string(level - 1) + "段";
    return std::string("职业") + std::to_string(level - 10) + "段";
}

}  // namespace vgo
