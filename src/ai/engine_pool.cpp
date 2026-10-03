#include "engine_pool.h"

#include <utility>

namespace vgo {

EnginePool::EnginePool(std::vector<std::unique_ptr<EngineInterface>> engines)
    : engines_(std::move(engines)) {
    busy_.assign(engines_.size(), false);
}

void EnginePool::requestMove(const Board& board, Color to_move,
                             const StrengthParams& params, Callback cb) {
    if (engines_.empty()) {
        EngineResult r;
        r.move.is_pass = true;
        cb(std::move(r));
        return;
    }
    // 落子请求绕过分析调度、直接转发给第一个引擎；引擎内部会取消在途分析并丢弃其
    // 回调（写 stop）。这里同步复位该槽位的 busy 标记，避免被丢弃的回调无法复位
    // busy 导致槽位永久卡死（形势刷新永不下发——即「重启才恢复」）。
    if (!busy_.empty()) busy_[0] = false;
    engines_.front()->requestMove(board, to_move, params, std::move(cb));
}

void EnginePool::analyze(const Board& board, Color to_move,
                         const StrengthParams& params, AnalysisCallback cb) {
    analyze(board, to_move, params, EnginePriority::Background, std::move(cb));
}

void EnginePool::analyze(const Board& board, Color to_move,
                         const StrengthParams& params, EnginePriority prio,
                         AnalysisCallback cb) {
    if (engines_.empty()) {
        cb(Analysis{});
        return;
    }
    // 合并 Background 刷新：只保留最新一个尚未开始的形势刷新，避免其随每手落子堆积。
    if (prio == EnginePriority::Background) {
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->prio == EnginePriority::Background)
                it = queue_.erase(it);
            else
                ++it;
        }
    }
    const int slot = findFreeSlot();
    if (slot >= 0) {
        start(Request{board, to_move, params, prio, std::move(cb), next_seq_++}, slot);
    } else {
        queue_.push_back(Request{board, to_move, params, prio, std::move(cb), next_seq_++});
    }
}

void EnginePool::cancel() {
    queue_.clear();
    for (size_t i = 0; i < engines_.size(); ++i) {
        engines_[i]->cancel();
        busy_[i] = false;
    }
}

int EnginePool::findFreeSlot() const {
    for (size_t i = 0; i < busy_.size(); ++i)
        if (!busy_[i]) return static_cast<int>(i);
    return -1;
}

void EnginePool::start(Request r, int slot) {
    busy_[slot] = true;
    EngineInterface* eng = engines_[slot].get();
    eng->analyze(r.board, r.to_move, r.params,
                 [this, slot, cb = std::move(r.cb)](const Analysis& a) mutable {
                     busy_[slot] = false;
                     // 先调度队列中的高优先级请求，再回调。回调可能重入 analyze
                     // （恶手 what-if 的链式第二步）：若先回调，重入请求会抢走刚释放的
                     // 槽位，随后 runNext 弹出的排队请求就找不到空槽而被静默丢弃，
                     // 形势刷新（Background）因此停摆。先 runNext 保证排队请求优先
                     // 派发，重入请求则会正确入队。
                     runNext();
                     if (cb) cb(a);
                 });
}

void EnginePool::runNext() {
    // 服务优先级最高（数值最小）的排队请求；同优先级内按入队顺序（seq 递增）。
    // 用 while 循环：每次派发一个后立刻尝试下一个（仍按优先级），避免遗漏。
    while (true) {
        auto best = queue_.end();
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (best == queue_.end() || it->prio < best->prio ||
                (it->prio == best->prio && it->seq < best->seq)) {
                best = it;
            }
        }
        if (best == queue_.end()) return;
        // 无空槽时保留在队列里，不要弹出后丢弃——否则该请求的回调永不触发，
        // 调用方（如形势刷新）会永久得不到结果。
        const int slot = findFreeSlot();
        if (slot < 0) return;
        Request r = std::move(*best);
        queue_.erase(best);
        start(std::move(r), slot);
    }
}

void analyzePrioritized(EngineInterface* eng, const Board& board, Color to_move,
                        const StrengthParams& params, EnginePriority prio,
                        EngineInterface::AnalysisCallback cb) {
    if (auto* pool = dynamic_cast<EnginePool*>(eng)) {
        pool->analyze(board, to_move, params, prio, std::move(cb));
    } else {
        eng->analyze(board, to_move, params, std::move(cb));
    }
}

}  // namespace vgo
