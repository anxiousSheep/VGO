#include "handicap.h"

#include <algorithm>

namespace vgo {

std::vector<Point> handicapPoints(int size, int count) {
    // Even games (count < 2) produce no stones; larger counts clamp to 9.
    // Handicap stones only make sense on the standard square boards we offer.
    if (count < 2) return {};
    if (size < 7 || size > 25) return {};

    const int c = (size >= 13) ? 3 : 2;  // 4-4 on 13/19, 3-3 on smaller boards
    const int m = size - 1 - c;
    const int mid = size / 2;

    // Fixed symmetric order (see handicap.h). Every entry is a prefix of a
    // larger count, so the layout is stable as the handicap grows.
    const std::vector<Point> order = {
        {m, c},          // 右上
        {c, m},          // 左下
        {c, c},          // 左上
        {m, m},          // 右下
        {mid, mid},      // 天元
        {c, mid},        // 左
        {m, mid},        // 右
        {mid, c},        // 上
        {mid, m},        // 下
    };

    const int n = std::min(count, static_cast<int>(order.size()));
    return std::vector<Point>(order.begin(), order.begin() + n);
}

}  // namespace vgo
