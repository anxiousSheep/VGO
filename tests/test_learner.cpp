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

TEST(learner_serialize_roundtrip) {
    LearnerModel m;
    m.recordBlunder({WeakArea::Ko, 2.0});
    m.recordBlunder({WeakArea::LifeDeath, 0.5});
    GameSummary g;
    g.avg_loss_per_move = 3.0;
    g.won = true;
    g.level = 8;
    m.recordGame(g);
    ReviewItem it;
    it.id = "ko-1";
    it.area = WeakArea::Ko;
    m.addReviewItem(it);
    m.review("ko-1", 4, 0);

    const std::string json = m.serialize();
    LearnerModel out;
    CHECK(out.deserialize(json));
    CHECK_EQ(out.gameCount(), m.gameCount());
    CHECK_EQ(out.reviewCount(), m.reviewCount());
    CHECK_EQ(out.weakScore(WeakArea::Ko), m.weakScore(WeakArea::Ko));
    CHECK_EQ(out.estimateLevel(), m.estimateLevel());
    CHECK_EQ(out.dueItems(1).size(), m.dueItems(1).size());
}

TEST(learner_deserialize_rejects_garbage) {
    LearnerModel m;
    CHECK(!m.deserialize("not json"));
    CHECK(!m.deserialize("[1,2]"));
}

TEST(learner_classify_blunder_area) {
    // 结构信号优先：自紧气归入死活，提子/打吃归入战斗。
    CHECK(classifyBlunderArea({StructuralFact{FactType::SelfAtari, {}, 1}}, 30, 19) ==
          WeakArea::LifeDeath);
    CHECK(classifyBlunderArea({StructuralFact{FactType::Captures, {}, -1}}, 30, 19) ==
          WeakArea::Fighting);
    CHECK(classifyBlunderArea({StructuralFact{FactType::PutsInAtari, {}, -1}}, 200, 19) ==
          WeakArea::Fighting);
    // 无结构信号时按阶段兜底：布局→定式，中盘→战斗，收官→官子。
    CHECK(classifyBlunderArea({}, 10, 19) == WeakArea::Joseki);
    CHECK(classifyBlunderArea({}, 180, 19) == WeakArea::Fighting);
    CHECK(classifyBlunderArea({}, 300, 19) == WeakArea::Endgame);
}
