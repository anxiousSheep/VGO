#include "test_framework.h"

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
