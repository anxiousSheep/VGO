#include "test_framework.h"

#include "ai/learner/learner_model.h"

using namespace vgo;

// --- D-a：画像 → 讲解侧重映射（纯逻辑） ---

TEST(weakness_directive_empty) {
    CHECK(buildWeaknessDirective({}).empty());
}

TEST(weakness_directive_each_area) {
    CHECK(buildWeaknessDirective({{WeakArea::LifeDeath, 3.0}}).find("死活") !=
          std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::Ko, 3.0}}).find("打劫") != std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::Counting, 3.0}}).find("数目") !=
          std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::Fighting, 3.0}}).find("战斗") !=
          std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::Endgame, 3.0}}).find("官子") !=
          std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::Joseki, 3.0}}).find("定式") !=
          std::string::npos);
    CHECK(buildWeaknessDirective({{WeakArea::ShapeJudgment, 3.0}}).find("形势判断") !=
          std::string::npos);
}

TEST(weakness_directive_top_n_order) {
    // 按给定顺序渲染，多条以「；」连接。
    const std::string s =
        buildWeaknessDirective({{WeakArea::Ko, 3.0}, {WeakArea::Fighting, 2.0}});
    const std::size_t ko = s.find("打劫");
    const std::size_t fight = s.find("战斗");
    CHECK(ko != std::string::npos);
    CHECK(fight != std::string::npos);
    CHECK(ko < fight);
    CHECK(s.find("；") != std::string::npos);
}
