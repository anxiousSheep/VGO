#pragma once

#include <cstdint>

namespace vgo {

// Stone color. Empty means no stone.
enum class Color : std::uint8_t {
    Empty = 0,
    Black = 1,
    White = 2,
};

// Returns the opponent of a color. Empty stays Empty.
inline Color opponent(Color c) {
    if (c == Color::Black) return Color::White;
    if (c == Color::White) return Color::Black;
    return Color::Empty;
}

// Character used in snapshots / text rendering.
inline char stone_char(Color c) {
    switch (c) {
        case Color::Black: return 'X';
        case Color::White: return 'O';
        default:           return '.';
    }
}

// A board intersection, (x, y) with 0 <= x, y < size.
struct Point {
    int x = 0;
    int y = 0;

    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point& o) const { return !(*this == o); }
};

// A move: a stone placement, a pass, or a resignation.
struct Move {
    Point p;
    bool is_pass = false;
    bool is_resign = false;
};

}  // namespace vgo
