#include "learner_model.h"

#include <algorithm>
#include <cmath>

#include "../gtp.h"
#include "../json.h"
#include "daily_puzzle.h"

namespace vgo {

namespace {
constexpr std::size_t kMaxGames = 20;

// 连续棋力 R（kyu/dan：20 = 20k 新手，0 = 1d 业余1段，负 = 段位）。据平均每手
// 损点对数映射：avg_loss 1.0 → 0（1d，毕业），avg_loss 20 → 20（20k），可负。
// 这是 L1 精确标定前的合理启发式（avg_loss 每减半约升 ~4.6 档），保证「毕业（1d）」
// 与「业余1段（level 2）」一致，而非旧线性映射下误判成职业9段。
double strengthRank(double avg_loss) {
    const double avg = std::max(avg_loss, 0.01);
    return 20.0 * std::log(avg) / std::log(20.0);
}

// 近 20 局滚动窗口的平均每手损点；无历史返回 -1。
double avgLoss(const std::vector<GameSummary>& games) {
    if (games.empty()) return -1.0;
    double sum = 0.0;
    for (const GameSummary& g : games) sum += g.avg_loss_per_move;
    return sum / static_cast<double>(games.size());
}
}  // namespace

const char* weakAreaLabel(WeakArea a) {
    switch (a) {
        case WeakArea::Fighting: return "战斗";
        case WeakArea::LifeDeath: return "死活";
        case WeakArea::Endgame: return "官子";
        case WeakArea::Joseki: return "定式";
        case WeakArea::ShapeJudgment: return "形势判断";
        case WeakArea::Ko: return "打劫";
        case WeakArea::Counting: return "数目";
    }
    return "其他";
}

std::string buildWeaknessDirective(const std::vector<std::pair<WeakArea, double>>& topWeak) {
    std::string out;
    for (const auto& p : topWeak) {
        if (!out.empty()) out += "；";
        switch (p.first) {
            case WeakArea::LifeDeath:
                out += "用户最近死活偏弱，涉及攻杀/做活的讲解务必带气数与眼位提醒";
                break;
            case WeakArea::Ko:
                out += "用户最近在打劫上吃亏，涉及劫争的讲解务必说明劫材与得失";
                break;
            case WeakArea::Counting:
                out += "用户数目偏弱，涉及形势的讲解务必逐块说明归属";
                break;
            case WeakArea::Fighting:
                out += "用户战斗判断偏弱，讲解攻防时点明方向与厚薄";
                break;
            case WeakArea::Endgame:
                out += "用户收官偏弱，涉及官子的讲解务必说明目数大小与先后手";
                break;
            case WeakArea::Joseki:
                out += "用户定式掌握不足，涉及定式的讲解务必说明后续变化与选择理由";
                break;
            case WeakArea::ShapeJudgment:
                out += "用户形势判断偏弱，讲解时务必点明各块棋的厚薄与潜力";
                break;
        }
    }
    return out;
}

WeakArea classifyBlunderArea(const std::vector<StructuralFact>& facts,
                             int move_number, int size) {
    // 结构信号优先（按信号强度分档，与 facts 的向量顺序无关）：
    // 自紧气（气紧/眼形）→死活；提子/打吃→战斗；断/扳→战斗；虎口→形势判断；
    // 尖/飞/跳等几何着法→定式。
    bool self_atari = false;
    bool capture_fight = false;
    bool cut_hane = false;
    bool tiger_mouth = false;
    bool joseki_shape = false;
    for (const StructuralFact& f : facts) {
        switch (f.type) {
            case FactType::SelfAtari: self_atari = true; break;
            case FactType::Captures:
            case FactType::PutsInAtari: capture_fight = true; break;
            case FactType::Cut:
            case FactType::Hane: cut_hane = true; break;
            case FactType::TigerMouth: tiger_mouth = true; break;
            case FactType::Diagonal:
            case FactType::SmallKnight:
            case FactType::LargeKnight:
            case FactType::OneSpaceJump: joseki_shape = true; break;
            default: break;
        }
    }
    if (self_atari) return WeakArea::LifeDeath;
    if (capture_fight || cut_hane) return WeakArea::Fighting;
    if (tiger_mouth) return WeakArea::ShapeJudgment;
    if (joseki_shape) return WeakArea::Joseki;

    // 无明确结构信号时按棋局阶段兜底：布局→定式，中盘→战斗，收官→官子。
    const int area = size * size;
    if (area <= 0) return WeakArea::Fighting;
    if (move_number <= area / 4) return WeakArea::Joseki;
    if (move_number <= area / 2) return WeakArea::Fighting;
    return WeakArea::Endgame;
}

ReviewItem reviewItemFromBlunder(int move_number, const Point& point,
                                 const std::string& note, WeakArea area,
                                 int today, int size,
                                 const std::string& board_snapshot,
                                 double lead_before, double lead_blunder,
                                 Color to_move) {
    ReviewItem it;
    const std::string coord =
        (point.x >= 0 && point.y >= 0) ? gtpPoint(point, size) : std::string();
    // 有棋盘快照时按「局面」去重（跨局同位复用同一条）；否则回退「手数-坐标」。
    if (!board_snapshot.empty())
        it.id = "blunder-" + snapshotHash(board_snapshot);
    else
        it.id = "blunder-" + std::to_string(move_number) + "-" + coord;
    it.area = area;
    it.due_day = today;
    // 题面：手数 + 落点 + 恶手说明；无坐标（虚着）时退化为纯文字。
    it.prompt = "第 " + std::to_string(move_number) + " 手";
    if (!coord.empty()) it.prompt += "（" + coord + "）";
    it.prompt += "：";
    it.prompt += note.empty() ? std::string("这一步是恶手") : note;
    it.prompt += "。回想一下这手棋的问题，以及更稳的下法。";
    it.position_key = std::to_string(move_number) + "-" + coord;
    // D3.5 棋盘题字段。
    it.size = size;
    it.board_snapshot = board_snapshot;
    it.to_move = to_move;
    it.blunder_move = point;
    it.lead_before = lead_before;
    it.lead_blunder = lead_blunder;
    it.note = note;
    return it;
}

void LearnerModel::recordBlunder(const BlunderEvent& e) {
    const int i = static_cast<int>(e.area);
    if (i >= 0 && i < kWeakAreaCount) weak_[i] += std::max(0.0, e.severity);
}

void LearnerModel::recordGame(const GameSummary& g) {
    games_.push_back(g);
    if (games_.size() > kMaxGames) games_.erase(games_.begin());
}

double LearnerModel::weakScore(WeakArea a) const {
    const int i = static_cast<int>(a);
    return (i >= 0 && i < kWeakAreaCount) ? weak_[i] : 0.0;
}

std::vector<std::pair<WeakArea, double>> LearnerModel::topWeakAreas(int n) const {
    std::vector<std::pair<WeakArea, double>> out;
    for (int i = 0; i < kWeakAreaCount; ++i) {
        if (weak_[i] > 0.0) out.emplace_back(static_cast<WeakArea>(i), weak_[i]);
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (n >= 0 && static_cast<std::size_t>(n) < out.size())
        out.resize(static_cast<std::size_t>(n));
    return out;
}

int LearnerModel::estimateLevel() const {
    const double avg = avgLoss(games_);
    if (avg < 0.0) return 1;  // 无历史：默认新手
    const double R = strengthRank(avg);
    // 连续 kyu/dan R → 1..19 档：R=0（1d）→ 2（业余1段），R=-8（9d）→ 10，
    // R=-17（职业9段）→ 19。与 estimateKyuRank 同源，保证毕业↔业余1段一致。
    return std::clamp(static_cast<int>(std::lround(2.0 - R)), 1, 19);
}

double LearnerModel::estimateKyuRank() const {
    const double avg = avgLoss(games_);
    if (avg < 0.0) return 20.0;  // 无历史：默认新手 20k
    // 新手带连续 kyu：avg_loss 1.0 → 0（1d 毕业），20 → 20（20k）；1d+ 钳制为 0。
    return std::clamp(strengthRank(avg), 0.0, 20.0);
}

std::string LearnerModel::coachMemory() const {
    if (games_.empty()) return {};
    const auto top = topWeakAreas(1);
    if (top.empty() || top.front().second < 1.0) return {};
    return std::string("你最近在「") + weakAreaLabel(top.front().first) +
           "」上多次吃亏，可以多练练这方面的判断。";
}

void LearnerModel::addReviewItem(const ReviewItem& item) {
    for (ReviewItem& r : reviews_) {
        if (r.id == item.id) {
            r = item;
            return;
        }
    }
    reviews_.push_back(item);
}

void LearnerModel::review(const std::string& id, int quality, int today) {
    for (ReviewItem& r : reviews_) {
        if (r.id != id) continue;
        quality = std::clamp(quality, 0, 5);
        if (quality >= 3) {
            ++r.repetitions;
            if (r.repetitions == 1) r.interval_days = 1.0;
            else if (r.repetitions == 2) r.interval_days = 6.0;
            else r.interval_days = std::round(r.interval_days * r.ease);
        } else {
            r.repetitions = 0;
            r.interval_days = 1.0;
        }
        r.ease = std::max(1.3, r.ease +
                                   (0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02)));
        r.due_day = today + static_cast<int>(std::lround(r.interval_days));
        return;
    }
}

std::vector<const ReviewItem*> LearnerModel::dueItems(int today) const {
    std::vector<const ReviewItem*> out;
    for (const ReviewItem& r : reviews_)
        if (r.due_day <= today) out.push_back(&r);
    return out;
}

void LearnerModel::clear() {
    for (double& w : weak_) w = 0.0;
    games_.clear();
    reviews_.clear();
}

std::string LearnerModel::serialize() const {
    std::vector<Json> weak;
    for (double w : weak_) weak.push_back(Json::makeNumber(w));

    std::vector<Json> games;
    for (const GameSummary& g : games_) {
        games.push_back(Json::makeObject({
            {"avg_loss", Json::makeNumber(g.avg_loss_per_move)},
            {"winrate_dev", Json::makeNumber(g.winrate_deviation)},
            {"moves", Json::makeNumber(g.moves)},
            {"won", Json::makeBool(g.won)},
            {"level", Json::makeNumber(g.level)},
        }));
    }

    std::vector<Json> reviews;
    for (const ReviewItem& r : reviews_) {
        reviews.push_back(Json::makeObject({
            {"id", Json::makeString(r.id)},
            {"area", Json::makeNumber(static_cast<int>(r.area))},
            {"repetitions", Json::makeNumber(r.repetitions)},
            {"interval", Json::makeNumber(r.interval_days)},
            {"ease", Json::makeNumber(r.ease)},
            {"due_day", Json::makeNumber(r.due_day)},
            {"prompt", Json::makeString(r.prompt)},
            {"position_key", Json::makeString(r.position_key)},
            {"size", Json::makeNumber(r.size)},
            {"board_snapshot", Json::makeString(r.board_snapshot)},
            {"to_move", Json::makeNumber(static_cast<int>(r.to_move))},
            {"blunder_x", Json::makeNumber(r.blunder_move.x)},
            {"blunder_y", Json::makeNumber(r.blunder_move.y)},
            {"lead_before", Json::makeNumber(r.lead_before)},
            {"lead_blunder", Json::makeNumber(r.lead_blunder)},
            {"note", Json::makeString(r.note)},
        }));
    }

    return dumpJson(Json::makeObject({
        {"weak", Json::makeArray(std::move(weak))},
        {"games", Json::makeArray(std::move(games))},
        {"reviews", Json::makeArray(std::move(reviews))},
    }));
}

bool LearnerModel::deserialize(const std::string& json) {
    Json root;
    if (!parseJson(json, &root) || !root.isObject()) return false;

    if (const Json* weak = root.get("weak"); weak && weak->isArray()) {
        const int n = std::min<int>(kWeakAreaCount,
                                    static_cast<int>(weak->array.size()));
        for (int i = 0; i < n; ++i) weak_[i] = weak->array[static_cast<std::size_t>(i)].asNumber();
    }

    games_.clear();
    if (const Json* games = root.get("games"); games && games->isArray()) {
        for (const Json& g : games->array) {
            GameSummary s;
            s.avg_loss_per_move = g.get("avg_loss") ? g.get("avg_loss")->asNumber() : 0.0;
            s.winrate_deviation = g.get("winrate_dev") ? g.get("winrate_dev")->asNumber() : 0.0;
            s.moves = g.get("moves") ? static_cast<int>(g.get("moves")->asNumber()) : 0;
            s.won = g.get("won") ? g.get("won")->asBool(false) : false;
            s.level = g.get("level") ? static_cast<int>(g.get("level")->asNumber()) : 5;
            games_.push_back(s);
        }
        if (games_.size() > kMaxGames) games_.erase(games_.begin(), games_.end() - kMaxGames);
    }

    reviews_.clear();
    if (const Json* reviews = root.get("reviews"); reviews && reviews->isArray()) {
        for (const Json& r : reviews->array) {
            ReviewItem it;
            it.id = r.get("id") ? r.get("id")->asString() : "";
            const int area = r.get("area") ? static_cast<int>(r.get("area")->asNumber()) : 0;
            it.area = (area >= 0 && area < kWeakAreaCount) ? static_cast<WeakArea>(area)
                                                           : WeakArea::Fighting;
            it.repetitions =
                r.get("repetitions") ? static_cast<int>(r.get("repetitions")->asNumber()) : 0;
            it.interval_days = r.get("interval") ? r.get("interval")->asNumber() : 1.0;
            it.ease = r.get("ease") ? r.get("ease")->asNumber() : 2.5;
            it.due_day = r.get("due_day") ? static_cast<int>(r.get("due_day")->asNumber()) : 0;
            it.prompt = r.get("prompt") ? r.get("prompt")->asString() : "";
            it.position_key = r.get("position_key") ? r.get("position_key")->asString() : "";
            it.size = r.get("size") ? static_cast<int>(r.get("size")->asNumber()) : 9;
            it.board_snapshot = r.get("board_snapshot") ? r.get("board_snapshot")->asString() : "";
            const int to_move = r.get("to_move") ? static_cast<int>(r.get("to_move")->asNumber()) : 1;
            it.to_move = (to_move == 2) ? Color::White : (to_move == 1 ? Color::Black : Color::Empty);
            it.blunder_move = {r.get("blunder_x") ? static_cast<int>(r.get("blunder_x")->asNumber()) : -1,
                               r.get("blunder_y") ? static_cast<int>(r.get("blunder_y")->asNumber()) : -1};
            it.lead_before = r.get("lead_before") ? r.get("lead_before")->asNumber() : 0.0;
            it.lead_blunder = r.get("lead_blunder") ? r.get("lead_blunder")->asNumber() : 0.0;
            it.note = r.get("note") ? r.get("note")->asString() : "";
            reviews_.push_back(std::move(it));
        }
    }
    return true;
}

}  // namespace vgo
