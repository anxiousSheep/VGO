#include "agent_tools.h"

#include <memory>
#include <utility>
#include <vector>

#include "../gtp.h"
#include "../json.h"
#include "../lifedeath.h"
#include "../lifedeath_shape.h"
#include "../ownership.h"
#include "../record.h"
#include "../sgf.h"

namespace vgo {

namespace {

Json pointsToJson(const std::vector<Point>& pts, int size) {
    std::vector<Json> arr;
    arr.reserve(pts.size());
    for (const Point& p : pts) arr.push_back(Json::makeString(gtpPoint(p, size)));
    return Json::makeArray(std::move(arr));
}

// Parse an optional `point`/`group` argument. Returns false when the argument
// is present but not a legal coordinate (so the tool can refuse instead of
// silently guessing).
bool parsePointArg(const std::string& args, const std::string& key, int size, Point* out) {
    if (args.empty()) return true;  // no args: keep *out as {-1,-1}
    Json j;
    if (!parseJson(args, &j) || !j.isObject()) return false;
    const Json* v = j.get(key);
    if (!v || v->isNull()) return true;
    const std::string s = v->asString();
    if (s.empty()) return true;
    if (!gtpParsePoint(s, size, out)) return false;
    return true;
}

const char* ldStatusStr(LDStatus s) {
    switch (s) {
        case LDStatus::Alive:     return "alive";
        case LDStatus::Dead:      return "dead";
        case LDStatus::Unsettled: return "unsettled";
        default:                  return "unknown";
    }
}

}  // namespace

ToolRegistry buildAgentTools(const AgentToolContext& ctx) {
    auto c = std::make_shared<AgentToolContext>(ctx);
    ToolRegistry r;

    {
        Tool t;
        t.name = "count_territory";
        t.description = "用数子法计算当前双方领地（子 + 围住的空点）与贴目后的领先目数。";
        t.json_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
        t.invoke = [c](const std::string&) -> ToolResult {
            const auto area = c->game.score_area();
            ToolResult r;
            r.json = dumpJson(Json::makeObject({
                {"black", Json::makeNumber(area.first)},
                {"white", Json::makeNumber(area.second)},
                {"komi", Json::makeNumber(c->game.komi())},
                {"black_lead", Json::makeNumber(c->game.score_black())},
            }));
            return r;
        };
        r.registerTool(t);
    }

    {
        Tool t;
        t.name = "life_death_solve";
        t.description =
            "判断某块棋（或盘面最大的一块死棋）的死活，识别常型（刀五/直三/方四等）并给出"
            "杀棋/做活要点。纯棋形分析，无需引擎；无引擎时只按常型判断。";
        t.json_schema =
            "{\"type\":\"object\",\"properties\":{\"group\":{\"type\":\"string\","
            "\"description\":\"要判断的棋块上的一个 GTP 坐标（如 C3），省略则判断盘面最大死棋\"}},"
            "\"required\":[]}";
        t.invoke = [c](const std::string& args) -> ToolResult {
            ToolResult r;
            const Board& board = c->game.board();
            Point anchor{-1, -1};
            if (!parsePointArg(args, "group", c->game.size(), &anchor)) {
                r.ok = false;
                r.error = "非法坐标";
                return r;
            }
            std::vector<Point> group;
            if (anchor.x >= 0) {
                if (board.at(anchor.x, anchor.y) == Color::Empty) {
                    r.ok = false;
                    r.error = "该坐标是空点，请给棋块上的一颗子";
                    return r;
                }
                group = board.group(anchor.x, anchor.y);
            } else if (!c->ownership.empty()) {
                const auto groups = deadGroups(board, c->ownership);
                if (!groups.empty()) {
                    group = groups[0];
                    for (const auto& g : groups)
                        if (g.size() > group.size()) group = g;
                }
            }
            if (group.empty()) {
                // 无 anchor 且无死棋归属：空结论（不再硬失败）。
                r.json = dumpJson(Json::makeObject({
                    {"alive", Json::makeBool(true)},
                    {"group", Json::makeArray({})},
                    {"shape", Json::makeString("")},
                    {"status", Json::makeString("unknown")},
                    {"verification", Json::makeString("none")},
                    {"reason",
                     Json::makeString("未给出棋块坐标，且无引擎归属分析可定位死棋")},
                }));
                return r;
            }
            // L3 常型识别（纯逻辑，无引擎）。
            const ShapeAnalysis sa = analyzeShape(board, group);
            // alive：优先引擎归属（deadStones 判死）；无引擎时由常型状态派生。
            bool alive = true;
            if (!c->ownership.empty()) {
                const auto dead = deadStones(board, c->ownership, 0.8);
                for (const Point& p : group) {
                    bool isDead = false;
                    for (const Point& d : dead)
                        if (d == p) { isDead = true; break; }
                    if (isDead) { alive = false; break; }
                }
            } else if (sa.status == LDStatus::Dead || sa.status == LDStatus::Unsettled) {
                alive = false;
            }
            std::vector<Point> saving = sa.live_points;
            if (saving.empty() && !alive) saving = savingCandidates(board, group);
            const std::string reason = sa.shape.empty() ? "未能识别常型" : ("常型：" + sa.shape);
            r.json = dumpJson(Json::makeObject({
                {"alive", Json::makeBool(alive)},
                {"group", pointsToJson(group, c->game.size())},
                {"saving_moves", pointsToJson(saving, c->game.size())},
                {"shape", Json::makeString(sa.shape)},
                {"status", Json::makeString(ldStatusStr(sa.status))},
                {"eye_space", pointsToJson(sa.eye_space, c->game.size())},
                {"kill_points", pointsToJson(sa.kill_points, c->game.size())},
                {"live_points", pointsToJson(sa.live_points, c->game.size())},
                {"verification", Json::makeString("pattern_only")},
                {"reason", Json::makeString(reason)},
            }));
            return r;
        };
        r.registerTool(t);
    }

    {
        // A.3：定式词典查询。返回定式的完整走法序列（已投影到目标角）。
        Tool t;
        t.name = "joseki_lookup";
        t.description =
            "查询定式词典：按名称/别名匹配一条定式（点三三、小飞挂、托退等），返回其完整走法"
            "序列（可投影到指定角）。当用户点名定式/具体变化时先调用它。";
        t.json_schema =
            "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":"
            "\"定式名称或别名\"},\"corner\":{\"type\":\"string\",\"description\":\"目标角 "
            "UR/UL/BL/BR，默认 UR\"}},\"required\":[\"name\"]}";
        t.invoke = [c](const std::string& args) -> ToolResult {
            ToolResult r;
            if (!c->joseki) {
                r.ok = false;
                r.error = "这个定式我还没收录";
                return r;
            }
            Json j;
            if (!parseJson(args, &j) || !j.isObject()) {
                r.ok = false;
                r.error = "非法参数";
                return r;
            }
            const Json* name = j.get("name");
            if (!name || !name->isString() || name->string.empty()) {
                r.ok = false;
                r.error = "缺少 name";
                return r;
            }
            const JosekiEntry* e = c->joseki->find(name->string);
            if (!e) {
                r.ok = false;
                r.error = "这个定式我还没收录";
                return r;
            }
            if (e->size != c->game.size()) {
                r.ok = false;
                r.error = "该定式适用于 " + std::to_string(e->size) + " 路棋盘（当前 " +
                          std::to_string(c->game.size()) + " 路）";
                return r;
            }
            const Json* corner = j.get("corner");
            const std::string cornerStr =
                (corner && corner->isString()) ? corner->string : "UR";
            const std::vector<JosekiMove> seq = projectToCorner(e->sequence, e->size, cornerStr);
            std::vector<Json> seqArr;
            seqArr.reserve(seq.size());
            for (const JosekiMove& m : seq) {
                seqArr.push_back(Json::makeObject({
                    {"move", Json::makeString(gtpPoint(m.p, e->size))},
                    {"by", Json::makeString(m.by == Color::Black ? "black" : "white")},
                    {"note", Json::makeString(m.note)},
                }));
            }
            r.json = dumpJson(Json::makeObject({
                {"id", Json::makeString(e->id)},
                {"name", Json::makeString(e->name)},
                {"trigger", Json::makeString(e->trigger)},
                {"corner", Json::makeString(cornerStr)},
                {"size", Json::makeNumber(static_cast<double>(e->size))},
                {"sequence", Json::makeArray(std::move(seqArr))},
                {"choices", Json::makeString(e->choices)},
            }));
            return r;
        };
        r.registerTool(t);
    }

    {
        Tool t;
        t.name = "ownership_map";
        t.description = "返回当前局面的领地归属摘要：黑方领先目数、被判死的子数与棋块数。";
        t.json_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
        t.invoke = [c](const std::string&) -> ToolResult {
            ToolResult r;
            if (c->ownership.empty()) {
                r.ok = false;
                r.error = "需要 KataGo 归属分析";
                return r;
            }
            const auto groups = deadGroups(c->game.board(), c->ownership);
            int stones = 0;
            for (const auto& g : groups) stones += static_cast<int>(g.size());
            r.json = dumpJson(Json::makeObject({
                {"black_lead", Json::makeNumber(ownershipLead(c->ownership))},
                {"dead_stones", Json::makeNumber(stones)},
                {"dead_groups", Json::makeNumber(static_cast<double>(groups.size()))},
            }));
            return r;
        };
        r.registerTool(t);
    }

    {
        Tool t;
        t.name = "dead_groups";
        t.description = "列出当前被判死的所有棋块（每块返回其中棋子的 GTP 坐标）。";
        t.json_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
        t.invoke = [c](const std::string&) -> ToolResult {
            ToolResult r;
            if (c->ownership.empty()) {
                r.ok = false;
                r.error = "需要 KataGo 归属分析";
                return r;
            }
            const auto groups = deadGroups(c->game.board(), c->ownership);
            std::vector<Json> arr;
            arr.reserve(groups.size());
            for (const auto& g : groups) arr.push_back(pointsToJson(g, c->game.size()));
            r.json = dumpJson(Json::makeArray(std::move(arr)));
            return r;
        };
        r.registerTool(t);
    }

    {
        Tool t;
        t.name = "analyze_position";
        t.description = "返回当前局面概要：黑方胜率、领先目数、最佳一手及候选点。";
        t.json_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
        t.invoke = [c](const std::string&) -> ToolResult {
            const int size = c->game.size();
            const bool blackToMove = (c->game.current() == Color::Black);
            std::vector<std::pair<std::string, Json>> fields;
            if (c->analysis.valid) {
                const double bw =
                    blackToMove ? c->analysis.winrate : 1.0 - c->analysis.winrate;
                const double bl =
                    blackToMove ? c->analysis.score_lead : -c->analysis.score_lead;
                fields.emplace_back("winrate_black", Json::makeNumber(bw));
                fields.emplace_back("lead_black", Json::makeNumber(bl));
                std::vector<Json> cands;
                for (std::size_t i = 0;
                     i < c->analysis.candidates.size() && i < 4; ++i) {
                    const MoveCandidate& mc = c->analysis.candidates[i];
                    const double cw = blackToMove ? mc.winrate : 1.0 - mc.winrate;
                    cands.push_back(Json::makeObject({
                        {"point", Json::makeString(gtpPoint(mc.p, size))},
                        {"winrate_black", Json::makeNumber(cw)},
                    }));
                }
                fields.emplace_back("candidates", Json::makeArray(std::move(cands)));
            } else {
                fields.emplace_back("lead_black",
                                    Json::makeNumber(c->game.score_black()));
                fields.emplace_back("note",
                                    Json::makeString("规则计分，无 KataGo 胜率"));
            }
            if (!c->ownership.empty()) {
                fields.emplace_back("ownership_lead",
                                    Json::makeNumber(ownershipLead(c->ownership)));
            }
            ToolResult r;
            r.json = dumpJson(Json::makeObject(std::move(fields)));
            return r;
        };
        r.registerTool(t);
    }

    {
        // D1.7：把 L5 落地的 SGF 读写能力注册为工具——把当前局面的手顺（本局已下
        // 着法序列，含让子摆子）以标准 SGF（FF[4]）文本 + 结构化着法列表两种形式
        // 交给模型，供其核对「这盘棋走到这里是怎么下的」。坐标用 SGF 记法（左上角
        // aa 为原点、列在前行在后、不跳字母），与 GTP 记法相区别。棋谱库位置索引
        // （跨局查找）仍另行推进。
        Tool t;
        t.name = "sgf_lookup_position";
        t.description =
            "返回当前局面的手顺（本局已下的着法序列，含让子摆子），以标准 SGF（FF[4]）"
            "文本与结构化着法列表两种形式给出。坐标为 SGF 记法：左上角 aa 为原点，"
            "列在前行在后，不跳过字母（与 GTP 的 D4 记法不同）。";
        t.json_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
        t.invoke = [c](const std::string&) -> ToolResult {
            const GameRecord rec = recordFromGame(c->game);
            std::vector<Json> moves;
            moves.reserve(rec.moves.size());
            int n = 0;
            for (const MoveRecord& m : rec.moves) {
                ++n;
                moves.push_back(Json::makeObject({
                    {"move", Json::makeNumber(n)},
                    {"color", Json::makeString(m.color == Color::Black ? "B" : "W")},
                    {"point", m.move.is_pass
                                  ? Json::makeString("")
                                  : Json::makeString(sgfPoint(m.move.p))},
                    {"pass", Json::makeBool(m.move.is_pass)},
                }));
            }
            std::vector<Json> setup;
            setup.reserve(rec.setup_black.size() + rec.setup_white.size());
            for (const Point& p : rec.setup_black)
                setup.push_back(Json::makeObject({
                    {"color", Json::makeString("B")},
                    {"point", Json::makeString(sgfPoint(p))},
                }));
            for (const Point& p : rec.setup_white)
                setup.push_back(Json::makeObject({
                    {"color", Json::makeString("W")},
                    {"point", Json::makeString(sgfPoint(p))},
                }));

            ToolResult r;
            r.json = dumpJson(Json::makeObject({
                {"size", Json::makeNumber(c->game.size())},
                {"komi", Json::makeNumber(c->game.komi())},
                {"move_count", Json::makeNumber(static_cast<double>(rec.moves.size()))},
                {"to_move",
                 Json::makeString(c->game.current() == Color::Black ? "black" : "white")},
                {"setup_stones", Json::makeArray(std::move(setup))},
                {"moves", Json::makeArray(std::move(moves))},
                {"sgf", Json::makeString(serializeSgf(rec))},
            }));
            return r;
        };
        r.registerTool(t);
    }

    return r;
}

}  // namespace vgo
