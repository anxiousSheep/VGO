#include "test_framework.h"

#include <cmath>

#include "ai/skill.h"

using namespace vgo;

TEST(skill_builtin_has_five) {
    SkillRegistry r = SkillRegistry::builtin();
    CHECK_EQ(r.size(), 5u);
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
