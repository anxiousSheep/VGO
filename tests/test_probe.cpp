#include "test_framework.h"

#include <cmath>
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

// G5-4：selfPlayThenAnalyze 逐手自对弈后分析，回调 winrate 统一为起始方视角。
namespace {

struct SelfPlayEngine : EngineInterface {
    std::vector<Point> moves = {{3, 3}, {4, 4}};  // 自对弈脚本手
    int move_calls = 0;
    int analyze_calls = 0;
    double final_winrate = 0.7;  // 分析返回的 winrate（最终 to_move 视角）
    Color last_to_move = Color::Empty;

    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        EngineResult r;
        if (move_calls < static_cast<int>(moves.size())) r.move = {moves[move_calls], false};
        else r.move.is_pass = true;
        ++move_calls;
        cb(std::move(r));
    }
    void analyze(const Board&, Color to_move, const StrengthParams&, AnalysisCallback cb) override {
        ++analyze_calls;
        last_to_move = to_move;
        Analysis a;
        a.valid = true;
        a.winrate = final_winrate;
        cb(a);
    }
    void cancel() override {}
};

}  // namespace

TEST(probe_self_play_then_analyze_even_plies) {
    SelfPlayEngine e;
    Probe p(&e, kParams);
    bool got = false;
    ProbeResult out;
    p.selfPlayThenAnalyze(Board(9), Color::Black, 2, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(out.ok);
    CHECK_EQ(e.move_calls, 2);       // 自对弈 2 手
    CHECK_EQ(e.analyze_calls, 1);    // 最后分析一次
    CHECK(e.last_to_move == Color::Black);  // 2 手后回到起始方
    CHECK_EQ(out.winrate, 0.7);      // 起始方视角，同方不翻转
}

TEST(probe_self_play_then_analyze_odd_plies_flips) {
    SelfPlayEngine e;
    e.final_winrate = 0.3;  // 最终 to_move（白方）视角
    Probe p(&e, kParams);
    bool got = false;
    ProbeResult out;
    p.selfPlayThenAnalyze(Board(9), Color::Black, 1, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(out.ok);
    CHECK_EQ(e.move_calls, 1);
    CHECK(e.last_to_move == Color::White);  // 1 手后轮到白方
    CHECK(std::abs(out.winrate - 0.7) < 1e-9);  // 0.3 翻转为黑方视角
}

TEST(probe_self_play_zero_plies_degenerates) {
    SelfPlayEngine e;
    e.final_winrate = 0.55;
    Probe p(&e, kParams);
    bool got = false;
    ProbeResult out;
    p.selfPlayThenAnalyze(Board(9), Color::Black, 0, [&](const ProbeResult& r) {
        got = true;
        out = r;
    });
    CHECK(got);
    CHECK(out.ok);
    CHECK_EQ(e.move_calls, 0);       // 无自对弈
    CHECK_EQ(e.analyze_calls, 1);
    CHECK_EQ(out.winrate, 0.55);     // 起始方视角，不翻转
}
