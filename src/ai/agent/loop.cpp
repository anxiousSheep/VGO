#include "loop.h"

#include <chrono>

namespace vgo {

std::int64_t AgentLoop::defaultClock() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

AgentLoop::AgentLoop(int max_steps, int max_wallclock_ms, ClockFn clock)
    : max_steps_(max_steps), max_wallclock_ms_(max_wallclock_ms), clock_(std::move(clock)) {}

AgentLoop::Outcome AgentLoop::run(const ModelFn& model, const InvokeFn& invoke,
                                  const StepFn& on_step) {
    Outcome out;
    cancelled_ = false;
    const std::int64_t start = clock_();

    for (int step = 0; step < max_steps_; ++step) {
        if (cancelled_) {
            out.cancelled = true;
            return out;
        }

        AgentAction a;
        if (!model(out.steps, a)) {
            // Model asked to stop without a final answer: caller falls back to
            // the existing single-shot dispatch / template path.
            out.finished = false;
            return out;
        }
        if (a.is_final) {
            out.finished = true;
            out.answer = a.answer;
            return out;
        }

        if (clock_() - start > max_wallclock_ms_) {
            out.timed_out = true;
            return out;
        }

        const ToolResult r = invoke(a.tool_name, a.tool_args);
        Step s;
        s.tool = a.tool_name;
        s.args = a.tool_args;
        s.ok = r.ok;
        s.result_json = r.ok ? r.json : r.error;
        out.steps.push_back(std::move(s));
        if (on_step) on_step(out.steps.back());
    }

    out.step_limit = true;
    return out;
}

}  // namespace vgo
