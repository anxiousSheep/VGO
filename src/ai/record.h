#pragma once

#include <string>
#include <vector>

#include "go/game.h"

namespace vgo {

// One recorded move: who played it and what they played (stone or pass).
struct MoveRecord {
    Color color = Color::Black;
    Move move;  // is_pass marks a pass; is_resign is not serialized
};

// 一局中检测到的一次恶手（供复盘交互：面板提示 + 落点命中检测）。
struct BlunderNote {
    int move = 0;          // 1-based 手数
    Point point{-1, -1};   // 落点（GTP 坐标；虚着/未知为 {-1,-1}）
    std::string text;      // 说明（胜率/领先缩水等）
};

// A whole game record (棋谱): board size, komi, setup stones (handicap), the
// ordered move list, and optional notes (悔棋记录 / 本局恶手), serialized as
// `#` comment lines. Pure data, no Qt. Serialized to the `.vgo` text format
// (see record.cpp).
struct GameRecord {
    int size = 9;
    double komi = 7.5;
    std::vector<Point> setup_black;  // 让子摆子（黑，L6；SGF AB）
    std::vector<Point> setup_white;  // 摆子（白，SGF AW；罕见）
    std::vector<MoveRecord> moves;
    std::vector<BlunderNote> blunders;  // 本局恶手（结构化，可命中检测）
    std::vector<std::string> notes;     // 其它备注（悔棋等）
};

// Serialize a record to the `.vgo` text format:
//   VGO 1
//   SIZE 9
//   KOMI 7.5
//   B D4
//   W C3
//   B PASS
//   # 本局恶手：第 12 手：…
std::string serializeRecord(const GameRecord& r);

// Parse a `.vgo` text into a record. Returns false on malformed input (the
// record is left in a best-effort but unspecified state on failure).
bool parseRecord(const std::string& text, GameRecord* out);

// Extract a record from a live Game (its move sequence + size + komi). Undone
// moves are preserved as notes (悔棋记录) rather than dropped, so the saved
// record keeps the full undo history.
GameRecord recordFromGame(const Game& game);

// Rebuild a Game by replaying the first `reveal` moves of the record. Used by
// the review mode to show an arbitrary position. `reveal` is clamped to
// [0, moves.size()].
Game recordToGame(const GameRecord& r, int reveal);

}  // namespace vgo
