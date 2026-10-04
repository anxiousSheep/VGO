#pragma once

#include <vector>

#include "go_types.h"

namespace vgo {

// Standard star-point (hoshi) handicap placements. Returns the first `count`
// stones for a board of `size`, in a fixed symmetric order (so `count` is a
// prefix of `count+1`):
//
//   [0] 右上 (m, c)   [1] 左下 (c, m)   [2] 左上 (c, c)   [3] 右下 (m, m)
//   [4] 天元 (mid,mid) [5] 左 (c,mid)   [6] 右 (m,mid)   [7] 上 (mid,c)  [8] 下 (mid,m)
//
// where c = (size >= 13 ? 3 : 2) is the corner offset (4-4 on 13/19, 3-3 on 9),
// m = size-1-c, and mid = size/2. Coordinates use the internal convention
// (x = column 0..size-1 left-to-right, y = row 0..size-1 top-to-bottom).
//
// `count` is clamped to [0, 9]; counts below 2 (an even game) yield an empty
// list. Out-of-range board sizes yield an empty list.
std::vector<Point> handicapPoints(int size, int count);

}  // namespace vgo
