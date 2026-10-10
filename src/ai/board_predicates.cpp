#include "board_predicates.h"

#include "gtp.h"

namespace vgo {

namespace {

int zoneFriendly(const CornerContext& ctx, const std::string& zone) {
    if (zone == "left_flank") return ctx.left_friendly;
    if (zone == "right_flank") return ctx.right_friendly;
    return 0;
}

int zoneEnemy(const CornerContext& ctx, const std::string& zone) {
    if (zone == "left_flank") return ctx.left_enemy;
    if (zone == "right_flank") return ctx.right_enemy;
    return 0;
}

}  // namespace

PrecondReport checkPreconditions(const Board& b, const JosekiEntry& e, Corner c) {
    PrecondReport r;
    r.satisfied = true;
    if (b.size() != e.size) {
        r.satisfied = false;
        r.missing.push_back("该定式适用于 " + std::to_string(e.size) + " 路棋盘（当前 " +
                            std::to_string(b.size()) + " 路）");
        return r;
    }
    const Symmetry s = cornerSymmetry(c);

    for (const JosekiStone& st : e.preconditions.required_stones) {
        const Point p = applySymmetry(s, st.point, e.size);
        if (b.at(p.x, p.y) != st.color) {
            r.satisfied = false;
            r.missing.push_back(st.label.empty()
                                    ? ("缺 " + gtpPoint(p, e.size) + " 的 " +
                                       (st.color == Color::Black ? "黑" : "白") + "子")
                                    : ("缺 " + st.label));
        }
    }
    for (const Point& q : e.preconditions.required_empty) {
        const Point p = applySymmetry(s, q, e.size);
        if (!b.is_empty(p.x, p.y)) {
            r.satisfied = false;
            r.occupied_path.push_back(p);
        }
    }
    for (const Point& q : e.preconditions.forbidden) {
        const Point p = applySymmetry(s, q, e.size);
        if (!b.is_empty(p.x, p.y)) {
            r.satisfied = false;
            r.occupied_path.push_back(p);
            r.missing.push_back("禁点 " + gtpPoint(p, e.size) + " 已有子");
        }
    }
    return r;
}

CornerContext classifyContext(const Board& b, const JosekiEntry& e, Corner c, Color invader) {
    CornerContext ctx;
    ctx.path_clear = true;
    const Symmetry s = cornerSymmetry(c);

    const auto count = [&](const std::string& name, int* friendly, int* enemy) {
        const auto it = e.preconditions.zones.find(name);
        if (it == e.preconditions.zones.end()) return;
        for (const Point& q : it->second.points) {
            const Point p = applySymmetry(s, q, e.size);
            const Color col = b.at(p.x, p.y);
            if (col == invader) ++*friendly;
            else if (col == opponent(invader)) ++*enemy;
        }
    };
    count("left_flank", &ctx.left_friendly, &ctx.left_enemy);
    count("right_flank", &ctx.right_friendly, &ctx.right_enemy);

    for (const Point& q : e.preconditions.required_empty) {
        const Point p = applySymmetry(s, q, e.size);
        if (!b.is_empty(p.x, p.y)) ctx.path_clear = false;
    }
    for (const Point& q : e.preconditions.forbidden) {
        const Point p = applySymmetry(s, q, e.size);
        if (!b.is_empty(p.x, p.y)) ctx.path_clear = false;
    }
    return ctx;
}

bool evaluateWhen(const std::string& when, const CornerContext& ctx) {
    const std::size_t colon = when.find(':');
    const std::string kind = when.substr(0, colon);
    const std::string zone = (colon == std::string::npos) ? std::string() : when.substr(colon + 1);
    if (kind == "default") return true;
    if (kind == "ladder_favorable") return false;  // B1b 征子模块落地前恒不命中
    if (kind == "zone_friendly_majority") return zoneFriendly(ctx, zone) > zoneEnemy(ctx, zone);
    if (kind == "zone_enemy_majority") return zoneEnemy(ctx, zone) > zoneFriendly(ctx, zone);
    return false;
}

std::vector<int> selectVariants(const Board& b, const JosekiEntry& e, Corner c, Color invader) {
    const CornerContext ctx = classifyContext(b, e, c, invader);
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(e.variants.size()); ++i) {
        const JosekiVariant& v = e.variants[static_cast<std::size_t>(i)];
        if (v.when == "default") {
            out.push_back(i);  // 默认变体恒为可摆线基线
            continue;
        }
        if (evaluateWhen(v.when, ctx)) out.push_back(i);  // 命中非默认 → 追加建议
    }
    return out;
}

bool directionMirrors(const CornerContext& ctx, const std::string& rule) {
    if (rule == "block_toward_friendly_side") return ctx.right_friendly > ctx.left_friendly;
    return false;
}

Symmetry directionSymmetry(Corner c) {
    switch (c) {
        case Corner::UR:
        case Corner::BL: return Symmetry::MirrorAntiDiag;
        case Corner::UL:
        case Corner::BR: return Symmetry::MirrorDiag;
    }
    return Symmetry::Id;
}

std::vector<JosekiMove> projectVariant(const JosekiVariant& v, int size, Corner c,
                                       const CornerContext& ctx) {
    std::vector<JosekiMove> seq = projectToCorner(v.sequence, size, c);
    if (directionMirrors(ctx, v.direction_rule)) {
        const Symmetry dm = directionSymmetry(c);
        for (JosekiMove& m : seq) m.p = applySymmetry(dm, m.p, size);
    }
    return seq;
}

}  // namespace vgo
