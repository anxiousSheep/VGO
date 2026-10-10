#pragma once

#include <functional>

#include "analysis.h"
#include "engine.h"
#include "go/board.h"
#include "strength.h"

namespace vgo {

// ---------------------------------------------------------------------------
// probe 协议（S0.3，新增）：在「只复制 Board」的副本上落一子 probe_move → 以
// Interactive 优先级 analyze → 回调。定式仲裁（线 A L4）与死活验证（线 B L4）共用。
//
// D8：只复制 Board，不复制整局 Game——evaluate.cpp 的教训（收官 Game 含 ~300KB 历史）。
// 结果按回调的世代号防竞态：每次 probe 递增 generation_，过期回调直接丢弃。
// ---------------------------------------------------------------------------

struct ProbeResult {
    bool ok = false;          // false = 无引擎 / 落子非法 / 已取消
    double winrate = 0.0;     // opponent(to_move)（下一手方）视角胜率；analysis.valid 时有效
    Board after;              // 试下后的棋盘副本（ok=true 时有效）
    Analysis analysis;        // 完整分析（analysis.valid=false 表示引擎未出归属/胜率）
};

class Probe {
public:
    using Callback = std::function<void(const ProbeResult&)>;

    explicit Probe(EngineInterface* engine, StrengthParams sp);

    // 在 board 副本上落 to_move 的 probe_move → analyze → cb。
    // 无引擎 / 落子非法：立即 cb(ok=false)。
    void probe(const Board& board, Color to_move, const Point& probe_move, Callback cb);

    // 只分析一个局面（不落子）。供定式仲裁「摆完变体线后」直接读胜率、以及「同局面引擎
    // 最优」读 analysis.best 使用。无引擎：立即 cb(ok=false)。
    void analyzePosition(const Board& board, Color to_move, Callback cb);

    // 是否接了引擎（供消费者在发起 probe 前决定降级路径）。
    bool hasEngine() const { return engine_ != nullptr; }

    // 丢弃全部在途 probe 回调（递增世代号）。
    void cancel();

private:
    EngineInterface* engine_ = nullptr;
    StrengthParams sp_;
    int generation_ = 0;
};

}  // namespace vgo
