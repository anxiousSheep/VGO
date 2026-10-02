#pragma once

#include <string>
#include <vector>

#include "go/board.h"
#include "go/game.h"

namespace vgo {

// A variation is an independent demonstration sequence that never mutates the
// real game: it starts from a board snapshot and replays a list of steps. Used
// by the board-demo mode (N2). Pure data + state machine, no Qt.
enum class VariationState {
    WaitingForMove,   // ready to accept the next move (AI or user)
    Explaining,       // a move was just played; explanation (possibly streaming)
    WaitingForConfirm,  // explanation done; awaiting user confirmation
    Finished,         // exited
};

struct VariationStep {
    Move move;
    Color color = Color::Black;     // who plays this step
    std::string explanation;        // may be appended to incrementally
    std::vector<Point> marks;       // points to highlight
    bool by_ai = false;             // true if an engine chose this move
};

struct Variation {
    Board start;                    // board at demo start
    Color to_move = Color::Black;
    std::vector<VariationStep> steps;
    int cursor = 0;                 // number of steps currently revealed

    // Board after replaying the first `reveal` steps.
    Board boardAt(int reveal) const;
};

class VariationController {
public:
    void start(const Board& start, Color to_move);

    // Play a move. Truncates any branch beyond the current cursor first.
    void commitStep(const Move& m, Color color, bool by_ai);

    void appendExplanation(const std::string& delta);
    void setExplanation(const std::string& text);
    void finishExplanation();

    // Advance to the next move. Returns true if now WaitingForMove.
    bool confirmNext();
    bool back();      // hide one step; false at the start
    void replay();    // hide all steps
    void exit();

    const Variation& variation() const { return v_; }
    VariationState state() const { return state_; }
    Board board() const { return v_.boardAt(v_.cursor); }
    Color toMove() const;
    const VariationStep* currentStep() const;

private:
    Variation v_;
    VariationState state_ = VariationState::Finished;
};

}  // namespace vgo
