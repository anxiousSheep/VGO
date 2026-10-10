#include "test_framework.h"

#include <memory>
#include <string>
#include <vector>

#include "ai/agent/agent_tools.h"
#include "ai/gtp.h"
#include "ai/json.h"
#include "ai/joseki.h"

using namespace vgo;

namespace {
bool sameSeq(const std::vector<JosekiMove>& a, const std::vector<JosekiMove>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i].p == b[i].p) || a[i].by != b[i].by) return false;
    }
    return true;
}
}  // namespace

TEST(joseki_builtin_find_by_id_name_alias) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (e) CHECK_EQ(e->name, std::string("点三三"));
    CHECK(lib.find("点三三") != nullptr);
    CHECK(lib.find("三三入侵") != nullptr);
    CHECK(lib.find("点33") != nullptr);
    // 大小写不敏感
    CHECK(lib.find("HOSHI-SANSAN-INVASION") != nullptr);
}

TEST(joseki_builtin_find_substring_and_miss) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    CHECK(lib.find("托退") != nullptr);
    CHECK(lib.find("挂") != nullptr);        // 子串命中
    CHECK(lib.find("不存在的定式") == nullptr);
    CHECK(lib.find("") == nullptr);
}

TEST(joseki_builtin_has_starter_entries) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    CHECK(lib.size() >= 6);
}

TEST(joseki_project_to_corner_roundtrip) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (!e) return;

    // UR → 四角 → UR 恒等：四个角的 cornerSymmetry 均为对合（自逆）。
    for (Corner c : {Corner::UR, Corner::UL, Corner::BL, Corner::BR}) {
        const std::string name = cornerName(c);
        const std::vector<JosekiMove> once = projectToCorner(e->sequence, e->size, name);
        const std::vector<JosekiMove> twice = projectToCorner(once, e->size, name);
        CHECK(sameSeq(twice, e->sequence));
    }
}

TEST(joseki_project_to_corner_ur_is_identity) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const JosekiEntry* e = lib.find("hoshi-sansan-invasion");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(sameSeq(projectToCorner(e->sequence, e->size, "UR"), e->sequence));
}

TEST(joseki_from_json_text_roundtrip) {
    const std::string json = R"({
        "id": "test-joseki", "name": "测试定式", "aliases": ["测", "试"],
        "corner": "UR", "size": 19, "trigger": "t", "choices": "c", "source": "json",
        "sequence": [
            {"move": "R17", "by": "white", "note": "n1"},
            {"move": "Q17", "by": "black", "note": "n2"}
        ]
    })";
    JosekiLibrary lib;
    std::string err;
    CHECK(lib.fromJsonText(json, &err));
    const JosekiEntry* e = lib.find("test-joseki");
    CHECK(e != nullptr);
    if (e) {
        CHECK_EQ(e->sequence.size(), 2u);
        CHECK(e->sequence[0].by == Color::White);
        CHECK(e->sequence[1].by == Color::Black);
        Point p0, p1;
        CHECK(gtpParsePoint("R17", 19, &p0));
        CHECK(gtpParsePoint("Q17", 19, &p1));
        CHECK(e->sequence[0].p == p0);
        CHECK(e->sequence[1].p == p1);
    }
}

TEST(joseki_from_json_text_rejects_bad_input) {
    JosekiLibrary lib;
    std::string err;
    CHECK(!lib.fromJsonText("{ not json", &err));
    CHECK(!err.empty());
    // 非法坐标
    CHECK(!lib.fromJsonText(
        R"({"id":"x","sequence":[{"move":"Z99","by":"black"}]})", &err));
    // 缺少 by
    CHECK(!lib.fromJsonText(
        R"({"id":"x","sequence":[{"move":"R17"}]})", &err));
    // 空 sequence
    CHECK(!lib.fromJsonText(R"({"id":"x","sequence":[]})", &err));
}

TEST(joseki_directory_index_has_no_body) {
    const JosekiLibrary lib = JosekiLibrary::builtin();
    const std::string idx = lib.directoryIndex();
    CHECK(idx.find("点三三") != std::string::npos);
    // 目录只含索引，不含讲解正文。
    CHECK(idx.find("挡，必然") == std::string::npos);
}

TEST(joseki_parse_corner) {
    Corner c;
    CHECK(parseCorner("UR", &c) && c == Corner::UR);
    CHECK(parseCorner("ul", &c) && c == Corner::UL);
    CHECK(parseCorner("bl", &c) && c == Corner::BL);
    CHECK(parseCorner("BR", &c) && c == Corner::BR);
    CHECK(!parseCorner("xx", &c));
}

// A.3 工具级：joseki_lookup 走 AgentToolContext。
TEST(joseki_tool_lookup_returns_sequence) {
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
    CHECK_EQ(j.get("name")->asString(), std::string("点三三"));
    const Json* seq = j.get("sequence");
    CHECK(seq != nullptr && seq->isArray());
    CHECK_EQ(seq->array.size(), 7u);
}

TEST(joseki_tool_size_mismatch_refused) {
    Game g(9);  // 9 路，而定式是 19 路。
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

TEST(joseki_tool_unknown_name_and_no_library_honest) {
    Game g(19);
    AgentToolContext ctx;
    ctx.game = g;
    ctx.joseki = std::make_shared<const JosekiLibrary>(JosekiLibrary::builtin());
    const ToolRegistry r = buildAgentTools(ctx);
    const Tool* t = r.find("joseki_lookup");
    const ToolResult res = t->invoke(R"({"name":"不存在的定式"})");
    CHECK(!res.ok);
    CHECK(res.error.find("还没收录") != std::string::npos);

    // 无词典 → 诚实回绝。
    AgentToolContext ctx2;
    ctx2.game = g;
    const ToolRegistry r2 = buildAgentTools(ctx2);
    const Tool* t2 = r2.find("joseki_lookup");
    const ToolResult res2 = t2->invoke(R"({"name":"点三三"})");
    CHECK(!res2.ok);
}
