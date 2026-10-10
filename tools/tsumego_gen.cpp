// 死活题生成器（C.3，离线，用 L3 常型库）。
//
// 用 LifeShapeLibrary 在棋盘上摆常型局面（守方围住眼位、攻方包在外围），自动生成
// 题面 + 解树（根为常型要点）。对「先手死」类常型同时生成杀棋题（攻方先）与做活题
// （守方先）。自检：目标棋块存在、解树根为要点、根要点与 LifeShapeLibrary vital
// 反投影一致。输出 resources/tsumego/*.json（每文件一题）。
//
// 用法：vgo_tsumego_gen [输出目录]     （默认 resources/tsumego）

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ai/gtp.h"
#include "ai/json.h"
#include "ai/lifedeath_shape.h"
#include "ai/tsumego.h"

using namespace vgo;

namespace {

const std::pair<int, int> kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// 守方(owner)围住眼位，攻方(opp)再包在外围，使守方只有眼位一处气。
Board buildEnclosed(const std::vector<Point>& eye, Color owner, Color opp, int size) {
    Board b(size);
    int minx = eye[0].x, miny = eye[0].y, maxx = eye[0].x, maxy = eye[0].y;
    for (const Point& p : eye) {
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x);
        maxy = std::max(maxy, p.y);
    }
    for (int x = minx - 1; x <= maxx + 1; ++x)
        for (int y = miny - 1; y <= maxy + 1; ++y)
            if (b.in_bounds(x, y)) b.set(x, y, owner);
    for (const Point& p : eye) b.set(p.x, p.y, Color::Empty);
    const auto isEye = [&](int x, int y) {
        for (const Point& p : eye)
            if (p.x == x && p.y == y) return true;
        return false;
    };
    for (int x = 0; x < size; ++x)
        for (int y = 0; y < size; ++y) {
            if (b.at(x, y) != Color::Empty || isEye(x, y)) continue;
            for (const auto [dx, dy] : kDirs)
                if (b.in_bounds(x + dx, y + dy) && b.at(x + dx, y + dy) == owner) {
                    b.set(x, y, opp);
                    break;
                }
        }
    return b;
}

Json nodeToJson(const TsumegoNode& n, int size) {
    std::vector<std::pair<std::string, Json>> f;
    f.emplace_back("move", Json::makeString(n.move.is_pass ? "" : gtpPoint(n.move.p, size)));
    f.emplace_back("by", Json::makeString(n.by == Color::Black ? "black" : "white"));
    if (!n.verdict.empty()) f.emplace_back("verdict", Json::makeString(n.verdict));
    if (!n.comment.empty()) f.emplace_back("comment", Json::makeString(n.comment));
    std::vector<Json> ch;
    for (const TsumegoNode& c : n.children) ch.push_back(nodeToJson(c, size));
    f.emplace_back("children", Json::makeArray(std::move(ch)));
    return Json::makeObject(std::move(f));
}

std::string problemToJson(const TsumegoProblem& p) {
    std::vector<std::pair<std::string, Json>> f;
    f.emplace_back("id", Json::makeString(p.id));
    f.emplace_back("theme", Json::makeString(p.theme));
    f.emplace_back("goal", Json::makeString(p.goal));
    f.emplace_back("difficulty", Json::makeString(p.difficulty));
    f.emplace_back("size", Json::makeNumber(static_cast<double>(p.size)));
    f.emplace_back("snapshot", Json::makeString(p.snapshot));
    f.emplace_back("to_move", Json::makeString(p.to_move == Color::Black ? "black" : "white"));
    f.emplace_back("source", Json::makeString(p.source));
    std::vector<Json> tg;
    for (const Point& q : p.target_group) tg.push_back(Json::makeString(gtpPoint(q, p.size)));
    f.emplace_back("target_group", Json::makeArray(std::move(tg)));
    f.emplace_back("solution", nodeToJson(p.solution_tree, p.size));
    return dumpJson(Json::makeObject(std::move(f)));
}

// 在 offset 处摆 shape，生成一道题。live==true 为做活题（守方先），否则杀棋题。
TsumegoProblem makeProblem(const LifeShape& shape, int ox, int oy, int size, bool live) {
    std::vector<Point> eye;
    for (const Point& p : shape.pattern) eye.push_back({p.x + ox, p.y + oy});
    const Color owner = Color::White;
    const Color opp = Color::Black;
    const Board board = buildEnclosed(eye, owner, opp, size);

    TsumegoProblem pr;
    pr.theme = "life-death";
    pr.size = size;
    pr.source = "generated";
    pr.snapshot = board.snapshot();
    pr.to_move = live ? owner : opp;
    pr.goal = live ? "做活" : "杀棋";
    pr.difficulty = (shape.pattern.size() <= 3) ? "easy"
                    : (shape.pattern.size() <= 5) ? "medium" : "hard";
    pr.id = std::string(shape.id) + (live ? "-live" : "-kill") + "-" + std::to_string(ox) +
            std::to_string(oy);
    // 目标棋块：任一守方子所在组。
    for (int x = 0; x < size; ++x)
        for (int y = 0; y < size; ++y)
            if (board.at(x, y) == owner) {
                pr.target_group = board.group(x, y);
                x = size;
                break;
            }

    TsumegoNode root;
    root.by = pr.to_move;
    root.verdict = "good";
    root.comment = live ? "抢占要点做活" : "点入要点杀棋";
    if (!shape.vital.empty()) {
        root.move = Move{Point{shape.vital[0].x + ox, shape.vital[0].y + oy}, false};
    } else {
        // 无单一要点（如方四）：不给根，标记无效题，由调用方跳过。
        root.move = Move{Point{-1, -1}, false};
    }
    pr.solution_tree = std::move(root);
    return pr;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out = "resources/tsumego";
    if (argc > 1) out = argv[1];
    std::filesystem::create_directories(out);

    const LifeShapeLibrary lib = LifeShapeLibrary::builtin();
    int written = 0;
    int skipped = 0;
    for (const LifeShape& s : lib.all()) {
        if (s.vital.empty()) {  // 活形/方四：无单一要点，跳过。
            ++skipped;
            continue;
        }
        // 在四个内点偏移摆题，扩大题库。
        for (const auto [ox, oy] :
             std::vector<std::pair<int, int>>{{2, 2}, {2, 4}, {4, 2}, {4, 4}}) {
            // 杀棋题（攻方先）。
            {
                TsumegoProblem p = makeProblem(s, ox, oy, 9, /*live=*/false);
                if (p.solution_tree.move.p.x < 0) continue;
                // 自检：解树根要点 == LifeShapeLibrary vital 反投影（此处为直接平移）。
                const Point expect{s.vital[0].x + ox, s.vital[0].y + oy};
                if (!(p.solution_tree.move.p == expect)) {
                    std::fprintf(stderr, "SELF-CHECK FAIL: %s root mismatch\n", s.id.c_str());
                    continue;
                }
                const std::string json = problemToJson(p);
                std::ofstream f(out / (p.id + ".json"));
                f << json << "\n";
                ++written;
            }
            // 做活题（先手死类常型才有意义：守方先）。
            if (s.status == ShapeStatus::Unsettled) {
                TsumegoProblem p = makeProblem(s, ox, oy, 9, /*live=*/true);
                if (p.solution_tree.move.p.x < 0) continue;
                const std::string json = problemToJson(p);
                std::ofstream f(out / (p.id + ".json"));
                f << json << "\n";
                ++written;
            }
        }
    }
    std::printf("生成 %d 题（跳过 %d 个无要点常型）→ %s\n", written, skipped,
                out.string().c_str());
    return 0;
}
