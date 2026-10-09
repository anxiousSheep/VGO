#include "test_framework.h"

#include "ai/quality_judge.h"

using namespace vgo;

// --- B4：讲解质量评估（纯逻辑：prompt 构造 + 打分结果解析） ---

TEST(quality_judge_prompt_contains_dimensions_and_truth) {
    QualityCase c;
    c.question = "为什么下这里";
    c.reference_truth = "黑领先 3 目";
    c.reply = "因为这里能守住角。";
    c.phase = "中盘";

    const std::string p = buildJudgePrompt(c);
    CHECK(p.find("为什么下这里") != std::string::npos);
    CHECK(p.find("黑领先 3 目") != std::string::npos);
    CHECK(p.find("因为这里能守住角。") != std::string::npos);
    CHECK(p.find("中盘") != std::string::npos);

    const std::string sys = buildJudgeSystemPrompt();
    CHECK(sys.find("correctness") != std::string::npos);
    CHECK(sys.find("professionalism") != std::string::npos);
    CHECK(sys.find("tone") != std::string::npos);
    CHECK(sys.find("structure") != std::string::npos);
}

TEST(quality_judge_parse_normal) {
    JudgeVerdict v;
    CHECK(parseJudgeVerdict(
        "{\"correctness\":5,\"professionalism\":4,\"tone\":3,\"structure\":4,\"note\":\"不错\"}",
        &v));
    CHECK(v.valid);
    CHECK_EQ(v.scores.correctness, 5.0);
    CHECK_EQ(v.scores.professionalism, 4.0);
    CHECK_EQ(v.scores.tone, 3.0);
    CHECK_EQ(v.scores.structure, 4.0);
    CHECK(v.note.find("不错") != std::string::npos);
}

TEST(quality_judge_parse_fenced) {
    JudgeVerdict v;
    const std::string reply =
        "```json\n{\"correctness\":5,\"professionalism\":4,\"tone\":3,\"structure\":4,"
        "\"note\":\"好\"}\n```";
    CHECK(parseJudgeVerdict(reply, &v));
    CHECK(v.valid);
    CHECK_EQ(v.scores.correctness, 5.0);
}

TEST(quality_judge_parse_clamps_out_of_range) {
    JudgeVerdict v;
    CHECK(parseJudgeVerdict(
        "{\"correctness\":99,\"professionalism\":-3,\"tone\":3,\"structure\":4}", &v));
    CHECK(v.valid);
    CHECK_EQ(v.scores.correctness, 5.0);        // 钳制到 5
    CHECK_EQ(v.scores.professionalism, 1.0);    // 钳制到 1
}

TEST(quality_judge_parse_malformed) {
    JudgeVerdict v;
    CHECK(!parseJudgeVerdict("not json at all", &v));
    CHECK(!v.valid);
}

TEST(quality_judge_parse_missing_field) {
    JudgeVerdict v;
    CHECK(!parseJudgeVerdict("{\"correctness\":5,\"tone\":4}", &v));
    CHECK(!v.valid);
}

TEST(quality_judge_builtin_cases_nonempty) {
    const auto cases = builtinQualityCases();
    CHECK(!cases.empty());
    for (const auto& c : cases) {
        CHECK(!c.question.empty());
        CHECK(!c.reference_truth.empty());
    }
}
