#pragma once

#include <string>
#include <vector>

#include "explain.h"
#include "go/board.h"

namespace vgo {

// ---------------------------------------------------------------------------
// L1 眼位提取与假眼识别（纯逻辑，Qt-free）。
// ---------------------------------------------------------------------------

// 目标棋块 G 的一个眼位区域：从 G 的气出发 flood-fill 空区，边界 ≥90% 为 G 色（含
// 棋盘边）即视为眼位；混入对方子 → contaminated。
struct EyeRegion {
    std::vector<Point> points;
    bool contaminated = false;
};
std::vector<EyeRegion> eyeRegions(const Board& b, const std::vector<Point>& group);

// 单点眼真假（对角规则）：中央 ≥3/4、边上 ≥2/2、角上 ≥1/1；对角对方子可被立即提掉者
// 按己方计。p 须为空点。
bool isTrueEye(const Board& b, const Point& p, Color owner);

// 直判活（无需引擎）：≥2 个单子真眼，或眼位匹配为活形（直四/曲四/板六）。
bool isPatternAlive(const Board& b, const std::vector<Point>& group);

// ---------------------------------------------------------------------------
// L2 常型库（双源：builtin + JSON 热更）。条目存 canonical 帧。
// ---------------------------------------------------------------------------

enum class ShapeStatus { Alive, Dead, Unsettled };  // Unsettled = 先手死

struct LifeShape {
    std::string id, name, note;
    ShapeStatus status = ShapeStatus::Unsettled;
    std::vector<Point> pattern;  // canonical 帧（平移至原点、坐标非负）
    std::vector<Point> vital;    // canonical 帧要点（nakade 类：杀=活=同点）
};

class LifeShapeLibrary {
public:
    static LifeShapeLibrary builtin();

    // 解析单条常型 JSON；内部对 pattern 做防御性 canonicalize() 再入库，vital 同帧变换。
    bool fromJsonText(const std::string& text, std::string* err);
    // 入库（对 pattern 做防御性 canonicalize，vital 同步变换）。
    bool upsert(const LifeShape& shape);

    struct Match {
        const LifeShape* shape = nullptr;
        std::vector<Point> vital_board;  // 反投影到真实棋盘的要点
    };
    // 眼位点集 → canonical → 查表；命中返回条目 + 反投影要点。
    Match match(const std::vector<Point>& eye_space) const;

    std::size_t size() const { return shapes_.size(); }
    const std::vector<LifeShape>& all() const { return shapes_; }

private:
    std::vector<LifeShape> shapes_;
};

// ---------------------------------------------------------------------------
// L3 要点候选（L1+L2+L3 打包，无引擎、可单测）。
// ---------------------------------------------------------------------------

struct ShapeAnalysis {
    std::string shape;                     // 常型名，空 = 未识别
    LDStatus status = LDStatus::Unknown;   // 常型直判或 Unknown
    std::vector<Point> eye_space;
    std::vector<Point> kill_points;        // ≤3
    std::vector<Point> live_points;        // ≤3
};

// 对给定棋块做常型识别 + 要点候选。要点来源：常型 vital（直接反投影）→ 分割势最大点
// → 区域形心 → 角部二·1；输出 ≤3 候选。
ShapeAnalysis analyzeShape(const Board& b, const std::vector<Point>& group);

// 把 ShapeStatus 映射为 LDStatus（Alive/Dead/Unsettled）。
LDStatus ldStatusFromShape(ShapeStatus s);

}  // namespace vgo
