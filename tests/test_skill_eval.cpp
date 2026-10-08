#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/skill_eval.h"

using namespace vgo;

TEST(eval_builtin_golden_passes_gate) {
    const SkillRegistry reg = SkillRegistry::builtin();
    const SkillEvalReport r = evaluateSkills(reg, builtinGoldenQuestions());
    CHECK_EQ(r.total, static_cast<int>(builtinGoldenQuestions().size()));
    CHECK(r.classify_hit_rate >= 0.9);
    CHECK(r.top1_hit_rate >= 0.9);
    CHECK(r.pass());
    // 每个有样本的技能都有可复跑评分（在 [0,1] 内）。
    bool has_scored = false;
    for (const SkillScore& sc : r.per_skill) {
        if (sc.samples > 0) {
            has_scored = true;
            CHECK(sc.score >= 0.0);
            CHECK(sc.score <= 1.0);
        }
    }
    CHECK(has_scored);
}

TEST(eval_avg_injected_bounded) {
    const SkillRegistry reg = SkillRegistry::builtin();
    const SkillEvalReport r = evaluateSkills(reg, builtinGoldenQuestions());
    // 注入数应远小于技能总数（渐进式披露：不是把 5 个技能全灌进去）。
    CHECK(r.avg_injected <= static_cast<double>(reg.size()));
    CHECK(r.avg_injected >= 1.0);  // 每题至少命中一个技能（terms 全类适用）
}

TEST(eval_regression_is_detected) {
    // 把 counting 技能彻底改成无关内容（含 name/description，避免残留「形势/领先」等词
    // 让检索仍命中），评估应检测到命中率回退。
    SkillRegistry reg = SkillRegistry::builtin();
    Skill broken = *reg.find("counting");
    broken.name = "水果知识";
    broken.description = "与围棋完全无关";
    broken.prompt = "香蕉、苹果、橘子、西瓜。";
    reg.upsert(broken);

    const SkillEvalReport r = evaluateSkills(reg, builtinGoldenQuestions());
    CHECK(r.top1_hit_rate < 1.0);
    CHECK(!r.pass(1.0, 1.0));  // 用 100% 阈值必然不通过
}

TEST(eval_per_skill_score_reporting) {
    const SkillRegistry reg = SkillRegistry::builtin();
    const SkillEvalReport r = evaluateSkills(reg, builtinGoldenQuestions());
    // counting 有 golden 样本，且 score 已算出（>= 0）。
    bool found = false;
    for (const SkillScore& sc : r.per_skill) {
        if (sc.id == "counting") {
            found = true;
            CHECK(sc.samples > 0);
            CHECK(sc.score >= 0.0);
        }
    }
    CHECK(found);
}
