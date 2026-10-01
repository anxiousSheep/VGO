#pragma once

#include <string>
#include <vector>

#include "go_types.h"

namespace vgo {

// A square Go board. Pure data structure and geometric queries; it does not
// enforce any rule by itself (rules live in Game).
class Board {
public:
    explicit Board(int size = 9);

    int size() const { return size_; }
    int index(int x, int y) const { return y * size_ + x; }

    bool in_bounds(int x, int y) const {
        return x >= 0 && x < size_ && y >= 0 && y < size_;
    }

    // Returns Empty for out-of-bounds positions, which simplifies queries.
    Color at(int x, int y) const {
        return in_bounds(x, y) ? grid_[index(x, y)] : Color::Empty;
    }

    void set(int x, int y, Color c) { grid_[index(x, y)] = c; }
    bool is_empty(int x, int y) const { return at(x, y) == Color::Empty; }

    // Connected group of same-colored stones containing (x, y). (x, y) must
    // hold a stone; otherwise an empty vector is returned.
    std::vector<Point> group(int x, int y) const;

    // Distinct empty intersections adjacent to the group containing (x, y).
    std::vector<Point> liberties(int x, int y) const;

    // Removes the given stones (sets them to Empty).
    void remove_stones(const std::vector<Point>& stones);

    // Canonical string form of the board, used for superko comparison.
    std::string snapshot() const;

private:
    int size_;
    std::vector<Color> grid_;
};

}  // namespace vgo
