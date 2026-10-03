#include "test_framework.h"

#include "ai/learner/learner_model.h"

using namespace vgo;

TEST(learner_blunder_weak_areas) {
    LearnerModel m;
    m.recordBlunder({WeakArea::Ko, 2.0});
    m.recordBlunder({WeakArea::Ko, 1.0});
    m.recordBlunder({WeakArea::LifeDeath, 0.5});
    const auto top = m.topWeakAreas(3);
    CHECK_EQ(top.size(), 2u);
    CHECK(top[0].first == WeakArea::Ko);
    CHECK_EQ(top[0].second, 3.0);
    CHECK(top[1].first == WeakArea::LifeDeath);
}

TEST(learner_estimate_level) {
    LearnerModel m;
    CHECK_EQ(m.estimateLevel(), 5);  // no history -> default
    GameSummary strong;
    strong.avg_loss_per_move = 1.0;
    m.recordGame(strong);
    CHECK_EQ(m.estimateLevel(), 19);

    LearnerModel m2;
    GameSummary weak;
    weak.avg_loss_per_move = 20.0;
    m2.recordGame(weak);
    CHECK_EQ(m2.estimateLevel(), 1);
}

TEST(learner_rolling_window_caps_20) {
    LearnerModel m;
    for (int i = 0; i < 25; ++i) m.recordGame(GameSummary{});
    CHECK_EQ(m.gameCount(), 20u);
}

TEST(learner_coach_memory) {
    LearnerModel m;
    CHECK(m.coachMemory().empty());
    m.recordBlunder({WeakArea::Ko, 3.0});
    m.recordGame(GameSummary{});
    CHECK(!m.coachMemory().empty());
    CHECK(m.coachMemory().find("打劫") != std::string::npos);
}

TEST(learner_sm2_review_schedule) {
    LearnerModel m;
    ReviewItem it;
    it.id = "a";
    it.area = WeakArea::Ko;
    m.addReviewItem(it);
    m.review("a", 4, 0);  // good recall
    CHECK(m.dueItems(0).empty());
    CHECK_EQ(m.dueItems(1).size(), 1u);
}

TEST(learner_sm2_reset_on_fail) {
    LearnerModel m;
    ReviewItem it;
    it.id = "a";
    m.addReviewItem(it);
    m.review("a", 4, 0);
    m.review("a", 2, 1);  // fail -> repetitions reset
    CHECK(m.dueItems(1).empty());
    CHECK_EQ(m.dueItems(2).size(), 1u);
}
