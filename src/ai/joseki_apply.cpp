#include "joseki_apply.h"

#include <cstdio>
#include <memory>

#include "board_predicates.h"
#include "go/game.h"

namespace vgo {

namespace {

std::string pp(double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", x * 100.0);
    return std::string(buf) + "pp";
}

// 首个可摆线（sequence 非空）的可用变体；无则 nullptr。
const JosekiVariant* firstDemonstrable(const JosekiEntry& e, Corner c, Color invader,
                                       const Board& b) {
    const std::vector<int> sel = selectVariants(b, e, c, invader);
    for (int idx : sel) {
        const JosekiVariant& v = e.variants[static_cast<std::size_t>(idx)];
        if (!v.sequence.empty()) return &v;
    }
    return nullptr;
}

}  // namespace

void JosekiVerdictStore::put(const Board& board, const JosekiVerdict& v) {
    map_[board.snapshot()] = v;
}

bool JosekiVerdictStore::get(const Board& board, JosekiVerdict* out) const {
    const auto it = map_.find(board.snapshot());
    if (it == map_.end()) return false;
    if (out) *out = it->second;
    return true;
}

void JosekiVerdictStore::clear() {
    map_.clear();
}

DemoDecision decideJosekiDemo(const Board& b, const JosekiEntry& e, Corner c, Color invader,
                              const std::string& mode, const JosekiVerdict* verdict) {
    const PrecondReport pre = checkPreconditions(b, e, c);
    const CornerContext ctx = classifyContext(b, e, c, invader);
    const JosekiVariant* demo = firstDemonstrable(e, c, invader, b);

    const auto textbook = [&](const std::string& announce) {
        DemoDecision d;
        d.action = DemoAction::Textbook;
        if (demo) {
            d.sequence = projectVariant(*demo, e.size, c, ctx);
            d.variant_id = demo->id;
        }
        d.announce = announce;
        return d;
    };
    const auto refuse = [&](const std::string& why) {
        DemoDecision d;
        d.action = DemoAction::Refuse;
        d.announce = why;
        return d;
    };

    if (mode == "textbook") {
        return textbook("这是标准形教学演示（已摆出「" + e.applies_to_formation +
                        "」的前提局面）");
    }

    // apply / auto 都要求前提满足 + 有可摆线变体。
    if (!pre.satisfied) {
        std::string why = "前提不满足：";
        if (!pre.missing.empty()) {
            why += pre.missing[0];
        } else if (!pre.occupied_path.empty()) {
            why += "路径点已被占";
        } else {
            why += "当前局面不适用";
        }
        return refuse(why);
    }
    if (!demo) return refuse("无可用定式线");

    // apply 门禁闭合（§4.4.1）：L4 仲裁异步，工具环路只消费已注入 verdict；
    // 无 verdict 一律降级 textbook（安全默认），绝不未经仲裁摆线。
    if (!verdict || !verdict->computed) {
        DemoDecision d = textbook("未经引擎验证，按安全默认降级为标准形教学演示");
        return d;
    }

    if (verdict->favorable) {
        DemoDecision d;
        d.action = DemoAction::Apply;
        d.sequence = projectVariant(*demo, e.size, c, ctx);
        d.variant_id = demo->id;
        d.note = verdict->note;
        return d;
    }

    std::string why = "此局面下该定式不便宜（" + verdict->note + "）";
    if (!e.alternatives_if_rejected.empty()) {
        why += "；可考虑：";
        for (std::size_t i = 0; i < e.alternatives_if_rejected.size(); ++i) {
            if (i) why += "/";
            why += e.alternatives_if_rejected[i];
        }
    }
    return refuse(why);
}

namespace {

struct ArbState {
    int pending = 0;
    bool illegal = false;
    double w_joseki = 0.0;
    double w_best = 0.0;
    std::function<void(const JosekiVerdict&)> cb;
};

void finishArb(const std::shared_ptr<ArbState>& st) {
    JosekiVerdict v;
    v.computed = !st->illegal;
    v.w_joseki = st->w_joseki;
    v.w_best = st->w_best;
    const double diff = st->w_best - st->w_joseki;
    v.favorable = (diff <= kJosekiMargin);
    if (st->illegal) {
        v.note = "摆线非法";
    } else if (v.favorable) {
        v.note = "定式成立（引擎验证，胜率差 " + pp(diff) + " ≤ 3pp）";
    } else {
        v.note = "定式不便宜（胜率差 " + pp(diff) + " > 3pp）";
    }
    st->cb(v);
}

}  // namespace

void arbitrateJoseki(Probe& probe, const Board& board, Color invader,
                     const std::vector<JosekiMove>& projected_seq,
                     std::function<void(const JosekiVerdict&)> cb) {
    if (!probe.hasEngine() || projected_seq.empty()) {
        JosekiVerdict v;
        v.computed = false;
        v.note = "无引擎，未仲裁";
        cb(v);
        return;
    }

    auto st = std::make_shared<ArbState>();
    st->cb = std::move(cb);
    st->pending = 2;

    // step 1：摆完变体线 → W_joseki（入侵方视角）。
    {
        Game g(board, invader);
        bool ok = true;
        for (const JosekiMove& m : projected_seq) {
            if (!g.play(m.p.x, m.p.y)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            st->illegal = true;
            if (--st->pending == 0) finishArb(st);
        } else {
            const Board after = g.board();
            const Color next = g.current();  // 下一手方 = 守方
            probe.analyzePosition(after, next, [st](const ProbeResult& r) {
                // r.winrate 是守方视角，入侵方视角 = 1 - 守方。
                if (r.ok && r.analysis.valid) st->w_joseki = 1.0 - r.winrate;
                if (--st->pending == 0) finishArb(st);
            });
        }
    }

    // step 2：同局面引擎自选最优 → W_best（入侵方视角）。
    probe.analyzePosition(board, invader, [st](const ProbeResult& r) {
        if (r.ok && r.analysis.valid) st->w_best = r.analysis.best.winrate;
        if (--st->pending == 0) finishArb(st);
    });
}

}  // namespace vgo
