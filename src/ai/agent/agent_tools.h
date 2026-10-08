#pragma once

#include "../analysis.h"
#include "../go/game.h"

#include "tool_registry.h"

namespace vgo {

// The position snapshot a tool invocation reads from. Captured by value so the
// tools are self-contained and deterministic (no engine re-entry).
struct AgentToolContext {
    Game game;              // position snapshot (territory / move count)
    OwnershipMap ownership; // may be empty (fallback engine)
    Analysis analysis;      // may be invalid (fallback engine)
};

// Registers the existing deterministic capabilities as model-callable tools:
//   count_territory / life_death_solve / ownership_map / dead_groups /
//   analyze_position / sgf_lookup_position
// No new algorithms: each invoker calls straight into the existing pure code
// (Game scoring, LifeDeathAnalyzer, ownership, record.h + sgf.h).
//
// Note: `evaluate_move` / `evaluate_last_move` are intentionally NOT registered
// here — they need an async engine query (MoveEvaluator), but Tool::invoke is
// synchronous. Move-quality questions are instead handled by the advisor's
// async MoveEvaluator path, whose result is injected into the prompt as facts.
ToolRegistry buildAgentTools(const AgentToolContext& ctx);

}  // namespace vgo
