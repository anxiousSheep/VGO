#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/agent/agent_tools.h"
#include "ai/agent/loop.h"
#include "ai/agent/tool_registry.h"
#include "ai/agent/trace.h"
#include "ai/json.h"

using namespace vgo;

namespace {

OwnershipMap deadBlackAt(int size, const std::vector<Point>& pts) {
    OwnershipMap om;
    om.size = size;
    om.value.assign(static_cast<std::size_t>(size) * size, 0.0);
    for (const Point& p : pts)
        om.value[static_cast<std::size_t>(p.y) * size + p.x] = -0.95;
    return om;
}

Json resultOf(const Tool& t, const std::string& args = "") {
    const ToolResult r = t.invoke(args);
    Json j;
    parseJson(r.json, &j);
    return j;
}

}  // namespace

TEST(json_roundtrip_object) {
    Json j = Json::makeObject(
        {{"a", Json::makeNumber(1.5)}, {"b", Json::makeString("x")}});
    const std::string s = dumpJson(j);
    Json back;
    CHECK(parseJson(s, &back));
    CHECK(back.isObject());
    CHECK_EQ(back.get("a")->asNumber(), 1.5);
    CHECK_EQ(back.get("b")->asString(), std::string("x"));
}

TEST(json_reject_malformed) {
    Json j;
    CHECK(!parseJson("{\"a\":1} extra", &j));
    CHECK(!parseJson("{", &j));
    CHECK(!parseJson("[1,", &j));
}

TEST(registry_register_find_replace) {
    ToolRegistry r;
    Tool t;
    t.name = "a";
    t.json_schema = "{}";
    CHECK(r.registerTool(t));
    CHECK(r.find("a") != nullptr);
    CHECK(r.find("b") == nullptr);
    CHECK(!r.registerTool(Tool{}));  // empty name rejected
    Tool t2;
    t2.name = "a";
    t2.description = "replaced";
    t2.json_schema = "{}";
    CHECK(r.registerTool(t2));
    CHECK_EQ(r.size(), 1u);
    CHECK_EQ(r.find("a")->description, std::string("replaced"));
}

TEST(schemas_for_model_contains_names) {
    ToolRegistry r;
    for (const char* n : {"a", "b"}) {
        Tool t;
        t.name = n;
        t.description = "desc";
        t.json_schema = "{\"type\":\"object\"}";
        r.registerTool(t);
    }
    const std::string s = r.schemasForModel();
    CHECK(s.find("\"name\":\"a\"") != std::string::npos);
    CHECK(s.find("\"name\":\"b\"") != std::string::npos);
    CHECK(s.find("\"type\":\"function\"") != std::string::npos);
}

TEST(count_territory_returns_json) {
    Game g(9);
    g.play(2, 2);
    g.play(2, 3);
    AgentToolContext ctx;
    ctx.game = g;
    ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("count_territory");
    CHECK(t != nullptr);
    const Json j = resultOf(*t);
    CHECK(j.isObject());
    CHECK(j.get("black_lead") != nullptr);
    CHECK(j.get("komi") != nullptr);
}

TEST(life_death_solve_dead_group) {
    Board b(9);
    b.set(0, 0, Color::Black);
    b.set(1, 0, Color::Black);
    Game g(b, Color::White, 7.5);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.ownership = deadBlackAt(9, {{0, 0}, {1, 0}});
    ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("life_death_solve");
    CHECK(t != nullptr);
    const Json j = resultOf(*t);
    CHECK(j.isObject());
    CHECK_EQ(j.get("alive")->asBool(true), false);
    const Json* grp = j.get("group");
    CHECK(grp != nullptr && grp->isArray());
    CHECK_EQ(grp->array.size(), 2u);
}

TEST(illegal_coord_refused) {
    Game g(9);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.ownership = deadBlackAt(9, {{0, 0}});
    ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("life_death_solve");
    const ToolResult res = t->invoke("{\"group\":\"Z99\"}");
    CHECK(!res.ok);
}

// B.5：无引擎（ownership 为空）时不再硬失败，退化为 PatternOnly 空结论。
TEST(life_death_solve_degrades_without_engine) {
    Game g(9);
    AgentToolContext ctx;
    ctx.game = g;  // ownership 为空
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("life_death_solve");
    CHECK(t != nullptr);
    const ToolResult res = t->invoke("{}");
    CHECK(res.ok);
    const Json j = resultOf(*t);
    CHECK_EQ(j.get("status")->asString(), std::string("unknown"));
    CHECK_EQ(j.get("verification")->asString(), std::string("none"));
}

TEST(sgf_lookup_position_returns_handseq) {
    Game g(9);
    g.play(3, 3);   // Black "dd"
    g.play(6, 6);   // White "gg"
    g.pass();       // Black pass
    AgentToolContext ctx;
    ctx.game = g;
    ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("sgf_lookup_position");
    CHECK(t != nullptr);

    const Json j = resultOf(*t);
    CHECK(j.isObject());
    CHECK_EQ(j.get("move_count")->asNumber(), 3.0);
    CHECK_EQ(j.get("size")->asNumber(), 9.0);
    CHECK_EQ(j.get("to_move")->asString(), std::string("white"));

    const Json* moves = j.get("moves");
    CHECK(moves != nullptr && moves->isArray());
    CHECK_EQ(moves->array.size(), 3u);
    CHECK_EQ(moves->array[0].get("color")->asString(), std::string("B"));
    CHECK_EQ(moves->array[0].get("point")->asString(), std::string("dd"));
    CHECK(!moves->array[0].get("pass")->asBool());
    CHECK_EQ(moves->array[1].get("color")->asString(), std::string("W"));
    CHECK_EQ(moves->array[1].get("point")->asString(), std::string("gg"));
    CHECK(moves->array[2].get("pass")->asBool());
    CHECK_EQ(moves->array[2].get("point")->asString(), std::string(""));

    const std::string sgf = j.get("sgf")->asString();
    CHECK(sgf.find("GM[1]FF[4]") != std::string::npos);
    CHECK(sgf.find("SZ[9]") != std::string::npos);
    CHECK(sgf.find("B[dd]") != std::string::npos);
    CHECK(sgf.find("W[gg]") != std::string::npos);
}

TEST(sgf_lookup_position_setup_stones) {
    // 让子局：黑方预置一颗星位摆子，白方先行。
    Game g(9, 7.5, {Point{2, 2}});
    AgentToolContext ctx;
    ctx.game = g;
    ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("sgf_lookup_position");
    CHECK(t != nullptr);

    const Json j = resultOf(*t);
    CHECK_EQ(j.get("to_move")->asString(), std::string("white"));
    const Json* setup = j.get("setup_stones");
    CHECK(setup != nullptr && setup->isArray());
    CHECK_EQ(setup->array.size(), 1u);
    CHECK_EQ(setup->array[0].get("color")->asString(), std::string("B"));
    CHECK_EQ(setup->array[0].get("point")->asString(), std::string("cc"));
    CHECK_EQ(j.get("move_count")->asNumber(), 0.0);
    // 摆子不在着法列表里，但会进入 SGF 的 AB[] 属性。
    const std::string sgf = j.get("sgf")->asString();
    CHECK(sgf.find("AB[cc]") != std::string::npos);
}

TEST(loop_final_immediately) {
    AgentLoop loop;
    auto model = [](const std::vector<AgentLoop::Step>&, AgentAction& a) {
        a.is_final = true;
        a.answer = "答案";
        return true;
    };
    auto invoke = [](const std::string&, const std::string&) { return ToolResult{}; };
    const auto out = loop.run(model, invoke);
    CHECK(out.finished);
    CHECK_EQ(out.answer, std::string("答案"));
    CHECK_EQ(out.steps.size(), 0u);
}

TEST(loop_single_tool_then_final) {
    AgentLoop loop;
    auto model = [](const std::vector<AgentLoop::Step>& steps, AgentAction& a) {
        if (steps.empty()) {
            a.is_final = false;
            a.tool_name = "t";
            a.tool_args = "{}";
        } else {
            a.is_final = true;
            a.answer = "done";
        }
        return true;
    };
    auto invoke = [](const std::string&, const std::string&) {
        ToolResult r;
        r.json = "{\"ok\":true}";
        return r;
    };
    const auto out = loop.run(model, invoke);
    CHECK(out.finished);
    CHECK_EQ(out.steps.size(), 1u);
    CHECK_EQ(out.steps[0].tool, std::string("t"));
    CHECK(out.steps[0].ok);
}

TEST(loop_step_limit) {
    AgentLoop loop(3, 3000);
    auto model = [](const std::vector<AgentLoop::Step>&, AgentAction& a) {
        a.is_final = false;
        a.tool_name = "t";
        a.tool_args = "{}";
        return true;
    };
    auto invoke = [](const std::string&, const std::string&) { return ToolResult{}; };
    const auto out = loop.run(model, invoke);
    CHECK(out.step_limit);
    CHECK(!out.finished);
    CHECK_EQ(out.steps.size(), 3u);
}

TEST(loop_timeout) {
    int t = 0;
    AgentLoop::ClockFn clock = [&t]() {
        const int now = t * 10000;
        ++t;
        return now;
    };
    AgentLoop loop(4, 3000, clock);
    auto model = [](const std::vector<AgentLoop::Step>&, AgentAction& a) {
        a.is_final = false;
        a.tool_name = "t";
        a.tool_args = "{}";
        return true;
    };
    auto invoke = [](const std::string&, const std::string&) { return ToolResult{}; };
    const auto out = loop.run(model, invoke);
    CHECK(out.timed_out);
    CHECK_EQ(out.steps.size(), 0u);
}

TEST(loop_model_stop_falls_back) {
    AgentLoop loop;
    auto model = [](const std::vector<AgentLoop::Step>&, AgentAction&) { return false; };
    auto invoke = [](const std::string&, const std::string&) { return ToolResult{}; };
    const auto out = loop.run(model, invoke);
    CHECK(!out.finished);
    CHECK(!out.step_limit);
    CHECK_EQ(out.steps.size(), 0u);
}

TEST(loop_cancel) {
    AgentLoop loop;
    AgentLoop* lp = &loop;
    auto model = [lp](const std::vector<AgentLoop::Step>& steps, AgentAction& a) {
        if (steps.empty()) {
            lp->cancel();
            a.is_final = false;
            a.tool_name = "t";
            a.tool_args = "{}";
        } else {
            a.is_final = true;
            a.answer = "done";
        }
        return true;
    };
    auto invoke = [](const std::string&, const std::string&) { return ToolResult{}; };
    const auto out = loop.run(model, invoke);
    CHECK(out.cancelled);
    CHECK(!out.finished);
}

TEST(position_hash_stable_and_distinct) {
    CHECK_EQ(positionHash(Board(9)), positionHash(Board(9)));
    Board b(9);
    b.set(0, 0, Color::Black);
    CHECK(positionHash(b) != positionHash(Board(9)));
    CHECK_EQ(positionHash(Board(9)).size(), 16u);
}

TEST(trace_jsonl_valid) {
    AgentTraceRecord r;
    r.position_hash = "abc";
    r.step = 1;
    r.tool = "x";
    r.route = "tools";
    const std::string line = traceToJsonl(r);
    Json j;
    CHECK(parseJson(line, &j));
    CHECK_EQ(j.get("position_hash")->asString(), std::string("abc"));
    CHECK_EQ(j.get("tool")->asString(), std::string("x"));
    CHECK_EQ(j.get("route")->asString(), std::string("tools"));
}

TEST(trace_jsonl_verify_roundtrip) {
    AgentTraceRecord r;
    r.ts_ms = 1234567890;
    r.position_hash = "deadbeef00000000";
    r.route = "http";
    r.model = "qwen2.5:7b";
    r.latency_ms = 420;
    r.verify = "corrected";
    const std::string line = traceToJsonl(r);

    AgentTraceRecord back;
    CHECK(parseTraceRecord(line, &back));
    CHECK_EQ(back.ts_ms, 1234567890);
    CHECK_EQ(back.position_hash, std::string("deadbeef00000000"));
    CHECK_EQ(back.route, std::string("http"));
    CHECK_EQ(back.model, std::string("qwen2.5:7b"));
    CHECK_EQ(back.latency_ms, 420);
    CHECK_EQ(back.verify, std::string("corrected"));
}

TEST(trace_parse_rejects_garbage) {
    AgentTraceRecord r;
    CHECK(!parseTraceRecord("not json", &r));
    CHECK(!parseTraceRecord("", &r));
}
