#pragma once

#include <optional>
#include <string>
#include <vector>

#include "analysis.h"
#include "go/board.h"

namespace vgo {

enum class FactType {
    Captures,      // 提掉对方棋子
    PutsInAtari,   // 把对方某块棋打成只剩一口气（打吃）
    Connects,      // 把己方两块棋连成一块
    Extends,       // 延展/加强己方已有的一块棋
    Placement,     // 普通落子
    SelfAtari,     // 自己气紧（只剩一口气，警示）
    Cut,           // 断：把对方两块棋切开（B1a）
    Hane,          // 扳：贴住对方拐头（B1a）
    Diagonal,      // 尖（B1a）
    SmallKnight,   // 小飞（B1a）
    LargeKnight,   // 大飞（B1a）
    OneSpaceJump,  // 跳（一间跳，B1a）
    TigerMouth,    // 虎（虎口，B1a）
};

struct StructuralFact {
    FactType type = FactType::Placement;
    std::vector<Point> stones;  // captured stones, or the involved group
    int liberties = -1;         // resulting liberties of the mover's group
};

// A what-if evaluation of playing a single move: the mover's winrate/lead before
// and after placing it, computed from two engine analyses. `valid == false` means
// no engine produced the analyses (e.g. the fallback engine).
struct MoveEvaluation {
    Point p{-1, -1};
    bool legal = true;          // false => the point cannot be played
    bool valid = false;         // true => winrate/lead deltas are meaningful
    double winrate_before = 0.0;  // mover's perspective
    double winrate_after = 0.0;
    double lead_before = 0.0;     // points the mover leads by (mover perspective)
    double lead_after = 0.0;
    Color mover = Color::Empty;   // who made the move (Black/White; Empty=unknown)
    std::string note;             // e.g. "非法落子（该点不可下）"
};

// L3 死活状态：Alive/Dead = 直判结论；Unsettled = 先手相关（先手死/劫/特殊型）；
// Unknown = 常型未识别且无引擎结论。
enum class LDStatus { Alive, Dead, Unsettled, Unknown };
// 结论来源：PatternOnly = 常型直判（无引擎）；EngineVerified = KataGo probe 验证；
// None = 无死活结论。
enum class LDVerify { PatternOnly, EngineVerified, None };

// A deterministic life-and-death verdict for the largest confidently-dead group.
struct LifeDeathResult {
    std::vector<Point> group;         // the dead block (empty if none)
    bool alive = true;                // no confident dead group（由 status 派生，保留兼容）
    std::vector<Point> saving_moves;  // candidate moves that might save it
    std::string reason;
    // —— L3 新增 ——
    LDStatus status = LDStatus::Unknown;
    LDVerify verification = LDVerify::None;
    std::string shape;                       // 常型名（"刀五"），空 = 未识别
    std::vector<Point> eye_space;            // 眼位区域（供 UI 高亮）
    std::vector<Point> kill_points;          // 杀棋要点（已验证优先）
    std::vector<Point> live_points;          // 做活要点（已验证优先）
};

struct GroundedFacts {
    Analysis analysis;                         // may be invalid (no engine)
    std::vector<StructuralFact> facts;         // always-available board facts
    std::vector<std::vector<Point>> dead_groups;  // confident dead blocks (N1)
    std::optional<MoveEvaluation> move_eval;      // what-if of a questioned move (N1)
    std::optional<LifeDeathResult> life_death;    // life-death verdict (N1)
};

// Board-derived facts describing what `move` accomplished. `captured` is the
// number of stones captured by this move. No engine analysis is required.
std::vector<StructuralFact> extractBoardFacts(const Board& before,
                                              const Board& after,
                                              const Move& move,
                                              int captured,
                                              Color mover);

// Render facts as a short, honest Chinese explanation. Empty if no signal.
std::string renderExplanation(const std::vector<StructuralFact>& facts, Color mover);

// ASCII rendering of the board (X=black, O=white, .=empty) with coordinates.
// Used as context for language models and in tests.
std::string boardToAscii(const Board& board);

// Render a what-if move evaluation as a short, honest Chinese sentence.
std::string renderMoveEvaluation(const MoveEvaluation& e, int size);

// Render a life-death verdict as a short, honest Chinese sentence.
std::string renderLifeDeath(const LifeDeathResult& r, int size);

// Render the list of dead groups as a short Chinese sentence (empty if none).
std::string renderDeadGroups(const std::vector<std::vector<Point>>& groups, int size);

// A board-region hint for a point (半提示模式, M3b.1): "右上角附近" etc.
std::string moveHint(const Point& p, int size);

}  // namespace vgo
