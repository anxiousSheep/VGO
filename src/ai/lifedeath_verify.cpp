#include "lifedeath_verify.h"

#include <memory>

#include "ownership.h"

namespace vgo {

namespace {

struct ProbeState {
    int pending = 0;
    bool kills_at = false;
    bool lives_at = false;
    std::vector<Point> kill_points_verified;
    std::vector<Point> live_points_verified;
    LifeDeathResult base;
    LifeDeathVerifier::Callback cb;
};

bool groupHasDeadStone(const Board& after, const std::vector<Point>& group, Color defender,
                       const OwnershipMap& ownership) {
    const std::vector<Point> dead = deadStones(after, ownership, 0.8);
    for (const Point& p : group) {
        if (after.at(p.x, p.y) != defender) continue;  // 已被提掉
        for (const Point& d : dead)
            if (d == p) return true;
    }
    return false;
}

void finish(const std::shared_ptr<ProbeState>& st) {
    LifeDeathResult r = st->base;
    r.kill_points = st->kill_points_verified;
    r.live_points = st->live_points_verified;
    r.verification = LDVerify::EngineVerified;
    if (st->kills_at && st->lives_at) {
        r.status = LDStatus::Unsettled;
        r.alive = false;
        r.reason = "先手相关（先手死，疑似劫/特殊型）";
    } else if (st->kills_at) {
        r.status = LDStatus::Dead;
        r.alive = false;
        r.reason = "引擎验证：对方抢占要点后该块被判死";
    } else if (st->lives_at) {
        r.status = LDStatus::Alive;
        r.alive = true;
        r.reason = "引擎验证：己方补要点后可活";
    } else {
        r.status = LDStatus::Unknown;
        r.alive = true;
        r.reason = "引擎无法判明该块死活";
    }
    st->cb(r);
}

}  // namespace

LifeDeathVerifier::LifeDeathVerifier(EngineInterface* engine, StrengthParams sp)
    : probe_(engine, sp) {}

void LifeDeathVerifier::verify(const Board& board, Color defender,
                               const std::vector<Point>& group,
                               const std::vector<Point>& kill_candidates,
                               const std::vector<Point>& live_candidates, Callback cb) {
    const int gen = ++generation_;
    LifeDeathResult base;
    base.group = group;
    base.status = LDStatus::Unknown;
    base.alive = true;

    // 无引擎：P5 降级——只出常型直判，引擎验证诚实标 None。
    if (!probe_.hasEngine()) {
        base.verification = LDVerify::None;
        cb(base);
        return;
    }
    const Color attacker = opponent(defender);

    auto st = std::make_shared<ProbeState>();
    st->base = base;
    st->cb = std::move(cb);
    st->pending = static_cast<int>(kill_candidates.size() + live_candidates.size());
    if (st->pending == 0) {
        finish(st);
        return;
    }

    const auto settle = [gen, st, defender, this](const ProbeResult& pr, bool is_kill,
                                                   const Point& cand) {
        if (gen != generation_) return;  // verify 级过期：丢弃
        if (pr.ok && !pr.analysis.ownership.empty()) {
            const bool dead =
                groupHasDeadStone(pr.after, st->base.group, defender, pr.analysis.ownership);
            if (is_kill && dead) {
                st->kills_at = true;
                st->kill_points_verified.push_back(cand);
            } else if (!is_kill && !dead) {
                st->lives_at = true;
                st->live_points_verified.push_back(cand);
            }
        }
        if (--st->pending == 0) finish(st);
    };

    for (const Point& v : kill_candidates) {
        probe_.probe(board, attacker, v,
                     [settle, v](const ProbeResult& pr) { settle(pr, true, v); });
    }
    for (const Point& d : live_candidates) {
        probe_.probe(board, defender, d,
                     [settle, d](const ProbeResult& pr) { settle(pr, false, d); });
    }
}

void LifeDeathVerifier::cancel() {
    ++generation_;
    probe_.cancel();
}

}  // namespace vgo
