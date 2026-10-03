#include "test_framework.h"

#include "ai/strength.h"

using namespace vgo;

TEST(strength_clamped_19) {
    CHECK_EQ(strengthForLevel(0).level, 1);
    CHECK_EQ(strengthForLevel(20).level, 19);
    CHECK_EQ(strengthForLevel(-5).level, 1);
}

TEST(strength_monotonic) {
    // Higher levels are less random for the fallback engine, across all 19.
    for (int l = 1; l < 19; ++l) {
        CHECK(strengthForLevel(l + 1).randomness <= strengthForLevel(l).randomness);
    }
}

TEST(strength_human_sl_range) {
    // 新手 + 业余段 use the HumanSL model (rank_*); 职业高段 use the standard model.
    CHECK(strengthForLevel(1).human_profile == "rank_20k");
    CHECK(strengthForLevel(2).human_profile == "rank_1d");
    CHECK(strengthForLevel(10).human_profile == "rank_9d");
    CHECK(strengthForLevel(11).human_profile == "rank_9d");  // 职业1段 = HumanSL 顶档
    CHECK(strengthForLevel(12).human_profile.empty());       // 职业2段 -> 标准模型
    CHECK(strengthForLevel(19).human_profile.empty());
}

TEST(strength_pro_visits_increase) {
    CHECK(strengthForLevel(12).max_visits < strengthForLevel(13).max_visits);
    CHECK(strengthForLevel(15).max_visits < strengthForLevel(16).max_visits);
    CHECK_EQ(strengthForLevel(19).max_visits, 100000);
}

TEST(rank_labels) {
    CHECK(rankLabel(1) == "新手");
    CHECK(rankLabel(2) == "业余1段");
    CHECK(rankLabel(10) == "业余9段");
    CHECK(rankLabel(11) == "职业1段");
    CHECK(rankLabel(19) == "职业9段");
}

TEST(human_profile_ladder) {
    CHECK_EQ(humanProfileForStrength(20.0), std::string("rank_20k"));
    CHECK_EQ(humanProfileForStrength(15.0), std::string("rank_15k"));
    CHECK_EQ(humanProfileForStrength(10.0), std::string("rank_10k"));
    CHECK_EQ(humanProfileForStrength(7.0), std::string("rank_7k"));
    CHECK_EQ(humanProfileForStrength(5.0), std::string("rank_5k"));
    CHECK_EQ(humanProfileForStrength(3.0), std::string("rank_3k"));
    CHECK_EQ(humanProfileForStrength(1.0), std::string("rank_1k"));
    CHECK_EQ(humanProfileForStrength(0.0), std::string("rank_1d"));
    CHECK_EQ(humanProfileForStrength(-5.0), std::string("rank_1d"));  // 1d 及以上钳制
    CHECK_EQ(humanProfileForStrength(30.0), std::string("rank_20k")); // 20k 以下钳制
}

TEST(strength_level1_uses_kyu_hint) {
    // 新手（level 1）据连续 kyu 提示在阶梯上选档，消除 20k→1d 断崖。
    CHECK_EQ(strengthForLevel(1, 20.0).human_profile, std::string("rank_20k"));
    CHECK_EQ(strengthForLevel(1, 10.0).human_profile, std::string("rank_10k"));
    CHECK_EQ(strengthForLevel(1, 0.0).human_profile, std::string("rank_1d"));
    // 默认（无 kyu 提示）仍是 20k，向后兼容。
    CHECK_EQ(strengthForLevel(1).human_profile, std::string("rank_20k"));
    // 非新手档位忽略 kyu 提示。
    CHECK_EQ(strengthForLevel(2, 20.0).human_profile, std::string("rank_1d"));
    CHECK_EQ(strengthForLevel(5, 20.0).human_profile, std::string("rank_3d"));
}

TEST(snap_kyu_rung) {
    CHECK_EQ(snapKyuRung(20.0), 20);
    CHECK_EQ(snapKyuRung(15.0), 15);
    CHECK_EQ(snapKyuRung(10.0), 10);
    CHECK_EQ(snapKyuRung(7.0), 7);
    CHECK_EQ(snapKyuRung(5.0), 5);
    CHECK_EQ(snapKyuRung(3.0), 3);
    CHECK_EQ(snapKyuRung(1.0), 1);
    CHECK_EQ(snapKyuRung(0.0), 0);
    CHECK_EQ(snapKyuRung(-5.0), 0);   // 1d 及以上钳制
    CHECK_EQ(snapKyuRung(30.0), 20);  // 20k 以下钳制
}

TEST(novice_level_label_dynamic) {
    CHECK_EQ(noviceLevelLabel(20.0, 1), std::string("新手（20级）"));
    CHECK_EQ(noviceLevelLabel(15.0, 1), std::string("新手（15级）"));
    CHECK_EQ(noviceLevelLabel(10.0, 1), std::string("新手（10级）"));
    CHECK_EQ(noviceLevelLabel(7.0, 1), std::string("新手（7级）"));
    CHECK_EQ(noviceLevelLabel(5.0, 1), std::string("新手（5级）"));
    CHECK_EQ(noviceLevelLabel(3.0, 1), std::string("新手（3级）"));
    CHECK_EQ(noviceLevelLabel(1.0, 1), std::string("新手（1级）"));
}

TEST(novice_level_label_past_novice_hints) {
    // 已过新手（kyu<=0）给趣味提示词（非等级标注），且长度可控（按字符计，防溢出）。
    const auto utf8Len = [](const std::string& s) {
        std::size_t n = 0;
        for (unsigned char c : s)
            if ((c & 0xC0) != 0x80) ++n;  // 只数非续字节 = UTF-8 字符数
        return n;
    };
    const std::string hint = noviceLevelLabel(0.0, 19);
    CHECK(hint.find("新手（") == 0);
    CHECK(hint.find("级）") == std::string::npos);  // 不是「X级」
    CHECK(utf8Len(hint) <= 13u);                   // 控制字符长度防溢出
    // 不同 level 轮换不同提示词。
    CHECK(noviceLevelLabel(0.0, 0) != noviceLevelLabel(0.0, 1));
}

TEST(smurf_guard_triggers_for_strong_manual_novice) {
    // 强手（业余1段+）手动选新手 + 5% 命中 → 匹配职业 AI（analysis_level）。
    CHECK_EQ(smurfGuardedLevel(1, 2, 15, /*manual=*/true, 0.01), 15);
    CHECK_EQ(smurfGuardedLevel(1, 19, 17, true, 0.049), 17);
}

TEST(smurf_guard_no_trigger_when_roll_high) {
    // 5% 未命中 → 保持新手。
    CHECK_EQ(smurfGuardedLevel(1, 2, 15, true, 0.05), 1);
    CHECK_EQ(smurfGuardedLevel(1, 19, 15, true, 0.9), 1);
}

TEST(smurf_guard_ignores_weak_or_auto_or_non_novice) {
    // 弱者（估计 < 2，业余1段以下）不惩罚。
    CHECK_EQ(smurfGuardedLevel(1, 1, 15, true, 0.01), 1);
    // 自动选档不触发。
    CHECK_EQ(smurfGuardedLevel(1, 2, 15, /*manual=*/false, 0.01), 1);
    // 手动选非新手档不触发。
    CHECK_EQ(smurfGuardedLevel(5, 2, 15, true, 0.01), 5);
}

TEST(smurf_guard_clamps_analysis_level) {
    CHECK_EQ(smurfGuardedLevel(1, 2, 0, true, 0.01), 2);   // 下限 2
    CHECK_EQ(smurfGuardedLevel(1, 2, 99, true, 0.01), 19); // 上限 19
}
