#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/agent/agent_tools.h"
#include "ai/harness_eval.h"
#include "ai/json.h"

using namespace vgo;

TEST(harness_routing_golden_passes_gate) {
    const HarnessEvalReport r = evaluateHarnessRouting(builtinHarnessGolden());
    CHECK_EQ(r.total, static_cast<int>(builtinHarnessGolden().size()));
    CHECK(r.classify_hit_rate >= 0.9);
    CHECK(r.pass());
}

TEST(harness_routing_golden_is_hermetic) {
    // golden 集覆盖全部 6 类问题（改写说法 + 复合意图），保证回归闸门有代表性。
    bool life = false, quality = false, winning = false, where = false, why = false,
         other = false;
    for (const HarnessGoldenCase& g : builtinHarnessGolden()) {
        switch (g.expected_cls) {
            case QuestionClass::LifeDeath: life = true; break;
            case QuestionClass::MoveQuality: quality = true; break;
            case QuestionClass::WhoIsWinning: winning = true; break;
            case QuestionClass::WhereToPlay: where = true; break;
            case QuestionClass::WhyThisMove: why = true; break;
            case QuestionClass::Other: other = true; break;
        }
    }
    CHECK(life && quality && winning && where && why && other);
}

TEST(harness_sync_tools_expose_full_capability) {
    // D1 完成判据：sync 能力（count_territory / life_death_solve / ownership_map /
    // dead_groups / analyze_position）全部注册为工具；async 的 evaluate_* 因
    // Tool::invoke 同步契约而不得注册（改由 handleMoveQuality 异步注入）。
    AgentToolContext ctx;
    ctx.game = Game(9);
    const ToolRegistry r = buildAgentTools(ctx);

    CHECK(r.find("count_territory") != nullptr);
    CHECK(r.find("life_death_solve") != nullptr);
    CHECK(r.find("ownership_map") != nullptr);
    CHECK(r.find("dead_groups") != nullptr);
    CHECK(r.find("analyze_position") != nullptr);
    CHECK(r.find("evaluate_move") == nullptr);
    CHECK(r.find("evaluate_last_move") == nullptr);
}

TEST(harness_tool_schema_is_valid_json_object) {
    // 每个工具声明可被端点解析的 JSON schema（function-calling 的 parameters）。
    AgentToolContext ctx;
    ctx.game = Game(9);
    const ToolRegistry r = buildAgentTools(ctx);
    for (const Tool& t : r.all()) {
        Json j;
        CHECK(parseJson(t.json_schema, &j));
        CHECK(j.isObject());
    }
}
