#pragma once

#include <memory>

#include "../analysis.h"
#include "../go/game.h"
#include "../joseki.h"

#include "tool_registry.h"

namespace vgo {

class JosekiVerdictStore;

// G3-2 定式指代消歧的对话状态：joseki_lookup 每次成功判定后写入，advisor 下一轮把它
// 渲染成「上一次定式判定」硬事实注入 prompt，让「演示这个定式」的「这个」由程序解析，
// 而不是让 LLM 凭记忆猜定式名。换局 / 换模式时由 AiAdvisor::resetJosekiDialogState 清空。
struct JosekiDialogState {
    std::string id;
    std::string name;
    bool applicable = false;   // 前提满足且有可演示线（= 可 apply）
    std::string mode;          // "apply" / "textbook"
};

// 渲染成注入 prompt 的硬事实句（如「点三三（可用，apply 模式）」）。空状态返回空串。
std::string renderJosekiDialogState(const JosekiDialogState& s);

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
    // G3-2：可变共享指针（const ctx 里共享指针指向的对象可变）。joseki_lookup 判定后写入，
    // advisor 下一轮据此消歧「这个定式」。
    std::shared_ptr<JosekiDialogState> last_joseki;
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
