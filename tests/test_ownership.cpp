#include "test_framework.h"

#include <cmath>

#include "ai/ownership.h"

using namespace vgo;

namespace {
// Build an OwnershipMap for `size` with a single value set at (x, y).
OwnershipMap mapWith(int size, int x, int y, double v) {
    OwnershipMap om;
    om.size = size;
    om.value.assign(static_cast<std::size_t>(size) * size, 0.0);
    om.value[static_cast<std::size_t>(y) * size + x] = v;
    return om;
}
}  // namespace

TEST(dead_stones_black_in_white_territory) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(8, 8, Color::White);
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    om.value[0] = -0.95;   // (0,0) strongly white -> black stone there is dead
    om.value[80] = 0.95;   // (8,8) strongly black -> white stone there is dead

    const auto dead = deadStones(b, om, 0.8);
    CHECK_EQ(dead.size(), 2u);
    bool found_00 = false;
    bool found_88 = false;
    for (const Point& p : dead) {
        if (p.x == 0 && p.y == 0) found_00 = true;
        if (p.x == 8 && p.y == 8) found_88 = true;
    }
    CHECK(found_00);
    CHECK(found_88);
}

TEST(dead_stones_below_threshold_not_dead) {
    Board b(9);
    b.set(0, 0, Color::Black);
    const OwnershipMap om = mapWith(9, 0, 0, -0.5);  // below 0.8
    CHECK(deadStones(b, om, 0.8).empty());
}

TEST(dead_stones_same_territory_is_alive) {
    Board b(9);
    b.set(0, 0, Color::Black);
    const OwnershipMap om = mapWith(9, 0, 0, 0.9);  // black stone in black territory
    CHECK(deadStones(b, om, 0.8).empty());
}

TEST(dead_stones_empty_point_ignored) {
    Board b(9);  // no stone at (0,0)
    const OwnershipMap om = mapWith(9, 0, 0, -0.9);
    CHECK(deadStones(b, om, 0.8).empty());
}

TEST(dead_stones_size_mismatch_returns_empty) {
    Board b(9);
    b.set(0, 0, Color::Black);
    OwnershipMap om;
    om.size = 13;
    om.value.assign(13 * 13, 0.0);
    CHECK(deadStones(b, om, 0.8).empty());
}

TEST(ownership_lead_sums_values) {
    OwnershipMap om;
    om.size = 2;
    om.value = {1.0, 1.0, 1.0, 1.0};
    CHECK_EQ(ownershipLead(om), 4.0);

    om.value = {0.5, 0.5, -0.5, -0.5};
    CHECK(std::abs(ownershipLead(om)) < 1e-9);

    om.value = {1.0, -1.0, 1.0, -1.0};
    CHECK(std::abs(ownershipLead(om)) < 1e-9);
}
