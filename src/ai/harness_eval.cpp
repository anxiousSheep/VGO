#include "harness_eval.h"

namespace vgo {

HarnessEvalReport evaluateHarnessRouting(const std::vector<HarnessGoldenCase>& golden) {
    HarnessEvalReport r;
    r.total = static_cast<int>(golden.size());
    for (const HarnessGoldenCase& g : golden) {
        if (classifyQuestion(g.question) == g.expected_cls) ++r.classify_hits;
    }
    r.classify_hit_rate = r.total > 0 ? static_cast<double>(r.classify_hits) /
                                            static_cast<double>(r.total)
                                      : 0.0;
    return r;
}

bool HarnessEvalReport::pass(double threshold) const {
    return total > 0 && classify_hit_rate >= threshold;
}

std::vector<HarnessGoldenCase> builtinHarnessGolden() {
    return {
        // 死活（含改写说法：这块棋 / 能活吗 / 眼睛）。
        {"这块棋死了吗", QuestionClass::LifeDeath},
        {"右上角这块黑棋能活吗", QuestionClass::LifeDeath},
        {"这步棋能活吗", QuestionClass::LifeDeath},
        {"白棋这块是不是死棋", QuestionClass::LifeDeath},
        // 落点质量（先于 WhyThisMove，避免「为什么这步不好」被误判）。
        {"这步下得怎么样", QuestionClass::MoveQuality},
        {"C3 这步好不好", QuestionClass::MoveQuality},
        {"我是不是下了一手恶手", QuestionClass::MoveQuality},
        // 谁领先 / 形势。
        {"现在谁领先", QuestionClass::WhoIsWinning},
        {"现在的形势", QuestionClass::WhoIsWinning},
        {"目数上谁占优", QuestionClass::WhoIsWinning},
        // 落点建议 / 我该下哪。
        {"我该下在哪里", QuestionClass::WhereToPlay},
        {"请给我点建议", QuestionClass::WhereToPlay},
        {"下一步走哪比较好", QuestionClass::WhereToPlay},
        // 为什么下这里（最近一手）。
        {"为什么下这里", QuestionClass::WhyThisMove},
        {"这手棋是什么意思", QuestionClass::WhyThisMove},
        // 其他（打劫 / 术语等，无引擎事实，走工具按需取）。
        {"什么是打劫", QuestionClass::Other},
        {"什么叫提子", QuestionClass::Other},
    };
}

}  // namespace vgo
