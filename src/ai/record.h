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

// A whole game record (棋谱): board size, komi, the ordered move list, and
// optional notes (悔棋记录 / 本局恶手), serialized as `#` comment lines. Pure
// data, no Qt. Serialized to the `.vgo` text format (see record.cpp).
struct GameRecord {
    int size = 9;
    double komi = 7.5;
    std::vector<MoveRecord> moves;
    std::vector<std::string> notes;
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
