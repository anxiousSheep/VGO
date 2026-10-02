#include "game.h"

#include <array>
#include <queue>
#include <utility>

namespace vgo {

namespace {
constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
}  // namespace

Game::Game(int size, double komi) : komi_(komi) {
    State s;
    s.board = Board(size);
    s.snapshot = s.board.snapshot();
    history_.push_back(std::move(s));
    history_.reserve(static_cast<std::size_t>(size) * size * 2 + 16);
}

Game::Game(const Board& board, Color to_move, double komi) : komi_(komi) {
    State s;
    s.board = board;
    s.snapshot = board.snapshot();
    s.current = to_move;
    history_.push_back(std::move(s));
    history_.reserve(static_cast<std::size_t>(board.size()) * board.size() * 2 + 16);
}

bool Game::legal_result(int x, int y, std::string* snap_out) const {
    const State& st = history_.back();
    if (st.is_over) return false;
    if (!st.board.in_bounds(x, y)) return false;
    if (!st.board.is_empty(x, y)) return false;

    Board b = st.board;
    const Color me = st.current;
    const Color opp = opponent(me);
    b.set(x, y, me);

    // Remove opponent groups left without liberties.
    for (const auto [dx, dy] : kDirs) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (b.in_bounds(nx, ny) && b.at(nx, ny) == opp && b.liberties(nx, ny).empty()) {
            b.remove_stones(b.group(nx, ny));
        }
    }

    // Suicide: the placed stone's group must have a liberty after captures.
    if (b.liberties(x, y).empty()) return false;

    // Positional superko: the resulting position must not have occurred before.
    const std::string snap = b.snapshot();
    for (const State& h : history_) {
        if (h.snapshot == snap) return false;
    }

    if (snap_out) *snap_out = snap;
    return true;
}

bool Game::is_legal(int x, int y) const {
    return legal_result(x, y, nullptr);
}

bool Game::play(int x, int y) {
    std::string snap;
    if (!legal_result(x, y, &snap)) return false;

    const State& st = history_.back();
    State next = st;
    const Color me = st.current;
    const Color opp = opponent(me);

    next.board.set(x, y, me);
    int captured = 0;
    for (const auto [dx, dy] : kDirs) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (next.board.in_bounds(nx, ny) && next.board.at(nx, ny) == opp &&
            next.board.liberties(nx, ny).empty()) {
            const std::vector<Point> group = next.board.group(nx, ny);
            captured += static_cast<int>(group.size());
            next.board.remove_stones(group);
        }
    }

    next.captures[static_cast<int>(me)] += captured;
    next.current = opp;
    ++next.move_number;
    next.consecutive_passes = 0;
    next.has_last_move = true;
    next.last_move = {x, y};
    next.snapshot = snap;
    history_.push_back(std::move(next));
    return true;
}

void Game::pass() {
    const State& st = history_.back();
    if (st.is_over) return;

    State next = st;
    ++next.consecutive_passes;
    ++next.move_number;
    next.current = opponent(next.current);
    next.has_last_move = false;
    if (next.consecutive_passes >= 2) next.is_over = true;
    // Board is unchanged by a pass, so the snapshot stays the same.
    next.snapshot = st.snapshot;
    history_.push_back(std::move(next));
}

void Game::settle() {
    history_.back().is_over = true;
}

bool Game::undo() {
    if (history_.size() <= 1) return false;
    history_.pop_back();
    return true;
}

std::vector<std::pair<Color, Move>> Game::moves() const {
    std::vector<std::pair<Color, Move>> out;
    out.reserve(history_.size());
    for (std::size_t i = 1; i < history_.size(); ++i) {
        const State& s = history_[i];
        const Color mover = opponent(s.current);  // current was advanced past the mover
        if (s.has_last_move) {
            out.emplace_back(mover, Move{s.last_move, false});
        } else {
            out.emplace_back(mover, Move{{-1, -1}, true});  // pass
        }
    }
    return out;
}

std::vector<Point> Game::legal_moves() const {
    std::vector<Point> moves;
    const int n = size();
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (is_legal(x, y)) moves.push_back({x, y});
        }
    }
    return moves;
}

std::pair<int, int> Game::score_area() const {
    const Board& b = board();
    const int n = b.size();
    int black = 0;
    int white = 0;

    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (b.at(x, y) == Color::Black) ++black;
            else if (b.at(x, y) == Color::White) ++white;
        }
    }

    // Flood-fill empty regions; a region fully enclosed by one color is that
    // color's territory. Regions touching both colors are neutral (dame/seki).
    std::vector<bool> visited(static_cast<std::size_t>(n) * n, false);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (b.at(x, y) != Color::Empty || visited[b.index(x, y)]) continue;

            std::queue<Point> queue;
            queue.push({x, y});
            visited[b.index(x, y)] = true;
            bool touches_black = false;
            bool touches_white = false;
            int region = 0;

            while (!queue.empty()) {
                const Point p = queue.front();
                queue.pop();
                ++region;
                for (const auto [dx, dy] : kDirs) {
                    const int nx = p.x + dx;
                    const int ny = p.y + dy;
                    if (!b.in_bounds(nx, ny)) continue;
                    const Color c = b.at(nx, ny);
                    if (c == Color::Black) touches_black = true;
                    else if (c == Color::White) touches_white = true;
                    else if (!visited[b.index(nx, ny)]) {
                        visited[b.index(nx, ny)] = true;
                        queue.push({nx, ny});
                    }
                }
            }

            if (touches_black && !touches_white) black += region;
            else if (touches_white && !touches_black) white += region;
        }
    }

    return {black, white};
}

double Game::score_black() const {
    const auto [black, white] = score_area();
    return static_cast<double>(black) - static_cast<double>(white) - komi_;
}

Color Game::winner() const {
    const double d = score_black();
    if (d > 0.0) return Color::Black;
    if (d < 0.0) return Color::White;
    return Color::Empty;
}

}  // namespace vgo
