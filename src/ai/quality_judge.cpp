#include "quality_judge.h"

#include "json.h"

namespace vgo {

namespace {

double clampScore(double v) {
    if (v < 1.0) return 1.0;
    if (v > 5.0) return 5.0;
    return v;
}

}  // namespace

std::string buildJudgeSystemPrompt() {
    return
        "你是一位严谨的围棋讲解质量评估员。请从四个维度给下面这条围棋讲解打分，"
        "每维 1（很差）到 5（优秀）：\n"
        "1. correctness 正确性：讲解是否与提供的引擎真值一致，不编造胜率/目数/坐标/棋手意图。\n"
        "2. professionalism 专业性：领域深度、术语是否准确、逻辑是否成立。\n"
        "3. tone 语气：是否人性化、面向棋手、无机械感、不生硬。\n"
        "4. structure 结构：是否符合约定讲解结构（结论→依据→关键变化→建议/鼓励）。\n"
        "只输出一个 JSON 对象，格式：\n"
        "{\"correctness\":<1-5>,\"professionalism\":<1-5>,\"tone\":<1-5>,"
        "\"structure\":<1-5>,\"note\":\"一句话理由\"}\n"
        "不要输出任何 JSON 以外的内容。\n";
}

std::string buildJudgePrompt(const QualityCase& c) {
    std::string s = "请评估下面这条围棋讲解的质量。\n";
    s += "提问：" + c.question + "\n";
    s += "引擎真值（ground truth，供对照）：" + c.reference_truth + "\n";
    if (!c.phase.empty()) s += "局面阶段：" + c.phase + "\n";
    s += "待打分回答：\n" + c.reply + "\n";
    s += "请输出 JSON 打分结果。\n";
    return s;
}

bool parseJudgeVerdict(const std::string& reply, JudgeVerdict* out) {
    if (!out) return false;
    out->valid = false;

    // 剥离 ``` 代码围栏 / 前后说明文字：截取第一个 '{' 到最后一个 '}'。
    std::string text = reply;
    const std::size_t b = text.find('{');
    const std::size_t e = text.rfind('}');
    if (b != std::string::npos && e != std::string::npos && e >= b)
        text = text.substr(b, e - b + 1);

    Json j;
    if (!parseJson(text, &j) || !j.isObject()) return false;

    const Json* c = j.get("correctness");
    const Json* p = j.get("professionalism");
    const Json* t = j.get("tone");
    const Json* s = j.get("structure");

    out->scores.correctness = clampScore(c ? c->asNumber(0.0) : 0.0);
    out->scores.professionalism = clampScore(p ? p->asNumber(0.0) : 0.0);
    out->scores.tone = clampScore(t ? t->asNumber(0.0) : 0.0);
    out->scores.structure = clampScore(s ? s->asNumber(0.0) : 0.0);
    if (const Json* n = j.get("note")) out->note = n->asString("");

    // 四维字段齐全才算解析成功；越界分数已钳制，不判失败。
    out->valid = (c && p && t && s);
    return out->valid;
}

std::vector<QualityCase> builtinQualityCases() {
    return {
        {"为什么下这里", "黑领先 3 目 / 黑胜率 55%", "", "中盘"},
        {"现在谁领先", "黑领先 5.5 目", "", "中盘"},
        {"这块棋能活吗", "该块棋有两只眼，是活棋", "", "中盘"},
    };
}

}  // namespace vgo
