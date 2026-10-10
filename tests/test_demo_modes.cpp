#include "test_framework.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/engine.h"
#include "ai/gtp.h"
#include "ai/joseki.h"
#include "ai/joseki_apply.h"
#include "ai/strength.h"

using namespace vgo;

namespace {

Point P(const std::string& gtp) {
    Point p;
    gtpParsePoint(gtp, 19, &p);
    return p;
}

const JosekiEntry* sansan() {
    static const JosekiLibrary lib = JosekiLibrary::builtin();
    return lib.find("hoshi-sansan-invasion");
}

const JosekiVariant* mainVariant() {
    for (const JosekiVariant& v : sansan()->variants)
        if (v.id == "main") return &v;
    return nullptr;
}

Board emptyBoard() { return Board(19); }

Board boardWithAnchor() {
    Board b(19);
    const Point a = P("Q16");
    b.set(a.x, a.y, Color::Black);
    return b;
}

JosekiVerdict favorableVerdict() {
    JosekiVerdict v;
    v.computed = true;
    v.favorable = true;
    v.w_joseki = 0.55;
    v.w_best = 0.57;
    v.note = "定式成立（引擎验证，胜率差 2.0pp）";
    return v;
}

JosekiVerdict unfavorableVerdict() {
    JosekiVerdict v;
    v.computed = true;
    v.favorable = false;
    v.w_joseki = 0.55;
    v.w_best = 0.68;
    v.note = "定式不便宜（胜率差 13.0pp）";
    return v;
}

// 仲裁替身引擎：第 1 次 analyze = 摆线后守方视角，第 2 次 = 原局面入侵方最优。
struct ArbEngine : EngineInterface {
    double defender_winrate = 0.4;  // 摆线后守方胜率 → w_joseki = 1 - 此值
    double best_winrate = 0.6;      // 入侵方最优 → w_best
    int calls = 0;

    void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
        Analysis a;
        a.valid = true;
        ++calls;
        if (calls == 1) {
            a.winrate = defender_winrate;
            a.best.winrate = defender_winrate;
        } else {
            a.winrate = best_winrate;
            a.best.winrate = best_winrate;
        }
        cb(a);
    }
    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        EngineResult r;
        r.move.is_pass = true;
        cb(std::move(r));
    }
    void cancel() override {}
};

}  // namespace

// —— 三模式分流（§4.5 + 矩阵 #1/#2/#3/#5/#6 + apply 门禁 §4.4.1）——

TEST(demo_textbook_on_empty_board) {
    const JosekiVerdict* none = nullptr;
    const DemoDecision d =
        decideJosekiDemo(emptyBoard(), *sansan(), Corner::UR, Color::White, "textbook", none);
    CHECK(d.action == DemoAction::Textbook);
    CHECK(!d.sequence.empty());
    CHECK(d.announce.find("教学演示") != std::string::npos);
}

TEST(demo_apply_on_empty_board_refuses) {
    const JosekiVerdict* none = nullptr;
    const DemoDecision d =
        decideJosekiDemo(emptyBoard(), *sansan(), Corner::UR, Color::White, "apply", none);
    CHECK(d.action == DemoAction::Refuse);
    CHECK(d.announce.find("前提不满足") != std::string::npos);
}

TEST(demo_apply_with_favorable_verdict_applies) {
    const JosekiVerdict v = favorableVerdict();
    const DemoDecision d =
        decideJosekiDemo(boardWithAnchor(), *sansan(), Corner::UR, Color::White, "apply", &v);
    CHECK(d.action == DemoAction::Apply);
    CHECK(!d.sequence.empty());
    CHECK(d.variant_id == std::string("main"));
}

TEST(demo_apply_without_verdict_degrades_to_textbook) {
    // apply 门禁闭合（§4.4.1）：无预仲裁 verdict 时请求 apply 必须降级 textbook，绝不摆线。
    const JosekiVerdict* none = nullptr;
    const DemoDecision d =
        decideJosekiDemo(boardWithAnchor(), *sansan(), Corner::UR, Color::White, "apply", none);
    CHECK(d.action == DemoAction::Textbook);
    CHECK(d.announce.find("降级") != std::string::npos);
}

TEST(demo_apply_with_uncomputed_verdict_degrades) {
    JosekiVerdict v;
    v.computed = false;
    const DemoDecision d =
        decideJosekiDemo(boardWithAnchor(), *sansan(), Corner::UR, Color::White, "apply", &v);
    CHECK(d.action == DemoAction::Textbook);
}

TEST(demo_apply_with_unfavorable_verdict_refuses) {
    const JosekiVerdict v = unfavorableVerdict();
    const DemoDecision d =
        decideJosekiDemo(boardWithAnchor(), *sansan(), Corner::UR, Color::White, "apply", &v);
    CHECK(d.action == DemoAction::Refuse);
    CHECK(d.announce.find("不便宜") != std::string::npos);
    CHECK(d.announce.find("approach") != std::string::npos);  // 附替代建议
}

TEST(demo_apply_with_occupied_path_refuses) {
    Board b = boardWithAnchor();
    const Point p = P("R17");
    b.set(p.x, p.y, Color::White);  // 路径点被占
    const JosekiVerdict v = favorableVerdict();
    const DemoDecision d = decideJosekiDemo(b, *sansan(), Corner::UR, Color::White, "apply", &v);
    CHECK(d.action == DemoAction::Refuse);
}

TEST(demo_auto_without_verdict_degrades_to_textbook) {
    const JosekiVerdict* none = nullptr;
    const DemoDecision d =
        decideJosekiDemo(boardWithAnchor(), *sansan(), Corner::UR, Color::White, "auto", none);
    CHECK(d.action == DemoAction::Textbook);
}

// —— L4 仲裁（§4.4，替身引擎）——

TEST(arbitrate_no_engine_not_computed) {
    Probe p(nullptr, strengthForLevel(15));
    bool got = false;
    JosekiVerdict out;
    arbitrateJoseki(p, boardWithAnchor(), Color::White, mainVariant()->sequence,
                    [&](const JosekiVerdict& v) {
                        got = true;
                        out = v;
                    });
    CHECK(got);
    CHECK(!out.computed);
}

TEST(arbitrate_favorable_when_diff_small) {
    ArbEngine e;
    e.defender_winrate = 0.4;  // w_joseki = 0.6
    e.best_winrate = 0.61;     // diff = 0.01 <= 3pp
    Probe p(&e, strengthForLevel(15));
    bool got = false;
    JosekiVerdict out;
    arbitrateJoseki(p, boardWithAnchor(), Color::White, mainVariant()->sequence,
                    [&](const JosekiVerdict& v) {
                        got = true;
                        out = v;
                    });
    CHECK(got);
    CHECK(out.computed);
    CHECK(out.favorable);
}

TEST(arbitrate_unfavorable_when_diff_large) {
    ArbEngine e;
    e.defender_winrate = 0.4;  // w_joseki = 0.6
    e.best_winrate = 0.75;     // diff = 0.15 > 3pp
    Probe p(&e, strengthForLevel(15));
    bool got = false;
    JosekiVerdict out;
    arbitrateJoseki(p, boardWithAnchor(), Color::White, mainVariant()->sequence,
                    [&](const JosekiVerdict& v) {
                        got = true;
                        out = v;
                    });
    CHECK(got);
    CHECK(out.computed);
    CHECK(!out.favorable);
    CHECK(out.note.find("不便宜") != std::string::npos);
}
