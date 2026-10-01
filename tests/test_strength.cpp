#include "test_framework.h"

#include "ai/strength.h"

using namespace vgo;

TEST(strength_clamped) {
    CHECK_EQ(strengthForLevel(0).level, 1);
    CHECK_EQ(strengthForLevel(11).level, 10);
    CHECK_EQ(strengthForLevel(-5).level, 1);
}

TEST(strength_monotonic) {
    // Higher levels are less random for the fallback engine.
    CHECK(strengthForLevel(10).randomness < strengthForLevel(1).randomness);
    CHECK(strengthForLevel(5).randomness < strengthForLevel(2).randomness);
}

TEST(strength_profile) {
    CHECK(!strengthForLevel(3).human_profile.empty());
    CHECK(!strengthLabel(5).empty());
}
