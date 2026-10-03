#include "learner_model.h"

#include <algorithm>
#include <cmath>

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

}  // namespace vgo
