#pragma once

#include <string>
#include <vector>

#include "analysis.h"
#include "go/board.h"

namespace vgo {

enum class FactType {
    Captures,      // 提掉对方棋子
    PutsInAtari,   // 把对方某块棋打成只剩一口气（打吃）
    Connects,      // 把己方两块棋连成一块
    Extends,       // 延展/加强己方已有的一块棋
    Placement,     // 普通落子
    SelfAtari,     // 自己气紧（只剩一口气，警示）
};

struct StructuralFact {
    FactType type = FactType::Placement;
    std::vector<Point> stones;  // captured stones, or the involved group
    int liberties = -1;         // resulting liberties of the mover's group
};

struct GroundedFacts {
    Analysis analysis;                    // may be invalid (no engine)
    std::vector<StructuralFact> facts;    // always-available board facts
};

// Board-derived facts describing what `move` accomplished. `captured` is the
// number of stones captured by this move. No engine analysis is required.
std::vector<StructuralFact> extractBoardFacts(const Board& before,
                                              const Board& after,
                                              const Move& move,
                                              int captured,
                                              Color mover);

// Render facts as a short, honest Chinese explanation. Empty if no signal.
std::string renderExplanation(const std::vector<StructuralFact>& facts, Color mover);

// ASCII rendering of the board (X=black, O=white, .=empty) with coordinates.
// Used as context for language models and in tests.
std::string boardToAscii(const Board& board);

}  // namespace vgo
