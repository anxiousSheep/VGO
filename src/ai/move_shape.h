#pragma once

#include <string>

#include "go/board.h"

namespace vgo {

// ---------------------------------------------------------------------------
// G1+G2 着法几何 + 空间事实（B1a 真正补全）。纯逻辑、Qt-free，进 CTest。
//
// 与既有 explain.cpp 的 extractBoardFacts（B1a「已落子相对己方子」的主形状）正交：
// 这里新增「候选点相对最近异色子」的轴——表达「R17 相对黑 Q16 对角相邻，点入三三」，
// 供「我该下哪」推荐点与术语闸门使用。不复写 explain.cpp 的几何原语，只重写 5 行
// Chebyshev/邻接判断。
// ---------------------------------------------------------------------------

// 候选着相对「最近异色子」的几何关系（D1/G1）。
enum class MoveShape {
    Contact,    // 贴（扳/挡/顶）：正交相邻
    Diagonal,   // 尖/觑：对角相邻
    OneGap,     // 隔一路（跳/飞/挂/点）：Chebyshev 距离 2
    TwoGap,     // 隔二路（大跳/拆二）：Chebyshev 距离 3
    Extension,  // 同线延伸（长/爬）：沿同一直线远离参照子
    Other,      // 距离较远 / 无参照子
};

struct ShapeFact {
    MoveShape shape = MoveShape::Other;
    std::string term;      // 具体术语："贴" / "尖" / "点" / "飞" / "跳" / "长" …
    std::string relation;  // 人读："R17 与白 Q16 对角相邻，属点入三三"
};

// 对候选着 p（由 mover 下）相对最近异色子分类，产出术语与人读关系。无异色子时返回
// MoveShape::Other + 普通落子描述。
ShapeFact classifyMove(const Board& b, Point p, Color mover);

// G2 空间事实：点 p 的区域 / 线数 / 与最近星位（或天元）的方位。
struct PointGeo {
    std::string region;   // "右上角" / "上边" / "中央" …
    int line = 0;         // 第几线（1 = 一线）
    std::string nearby;   // "距星位 Q16 一路" / "即天元"，空 = 远离星位
};

PointGeo describePoint(const Board& b, Point p);

}  // namespace vgo
