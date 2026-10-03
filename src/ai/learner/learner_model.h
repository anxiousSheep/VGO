#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vgo {

// Cross-game learner profile (D3): an explainable weak-point vector, an
// observed strength estimate, and a simplified SM-2 review queue. Pure logic —
// persistence lives in the Qt layer (src/ui/model/). No Qt, no I/O.

enum class WeakArea {
    Fighting = 0,      // 战斗
    LifeDeath,         // 死活
    Endgame,           // 官子/收官
    Joseki,            // 定式
    ShapeJudgment,     // 形势判断
    Ko,                // 打劫
    Counting,          // 数目
};

constexpr int kWeakAreaCount = 7;

const char* weakAreaLabel(WeakArea a);

// A detected blunder, tagged with the area it belongs to (from FactType + the
// blunder context) and its severity (winrate drop, >= 0).
struct BlunderEvent {
    WeakArea area = WeakArea::Fighting;
    double severity = 0.0;
};

// One finished game's observable summary, used for strength estimation.
struct GameSummary {
    double avg_loss_per_move = 0.0;  // mean points lost per move vs optimal
    double winrate_deviation = 0.0;  // how far off the optimal winrate the play was
    int moves = 0;
    bool won = false;
    int level = 5;                   // the level the game was played at
};

// A spaced-repetition item generated from the learner's own blunders.
struct ReviewItem {
    std::string id;
    WeakArea area = WeakArea::Fighting;
    int repetitions = 0;
    double interval_days = 1.0;
    double ease = 2.5;
    int due_day = 0;
};

class LearnerModel {
public:
    // Weak-point vector: accumulate blunder severities per area.
    void recordBlunder(const BlunderEvent& e);

    // Rolling window of recent games (kept to the most recent 20).
    void recordGame(const GameSummary& g);

    // Top-N weak areas, sorted by accumulated severity (descending).
    std::vector<std::pair<WeakArea, double>> topWeakAreas(int n = 3) const;

    // Observed strength -> level 1..19 (default 5 with no history).
    int estimateLevel() const;

    // A short cross-game "coach memory" line, empty if there is no signal yet.
    std::string coachMemory() const;

    // Simplified SM-2 spaced repetition.
    void addReviewItem(const ReviewItem& item);
    void review(const std::string& id, int quality, int today);  // quality 0..5
    std::vector<const ReviewItem*> dueItems(int today) const;

    std::size_t gameCount() const { return games_.size(); }
    std::size_t reviewCount() const { return reviews_.size(); }
    double weakScore(WeakArea a) const;
    void clear();

private:
    double weak_[kWeakAreaCount] = {0.0};
    std::vector<GameSummary> games_;     // rolling window (max 20)
    std::vector<ReviewItem> reviews_;
};

}  // namespace vgo
