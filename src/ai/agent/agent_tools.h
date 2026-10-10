#pragma once

#include <memory>

#include "../analysis.h"
#include "../go/game.h"
#include "../joseki.h"

#include "tool_registry.h"

namespace vgo {

class JosekiVerdictStore;

// The position snapshot a tool invocation reads from. Captured by value so the
// tools are self-contained and deterministic (no engine re-entry).
struct AgentToolContext {
    Game game;              // position snapshot (territory / move count)
    OwnershipMap ownership; // may be empty (fallback engine)
    Analysis analysis;      // may be invalid (fallback engine)
    // 定式词典（可空；由 UI 注入的 library 共享指针，工具据此查定式）。
    std::shared_ptr<const JosekiLibrary> joseki;
    // 预仲裁 verdict 存储（可空；§4.4.1 方案 A）。advisor 异步路径仲裁后按局面快照写入，
    // joseki_lookup 据此填充 engine_verdict；无 KataGo 时为空 → apply 安全降级 textbook。
    std::shared_ptr<const JosekiVerdictStore> joseki_verdicts;
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
