#include "test_framework.h"

#include <cmath>

#include "ai/skill.h"

using namespace vgo;

TEST(skill_builtin_has_eleven) {
    // A1：5 个存量包加厚 + 6 个新包（布局/中盘/手筋/官子/劫争/让子）。
    SkillRegistry r = SkillRegistry::builtin();
    CHECK_EQ(r.size(), 11u);
}

TEST(skill_for_question_matches) {
    SkillRegistry r = SkillRegistry::builtin();
    bool has_life = false, has_move = false, has_ko = false, has_terms = false;
    for (const Skill* s : r.forQuestion(QuestionClass::LifeDeath)) {
        if (s->id == "life-death") has_life = true;
        if (s->id == "terms") has_terms = true;
    }
    for (const Skill* s : r.forQuestion(QuestionClass::MoveQuality))
        if (s->id == "move-quality") has_move = true;
    for (const Skill* s : r.forQuestion(QuestionClass::Other))
        if (s->id == "ko") has_ko = true;
    CHECK(has_life);
    CHECK(has_move);
    CHECK(has_ko);
    CHECK(has_terms);  // terms applies to all
}

TEST(skill_text_roundtrip) {
    Skill s;
    s.id = "life-death";
    s.name = "死活基础";
    s.applies_to = {QuestionClass::LifeDeath, QuestionClass::WhoIsWinning};
    s.prompt = "两眼活，一眼死。\n第二行。";
    const std::string text = SkillRegistry::toText(s);

    Skill out;
    CHECK(SkillRegistry::fromText(text, &out));
    CHECK_EQ(out.id, std::string("life-death"));
    CHECK_EQ(out.name, std::string("死活基础"));
    CHECK_EQ(out.applies_to.size(), 2u);
    CHECK_EQ(out.prompt, std::string("两眼活，一眼死。\n第二行。"));
}

TEST(skill_upsert_replaces_by_id) {
    SkillRegistry r;
    Skill a;
    a.id = "x";
    a.name = "A";
    r.upsert(a);
    CHECK_EQ(r.size(), 1u);
    Skill b;
    b.id = "x";
    b.name = "B";
    CHECK(r.upsert(b));
    CHECK_EQ(r.size(), 1u);
    CHECK_EQ(r.find("x")->name, std::string("B"));
}

TEST(question_class_string_roundtrip) {
    for (QuestionClass q : {QuestionClass::WhyThisMove, QuestionClass::WhereToPlay,
                            QuestionClass::WhoIsWinning, QuestionClass::MoveQuality,
                            QuestionClass::LifeDeath, QuestionClass::Other}) {
        QuestionClass out = QuestionClass::Other;
        CHECK(questionClassFromString(questionClassToString(q), &out));
        CHECK(out == q);
    }
    QuestionClass o;
    CHECK(!questionClassFromString("nope", &o));
}

TEST(skill_frontmatter_roundtrip) {
    Skill s;
    s.id = "life-death";
    s.name = "死活基础";
    s.description = "两眼活一眼死";
    s.applies_to = {QuestionClass::LifeDeath};
    s.version = 3;
    s.last_score = 0.71;
    s.prompt = "两眼活。";
    const std::string text = SkillRegistry::toText(s);
    Skill out;
    CHECK(SkillRegistry::fromText(text, &out));
    CHECK_EQ(out.id, std::string("life-death"));
    CHECK_EQ(out.description, std::string("两眼活一眼死"));
    CHECK_EQ(out.version, 3);
    CHECK(std::abs(out.last_score - 0.71) < 1e-9);
}

TEST(skill_backward_compat_from_text) {
    // Old format (id/name/applies only) still parses.
    const std::string old =
        "id: ko\nname: 打劫规则\napplies: other\n---\n打劫：一方提子后。\n";
    Skill out;
    CHECK(SkillRegistry::fromText(old, &out));
    CHECK_EQ(out.id, std::string("ko"));
    CHECK_EQ(out.prompt, std::string("打劫：一方提子后。"));
    CHECK_EQ(out.version, 1);       // default
    CHECK_EQ(out.last_score, -1.0); // never evaluated
}

TEST(skill_retrieve_ko) {
    SkillRegistry r = SkillRegistry::builtin();
    const auto hits = r.retrieve("打劫的规则是什么", 1);
    CHECK_EQ(hits.size(), 1u);
    CHECK(hits[0].skill != nullptr);
    CHECK_EQ(hits[0].skill->id, std::string("ko"));
    CHECK(!hits[0].reason.empty());
}

TEST(skill_retrieve_life_death) {
    SkillRegistry r = SkillRegistry::builtin();
    const auto hits = r.retrieve("这块棋死没死，能不能活", 1);
    CHECK_EQ(hits.size(), 1u);
    CHECK_EQ(hits[0].skill->id, std::string("life-death"));
}

TEST(skill_select_dedup_and_category_first) {
    SkillRegistry r = SkillRegistry::builtin();
    // LifeDeath 类：forQuestion 先注入 life-death + terms，检索再补足，但不得重复。
    const auto chosen = r.select(QuestionClass::LifeDeath, "这块棋死没死", 3);
    bool has_life = false, has_terms = false;
    std::size_t life_count = 0;
    for (const Skill* s : chosen) {
        if (s->id == "life-death") { has_life = true; ++life_count; }
        if (s->id == "terms") has_terms = true;
    }
    CHECK(has_life);
    CHECK(has_terms);
    CHECK_EQ(life_count, 1u);  // 去重：life-death 只出现一次
    // 类别白名单优先：life-death 应排在最前。
    CHECK_EQ(chosen.front()->id, std::string("life-death"));
}

TEST(skill_select_budget_truncates) {
    SkillRegistry r = SkillRegistry::builtin();
    // 预算极小，只放得下第一个技能。
    const Skill* first = r.forQuestion(QuestionClass::LifeDeath).front();
    const auto chosen = r.select(QuestionClass::LifeDeath, "这块棋死没死", 3,
                                 first->prompt.size());
    CHECK_EQ(chosen.size(), 1u);
    CHECK_EQ(chosen[0]->id, std::string("life-death"));
}

TEST(skill_pending_adopt) {
    SkillRegistry r = SkillRegistry::builtin();
    CHECK(r.addPending("ko", "补充：三劫循环属无胜负。"));
    CHECK(r.addPending("ko", "再补充：找劫材要算清劫的价值。"));
    CHECK(r.adoptPending("ko"));
    const Skill* s = r.find("ko");
    CHECK(s->pending.empty());
    CHECK(s->prompt.find("三劫循环") != std::string::npos);
    CHECK(s->prompt.find("劫的价值") != std::string::npos);
    // 已无 pending，再次采纳应失败。
    CHECK(!r.adoptPending("ko"));
}

TEST(skill_set_score) {
    SkillRegistry r = SkillRegistry::builtin();
    CHECK(r.setScore("counting", 0.81));
    CHECK(r.find("counting")->last_score == 0.81);
    CHECK(!r.setScore("nonexistent", 0.5));
}

TEST(skill_validate) {
    Skill good;
    good.id = "x";
    good.prompt = "正文";
    good.version = 1;
    std::string err;
    CHECK(validateSkill(good, &err));
    CHECK(err.empty());

    Skill no_id = good;
    no_id.id = "";
    CHECK(!validateSkill(no_id, &err));
    CHECK(!err.empty());

    Skill no_body = good;
    no_body.prompt = "";
    CHECK(!validateSkill(no_body, &err));

    Skill bad_ver = good;
    bad_ver.version = 0;
    CHECK(!validateSkill(bad_ver, &err));
}

TEST(skill_from_text_rejects_oversize) {
    SkillRegistry r = SkillRegistry::builtin();
    const Skill* base = r.find("terms");
    std::string text = "id: huge\n---\n" + std::string(kMaxSkillBodyChars + 1, 'x');
    Skill out;
    CHECK(!SkillRegistry::fromText(text, &out));
    (void)base;
}

TEST(skill_pending_roundtrip) {
    Skill s;
    s.id = "ko";
    s.name = "打劫规则";
    s.prompt = "打劫：一方提子后不能立即提回。";
    s.pending = {"补充A", "补充B"};
    const std::string text = SkillRegistry::toText(s);
    Skill out;
    CHECK(SkillRegistry::fromText(text, &out));
    CHECK_EQ(out.pending.size(), 2u);
    CHECK_EQ(out.pending[0], std::string("补充A"));
    CHECK_EQ(out.pending[1], std::string("补充B"));
}

// --- D4.2 渐进式披露：frontmatter 常驻、body 按需加载 ---

TEST(skill_frontmatter_lists_without_body) {
    SkillRegistry r = SkillRegistry::builtin();
    const auto metas = r.frontmatter();
    CHECK_EQ(metas.size(), 11u);
    const SkillMeta* ko = nullptr;
    for (const SkillMeta& m : metas) {
        CHECK(!m.id.empty());
        CHECK(!m.name.empty());
        if (m.id == "ko") ko = &m;
    }
    CHECK(ko != nullptr);
    CHECK_EQ(ko->name, std::string("打劫规则"));
}

TEST(skill_frontmatter_matches_skill) {
    SkillRegistry r = SkillRegistry::builtin();
    const Skill* s = r.find("counting");
    const SkillMeta m = s->frontmatter();
    CHECK_EQ(m.id, std::string("counting"));
    CHECK_EQ(m.name, s->name);
    CHECK_EQ(m.description, s->description);
    CHECK_EQ(m.version, s->version);
    CHECK_EQ(m.last_score, s->last_score);
    CHECK_EQ(m.applies_to.size(), s->applies_to.size());
}

// --- D4.6 模型提议改写：提示词 + 回复提取 + 校验沉淀 pending ---

TEST(skill_rewrite_prompt_contains_context) {
    SkillRegistry r = SkillRegistry::builtin();
    const Skill* s = r.find("ko");
    const std::string p = buildRewritePrompt(*s, {"什么是劫材", "打劫怎么找劫材"});
    CHECK(p.find("打劫规则") != std::string::npos);
    CHECK(p.find("什么是劫材") != std::string::npos);
    CHECK(p.find("打劫怎么找劫材") != std::string::npos);
    CHECK(p.find(s->prompt) != std::string::npos);
}

TEST(skill_extract_rewrite_body_fenced) {
    const std::string reply = "以下是改写：\n```\n新的正文内容。\n```\n希望对你有帮助。";
    CHECK_EQ(extractRewriteBody(reply), std::string("新的正文内容。"));
}

TEST(skill_extract_rewrite_body_lang_tag) {
    const std::string reply = "```markdown\n打劫新正文。\n```";
    CHECK_EQ(extractRewriteBody(reply), std::string("打劫新正文。"));
}

TEST(skill_extract_rewrite_body_plain) {
    const std::string reply = "  直接给出的正文。  ";
    CHECK_EQ(extractRewriteBody(reply), std::string("直接给出的正文。"));
}

TEST(skill_extract_rewrite_body_no_fence) {
    // 没有围栏时的退化：整体去首尾空白（仍给一个可用候选）。
    const std::string reply = " 打劫：一方提子后不能立即提回。 ";
    CHECK(!extractRewriteBody(reply).empty());
}

TEST(skill_propose_rewrite_validates) {
    SkillRegistry r = SkillRegistry::builtin();
    CHECK(r.proposeRewrite("ko", " 补充：三劫循环属无胜负。 "));
    const Skill* s = r.find("ko");
    CHECK_EQ(s->pending.size(), 1u);
    CHECK_EQ(s->pending[0], std::string("补充：三劫循环属无胜负。"));
    // 空候选 / 超体积候选被拒。
    CHECK(!r.proposeRewrite("ko", "   "));
    CHECK(!r.proposeRewrite("ko", std::string(kMaxSkillBodyChars + 1, 'x')));
    // 采纳后 pending 清空、正文含新内容。
    CHECK(r.adoptPending("ko"));
    CHECK(s->prompt.find("三劫循环") != std::string::npos);
}

TEST(skill_propose_rewrite_unknown_id) {
    SkillRegistry r = SkillRegistry::builtin();
    CHECK(!r.proposeRewrite("nonexistent", "正文"));
}
