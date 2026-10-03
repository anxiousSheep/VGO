#pragma once

#include <deque>
#include <memory>
#include <vector>

#include "analysis.h"
#include "engine.h"
#include "go/board.h"
#include "strength.h"

namespace vgo {

// 分析请求的优先级：数值越小优先级越高。并发池只调度「分析」类工作（胜率/归属）；
// 落子（genmove）由独立的落子引擎承担，不经过本池——这正是 D5.4 正本清源的关键：
// 形势分析 / 提问 / 恶手量化共享一个或多个分析进程，但绝不与 AI 落子争抢同一进程。
enum class EnginePriority {
    Interactive = 0,  // 用户提问（谁领先 / 死活）——需要尽快回答
    Background = 1,   // 形势显示刷新——可被更新的刷新取代
    Idle = 2,         // 恶手 what-if / 投机预生成——空闲时跑，优先级最低
};

// 分析引擎并发池 + 优先级队列（D5.4）。
//
// 每个引擎槽位同一时刻只跑一个 analyze()；所有槽位都忙时，新请求按优先级入队
// （同优先级内 FIFO 稳定排序）。Background 请求会被「合并」：新的 Background 请求
// 取代队列中尚未开始的旧 Background 请求，避免形势刷新随每手落子堆积。
//
// 这取代了旧的「cancel-and-restart」行为（KataGoEngine::analyze 取消在途分析），
// 从「后到先得 + 取消」改为「排队 + 按优先级服务」，消除提问与形势分析的互相 cancel。
//
// 单线程：设计为由 Qt 事件循环驱动（所有引擎回调都回到主线程），非线程安全。
// 拥有其引擎（构造时接收 unique_ptr，析构时释放）。
class EnginePool : public EngineInterface {
public:
    explicit EnginePool(std::vector<std::unique_ptr<EngineInterface>> engines);

    // 落子不池化：转发给第一个引擎（空池则返回虚着）。
    void requestMove(const Board& board, Color to_move,
                     const StrengthParams& params, Callback cb) override;
    // 默认优先级为 Background（供普通 EngineInterface 路径使用）。
    void analyze(const Board& board, Color to_move,
                 const StrengthParams& params, AnalysisCallback cb) override;
    // 带优先级的 analyze（D5.4）。
    void analyze(const Board& board, Color to_move, const StrengthParams& params,
                 EnginePriority prio, AnalysisCallback cb);
    // 丢弃全部排队请求，并取消每个槽位在途的分析。
    void cancel() override;

    // 观测接口（测试 / 调试用）。
    size_t slotCount() const { return engines_.size(); }
    size_t pendingCount() const { return queue_.size(); }

private:
    struct Request {
        Board board;
        Color to_move;
        StrengthParams params;
        EnginePriority prio;
        AnalysisCallback cb;
        int seq = 0;  // 稳定 FIFO 排序用的入队序号
    };

    int findFreeSlot() const;
    void start(Request r, int slot);
    void runNext();

    std::vector<std::unique_ptr<EngineInterface>> engines_;
    std::vector<bool> busy_;
    std::deque<Request> queue_;
    int next_seq_ = 0;
};

// 以指定优先级发起 analyze：引擎是 EnginePool 时走优先级队列，否则退化为普通
// analyze（FallbackEngine / 裸 KataGoEngine / 测试替身）。
void analyzePrioritized(EngineInterface* eng, const Board& board, Color to_move,
                        const StrengthParams& params, EnginePriority prio,
                        EngineInterface::AnalysisCallback cb);

}  // namespace vgo
