#pragma once

#include "engine.h"

namespace vgo {

// A dependency-free engine that plays legal moves. At higher strength levels it
// avoids self-atari and prefers captures; at low levels it plays near-randomly.
// Used as the default engine and as a test double.
class FallbackEngine : public EngineInterface {
public:
    explicit FallbackEngine(unsigned seed = 0x9E3779B9u);
    void requestMove(const Board& board, Color to_move,
                     const StrengthParams& params, Callback cb) override;

private:
    unsigned state_;
};

}  // namespace vgo
