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
    CHECK_EQ(m.estimateLevel(), 1);  // no history -> 新手（而非中段）

    GameSummary one_dan;
    one_dan.avg_loss_per_move = 1.0;
    m.recordGame(one_dan);
    CHECK_EQ(m.estimateLevel(), 2);  // 1d -> 业余1段（而非职业9段）

    LearnerModel weak;
    GameSummary wk;
    wk.avg_loss_per_move = 20.0;
    weak.recordGame(wk);
    CHECK_EQ(weak.estimateLevel(), 1);  // 20k -> 新手

    LearnerModel pro;
    GameSummary pg;
    pg.avg_loss_per_move = 0.1;
    pro.recordGame(pg);
    CHECK(pro.estimateLevel() >= 11);  // 0.1 目/手 -> 职业段
}

TEST(learner_estimate_kyu_rank) {
    LearnerModel m;
    CHECK_EQ(m.estimateKyuRank(), 20.0);  // 无历史默认 20k 新手

    LearnerModel one_dan;
    GameSummary sg;
    sg.avg_loss_per_move = 1.0;
    one_dan.recordGame(sg);
    CHECK_EQ(one_dan.estimateKyuRank(), 0.0);  // 1d（毕业）

    LearnerModel weak;
    GameSummary wk;
    wk.avg_loss_per_move = 20.0;
    weak.recordGame(wk);
    CHECK_EQ(weak.estimateKyuRank(), 20.0);  // 20k

    LearnerModel mid;
    GameSummary md;
    md.avg_loss_per_move = 5.0;
    mid.recordGame(md);
    CHECK(mid.estimateKyuRank() > 0.0);   // 仍在新手带
    CHECK(mid.estimateKyuRank() < 20.0);
}

TEST(learner_kyu_level_consistency) {
    // 毕业（kyu=0）必须对应业余1段（level 2），而非职业9段（level 19）。
    LearnerModel m;
    GameSummary g;
    g.avg_loss_per_move = 1.0;  // 1d
    m.recordGame(g);
    CHECK_EQ(m.estimateKyuRank(), 0.0);
    CHECK_EQ(m.estimateLevel(), 2);
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

TEST(learner_review_item_from_blunder) {
    // 由恶手生成复习项：id 稳定（手数+落点）、题面含手数/坐标/说明、到期日=今天。
    // Point{2,6} 在 9 路盘 = GTP "C3"（y=0 为顶，行号 = size - y）。
    const ReviewItem it =
        reviewItemFromBlunder(12, Point{2, 6}, "胜率掉了 15%", WeakArea::Fighting, 100, 9);
    CHECK_EQ(it.id, std::string("blunder-12-C3"));
    CHECK(it.prompt.find("第 12 手") != std::string::npos);
    CHECK(it.prompt.find("C3") != std::string::npos);
    CHECK(it.prompt.find("胜率掉了 15%") != std::string::npos);
    CHECK_EQ(it.position_key, std::string("12-C3"));
    CHECK_EQ(it.due_day, 100);
    CHECK(it.area == WeakArea::Fighting);

    // 无坐标（虚着）退化为纯文字题面，仍可复习。
    const ReviewItem pass = reviewItemFromBlunder(5, Point{-1, -1}, "", WeakArea::Endgame, 0, 9);
    CHECK(pass.prompt.find("第 5 手") != std::string::npos);
    CHECK(!pass.prompt.empty());
}

TEST(learner_review_item_content_roundtrip) {
    // prompt / position_key 应随画像序列化往返，供「今日一题」跨启动展示题面。
    LearnerModel m;
    const ReviewItem it =
        reviewItemFromBlunder(12, Point{2, 6}, "胜率掉了 15%", WeakArea::Ko, 100, 9);
    m.addReviewItem(it);

    const std::string json = m.serialize();
    LearnerModel out;
    CHECK(out.deserialize(json));
    CHECK_EQ(out.reviewCount(), 1u);
    const std::vector<const ReviewItem*> due = out.dueItems(100);
    CHECK_EQ(due.size(), 1u);
    CHECK_EQ(due[0]->prompt, it.prompt);
    CHECK_EQ(due[0]->position_key, it.position_key);
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

TEST(learner_classify_blunder_area_new_shapes) {
    // B1a 新 FactType 映射：断/扳→战斗，尖/小飞/大飞/跳→定式，虎→形势判断。
    CHECK(classifyBlunderArea({StructuralFact{FactType::Cut, {}, -1}}, 100, 19) ==
          WeakArea::Fighting);
    CHECK(classifyBlunderArea({StructuralFact{FactType::Hane, {}, -1}}, 100, 19) ==
          WeakArea::Fighting);
    CHECK(classifyBlunderArea({StructuralFact{FactType::TigerMouth, {}, -1}}, 100, 19) ==
          WeakArea::ShapeJudgment);
    CHECK(classifyBlunderArea({StructuralFact{FactType::Diagonal, {}, -1}}, 100, 19) ==
          WeakArea::Joseki);
    CHECK(classifyBlunderArea({StructuralFact{FactType::SmallKnight, {}, -1}}, 100, 19) ==
          WeakArea::Joseki);
    CHECK(classifyBlunderArea({StructuralFact{FactType::LargeKnight, {}, -1}}, 100, 19) ==
          WeakArea::Joseki);
    CHECK(classifyBlunderArea({StructuralFact{FactType::OneSpaceJump, {}, -1}}, 100, 19) ==
          WeakArea::Joseki);
}
