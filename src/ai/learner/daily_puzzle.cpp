#include "daily_puzzle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace vgo {

DailyVerdict judgeDailyMove(double lead_new, double lead_blunder, double lead_before) {
    DailyVerdict v;
    v.improvement = lead_new - lead_blunder;
    const double max_improve = lead_before - lead_blunder;
    if (v.improvement <= 0.0 || max_improve <= 0.0) {
        v.passed = false;
        v.score = 0;
        return v;
    }
    v.passed = true;
    const double ratio = v.improvement / max_improve;
    const long raw = std::lround(60.0 + 40.0 * ratio);
    v.score = static_cast<int>(std::clamp(raw, 60L, 100L));
    return v;
}

bool boardFromSnapshot(const std::string& snap, int size, Board* out) {
    if (!out || size <= 0) return false;
    const std::size_t expect = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (snap.size() != expect) return false;
    Board b(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const char c = snap[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                                 static_cast<std::size_t>(x)];
            if (c == 'X')
                b.set(x, y, Color::Black);
            else if (c == 'O')
                b.set(x, y, Color::White);
            else
                b.set(x, y, Color::Empty);  // '.' 或未知字符按空处理
        }
    }
    *out = b;
    return true;
}

std::string snapshotHash(const std::string& snap) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV offset basis
    for (unsigned char c : snap) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV prime
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

}  // namespace vgo
