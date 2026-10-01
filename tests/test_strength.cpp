#include "test_framework.h"

#include "ai/strength.h"

using namespace vgo;

TEST(strength_clamped_19) {
    CHECK_EQ(strengthForLevel(0).level, 1);
    CHECK_EQ(strengthForLevel(20).level, 19);
    CHECK_EQ(strengthForLevel(-5).level, 1);
}

TEST(strength_monotonic) {
    // Higher levels are less random for the fallback engine, across all 19.
    for (int l = 1; l < 19; ++l) {
        CHECK(strengthForLevel(l + 1).randomness <= strengthForLevel(l).randomness);
    }
}

TEST(strength_human_sl_range) {
    // 新手 + 业余段 use the HumanSL model (rank_*); 职业高段 use the standard model.
    CHECK(strengthForLevel(1).human_profile == "rank_20k");
    CHECK(strengthForLevel(2).human_profile == "rank_1d");
    CHECK(strengthForLevel(10).human_profile == "rank_9d");
    CHECK(strengthForLevel(11).human_profile == "rank_9d");  // 职业1段 = HumanSL 顶档
    CHECK(strengthForLevel(12).human_profile.empty());       // 职业2段 -> 标准模型
    CHECK(strengthForLevel(19).human_profile.empty());
}

TEST(strength_pro_visits_increase) {
    CHECK(strengthForLevel(12).max_visits < strengthForLevel(13).max_visits);
    CHECK(strengthForLevel(15).max_visits < strengthForLevel(16).max_visits);
    CHECK_EQ(strengthForLevel(19).max_visits, 100000);
}

TEST(rank_labels) {
    CHECK(rankLabel(1) == "新手");
    CHECK(rankLabel(2) == "业余1段");
    CHECK(rankLabel(10) == "业余9段");
    CHECK(rankLabel(11) == "职业1段");
    CHECK(rankLabel(19) == "职业9段");
}
