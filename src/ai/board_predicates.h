#pragma once

#include <string>
#include <vector>

#include "go/board.h"
#include "joseki.h"
#include "symmetry.h"

namespace vgo {

// ---------------------------------------------------------------------------
// 谓词引擎（S0.2，新增）。D1（=P1 前提即代码）的唯一实现点：定式/常型的适用条件是对
// 真实 Board 求值的机器谓词，不是注入 prompt 的自然语言——LLM 永远不做适用性判断。
//
// 谓词种类（封闭集合，不允许扩张成 DSL）：required_stones（指定点指定色）、
// required_empty、forbidden、zone_friendly_majority / zone_enemy_majority（侧翼分区
// 计数）、path_clear、ladder_favorable（旗标）。该引擎同时驱动定式线 L1 与演示 auto
// 分流（§3.4）。
// ---------------------------------------------------------------------------

struct PrecondReport {
    bool satisfied = false;
    std::vector<std::string> missing;   // 缺什么（人读："右上角没有对方星位子"）
    std::vector<Point> occupied_path;   // 路径占用点（人读："R17 已有子"）
};

struct CornerContext {
    int left_friendly = 0, left_enemy = 0;
    int right_friendly = 0, right_enemy = 0;
    bool path_clear = true;
};

// 对目标角 c 求值条目 e 的前提：required_stones 色匹配 + required_empty/forbidden 为空。
// `size` 与棋盘不符 → 诚实降级（satisfied=false + 尺寸说明）。
PrecondReport checkPreconditions(const Board& b, const JosekiEntry& e, Corner c);

// 侧翼分区计数（invader 视角的「己方」= invader）。path_clear 由 required_empty/forbidden
// 的占用决定。
CornerContext classifyContext(const Board& b, const JosekiEntry& e, Corner c, Color invader);

// L3 变体选择：求值每个变体的 `when` 谓词。命中非 default 变体 → 返回其索引集；否则返回
// 首个 default 变体索引。`ladder_favorable`（B1b 落地前）恒不命中。
std::vector<int> selectVariants(const Board& b, const JosekiEntry& e, Corner c, Color invader);

// 方向规则求值：block_toward_friendly_side 且右翼己方占优 → 反向挡（镜像）。其余 → false。
bool directionMirrors(const CornerContext& ctx, const std::string& rule);

// 方向的镜像对称：角部角平分线反射（UR/BL=MirrorAntiDiag，UL/BR=MirrorDiag）。
Symmetry directionSymmetry(Corner c);

// 变体序列投影到目标角并（按方向规则）镜像。
std::vector<JosekiMove> projectVariant(const JosekiVariant& v, int size, Corner c,
                                       const CornerContext& ctx);

// `when` 谓词求值（微型谓词，仅 4 保留字；供 selectVariants 内部与测试使用）。
bool evaluateWhen(const std::string& when, const CornerContext& ctx);

}  // namespace vgo
