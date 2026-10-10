#pragma once

#include <string>
#include <vector>

#include "go/go_types.h"

namespace vgo {

// 正方形的 8 个对称（二面体群 D4）。内点坐标 (x,y) 0-indexed，x 列（左→右）、
// y 行（上→下），`size = N`。纯几何、Qt-free，供定式投影 / 常型规范化共用（S0.1）。
enum class Symmetry {
    Id, Rot90, Rot180, Rot270,          // 旋转（顺时针）
    MirrorX, MirrorY, MirrorDiag, MirrorAntiDiag   // 翻转
};
inline constexpr int kSymmetryCount = 8;

// 对单个点施加对称变换（在 size×size 的方形区域内）。
Point applySymmetry(Symmetry s, Point p, int size);

// 对称的逆（D4 群内每个元素自逆或互为逆）。
Symmetry inverseSymmetry(Symmetry s);

// 角（右上 UR 为规范化基准）→ 对称变换。canonical 定式/常型统一存 UR 帧。
// UR→UR: Id；UR→UL: MirrorX；UR→BL: Rot180；UR→BR: MirrorY。
enum class Corner { UR, UL, BL, BR };
Symmetry cornerSymmetry(Corner target);

// 点集平移归一：平移到 (min_x, min_y) 为原点，再在 8 对称中取字典序最小者。
// 返回 canonical 点集 + 所用对称（供「要点反投影回棋盘」）。
struct CanonicalShape {
    std::vector<Point> points;   // 规范化后的点集（已按 (x,y) 字典序排序）
    Symmetry used = Symmetry::Id;  // 输入→canonical 所用对称
    int dx = 0, dy = 0;            // 输入→原点 的平移量
    int n = 0;                     // 对称所用的方形尺寸（= 包围盒最长边）
    int post_dx = 0, post_dy = 0;  // 对称后再次平移到原点的量
};
CanonicalShape canonicalize(const std::vector<Point>& pts);

// 稳定字符串键（表查找用）："x,y;x,y;..."（输入须已排序）。
std::string shapeKey(const std::vector<Point>& canonical_pts);

// 把 canonical 点反投影回真实棋盘（canonicalize 的逆变换，供常型要点反投影）。
Point canonicalToBoard(const CanonicalShape& cs, Point canonical);

// 把真实棋盘点投影到 canonical 帧（canonicalize 的正向变换，供常型 vital 与 pattern
// 同帧规范化时使用）。
Point boardToCanonical(const CanonicalShape& cs, Point board);

}  // namespace vgo
