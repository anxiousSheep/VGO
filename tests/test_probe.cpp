#include "test_framework.h"

#include <functional>
#include <vector>

#include "ai/engine.h"
#include "ai/probe.h"
#include "ai/strength.h"

using namespace vgo;

namespace {

// 测试替身引擎：同步/异步返回固定胜率（无归属图）。用于验证 probe 的
// 「复制 Board → 试下 → analyze → 回调」协议与世代号防竞态。
struct FixedEngine : EngineInterface {
    double winrate = 0.5;
    bool async = false;
    AnalysisCallback held;

    void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
        Analysis a;
        a.valid = true;
        a.winrate = winrate;
        if (async) {
            held = std::move(cb);
        } else {
            cb(a);
        }
    }
    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        EngineResult r;
        r.move.is_pass = true;
        cb(std::move(r));
    }
    void cancel() override {}
};

const StrengthParams kParams = strengthForLevel(15);

}  // namespace

TEST(probe_no_engine_degrades_ok_false) {
    Probe p(nullptr, kParams);
    bool got = false;
    ProbeResult out;
    p.probe(Board(9), Color::Black, {1, 1}, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(!out.ok);
}

TEST(probe_illegal_move_ok_false) {
    FixedEngine e;
    Probe p(&e, kParams);
    Board b(9);
    b.set(1, 1, Color::Black);  // 已占用
    bool got = false;
    ProbeResult out;
    p.probe(b, Color::White, {1, 1}, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(!out.ok);
}

TEST(probe_legal_move_reports_after_and_winrate) {
    FixedEngine e;
    e.winrate = 0.62;
    Probe p(&e, kParams);
    Board b(9);
    bool got = false;
    ProbeResult out;
    p.probe(b, Color::Black, {4, 4}, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(out.ok);
    CHECK(out.after.at(4, 4) == Color::Black);  // 试下已落子
    CHECK(out.analysis.valid);
    CHECK_EQ(out.winrate, 0.62);
}

TEST(probe_cancel_discards_stale_callback) {
    FixedEngine e;
    e.async = true;
    Probe p(&e, kParams);
    int calls = 0;
    p.probe(Board(9), Color::Black, {4, 4}, [&](const ProbeResult&) { ++calls; });
    p.cancel();
    // 完成在途分析：回调必须被世代号丢弃，不得触发。
    auto held = std::move(e.held);
    Analysis a;
    a.valid = true;
    a.winrate = 0.5;
    held(a);
    CHECK_EQ(calls, 0);
}
