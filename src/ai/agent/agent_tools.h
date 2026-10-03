#pragma once

#include <functional>
#include <optional>

#include "../analysis.h"
#include "../explain.h"
#include "../go/game.h"

#include "tool_registry.h"

namespace vgo {

// The position snapshot a tool invocation reads from. Captured by value so the
// tools are self-contained and deterministic (no engine re-entry). The
// `evaluate_*` hooks are optional engine-bound what-ifs: when empty the
// corresponding tool reports "需引擎" instead of inventing numbers.
struct AgentToolContext {
    Game game;                       // position snapshot (territory / move count)
    OwnershipMap ownership;          // may be empty (fallback engine)
    Analysis analysis;               // may be invalid (fallback engine)
    std::function<std::optional<MoveEvaluation>(const Point&)> evaluate_move;
    std::function<std::optional<MoveEvaluation>()> evaluate_last_move;
};

// Registers the existing deterministic capabilities as model-callable tools:
//   count_territory / life_death_solve / ownership_map / dead_groups /
//   analyze_position / evaluate_move / evaluate_last_move
// No new algorithms: each invoker calls straight into the existing pure code
// (Game scoring, LifeDeathAnalyzer, ownership, MoveEvaluator).
ToolRegistry buildAgentTools(const AgentToolContext& ctx);

}  // namespace vgo
