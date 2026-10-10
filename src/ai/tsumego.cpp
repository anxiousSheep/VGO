#include "tsumego.h"

#include <cctype>
#include <utility>

#include "go/board.h"
#include "gtp.h"
#include "json.h"

namespace vgo {

namespace {

Color parseColorStr(const std::string& s) {
    if (s == "black" || s == "B") return Color::Black;
    if (s == "white" || s == "W") return Color::White;
    return Color::Empty;
}

bool sameMove(const Move& a, const Move& b) {
    if (a.is_pass != b.is_pass) return false;
    if (a.is_pass) return true;
    return a.p == b.p;
}

const TsumegoNode* findChild(const TsumegoNode& n, Color by, const Move& m) {
    for (const TsumegoNode& c : n.children)
        if (c.by == by && sameMove(c.move, m)) return &c;
    return nullptr;
}

// 递归解析解树节点。
bool parseNode(const Json& j, int size, TsumegoNode* out, std::string* err) {
    if (!j.isObject()) {
        if (err) *err = "solution 节点须为对象";
        return false;
    }
    TsumegoNode n;
    const Json* m = j.get("move");
    if (!m || !m->isString()) {
        if (err) *err = "solution 节点缺少 move";
        return false;
    }
    if (m->string.empty()) {
        n.move.is_pass = true;
    } else {
        Point p;
        if (!gtpParsePoint(m->string, size, &p)) {
            if (err) *err = "非法坐标：" + m->string;
            return false;
        }
        n.move.p = p;
    }
    const Json* by = j.get("by");
    if (by && by->isString()) n.by = parseColorStr(by->string);
    const Json* v = j.get("verdict");
    if (v && v->isString()) n.verdict = v->string;
    const Json* c = j.get("comment");
    if (c && c->isString()) n.comment = c->string;
    const Json* ch = j.get("children");
    if (ch && ch->isArray()) {
        for (const Json& x : ch->array) {
            TsumegoNode sub;
            if (!parseNode(x, size, &sub, err)) return false;
            n.children.push_back(std::move(sub));
        }
    }
    *out = std::move(n);
    return true;
}

TsumegoProblem straightThreeKill() {
    // 白方直三眼位 (1,1)(2,1)(3,1)，白环包围；黑先点 (2,1) 杀。
    Board b(9);
    for (Point p : {Point{0, 0}, Point{0, 1}, Point{0, 2}, Point{1, 0}, Point{2, 0},
                    Point{3, 0}, Point{4, 0}, Point{4, 1}, Point{4, 2}, Point{1, 2},
                    Point{2, 2}, Point{3, 2}})
        b.set(p.x, p.y, Color::White);

    TsumegoProblem pr;
    pr.id = "straight-three-kill";
    pr.theme = "life-death";
    pr.goal = "杀棋";
    pr.difficulty = "easy";
    pr.size = 9;
    pr.snapshot = b.snapshot();
    pr.to_move = Color::Black;
    pr.target_group = b.group(0, 0);
    TsumegoNode root;
    root.move = Move{Point{2, 1}, false};
    root.by = Color::Black;
    root.verdict = "good";
    root.comment = "点入直三正中央，白无法做出两眼";
    pr.solution_tree = std::move(root);
    return pr;
}

TsumegoProblem knifeFiveKill() {
    // 白方刀五眼位（2×3 缺一角），白环包围；黑先点要点 (2,2) 杀。
    // 刀五 canonical 眼位：(0,0)(1,0)(2,0)(1,1)(2,1)，置于偏移 (1,1)。
    Board b(9);
    const std::vector<Point> eye = {{1, 1}, {2, 1}, {3, 1}, {2, 2}, {3, 2}};
    for (const Point& p : eye) b.set(p.x, p.y, Color::Empty);
    // 用白环包围眼位：眼位邻接的空点摆白子。
    for (const Point& p : eye) {
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : dirs) {
            const int nx = p.x + d[0];
            const int ny = p.y + d[1];
            if (!b.in_bounds(nx, ny)) continue;
            if (b.at(nx, ny) == Color::Empty) b.set(nx, ny, Color::White);
        }
    }

    TsumegoProblem pr;
    pr.id = "knife-five-kill";
    pr.theme = "life-death";
    pr.goal = "杀棋";
    pr.difficulty = "medium";
    pr.size = 9;
    pr.snapshot = b.snapshot();
    pr.to_move = Color::Black;
    // target_group：任一白子所在组。
    pr.target_group = b.group(0, 1);
    TsumegoNode root;
    root.move = Move{Point{2, 2}, false};  // 刀五要点（与 LifeShapeLibrary 一致）
    root.by = Color::Black;
    root.verdict = "good";
    root.comment = "点刀五要点，白无力回天";
    pr.solution_tree = std::move(root);
    return pr;
}

}  // namespace

TsumegoLibrary TsumegoLibrary::builtin() {
    TsumegoLibrary lib;
    lib.problems_.push_back(straightThreeKill());
    lib.problems_.push_back(knifeFiveKill());
    return lib;
}

bool TsumegoLibrary::fromJsonText(const std::string& text, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    Json j;
    if (!parseJson(text, &j) || !j.isObject()) return fail("非法 JSON");

    TsumegoProblem pr;
    const Json* v = j.get("id");
    if (!v || !v->isString() || v->string.empty()) return fail("缺少 id");
    pr.id = v->string;
    v = j.get("theme");
    if (v && v->isString()) pr.theme = v->string;
    v = j.get("goal");
    if (v && v->isString()) pr.goal = v->string;
    v = j.get("difficulty");
    if (v && v->isString()) pr.difficulty = v->string;
    v = j.get("source");
    if (v && v->isString()) pr.source = v->string;
    v = j.get("size");
    pr.size = (v && v->isNumber()) ? static_cast<int>(v->number) : 9;
    if (pr.size < 2) return fail("size 非法");
    v = j.get("snapshot");
    if (!v || !v->isString()) return fail("缺少 snapshot");
    pr.snapshot = v->string;
    v = j.get("to_move");
    if (v && v->isString()) pr.to_move = parseColorStr(v->string);
    v = j.get("target_group");
    if (v && v->isArray()) {
        for (const Json& pt : v->array) {
            Point p;
            if (!pt.isString() || !gtpParsePoint(pt.string, pr.size, &p))
                return fail("target_group 坐标非法");
            pr.target_group.push_back(p);
        }
    }
    v = j.get("solution");
    if (!v || !parseNode(*v, pr.size, &pr.solution_tree, err)) return fail("缺少/非法 solution");
    problems_.push_back(std::move(pr));
    if (err) err->clear();
    return true;
}

const TsumegoProblem* TsumegoLibrary::find(const std::string& id) const {
    for (const TsumegoProblem& p : problems_)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<const TsumegoProblem*> TsumegoLibrary::filter(const std::string& theme,
                                                         const std::string& difficulty) const {
    std::vector<const TsumegoProblem*> out;
    for (const TsumegoProblem& p : problems_) {
        if (!theme.empty() && p.theme != theme) continue;
        if (!difficulty.empty() && p.difficulty != difficulty) continue;
        out.push_back(&p);
    }
    return out;
}

TsumegoJudgement judgeTsumegoMove(const TsumegoProblem& p,
                                  const std::vector<std::pair<Color, Move>>& path, Move user) {
    TsumegoJudgement j;
    const Color solver = p.to_move;
    const TsumegoNode& root = p.solution_tree;

    auto finalize = [&](const TsumegoNode& n) -> TsumegoJudgement {
        TsumegoJudgement r;
        if (n.verdict == "bad") {
            r.judge = TsumegoJudge::Wrong;
            r.comment = n.comment;
            return r;
        }
        r.comment = n.comment;
        if (n.children.empty()) {
            r.judge = TsumegoJudge::Solved;
            return r;
        }
        r.judge = TsumegoJudge::Correct;
        for (const TsumegoNode& ch : n.children) r.response.push_back(ch.move);
        return r;
    };

    if (path.empty()) {
        // 第一手：须命中根（正确首手）。
        if (root.by != solver || !sameMove(root.move, user)) return j;  // OffTree
        return finalize(root);
    }
    // 校验 path[0] 命中根。
    if (path[0].first != solver || !sameMove(root.move, path[0].second)) return j;  // OffTree
    const TsumegoNode* current = &root;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const TsumegoNode* next = findChild(*current, path[i].first, path[i].second);
        if (!next) return j;  // OffTree
        current = next;
    }
    // 用户（解题方）下一步落子。
    const TsumegoNode* found = findChild(*current, solver, user);
    if (!found) return j;  // OffTree
    return finalize(*found);
}

}  // namespace vgo
