#include "agent_tools.h"

#include <memory>
#include <utility>
#include <vector>

#include "../gtp.h"
#include "../json.h"
#include "../lifedeath.h"
#include "../ownership.h"

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
        t.description = "判断某块棋（或盘面最大的一块死棋）的死活，并给出可能的做活点。";
        t.json_schema =
            "{\"type\":\"object\",\"properties\":{\"group\":{\"type\":\"string\","
            "\"description\":\"要判断的棋块上的一个 GTP 坐标（如 C3），省略则判断盘面最大死棋\"}},"
            "\"required\":[]}";
        t.invoke = [c](const std::string& args) -> ToolResult {
            ToolResult r;
            if (c->ownership.empty()) {
                r.ok = false;
                r.error = "需要 KataGo 归属分析才能判断死活";
                return r;
            }
            Point anchor{-1, -1};
            if (!parsePointArg(args, "group", c->game.size(), &anchor)) {
                r.ok = false;
                r.error = "非法坐标";
                return r;
            }
            LifeDeathResult ld;
            if (anchor.x >= 0 && c->game.board().at(anchor.x, anchor.y) != Color::Empty) {
                const auto groups = deadGroups(c->game.board(), c->ownership);
                for (const auto& grp : groups) {
                    bool hit = false;
                    for (const Point& p : grp) {
                        if (p == anchor) { hit = true; break; }
                    }
                    if (hit) {
                        ld.group = grp;
                        ld.alive = false;
                        ld.saving_moves = savingCandidates(c->game.board(), grp);
                        ld.reason = "该点所在棋块被判死";
                        break;
                    }
                }
                if (ld.group.empty()) {
                    ld.alive = true;
                    ld.reason = "该点所在棋块未被判死";
                }
            } else {
                ld = analyzeLifeDeath(c->game.board(), c->ownership);
            }
            r.json = dumpJson(Json::makeObject({
                {"alive", Json::makeBool(ld.alive)},
                {"group", pointsToJson(ld.group, c->game.size())},
                {"saving_moves", pointsToJson(ld.saving_moves, c->game.size())},
                {"reason", Json::makeString(ld.reason)},
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

    return r;
}

}  // namespace vgo
