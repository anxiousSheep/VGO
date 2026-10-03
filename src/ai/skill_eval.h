#pragma once

#include <string>
#include <vector>

#include "skill.h"

namespace vgo {

// 一条 golden Q&A 样本（D4.5）：评估闭环的「标准答案」。
struct GoldenQuestion {
    std::string question;        // 自然语言提问
    QuestionClass cls;           // 期望的分类
    std::string expected_skill;  // 期望 Top-1 命中的技能 id；空 = 不校验 Top-1
};

// 单个技能在 golden 集上的可复跑评分。
struct SkillScore {
    std::string id;
    std::string name;
    int samples = 0;   // 期望命中该技能的题目数
    int hits = 0;      // 其中 Top-1 命中的题目数
    double score = -1.0;  // hit rate；-1 = 无样本
};

// 一次评估的完整报告（D4.5 / D5.7 release gate 的判据）。
struct SkillEvalReport {
    int total = 0;
    int classify_hits = 0;
    int top1_samples = 0;   // 有 expected_skill 的题目数
    int top1_hits = 0;
    double classify_hit_rate = 0.0;  // 分类命中率
    double top1_hit_rate = 0.0;      // Top-1 检索命中率（无样本时为 1.0）
    double avg_injected = 0.0;       // 平均每题注入的技能数（token 代理指标）
    std::vector<SkillScore> per_skill;

    // release gate：分类命中率与 Top-1 命中率都达到阈值才算通过。
    bool pass(double classify_threshold = 0.9, double top1_threshold = 0.9) const;
};

// 用 golden 集对一个技能注册表打分（纯逻辑、可复跑）。这是 L10「持续优化 Skills」
// 的离线完成标准：改动技能后跑一遍，命中率不得回退。
SkillEvalReport evaluateSkills(const SkillRegistry& reg,
                               const std::vector<GoldenQuestion>& golden);

// 随代码内置的 golden 集（覆盖内置技能），保证 CTest 回归闸门 hermetic、可复跑。
std::vector<GoldenQuestion> builtinGoldenQuestions();

}  // namespace vgo
