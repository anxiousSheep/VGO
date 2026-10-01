#include "template_conversation.h"

#include <cmath>
#include <utility>

#include "gtp.h"

namespace vgo {

void TemplateConversation::answer(const ConversationRequest& req, Callback cb) {
    ConversationReply r;
    switch (classifyQuestion(req.question)) {
        case QuestionClass::WhoIsWinning: {
            const double lead = req.score_black;
            std::string who;
            if (lead > 0.5) who = "黑方";
            else if (lead < -0.5) who = "白方";
            else who = "双方大致";
            r.text = "当前" + who + "领先约 " +
                     std::to_string(static_cast<int>(std::lround(std::abs(lead)))) +
                     " 目（数子法，已含贴目）。";
            if (req.profile.teaching && req.move_number < 60)
                r.text += " 中盘目数还会变化，先稳住自己的棋。";
            break;
        }
        case QuestionClass::WhereToPlay: {
            const auto& a = req.facts.analysis;
            if (a.valid && !a.candidates.empty()) {
                const MoveCandidate& best = a.candidates.front();
                r.text = "建议考虑 " + gtpPoint(best.p, req.size) + "。";
                r.highlight.push_back(best.p);
                for (std::size_t i = 1; i < a.candidates.size() && i < 4; ++i) {
                    r.text += " 其他候选：" + gtpPoint(a.candidates[i].p, req.size) + " ";
                    r.highlight.push_back(a.candidates[i].p);
                }
            } else {
                r.text = "当前没有接入分析引擎，我暂时给不出具体的推荐点。";
                r.unsure = true;
            }
            break;
        }
        case QuestionClass::WhyThisMove: {
            const std::string exp = renderExplanation(req.facts.facts, req.last_mover);
            r.text = exp.empty() ? "这一步是普通落子，暂时没有更多可说的。" : exp;
            break;
        }
        case QuestionClass::Other:
        default:
            r.text = "这个问题我暂时还不会回答（未接入语言模型时只能回答有限的问题）。"
                     "你可以问我「谁领先」「下哪里好」「为什么下这里」。";
            r.unsure = true;
            break;
    }
    cb(std::move(r));
}

}  // namespace vgo
