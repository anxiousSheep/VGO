#include "probe.h"

#include <utility>

#include "engine_pool.h"
#include "go/game.h"

namespace vgo {

Probe::Probe(EngineInterface* engine, StrengthParams sp) : engine_(engine), sp_(sp) {}

void Probe::probe(const Board& board, Color to_move, const Point& probe_move, Callback cb) {
    const int gen = ++generation_;

    if (!engine_) {
        ProbeResult r;
        r.ok = false;
        cb(std::move(r));
        return;
    }

    Game g(board, to_move);
    if (!g.play(probe_move.x, probe_move.y)) {
        ProbeResult r;
        r.ok = false;
        cb(std::move(r));
        return;
    }
    const Board after = g.board();

    // 落子后下一手方为 opponent(to_move)；analyze 以该方视角回报胜率。
    const Color next = opponent(to_move);
    analyzePrioritized(engine_, after, next, sp_, EnginePriority::Interactive,
                       [gen, this, after, next, cb = std::move(cb)](const Analysis& a) {
                           if (gen != generation_) return;  // 过期回调丢弃
                           ProbeResult r;
                           r.ok = true;
                           r.after = after;
                           r.analysis = a;
                           r.winrate = a.valid ? a.winrate : 0.0;
                           cb(std::move(r));
                       });
}

void Probe::analyzePosition(const Board& board, Color to_move, Callback cb) {
    const int gen = ++generation_;
    if (!engine_) {
        ProbeResult r;
        r.ok = false;
        cb(std::move(r));
        return;
    }
    analyzePrioritized(engine_, board, to_move, sp_, EnginePriority::Interactive,
                       [gen, this, board, to_move, cb = std::move(cb)](const Analysis& a) {
                           if (gen != generation_) return;
                           ProbeResult r;
                           r.ok = true;
                           r.after = board;
                           r.analysis = a;
                           r.winrate = a.valid ? a.winrate : 0.0;
                           cb(std::move(r));
                       });
}

void Probe::cancel() {
    ++generation_;
}

}  // namespace vgo
