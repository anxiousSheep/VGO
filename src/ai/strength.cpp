#include "strength.h"

#include <algorithm>

namespace vgo {

StrengthParams strengthForLevel(int level) {
    level = std::max(1, std::min(10, level));
    StrengthParams p;
    p.level = level;

    // KataGo human-style calibration (initial values; to be tuned against the
    // actual engine in M0). `randomness` drives the fallback engine.
    struct Row { const char* profile; double temp; int visits; double rand; };
    static const Row kRows[10] = {
        {"preaz_30k", 1.20, 1, 0.95},
        {"preaz_20k", 1.20, 1, 0.90},
        {"preaz_15k", 1.10, 1, 0.80},
        {"preaz_10k", 1.10, 1, 0.70},
        {"preaz_5k",  1.00, 1, 0.55},
        {"preaz_1k",  1.00, 1, 0.40},
        {"preaz_1d",  0.90, 1, 0.30},
        {"preaz_3d",  0.90, 1, 0.20},
        {"preaz_5d",  0.85, 1, 0.12},
        {"preaz_9d",  0.80, 1, 0.05},
    };
    const Row& r = kRows[level - 1];
    p.human_profile = r.profile;
    p.temperature = r.temp;
    p.max_visits = r.visits;
    p.randomness = r.rand;
    return p;
}

std::string strengthLabel(int level) {
    level = std::max(1, std::min(10, level));
    static const char* kNames[10] = {
        "入门 (1)", "入门 (2)", "初级 (3)", "初级 (4)", "中级 (5)",
        "中级 (6)", "高级 (7)", "高级 (8)", "高手 (9)", "顶尖 (10)",
    };
    return kNames[level - 1];
}

}  // namespace vgo
