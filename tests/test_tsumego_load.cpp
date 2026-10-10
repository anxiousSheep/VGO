#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/learner/daily_puzzle.h"
#include "ai/tsumego.h"

using namespace vgo;

TEST(tsumego_load_json_roundtrip) {
    Board b(9);
    b.set(1, 1, Color::Black);
    b.set(2, 1, Color::White);
    const std::string snap = b.snapshot();

    const std::string json =
        R"({"id":"p1","theme":"life-death","goal":"杀棋","difficulty":"easy","size":9,)"
        R"("snapshot":")" + snap +
        R"(","to_move":"black","source":"json",)"
        R"("target_group":["B2"],)"
        R"("solution":{"move":"C3","by":"black","verdict":"good","comment":"点要点",)"
        R"("children":[{"move":"D3","by":"white","verdict":"","comment":"应一手"}]}})";

    TsumegoLibrary lib;
    std::string err;
    CHECK(lib.fromJsonText(json, &err));
    const TsumegoProblem* p = lib.find("p1");
    CHECK(p != nullptr);
    if (p) {
        CHECK_EQ(p->theme, std::string("life-death"));
        CHECK_EQ(p->difficulty, std::string("easy"));
        CHECK(p->to_move == Color::Black);
        CHECK_EQ(p->target_group.size(), 1u);
        CHECK_EQ(p->solution_tree.children.size(), 1u);
    }

    Board rb;
    CHECK(boardFromSnapshot(snap, 9, &rb));
    CHECK(rb.at(1, 1) == Color::Black);
    CHECK(rb.at(2, 1) == Color::White);
}

TEST(tsumego_filter_and_find) {
    TsumegoLibrary lib;
    std::string err;
    CHECK(lib.fromJsonText(
        R"({"id":"a","theme":"life-death","difficulty":"easy","size":9,
            "snapshot":"","to_move":"black","solution":{"move":"C3","by":"black","verdict":"good"}})",
        &err));
    CHECK(lib.fromJsonText(
        R"({"id":"b","theme":"capture","difficulty":"hard","size":9,
            "snapshot":"","to_move":"white","solution":{"move":"C3","by":"white","verdict":"good"}})",
        &err));

    CHECK(lib.find("a") != nullptr);
    CHECK(lib.find("missing") == nullptr);
    CHECK_EQ(lib.filter("life-death", "easy").size(), 1u);
    CHECK_EQ(lib.filter("", "").size(), 2u);
    CHECK_EQ(lib.filter("life-death", "hard").size(), 0u);
}

TEST(tsumego_load_rejects_bad_json) {
    TsumegoLibrary lib;
    std::string err;
    CHECK(!lib.fromJsonText("{ not json", &err));
    CHECK(!lib.fromJsonText(R"({"id":"x"})", &err));  // 缺 solution
    CHECK(!lib.fromJsonText(
        R"({"id":"x","size":9,"solution":{"move":"Z99","by":"black"}})", &err));  // 非法坐标
}

TEST(tsumego_builtin_self_check) {
    const TsumegoLibrary lib = TsumegoLibrary::builtin();
    CHECK(lib.size() >= 1);
    for (const TsumegoProblem& p : lib.all()) {
        // snapshot 长度 = size²。
        CHECK_EQ(p.snapshot.size(), static_cast<std::size_t>(p.size) * p.size);
        // 解树根 move 合法。
        CHECK(!p.solution_tree.move.is_pass);
        // 目标棋块存在（非空）。
        CHECK(!p.target_group.empty());
        // snapshot 可重建。
        Board rb;
        CHECK(boardFromSnapshot(p.snapshot, p.size, &rb));
    }
}
