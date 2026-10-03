#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "tool.h"

namespace vgo {

// The model's next action: either a final answer, or a tool call to execute.
struct AgentAction {
    bool is_final = false;
    std::string answer;       // final answer text (when is_final)
    std::string tool_name;    // tool to call (when !is_final)
    std::string tool_args;    // JSON args for the tool (when !is_final)
};

// A bounded tool-use loop (D1.5): let the model decide which tools to call,
// feed results back, and iterate until it produces a final answer or a bound is
// hit. Pure orchestration — the model and the tool executor are injected, so the
// loop itself is fully unit-testable without a real LLM or engine.
class AgentLoop {
public:
    struct Step {
        std::string tool;
        std::string args;
        std::string result_json;  // tool result payload (or error text)
        bool ok = false;
    };

    struct Outcome {
        bool finished = false;     // model produced a final answer
        bool timed_out = false;    // exceeded max_wallclock_ms
        bool step_limit = false;   // reached max_steps without a final answer
        bool cancelled = false;
        std::string answer;        // final answer (when finished)
        std::vector<Step> steps;   // every tool call executed, in order
    };

    // `model` inspects the accumulated tool results and decides the next action.
    // Return false to stop (the caller then falls back to single-shot dispatch).
    // `invoke` runs a tool by name + args JSON. `on_step` observes each execution
    // (used to emit the agent_trace.jsonl). `clock` supplies wall-clock ms.
    using ModelFn = std::function<bool(const std::vector<Step>&, AgentAction&)>;
    using InvokeFn = std::function<ToolResult(const std::string&, const std::string&)>;
    using StepFn = std::function<void(const Step&)>;
    using ClockFn = std::function<std::int64_t()>;

    static std::int64_t defaultClock();

    AgentLoop(int max_steps = 4, int max_wallclock_ms = 3000,
              ClockFn clock = defaultClock);

    Outcome run(const ModelFn& model, const InvokeFn& invoke,
                const StepFn& on_step = {});

    void cancel() { cancelled_ = true; }
    bool cancelled() const { return cancelled_; }

private:
    int max_steps_;
    int max_wallclock_ms_;
    ClockFn clock_;
    bool cancelled_ = false;
};

}  // namespace vgo
