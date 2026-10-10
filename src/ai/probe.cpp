#include "probe.h"

#include <memory>
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

void Probe::selfPlayThenAnalyze(const Board& board, Color to_move, int plies, Callback cb) {
    if (plies <= 0) {
        analyzePosition(board, to_move, std::move(cb));
        return;
    }
    if (!engine_) {
        ProbeResult r;
        r.ok = false;
        cb(std::move(r));
        return;
    }
    const int gen = ++generation_;
    auto ctx = std::make_shared<SelfPlayCtx>();
    ctx->start_color = to_move;
    ctx->remaining = plies;
    ctx->cb = std::move(cb);
    selfPlayStep(ctx, board, to_move, gen);
}

void Probe::selfPlayStep(const std::shared_ptr<SelfPlayCtx>& ctx, const Board& board,
                         Color to_move, int gen) {
    if (gen != generation_) return;  // 已取消 / 被更新

    // 续下结束（手数耗尽 / 引擎虚着 / 非法手）→ 分析最终局面，winrate 转成起始方视角。
    const auto finalize = [ctx, to_move, gen, this](const Board& b) {
        analyzePrioritized(engine_, b, to_move, sp_, EnginePriority::Interactive,
                           [ctx, to_move, gen, b, this](const Analysis& a) {
                               if (gen != generation_) return;
                               ProbeResult out;
                               out.ok = true;
                               out.after = b;
                               out.analysis = a;
                               out.winrate = a.valid ? a.winrate : 0.0;
                               if (to_move != ctx->start_color) out.winrate = 1.0 - out.winrate;
                               ctx->cb(std::move(out));
                           });
    };

    if (ctx->remaining <= 0) {
        finalize(board);
        return;
    }

    // 引擎自选一手并落下。
    engine_->requestMove(board, to_move, sp_,
                         [ctx, board, to_move, gen, finalize, this](EngineResult er) {
                             if (gen != generation_) return;
                             if (er.move.is_pass) {
                                 finalize(board);
                                 return;
                             }
                             Game g(board, to_move);
                             if (!g.play(er.move.p.x, er.move.p.y)) {
                                 finalize(board);
                                 return;
                             }
                             --ctx->remaining;
                             selfPlayStep(ctx, g.board(), opponent(to_move), gen);
                         });
}

void Probe::cancel() {
    ++generation_;
}

}  // namespace vgo
