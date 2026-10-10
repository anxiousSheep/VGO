#pragma once

#include <string>
#include <utility>
#include <vector>

#include "go/go_types.h"

namespace vgo {

// 死活题解树节点（C.1，JSON 而非 SGF 树）。`move` 的 is_pass 表达虚着。
struct TsumegoNode {
    Move move{-1, -1, false};
    Color by = Color::Black;   // 谁下（对方应手分支）
    std::string verdict;       // "good"/"bad"/""（根子分支用）
    std::string comment;
    std::vector<TsumegoNode> children;
};

// 一道死活题。`snapshot` 用 Board::snapshot() 格式（复用 boardFromSnapshot 重建）。
struct TsumegoProblem {
    std::string id, theme, goal, difficulty, source;
    int size = 9;
    std::string snapshot;              // 'X'/'O'/'.' 行主序
    Color to_move = Color::Black;      // 解题方（谁先走）
    std::vector<Point> target_group;   // 目标棋块（供校验/提示）
    TsumegoNode solution_tree;
};

class TsumegoLibrary {
public:
    static TsumegoLibrary builtin();
    // 解析单题 JSON（含递归解树）。非法 JSON / 坐标 / 字段报错写 err。
    bool fromJsonText(const std::string& text, std::string* err);
    const TsumegoProblem* find(const std::string& id) const;
    std::vector<const TsumegoProblem*> filter(const std::string& theme,
                                              const std::string& difficulty) const;
    std::size_t size() const { return problems_.size(); }
    const std::vector<TsumegoProblem>& all() const { return problems_; }

private:
    std::vector<TsumegoProblem> problems_;
};

// 纯逻辑判题（树内，秒回，C.2）。
enum class TsumegoJudge { Correct, Wrong, OffTree, Solved };

struct TsumegoJudgement {
    TsumegoJudge judge = TsumegoJudge::OffTree;
    std::vector<Move> response;  // good → 从 by:对方 分支走应一手
    std::string comment;
};

// 沿用户已走路径在解树中定位，判定当前手 `user`。`path` = 已走路径（含解题方与应手），
// 每项 (color, move)。返回应手（Correct 时）、判负注释（Wrong 时）或 OffTree（交树外
// 引擎裁判）。
TsumegoJudgement judgeTsumegoMove(const TsumegoProblem& p,
                                  const std::vector<std::pair<Color, Move>>& path, Move user);

}  // namespace vgo
