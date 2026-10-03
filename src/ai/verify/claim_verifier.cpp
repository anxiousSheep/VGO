#include "claim_verifier.h"

#include <cmath>
#include <string>

#include "../lifedeath.h"

namespace vgo {

std::vector<ClaimVerdict> ClaimVerifier::verify(
    const std::vector<Claim>& claims, const Board& board,
    const OwnershipMap& ownership, const Analysis& analysis, double score_black,
    Color to_move, const Tolerances& tol) const {
    std::vector<ClaimVerdict> out;
    out.reserve(claims.size());

    const bool blackToMove = (to_move == Color::Black);
    const bool has_winrate = analysis.valid;
    const bool has_ownership = !ownership.empty() && ownership.size == board.size();

    // Black-perspective truth for lead and winrate.
    const double truth_lead_black =
        analysis.valid ? (blackToMove ? analysis.score_lead : -analysis.score_lead)
                       : score_black;
    const double truth_winrate_black =
        has_winrate ? (blackToMove ? analysis.winrate : 1.0 - analysis.winrate)
                    : 0.0;

    for (const Claim& c : claims) {
        if (!c.valid) continue;  // malformed claims are dropped by the extractor
        ClaimVerdict v;
        v.claim = c;

        switch (c.type) {
            case ClaimType::ScoreLead: {
                // Convert the claim to black-lead for a common comparison.
                const double claim_black =
                    (c.color == Color::White) ? -c.value : c.value;
                v.claim.source = analysis.valid ? "KataGo 分析" : "规则引擎";
                const double diff = std::abs(claim_black - truth_lead_black);
                if (diff <= tol.lead_points) {
                    v.disposition = ClaimDisposition::Pass;
                } else {
                    v.disposition = ClaimDisposition::Corrected;
                    v.truth = truth_lead_black;
                    v.note = diff > tol.lead_correctable
                                 ? "目数与引擎真值偏差过大，已按引擎数据修正"
                                 : "目数略有偏差，已按引擎数据修正";
                }
                break;
            }
            case ClaimType::Winrate: {
                if (!has_winrate) {
                    v.disposition = ClaimDisposition::Unverifiable;
                    v.claim.source = "模型推断";
                    v.note = "无 KataGo 胜率真值";
                } else {
                    const double claim_black =
                        (c.color == Color::White) ? 1.0 - c.value : c.value;
                    v.claim.source = "KataGo 分析";
                    const double diff = std::abs(claim_black - truth_winrate_black);
                    if (diff <= tol.winrate_pp) {
                        v.disposition = ClaimDisposition::Pass;
                    } else {
                        v.disposition = ClaimDisposition::Corrected;
                        v.truth = truth_winrate_black;
                        v.note = "胜率与引擎真值有偏差，已按引擎数据修正";
                    }
                }
                break;
            }
            case ClaimType::GroupLife: {
                if (!has_ownership) {
                    v.disposition = ClaimDisposition::Unverifiable;
                    v.claim.source = "模型推断";
                    v.note = "无 KataGo 归属分析";
                } else if (c.point.x < 0 || c.point.y < 0 ||
                           c.point.x >= board.size() || c.point.y >= board.size()) {
                    v.disposition = ClaimDisposition::Unverifiable;
                    v.claim.source = "模型推断";
                    v.note = "坐标不合法";
                } else {
                    const auto groups = deadGroups(board, ownership);
                    bool in_dead = false;
                    for (const auto& g : groups)
                        for (const Point& p : g)
                            if (p == c.point) { in_dead = true; break; }
                    v.claim.source = "KataGo 分析";
                    const bool truth_alive = !in_dead;
                    if (c.alive == truth_alive) {
                        v.disposition = ClaimDisposition::Pass;
                    } else {
                        v.disposition = ClaimDisposition::Corrected;
                        v.truth = truth_alive ? 1.0 : 0.0;
                        v.note = "死活结论与引擎相反，已按引擎数据修正";
                    }
                }
                break;
            }
        }
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace vgo
