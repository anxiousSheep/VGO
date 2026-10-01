#include "test_framework.h"

#include "board.h"

using namespace vgo;

TEST(board_starts_empty) {
    Board b(9);
    CHECK_EQ(b.size(), 9);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            CHECK(b.at(x, y) == Color::Empty);
        }
    }
}

TEST(board_place_and_get) {
    Board b(9);
    b.set(2, 3, Color::Black);
    CHECK(b.at(2, 3) == Color::Black);
    CHECK(b.at(3, 2) == Color::Empty);
}

TEST(board_group_connectivity) {
    Board b(9);
    b.set(2, 2, Color::Black);
    b.set(2, 3, Color::Black);
    b.set(3, 2, Color::Black);
    b.set(5, 5, Color::Black);  // isolated stone
    CHECK_EQ(static_cast<int>(b.group(2, 2).size()), 3);
    CHECK_EQ(static_cast<int>(b.group(5, 5).size()), 1);
}

TEST(board_liberties) {
    Board b(9);
    b.set(4, 4, Color::Black);
    CHECK_EQ(static_cast<int>(b.liberties(4, 4).size()), 4);  // center
    b.set(0, 0, Color::Black);
    CHECK_EQ(static_cast<int>(b.liberties(0, 0).size()), 2);  // corner
    b.set(0, 4, Color::Black);
    CHECK_EQ(static_cast<int>(b.liberties(0, 4).size()), 3);  // edge
}

TEST(board_remove_stones) {
    Board b(9);
    b.set(1, 1, Color::White);
    b.remove_stones(b.group(1, 1));
    CHECK(b.at(1, 1) == Color::Empty);
}

TEST(board_snapshot) {
    Board b(3);
    CHECK_EQ(b.snapshot(), std::string("........."));
    b.set(1, 1, Color::Black);
    CHECK_EQ(b.snapshot(), std::string("....X...."));
}
