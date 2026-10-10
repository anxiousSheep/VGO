#include "lifedeath_shape.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <utility>

#include "json.h"
#include "symmetry.h"

namespace vgo {

namespace {

constexpr std::array<std::pair<int, int>, 4> kOrtho = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
constexpr std::array<std::pair<int, int>, 4> kDiag = {{{1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};

bool pointLess(const Point& a, const Point& b) {
    return a.x != b.x ? a.x < b.x : a.y < b.y;
}

LifeShape makeShape(std::string id, std::string name, std::string note, ShapeStatus st,
                    std::vector<Point> pattern, std::vector<Point> vital) {
    LifeShape s;
    s.id = std::move(id);
    s.name = std::move(name);
    s.note = std::move(note);
    s.status = st;
    s.pattern = std::move(pattern);
    s.vital = std::move(vital);
    return s;
}

Point P(int x, int y) { return {x, y}; }

// 解析 "x,y" 形式（也接受单个整数或空白分隔的容错格式）。
bool parseXY(const std::string& s, Point* out) {
    const std::size_t comma = s.find(',');
    if (comma == std::string::npos) return false;
    const std::string xs = s.substr(0, comma);
    const std::string ys = s.substr(comma + 1);
    if (xs.empty() || ys.empty()) return false;
    out->x = std::atoi(xs.c_str());
    out->y = std::atoi(ys.c_str());
    return true;
}

bool parseStatus(const std::string& s, ShapeStatus* out) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "alive") { *out = ShapeStatus::Alive; return true; }
    if (v == "dead") { *out = ShapeStatus::Dead; return true; }
    if (v == "unsettled") { *out = ShapeStatus::Unsettled; return true; }
    return false;
}

}  // namespace

LDStatus ldStatusFromShape(ShapeStatus s) {
    switch (s) {
        case ShapeStatus::Alive:     return LDStatus::Alive;
        case ShapeStatus::Dead:      return LDStatus::Dead;
        case ShapeStatus::Unsettled: return LDStatus::Unsettled;
    }
    return LDStatus::Unknown;
}

std::vector<EyeRegion> eyeRegions(const Board& b, const std::vector<Point>& group) {
    std::vector<EyeRegion> out;
    if (group.empty()) return out;
    const Color owner = b.at(group[0].x, group[0].y);
    if (owner == Color::Empty) return out;
    const Color opp = opponent(owner);
    const int n = b.size();
    const std::vector<Point> libs = b.liberties(group[0].x, group[0].y);

    std::vector<bool> visited(static_cast<std::size_t>(n) * n, false);
    for (const Point& start : libs) {
        if (visited[b.index(start.x, start.y)]) continue;
        // flood-fill 连通空区（只走正交空点）。
        std::vector<Point> region;
        std::vector<Point> stack{start};
        visited[b.index(start.x, start.y)] = true;
        while (!stack.empty()) {
            const Point p = stack.back();
            stack.pop_back();
            region.push_back(p);
            for (const auto [dx, dy] : kOrtho) {
                const int nx = p.x + dx;
                const int ny = p.y + dy;
                if (b.in_bounds(nx, ny) && b.at(nx, ny) == Color::Empty &&
                    !visited[b.index(nx, ny)]) {
                    visited[b.index(nx, ny)] = true;
                    stack.push_back({nx, ny});
                }
            }
        }
        // 边界判据：只统计「石子邻居 + 棋盘边」，区域内部的空邻居不计入边界。
        // 边界 ≥90% 为 G 色/墙 → 眼位；对方子则污染。
        int total = 0;
        int mine = 0;
        int oppcnt = 0;
        for (const Point& p : region) {
            for (const auto [dx, dy] : kOrtho) {
                const int nx = p.x + dx;
                const int ny = p.y + dy;
                if (!b.in_bounds(nx, ny)) {
                    ++total;
                    ++mine;  // 棋盘边 = 墙
                } else if (b.at(nx, ny) == owner) {
                    ++total;
                    ++mine;
                } else if (b.at(nx, ny) == opp) {
                    ++total;
                    ++oppcnt;
                }
                // b.at == Empty（区域内其它空点）：不计入边界。
            }
        }
        // 边界 ≥90% 为 G 色/墙 → 眼位；否则为开放气，不算眼位。
        if (total == 0 || static_cast<double>(mine) / total < 0.9) continue;
        EyeRegion r;
        r.points = std::move(region);
        r.contaminated = oppcnt > 0;
        out.push_back(std::move(r));
    }
    return out;
}

bool isTrueEye(const Board& b, const Point& p, Color owner) {
    if (!b.in_bounds(p.x, p.y) || b.at(p.x, p.y) != Color::Empty) return false;
    const Color opp = opponent(owner);
    // 正交邻点出现对方子 → 眼被破，假眼。
    for (const auto [dx, dy] : kOrtho) {
        if (b.in_bounds(p.x + dx, p.y + dy) && b.at(p.x + dx, p.y + dy) == opp) return false;
    }
    // 对角规则：中央 ≥3/4、边上 ≥2/2、角上 ≥1/1。
    int total = 0;
    int good = 0;
    for (const auto [dx, dy] : kDiag) {
        const int nx = p.x + dx;
        const int ny = p.y + dy;
        if (!b.in_bounds(nx, ny)) continue;
        ++total;
        const Color c = b.at(nx, ny);
        if (c == owner) {
            ++good;
        } else if (c == opp && b.liberties(nx, ny).size() == 1) {
            ++good;  // 对角对方子可被立即提掉者按己方计
        }
    }
    const int required = (total == 4) ? 3 : total;
    return good >= required;
}

bool isPatternAlive(const Board& b, const std::vector<Point>& group) {
    if (group.empty()) return false;
    const Color owner = b.at(group[0].x, group[0].y);
    if (owner == Color::Empty) return false;
    const std::vector<EyeRegion> regions = eyeRegions(b, group);
    int true_single = 0;
    for (const EyeRegion& r : regions) {
        if (r.points.size() == 1 && !r.contaminated && isTrueEye(b, r.points[0], owner))
            ++true_single;
    }
    if (true_single >= 2) return true;
    const LifeShapeLibrary lib = LifeShapeLibrary::builtin();
    for (const EyeRegion& r : regions) {
        if (r.contaminated) continue;
        const auto m = lib.match(r.points);
        if (m.shape && m.shape->status == ShapeStatus::Alive) return true;
    }
    return false;
}

LifeShapeLibrary LifeShapeLibrary::builtin() {
    LifeShapeLibrary lib;
    lib.upsert(makeShape("straight-three", "直三", "直三，一点杀", ShapeStatus::Unsettled,
                         {P(0, 0), P(1, 0), P(2, 0)}, {P(1, 0)}));
    lib.upsert(makeShape("bent-three", "曲三", "曲三，一点杀", ShapeStatus::Unsettled,
                         {P(0, 0), P(1, 0), P(1, 1)}, {P(1, 0)}));
    lib.upsert(makeShape("four-nail", "丁四", "丁四，一点聚杀", ShapeStatus::Dead,
                         {P(0, 0), P(1, 0), P(2, 0), P(1, 1)}, {P(1, 0)}));
    lib.upsert(makeShape("square-four", "方四", "方四，死形", ShapeStatus::Dead,
                         {P(0, 0), P(1, 0), P(0, 1), P(1, 1)}, {}));
    lib.upsert(makeShape("straight-four", "直四", "直四，活形", ShapeStatus::Alive,
                         {P(0, 0), P(1, 0), P(2, 0), P(3, 0)}, {}));
    lib.upsert(makeShape("bent-four", "曲四", "曲四，活形", ShapeStatus::Alive,
                         {P(0, 0), P(1, 0), P(2, 0), P(0, 1)}, {}));
    lib.upsert(makeShape("knife-five", "刀五", "刀五，一点聚杀", ShapeStatus::Unsettled,
                         {P(0, 0), P(1, 0), P(2, 0), P(1, 1), P(2, 1)}, {P(1, 0)}));
    lib.upsert(makeShape("flower-five", "花五", "花五（十字），一点聚杀", ShapeStatus::Unsettled,
                         {P(0, 1), P(1, 0), P(1, 1), P(1, 2), P(2, 1)}, {P(1, 1)}));
    lib.upsert(makeShape("flower-six", "花六", "花六，一点聚杀", ShapeStatus::Unsettled,
                         {P(0, 1), P(1, 0), P(1, 1), P(1, 2), P(2, 1), P(2, 0)}, {P(1, 1)}));
    lib.upsert(makeShape("plank-six", "板六", "板六，活形", ShapeStatus::Alive,
                         {P(0, 0), P(1, 0), P(2, 0), P(0, 1), P(1, 1), P(2, 1)}, {}));
    return lib;
}

bool LifeShapeLibrary::fromJsonText(const std::string& text, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    Json j;
    if (!parseJson(text, &j) || !j.isObject()) return fail("非法 JSON");

    LifeShape s;
    const Json* v = j.get("id");
    if (!v || !v->isString() || v->string.empty()) return fail("缺少 id");
    s.id = v->string;
    v = j.get("name");
    if (v && v->isString()) s.name = v->string;
    v = j.get("note");
    if (v && v->isString()) s.note = v->string;
    v = j.get("status");
    if (!v || !v->isString() || !parseStatus(v->string, &s.status)) {
        return fail("缺少/非法 status（alive/dead/unsettled）");
    }
    v = j.get("pattern");
    if (!v || !v->isArray() || v->array.empty()) return fail("缺少 pattern");
    for (const Json& pt : v->array) {
        Point p;
        if (!pt.isString() || !parseXY(pt.string, &p)) return fail("pattern 坐标非法");
        s.pattern.push_back(p);
    }
    v = j.get("vital");
    if (v && v->isArray()) {
        for (const Json& pt : v->array) {
            Point p;
            if (!pt.isString() || !parseXY(pt.string, &p)) return fail("vital 坐标非法");
            s.vital.push_back(p);
        }
    }
    if (!upsert(std::move(s))) return fail("id 为空");
    if (err) err->clear();
    return true;
}

bool LifeShapeLibrary::upsert(const LifeShape& shape) {
    if (shape.id.empty()) return false;
    LifeShape s = shape;
    if (!s.pattern.empty()) {
        // 防御性 canonicalize：pattern 平移至原点 + 对称规范化；vital 同帧变换。
        const CanonicalShape cs = canonicalize(s.pattern);
        std::vector<Point> nv;
        nv.reserve(s.vital.size());
        for (const Point& v : s.vital) nv.push_back(boardToCanonical(cs, v));
        std::sort(nv.begin(), nv.end(), pointLess);
        s.pattern = cs.points;  // 已平移 + 排序
        s.vital = std::move(nv);
    }
    for (LifeShape& x : shapes_) {
        if (x.id == s.id) {
            x = std::move(s);
            return true;
        }
    }
    shapes_.push_back(std::move(s));
    return true;
}

LifeShapeLibrary::Match LifeShapeLibrary::match(const std::vector<Point>& eye_space) const {
    Match m;
    if (eye_space.empty()) return m;
    const CanonicalShape cs = canonicalize(eye_space);
    const std::string key = shapeKey(cs.points);
    for (const LifeShape& s : shapes_) {
        if (shapeKey(s.pattern) != key) continue;
        m.shape = &s;
        m.vital_board.reserve(s.vital.size());
        for (const Point& v : s.vital) m.vital_board.push_back(canonicalToBoard(cs, v));
        break;
    }
    return m;
}

ShapeAnalysis analyzeShape(const Board& b, const std::vector<Point>& group) {
    ShapeAnalysis sa;
    if (group.empty()) return sa;
    const Color owner = b.at(group[0].x, group[0].y);
    if (owner == Color::Empty) return sa;

    const std::vector<EyeRegion> regions = eyeRegions(b, group);
    if (regions.empty()) return sa;  // 无眼位 → Unknown

    const EyeRegion* best = &regions[0];
    for (const EyeRegion& r : regions)
        if (r.points.size() > best->points.size()) best = &r;
    sa.eye_space = best->points;

    const LifeShapeLibrary lib = LifeShapeLibrary::builtin();
    const auto m = lib.match(sa.eye_space);
    if (m.shape) {
        sa.shape = m.shape->name;
        sa.status = ldStatusFromShape(m.shape->status);
        sa.kill_points = m.vital_board;
        sa.live_points = m.vital_board;
    } else if (!sa.eye_space.empty()) {
        // 未识别：区域形心兜底（单一候选）。
        sa.status = LDStatus::Unknown;
        int sx = 0, sy = 0;
        for (const Point& p : sa.eye_space) {
            sx += p.x;
            sy += p.y;
        }
        const Point c{sx / static_cast<int>(sa.eye_space.size()),
                      sy / static_cast<int>(sa.eye_space.size())};
        sa.kill_points = {c};
        sa.live_points = {c};
    }
    return sa;
}

}  // namespace vgo
