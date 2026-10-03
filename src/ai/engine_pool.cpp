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
                     if (cb) cb(a);
                     runNext();
                 });
}

void EnginePool::runNext() {
    // 服务优先级最高（数值最小）的排队请求；同优先级内按入队顺序（seq 递增）。
    auto best = queue_.end();
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (best == queue_.end() || it->prio < best->prio ||
            (it->prio == best->prio && it->seq < best->seq)) {
            best = it;
        }
    }
    if (best == queue_.end()) return;
    Request r = std::move(*best);
    queue_.erase(best);
    const int slot = findFreeSlot();
    if (slot >= 0) start(std::move(r), slot);
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
