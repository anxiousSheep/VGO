#include "board.h"

#include <array>
#include <queue>
#include <utility>

namespace vgo {

namespace {
constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
}  // namespace

Board::Board(int size)
    : size_(size), grid_(static_cast<std::size_t>(size) * size, Color::Empty) {}

std::vector<Point> Board::group(int x, int y) const {
    std::vector<Point> result;
    if (!in_bounds(x, y) || at(x, y) == Color::Empty) return result;

    const Color c = at(x, y);
    std::vector<bool> visited(static_cast<std::size_t>(size_) * size_, false);
    std::queue<Point> queue;
    queue.push({x, y});
    visited[index(x, y)] = true;

    while (!queue.empty()) {
        Point p = queue.front();
        queue.pop();
        result.push_back(p);
        for (const auto [dx, dy] : kDirs) {
            const int nx = p.x + dx;
            const int ny = p.y + dy;
            if (in_bounds(nx, ny) && !visited[index(nx, ny)] && at(nx, ny) == c) {
                visited[index(nx, ny)] = true;
                queue.push({nx, ny});
            }
        }
    }
    return result;
}

std::vector<Point> Board::liberties(int x, int y) const {
    std::vector<Point> libs;
    if (!in_bounds(x, y) || at(x, y) == Color::Empty) return libs;

    const std::vector<Point> stones = group(x, y);
    std::vector<bool> seen(static_cast<std::size_t>(size_) * size_, false);
    for (const Point& p : stones) {
        for (const auto [dx, dy] : kDirs) {
            const int nx = p.x + dx;
            const int ny = p.y + dy;
            if (in_bounds(nx, ny) && at(nx, ny) == Color::Empty && !seen[index(nx, ny)]) {
                seen[index(nx, ny)] = true;
                libs.push_back({nx, ny});
            }
        }
    }
    return libs;
}

void Board::remove_stones(const std::vector<Point>& stones) {
    for (const Point& p : stones) {
        set(p.x, p.y, Color::Empty);
    }
}

std::string Board::snapshot() const {
    std::string s;
    s.reserve(static_cast<std::size_t>(size_) * size_);
    for (const Color c : grid_) {
        s.push_back(stone_char(c));
    }
    return s;
}

}  // namespace vgo
