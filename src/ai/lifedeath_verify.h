#pragma once

#include <functional>
#include <vector>

#include "explain.h"
#include "go/board.h"
#include "probe.h"
#include "strength.h"

namespace vgo {

class EngineInterface;

// L4 引擎验证协议（异步，有界）。Qt-free。对 L3 给出的要点候选做「攻方 probe / 守方
// probe」双向验证，合成四态死活结论。重构为 S0.3 `Probe` 的消费者——不再内联
// analyzePrioritized，probe 的「复制 Board → 试下 → analyze → 世代号防竞态」全部委托
// Probe；本类只保留 verify 级世代号（多次 verify 互相取代时的竞态）。
//   攻方 probe（每个 kill 候选 v）：副本落 v → analyze → 死子集是否覆盖 G 的存活子？
//   守方 probe（每个 live 候选 d）：副本落 d → analyze → 死子集是否不含 G？
//   结论合成：有×有=Unsettled；有×无=Dead；无×有=Alive；无×无=Unknown。
class LifeDeathVerifier {
public:
    using Callback = std::function<void(LifeDeathResult)>;

    LifeDeathVerifier(EngineInterface* engine, StrengthParams sp);

    // defender = 棋块所属方（守方）；对方 = 攻方。
    void verify(const Board& board, Color defender, const std::vector<Point>& group,
                const std::vector<Point>& kill_candidates,
                const std::vector<Point>& live_candidates, Callback cb);
    void cancel();

private:
    Probe probe_;
    int generation_ = 0;  // verify 级世代号（Probe 内部另有其自身世代号）
};

}  // namespace vgo
