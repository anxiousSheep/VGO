#include "learner_model.h"

#include <algorithm>
#include <cmath>

#include "../json.h"

namespace vgo {

namespace {
constexpr std::size_t kMaxGames = 20;
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

WeakArea classifyBlunderArea(const std::vector<StructuralFact>& facts,
                             int move_number, int size) {
    // 结构信号优先：自紧气（气紧/眼形）归入死活；提子/打吃上下文归入战斗。
    for (const StructuralFact& f : facts) {
        if (f.type == FactType::SelfAtari) return WeakArea::LifeDeath;
        if (f.type == FactType::Captures || f.type == FactType::PutsInAtari)
            return WeakArea::Fighting;
    }
    // 无明确结构信号时按棋局阶段兜底：布局→定式，中盘→战斗，收官→官子。
    const int area = size * size;
    if (area <= 0) return WeakArea::Fighting;
    if (move_number <= area / 4) return WeakArea::Joseki;
    if (move_number <= area / 2) return WeakArea::Fighting;
    return WeakArea::Endgame;
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
    if (games_.empty()) return 5;
    double sum = 0.0;
    for (const GameSummary& g : games_) sum += g.avg_loss_per_move;
    const double avg = sum / static_cast<double>(games_.size());
    // Linear map: avg loss ~1.0 pt/move -> level 19; ~20 pts/move -> level 1.
    const double t = (avg - 1.0) / (20.0 - 1.0);
    const int level = static_cast<int>(std::lround(19.0 - 18.0 * std::clamp(t, 0.0, 1.0)));
    return std::clamp(level, 1, 19);
}

double LearnerModel::estimateKyuRank() const {
    if (games_.empty()) return 20.0;  // 无历史：默认新手 20k
    double sum = 0.0;
    for (const GameSummary& g : games_) sum += g.avg_loss_per_move;
    const double avg = sum / static_cast<double>(games_.size());
    // 连续 kyu：avg loss 1.0 目/手 → 0 kyu（1d）；20 目/手 → 20 kyu（20k）。
    const double t = (avg - 1.0) / (20.0 - 1.0);
    return std::clamp(20.0 * t, 0.0, 20.0);
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
            reviews_.push_back(std::move(it));
        }
    }
    return true;
}

}  // namespace vgo
