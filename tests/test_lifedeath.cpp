#include "test_framework.h"

#include "ai/lifedeath.h"

using namespace vgo;

namespace {
OwnershipMap mapWith(int size, int x, int y, double v) {
    OwnershipMap om;
    om.size = size;
    om.value.assign(static_cast<std::size_t>(size) * size, 0.0);
    om.value[static_cast<std::size_t>(y) * size + x] = v;
    return om;
}
}  // namespace

TEST(dead_groups_connected_components) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(1, 0, Color::Black);  // connected to (0,0)
    b.set(8, 8, Color::White);  // separate group
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    om.value[0] = -0.95;   // black dead
    om.value[1] = -0.95;   // black dead
    om.value[80] = 0.95;   // white dead

    const auto groups = deadGroups(b, om, 0.8);
    CHECK_EQ(groups.size(), 2u);
    // one group has size 2 (black), the other size 1 (white)
    bool has2 = false, has1 = false;
    for (const auto& g : groups) {
        if (g.size() == 2) has2 = true;
        if (g.size() == 1) has1 = true;
    }
    CHECK(has2);
    CHECK(has1);
}

TEST(saving_candidates_are_liberties) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(1, 0, Color::Black);
    const auto cands = savingCandidates(b, b.group(0, 0));
    // liberties of {(0,0),(1,0)} are (0,1),(2,0),(1,1)
    CHECK_EQ(cands.size(), 3u);
}

TEST(analyze_life_death_no_dead) {
    Board b(9);
    b.set(0, 0, Color::Black);
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    om.value[0] = 0.9;  // black stone in black territory => alive
    const LifeDeathResult r = analyzeLifeDeath(b, om, 0.8);
    CHECK(r.alive);
    CHECK(r.group.empty());
}

TEST(analyze_life_death_dead_group) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(1, 0, Color::Black);
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    om.value[0] = -0.95;
    om.value[1] = -0.95;
    const LifeDeathResult r = analyzeLifeDeath(b, om, 0.8);
    CHECK(!r.alive);
    CHECK_EQ(r.group.size(), 2u);
    CHECK_EQ(r.saving_moves.size(), 3u);
    CHECK(!r.reason.empty());
}
