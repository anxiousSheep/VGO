#pragma once

#include <string>
#include <vector>

namespace vgo {

// B4：4 维讲解质量 rubric，每维 1..5 分。
struct QualityRubric {
    double correctness = 0.0;      // 正确性（对照引擎真值，不编造）
    double professionalism = 0.0;  // 专业性（领域深度、术语准确）
    double tone = 0.0;             // 语气（人性化、面向棋手、无机械感）
    double structure = 0.0;        // 结构（符合 A3 三档契约）
};

struct QualityCase {
    std::string question;         // 提问
    std::string reference_truth;  // 引擎真值摘要（ground truth，供 judge 对照）
    std::string reply;            // 待打分回答（模型实际产出；空 = 需现场生成）
    std::string phase;            // 可选：局面阶段（影响结构档）
};

struct JudgeVerdict {
    QualityRubric scores;
    std::string note;   // judge 一句话理由
    bool valid = false; // 解析成功（四维字段齐全）
};

// judge system prompt（4 维定义 + 打分标准 + 输出 JSON schema）。
std::string buildJudgeSystemPrompt();

// 单条打分 prompt（含真值 + 回答 + 要求输出 JSON 打分）。
std::string buildJudgePrompt(const QualityCase& c);

// 从 judge 回复解析 4 维分数（剥离围栏、解析 JSON、钳制 1..5、容错畸形/缺字段）。
// 返回 out->valid：四维字段齐全才为 true；越界分数被钳制而非判失败。
bool parseJudgeVerdict(const std::string& reply, JudgeVerdict* out);

// 内置最小 golden Q&A 集（纯数据，供离线工具与格式校验 CTest 复用）。
std::vector<QualityCase> builtinQualityCases();

}  // namespace vgo
