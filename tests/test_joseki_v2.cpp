#include "test_framework.h"

#include <memory>
#include <string>
#include <vector>

#include "ai/agent/agent_tools.h"
#include "ai/gtp.h"
#include "ai/joseki.h"
#include "ai/joseki_apply.h"
#include "ai/json.h"

using namespace vgo;

namespace {

bool sameSeq(const std::vector<JosekiMove>& a, const std::vector<JosekiMove>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i].p == b[i].p) || a[i].by != b[i].by) return false;
    }
    return true;
}

Point P(const std::string& gtp) {
    Point p;
    gtpParsePoint(gtp, 19, &p);
    return p;
}

// 主变体序列（点三三）。
std::vector<JosekiMove> mainSeq(const JosekiEntry* e) {
    for (const JosekiVariant& v : e->variants)
        if (v.id == "main") return v.sequence;
    return {};
}

}  // namespace

TEST(joseki_v2_builtin_find_by_id_name_alias) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (e) CHECK_EQ(e->name, std::string("点三三"));
    CHECK(lib.find("点三三") != nullptr);
    CHECK(lib.find("三三入侵") != nullptr);
    CHECK(lib.find("点33") != nullptr);
    CHECK(lib.find("HOSHI-SANSAN-INVASION") != nullptr);
}

TEST(joseki_v2_builtin_find_substring_and_miss) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    CHECK(lib.find("托退") != nullptr);
    CHECK(lib.find("挂") != nullptr);
    CHECK(lib.find("不存在的定式") == nullptr);
    CHECK(lib.find("") == nullptr);
}

TEST(joseki_v2_builtin_has_starter_entries) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    CHECK(lib.size() >= 6);
}

TEST(joseki_v2_flag_has_preconditions_and_variants) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("点三三");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK_EQ(e->preconditions.required_stones.size(), 1u);
    CHECK(!e->preconditions.required_empty.empty());
    CHECK_EQ(e->preconditions.zones.size(), 2u);  // left_flank / right_flank
    CHECK(e->variants.size() >= 2);
    CHECK(e->teaching.idea.find("点三三") != std::string::npos);
}

TEST(joseki_v2_invader_is_white) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    CHECK(josekiInvader(*lib.find("hoshi-sansan-invasion")) == Color::White);
}

TEST(joseki_v2_project_to_corner_roundtrip) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (!e) return;
    const std::vector<JosekiMove> seq = mainSeq(e);

    // UR → 四角 → UR 恒等：四个角的 cornerSymmetry 均为对合（自逆）。
    for (Corner c : {Corner::UR, Corner::UL, Corner::BL, Corner::BR}) {
        const std::vector<JosekiMove> once = projectToCorner(seq, e->size, c);
        const std::vector<JosekiMove> twice = projectToCorner(once, e->size, c);
        CHECK(sameSeq(twice, seq));
    }
}

TEST(joseki_v2_project_to_corner_ur_is_identity) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(sameSeq(projectToCorner(mainSeq(e), e->size, Corner::UR), mainSeq(e)));
}

TEST(joseki_v2_from_json_text_roundtrip) {
    const std::string json = R"({
        "id": "test-joseki", "name": "测试定式", "aliases": ["测", "试"],
        "size": 19, "applies_to_formation": "对方星位占角", "source": "json",
        "preconditions": {
            "required_stones": [{"point":"Q16","color":"black","role":"corner_anchor","label":"对方星位子"}],
            "required_empty": ["R17","Q17"],
            "forbidden": [],
            "zones": {"left_flank":{"points":["Q14"],"meaning":"左侧翼"},
                      "right_flank":{"points":["S14"],"meaning":"右侧翼"}}
        },
        "variants": [
            {"id":"main","name":"主线","when":"default","direction_rule":"block_toward_friendly_side",
             "sequence":[{"move":"R17","by":"white","note":"n1"},{"move":"Q17","by":"black","note":"n2"}]},
            {"id":"rev","name":"反向","when":{"zone_friendly_majority":"right_flank"},"sequence":[]}
        ],
        "alternatives_if_rejected": ["alt"],
        "teaching": {"idea":"i","caution":"c"}
    })";
    JosekiLibrary lib;
    std::string err;
    CHECK(lib.fromJsonText(json, &err));
    const JosekiEntry* e = lib.find("test-joseki");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK_EQ(e->preconditions.required_stones.size(), 1u);
    CHECK(e->preconditions.required_stones[0].color == Color::Black);
    CHECK(e->preconditions.required_stones[0].point == P("Q16"));
    CHECK_EQ(e->preconditions.zones.size(), 2u);
    CHECK_EQ(e->variants.size(), 2u);
    CHECK_EQ(e->variants[0].when, std::string("default"));
    // when 以 object 形式给出时规范化为 "zone_friendly_majority:right_flank"
    CHECK_EQ(e->variants[1].when, std::string("zone_friendly_majority:right_flank"));
    CHECK(e->variants[1].sequence.empty());
    CHECK_EQ(e->variants[0].sequence.size(), 2u);
    CHECK(e->variants[0].sequence[0].by == Color::White);
    CHECK(e->variants[0].sequence[0].p == P("R17"));
}

TEST(joseki_v2_from_json_text_rejects_bad_input) {
    JosekiLibrary lib;
    std::string err;
    CHECK(!lib.fromJsonText("{ not json", &err));
    CHECK(!err.empty());
    // 非法坐标
    CHECK(!lib.fromJsonText(
        R"({"id":"x","variants":[{"id":"m","sequence":[{"move":"Z99","by":"black"}]}]})", &err));
    // 缺少 variants
    CHECK(!lib.fromJsonText(R"({"id":"x"})", &err));
    // 非法 when
    CHECK(!lib.fromJsonText(
        R"({"id":"x","variants":[{"id":"m","when":{"bogus":"y"},"sequence":[]}]})", &err));
}

TEST(joseki_v2_directory_index_has_no_body) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const std::string idx = lib.directoryIndex();
    CHECK(idx.find("点三三") != std::string::npos);
    // 目录只含索引，不含讲解正文。
    CHECK(idx.find("夺角") == std::string::npos);
}

TEST(joseki_v2_parse_corner) {
    Corner c;
    CHECK(parseCorner("UR", &c) && c == Corner::UR);
    CHECK(parseCorner("ul", &c) && c == Corner::UL);
    CHECK(parseCorner("bl", &c) && c == Corner::BL);
    CHECK(parseCorner("BR", &c) && c == Corner::BR);
    CHECK(!parseCorner("xx", &c));
}

// A.3 工具级：joseki_lookup 返回「适用性判定书」。
// G3-1 瘦身：不适用条目收敛为 {name, summary, applicable:false, reason}，不再输出
// preconditions/occupied_path/context/applicable_variants 等全量字段（指代歧义源头）。
TEST(joseki_v2_tool_verdict_empty_board_not_satisfied) {
    Game g(19);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    CHECK(t != nullptr);
    const ToolResult res = t->invoke(R"({"name":"点三三"})");
    CHECK(res.ok);
    Json j;
    CHECK(parseJson(res.json, &j));
    CHECK(j.get("preconditions") == nullptr);      // 瘦身：无全量字段
    CHECK(j.get("modes_available") == nullptr);
    CHECK(j.get("applicable_variants") == nullptr);
    CHECK_EQ(j.get("applicable")->asBool(), false);  // 空盘：缺对方星位子
    CHECK(j.get("summary")->asString().find("不适用") != std::string::npos);
    CHECK(j.get("reason") != nullptr);
}

TEST(joseki_v2_tool_verdict_with_anchor_satisfied) {
    Game g(19);
    CHECK(g.play(P("Q16").x, P("Q16").y));  // 黑下 4-4 星位
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"点三三"})");
    CHECK(res.ok);
    Json j;
    CHECK(parseJson(res.json, &j));
    CHECK_EQ(j.get("preconditions")->get("satisfied")->asBool(), true);
    bool has_apply = false;
    for (const Json& m : j.get("modes_available")->array)
        if (m.asString() == "apply") has_apply = true;
    CHECK(has_apply);
    // 主变体可摆线（非空）。
    const Json* vars = j.get("applicable_variants");
    CHECK(vars != nullptr && vars->isArray() && !vars->array.empty());
    CHECK_EQ(vars->array[0].get("strategy_only")->asBool(), false);
    CHECK_EQ(vars->array[0].get("sequence")->array.size() >= 1u, true);
}

TEST(joseki_v2_tool_size_mismatch_refused) {
    Game g(9);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"点三三"})");
    CHECK(!res.ok);
    CHECK(res.error.find("19") != std::string::npos);
    CHECK(res.error.find("9") != std::string::npos);
}

TEST(joseki_v2_tool_unknown_name_and_no_library_honest) {
    Game g(19);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"不存在的定式"})");
    CHECK(!res.ok);
    CHECK(res.error.find("还没收录") != std::string::npos);

    AgentToolContext ctx2;
    ctx2.game = g;
    const ToolRegistry r2 = buildAgentTools(ctx2);
    const Tool* t2 = r2.find("joseki_lookup");
    const ToolResult res2 = t2->invoke(R"({"name":"点三三"})");
    CHECK(!res2.ok);
}

// G4-1/G3-1：joseki_lookup 返回顶层 summary（程序预生成人话），供 LLM 转述而非朗读字段名。
TEST(joseki_v2_tool_summary_field) {
    Game g(19);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");

    // 空盘：前提不满足 → summary 明说「不适用」。
    Json j;
    CHECK(parseJson(t->invoke(R"({"name":"点三三"})").json, &j));
    const std::string empty_summary = j.get("summary")->asString();
    CHECK(empty_summary.find("不适用") != std::string::npos);
    CHECK(empty_summary.find("点三三") != std::string::npos);

    // 有星位锚：前提满足 → summary 明说「前提满足」。
    Game g2(19);
    CHECK(g2.play(P("Q16").x, P("Q16").y));
    AgentToolContext ctx2;
    ctx2.game = g2;
    ctx2.joseki = ctx.joseki;
    const ToolRegistry r2 = buildAgentTools(ctx2);
    const Tool* t2 = r2.find("joseki_lookup");
    Json j2;
    CHECK(parseJson(t2->invoke(R"({"name":"点三三"})").json, &j2));
    CHECK(j2.get("summary")->asString().find("前提满足") != std::string::npos);
}

// §4.4.1 预仲裁注入：joseki_lookup 从共享 verdict 存储读取 engine_verdict。
TEST(joseki_v2_tool_engine_verdict_from_store) {
    Game g(19);
    CHECK(g.play(P("Q16").x, P("Q16").y));  // 黑下 4-4 星位
    auto store = std::make_shared<JosekiVerdictStore>();
    JosekiVerdict v;
    v.computed = true;
    v.favorable = true;
    v.w_joseki = 0.55;
    v.w_best = 0.57;
    v.note = "定式成立（引擎验证）";
    store->put(g.board(), v);

    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    ctx.joseki_verdicts = store;
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"点三三"})");
    CHECK(res.ok);
    Json j;
    CHECK(parseJson(res.json, &j));
    const Json* ev = j.get("engine_verdict");
    CHECK(ev != nullptr && !ev->isNull());
    if (ev && !ev->isNull()) {
        CHECK_EQ(ev->get("favorable")->asBool(), true);
        CHECK_EQ(ev->get("computed")->asBool(), true);
    }
}

// 无 verdict 存储时 engine_verdict 为 null（apply 由 decideJosekiDemo 安全降级）。
TEST(joseki_v2_tool_engine_verdict_null_without_store) {
    Game g(19);
    CHECK(g.play(P("Q16").x, P("Q16").y));
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"点三三"})");
    CHECK(res.ok);
    Json j;
    CHECK(parseJson(res.json, &j));
    CHECK(j.get("engine_verdict")->isNull());
}
