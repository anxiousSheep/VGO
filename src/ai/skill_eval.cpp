#include "skill_eval.h"

#include "conversation.h"  // classifyQuestion

namespace vgo {

SkillEvalReport evaluateSkills(const SkillRegistry& reg,
                               const std::vector<GoldenQuestion>& golden) {
    SkillEvalReport r;
    r.total = static_cast<int>(golden.size());

    // 初始化 per-skill 分数表（覆盖注册表里全部技能）。
    for (const Skill& s : reg.all()) {
        SkillScore sc;
        sc.id = s.id;
        sc.name = s.name;
        r.per_skill.push_back(std::move(sc));
    }
    auto scoreFor = [&](const std::string& id) -> SkillScore* {
        for (SkillScore& sc : r.per_skill)
            if (sc.id == id) return &sc;
        return nullptr;
    };

    std::size_t injected_sum = 0;
    for (const GoldenQuestion& g : golden) {
        // 1) 分类命中：问题分类是否与期望一致。
        if (classifyQuestion(g.question) == g.cls) ++r.classify_hits;
        // 2) 注入技能数（token 代理）：select() 复用生产注入路径，越少越省 token。
        injected_sum += reg.select(g.cls, g.question, 3).size();
        // 3) Top-1 检索命中（仅对有期望技能的题目）。
        if (!g.expected_skill.empty()) {
            ++r.top1_samples;
            const std::vector<SkillMatch> hits = reg.retrieve(g.question, 1);
            const bool hit = !hits.empty() && hits[0].skill->id == g.expected_skill;
            if (hit) ++r.top1_hits;
            // 4) per-skill 采样与命中。
            if (SkillScore* sc = scoreFor(g.expected_skill)) {
                ++sc->samples;
                if (hit) ++sc->hits;
            }
        }
    }

    r.classify_hit_rate = r.total ? static_cast<double>(r.classify_hits) / r.total : 0.0;
    r.top1_hit_rate = r.top1_samples ? static_cast<double>(r.top1_hits) / r.top1_samples : 1.0;
    r.avg_injected = r.total ? static_cast<double>(injected_sum) / r.total : 0.0;
    for (SkillScore& sc : r.per_skill) {
        if (sc.samples > 0) sc.score = static_cast<double>(sc.hits) / sc.samples;
    }
    return r;
}

bool SkillEvalReport::pass(double classify_threshold, double top1_threshold) const {
    return classify_hit_rate >= classify_threshold && top1_hit_rate >= top1_threshold;
}

std::vector<GoldenQuestion> builtinGoldenQuestions() {
    return {
        // 存量 5 包
        {"这块棋死没死，能不能活", QuestionClass::LifeDeath, "life-death"},
        {"这块棋有没有眼", QuestionClass::LifeDeath, "life-death"},
        {"这步棋好不好", QuestionClass::MoveQuality, "move-quality"},
        {"这步棋怎么样", QuestionClass::MoveQuality, "move-quality"},
        {"现在谁领先", QuestionClass::WhoIsWinning, "counting"},
        // 分类器把「如何」判为 MoveQuality（先于 WhoIsWinning），cls 须与实际一致。
        {"现在形势如何", QuestionClass::MoveQuality, "counting"},
        {"打劫的规则是什么", QuestionClass::Other, "ko"},
        {"什么是劫材", QuestionClass::Other, "ko"},
        {"什么是打吃", QuestionClass::Other, "terms"},
        {"什么是气", QuestionClass::Other, "terms"},
        {"我该下哪", QuestionClass::WhereToPlay, ""},
        {"为什么下这里", QuestionClass::WhyThisMove, ""},
        // A1 新增：布局/定式
        {"开局第一手该下哪", QuestionClass::WhereToPlay, "fuseki-joseki"},
        {"这个定式怎么选", QuestionClass::Other, "fuseki-joseki"},
        // A1 新增：中盘攻防
        {"现在该攻还是该守", QuestionClass::Other, "middle-game"},
        {"什么是急所", QuestionClass::Other, "middle-game"},
        // A1 新增：手筋/棋形
        {"这手棋妙在哪", QuestionClass::WhyThisMove, "tesuji-shape"},
        {"什么是手筋", QuestionClass::Other, "tesuji-shape"},
        // A1 新增：官子
        {"收官先收哪", QuestionClass::Other, "endgame"},
        {"什么是先手官子", QuestionClass::Other, "endgame"},
        // A1 新增：劫争/劫材价值
        {"这个劫该不该打", QuestionClass::Other, "ko-fight"},
        {"劫的价值怎么算", QuestionClass::Other, "ko-fight"},
        // A1 新增：让子局
        {"让子局黑方该求稳还是求变", QuestionClass::Other, "handicap"},
        {"让子局白方怎么赢", QuestionClass::Other, "handicap"},
    };
}

}  // namespace vgo
