#include "test_framework.h"

#include <memory>
#include <string>
#include <vector>

#include "ai/engine_pool.h"

using namespace vgo;

namespace {

// 手动引擎：记录 analyze 请求，由测试手动完成（异步完成），用于验证池的排队/优先级。
struct ManualEngine : EngineInterface {
    struct Pending {
        Board board;
        Color to_move;
        AnalysisCallback cb;
    };
    std::vector<Pending> pending;
    bool cancelled = false;

    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        EngineResult r;
        r.move.is_pass = true;
        cb(std::move(r));
    }
    void analyze(const Board& board, Color to_move,
                 const StrengthParams&, AnalysisCallback cb) override {
        pending.push_back(Pending{board, to_move, std::move(cb)});
    }
    void cancel() override { cancelled = true; }
};

// 构造 EnginePool 所需的槽位向量（unique_ptr 不可复制，不能用初始化列表）。
template <typename... Es>
std::vector<std::unique_ptr<EngineInterface>> slots(Es... es) {
    std::vector<std::unique_ptr<EngineInterface>> v;
    (v.push_back(std::move(es)), ...);
    return v;
}

Analysis makeAnalysis(double score_lead) {
    Analysis a;
    a.valid = true;
    a.score_lead = score_lead;
    return a;
}

const StrengthParams kParams = strengthForLevel(5);

}  // namespace

TEST(pool_dispatch_immediately_when_free) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));
    bool got = false;
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { got = true; });
    CHECK_EQ(ep->pending.size(), 1u);
    ep->pending[0].cb(makeAnalysis(1.0));
    CHECK(got);
}

TEST(pool_queues_when_busy) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));
    int calls = 0;
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { ++calls; });
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { ++calls; });
    CHECK_EQ(ep->pending.size(), 1u);  // 第一个立即派发，第二个排队
    CHECK_EQ(pool.pendingCount(), 1u);
    ep->pending[0].cb(makeAnalysis(0.0));  // 完成后排队请求被派发
    CHECK_EQ(ep->pending.size(), 2u);
    CHECK_EQ(pool.pendingCount(), 0u);
    ep->pending[1].cb(makeAnalysis(0.0));
    CHECK_EQ(calls, 2);
}

TEST(pool_priority_order) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));
    std::vector<std::string> order;
    // 占住唯一槽位。
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [&](const Analysis&) { order.push_back("first-idle"); });
    // 排队：Idle、Interactive、Idle——Interactive 必须优先于两个 Idle。
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [&](const Analysis&) { order.push_back("idle-2"); });
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Interactive,
                 [&](const Analysis&) { order.push_back("interactive"); });
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [&](const Analysis&) { order.push_back("idle-3"); });

    CHECK_EQ(pool.pendingCount(), 3u);
    ep->pending[0].cb(makeAnalysis(0.0));  // first-idle 完成
    CHECK_EQ(ep->pending.size(), 2u);      // interactive 被派发
    ep->pending[1].cb(makeAnalysis(0.0));  // interactive 完成
    CHECK_EQ(ep->pending.size(), 3u);      // idle-2 被派发
    ep->pending[2].cb(makeAnalysis(0.0));  // idle-2 完成
    CHECK_EQ(ep->pending.size(), 4u);      // idle-3 被派发
    ep->pending[3].cb(makeAnalysis(0.0));  // idle-3 完成

    CHECK_EQ(order.size(), 4u);
    CHECK_EQ(order[0], std::string("first-idle"));
    CHECK_EQ(order[1], std::string("interactive"));
    CHECK_EQ(order[2], std::string("idle-2"));
    CHECK_EQ(order[3], std::string("idle-3"));
}

TEST(pool_background_coalescing) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));
    int calls = 0;
    // 占住槽位后，两个 Background 刷新只保留最新一个。
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [&](const Analysis&) { ++calls; });
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Background,
                 [&](const Analysis&) { ++calls; });
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Background,
                 [&](const Analysis&) { ++calls; });
    CHECK_EQ(pool.pendingCount(), 1u);
    ep->pending[0].cb(makeAnalysis(0.0));  // idle 完成 -> 仅一个 bg 运行
    CHECK_EQ(ep->pending.size(), 2u);
    ep->pending[1].cb(makeAnalysis(0.0));
    CHECK_EQ(calls, 2);  // idle + 一个 bg（两个 bg 被合并）
}

TEST(pool_reentrant_completion_keeps_queued_background) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));

    int bg_calls = 0;
    int idle2_calls = 0;

    // 占住唯一槽位：Idle（模拟恶手 what-if 第一步），完成回调里重入 analyze（链式第二步）。
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [&](const Analysis&) {
                     pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                                  [&](const Analysis&) { ++idle2_calls; });
                 });
    // 排队：形势刷新（Background，高优先级，绝不能因重入而被静默丢弃）。
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Background,
                 [&](const Analysis&) { ++bg_calls; });

    CHECK_EQ(ep->pending.size(), 1u);  // Idle 派发，Background 排队
    CHECK_EQ(pool.pendingCount(), 1u);

    // 拷贝出来再调用：重入的 analyze 会 push_back 使 vector 扩容，若直接
    // ep->pending[0].cb(...) 会让正在执行的 std::function 的 this 悬空。
    auto finish_idle1 = ep->pending[0].cb;
    finish_idle1(makeAnalysis(0.0));

    // 修复后：Background 优先于链式第二步 Idle 被派发，第二步 Idle 正确入队。
    CHECK_EQ(ep->pending.size(), 2u);
    CHECK_EQ(pool.pendingCount(), 1u);
    CHECK_EQ(bg_calls, 0);

    auto finish_bg = ep->pending[1].cb;
    finish_bg(makeAnalysis(0.0));
    CHECK_EQ(bg_calls, 1);

    CHECK_EQ(ep->pending.size(), 3u);  // 第二步 Idle 随后被派发
    auto finish_idle2 = ep->pending[2].cb;
    finish_idle2(makeAnalysis(0.0));
    CHECK_EQ(idle2_calls, 1);
}

TEST(pool_request_move_resets_busy_slot) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));

    // 占住唯一槽位（在途分析）。
    pool.analyze(Board(9), Color::Black, kParams, [](const Analysis&) {});
    CHECK_EQ(ep->pending.size(), 1u);

    // 转发落子请求：槽位 busy 应被复位，后续 analyze 能立即派发而非永久排队。
    bool move_called = false;
    pool.requestMove(Board(9), Color::Black, kParams,
                     [&](const EngineResult&) { move_called = true; });
    CHECK(move_called);

    int calls = 0;
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { ++calls; });
    CHECK_EQ(ep->pending.size(), 2u);  // busy 已复位 → 立即派发
    CHECK_EQ(pool.pendingCount(), 0u);
    ep->pending[1].cb(makeAnalysis(0.0));
    CHECK_EQ(calls, 1);
}

TEST(pool_cancel_clears_queue_and_engines) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    EnginePool pool(slots(std::move(e)));
    int interactive_calls = 0;
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Idle,
                 [](const Analysis&) {});
    pool.analyze(Board(9), Color::Black, kParams, EnginePriority::Interactive,
                 [&](const Analysis&) { ++interactive_calls; });
    CHECK_EQ(pool.pendingCount(), 1u);
    pool.cancel();
    CHECK_EQ(pool.pendingCount(), 0u);
    CHECK(ep->cancelled);
    ep->pending[0].cb(makeAnalysis(0.0));  // 过期的在途请求完成
    CHECK_EQ(interactive_calls, 0);        // 排队请求已被丢弃，绝不运行
    CHECK_EQ(ep->pending.size(), 1u);      // cancel 后不再派发新请求
}

TEST(pool_two_slots_run_concurrently) {
    auto e1 = std::make_unique<ManualEngine>();
    auto e2 = std::make_unique<ManualEngine>();
    ManualEngine* p1 = e1.get();
    ManualEngine* p2 = e2.get();
    EnginePool pool(slots(std::move(e1), std::move(e2)));
    int calls = 0;
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { ++calls; });
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis&) { ++calls; });
    CHECK_EQ(p1->pending.size(), 1u);  // 两个槽位各派发一个
    CHECK_EQ(p2->pending.size(), 1u);
    CHECK_EQ(pool.pendingCount(), 0u);
    p1->pending[0].cb(makeAnalysis(0.0));
    p2->pending[0].cb(makeAnalysis(0.0));
    CHECK_EQ(calls, 2);
}

TEST(pool_empty_degrades) {
    EnginePool pool({});
    bool got = false;
    pool.analyze(Board(9), Color::Black, kParams, [&](const Analysis& a) {
        got = true;
        CHECK(!a.valid);
    });
    CHECK(got);
}

TEST(analyze_prioritized_falls_back_to_plain) {
    auto e = std::make_unique<ManualEngine>();
    ManualEngine* ep = e.get();
    bool got = false;
    analyzePrioritized(ep, Board(9), Color::Black, kParams, EnginePriority::Interactive,
                       [&](const Analysis&) { got = true; });
    CHECK_EQ(ep->pending.size(), 1u);
    ep->pending[0].cb(makeAnalysis(0.0));
    CHECK(got);
}
