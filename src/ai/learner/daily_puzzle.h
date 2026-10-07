#pragma once

#include <string>

#include "../go/board.h"
#include "../go/go_types.h"

namespace vgo {

// 每日一题（D3.5）判题结果。
struct DailyVerdict {
    bool passed = false;
    int score = 0;             // 60..100；未过 = 0
    double improvement = 0.0;  // 提升目数（可负）
};

// 判题：improvement = lead_new - lead_blunder；max = lead_before - lead_blunder。
// 提升 > 0 才算过（与恶手持平 / 更差都不过）；分数 60..100 与提升（0..max）双射
// （提升→0⁺ 得 60，提升 = max 得 100）。纯逻辑、可单测。
DailyVerdict judgeDailyMove(double lead_new, double lead_blunder, double lead_before);

// 从 Board::snapshot() 重建局面（'X'=黑 'O'=白 '.'=空，行主序 index = y*size+x）。
// 快照长度与 size² 不符时返回 false。
bool boardFromSnapshot(const std::string& snap, int size, Board* out);

// 快照字符串的稳定哈希（FNV-1a 十六进制），作复习项 id 以跨局去重。
std::string snapshotHash(const std::string& snap);

}  // namespace vgo
