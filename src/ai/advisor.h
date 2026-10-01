#pragma once

#include <functional>
#include <string>

#include "analysis.h"
#include "conversation.h"
#include "engine.h"
#include "go/game.h"

namespace vgo {

// Combines an Engine (analysis / candidate moves) with a ConversationService
// (natural language) to answer questions about a position and explain the last
// move. Shared by the placement mode (the UI asks directly) and human-vs-AI
// (the controller explains each AI move). It never plays moves or drives a
// game.
class AiAdvisor {
public:
    using ReplyCb = std::function<void(const ConversationReply&)>;

    AiAdvisor(EngineInterface* engine, ConversationService* conversation, int level);

    // Answer `question` about the current position of `game`.
    void ask(const Game& game, const std::string& question, ReplyCb cb);

    // Explain the last move of `game`, grounding on the most recently noted
    // analysis (see noteAnalysis; invalid when no engine analysis exists).
    void explainLastMove(const Game& game, ReplyCb cb);

    // Record the engine analysis of the last played move (used as grounding).
    void noteAnalysis(const Analysis& a) { last_analysis_ = a; }

    // Invalidate any in-flight request and bump the generation counter.
    void cancel();

    int level() const { return level_; }
    void setLevel(int l) { level_ = l; }

private:
    GroundedFacts currentFacts(const Game& game) const;
    void deliverAnswer(const Game& game, const std::string& question,
                       GroundedFacts facts, ReplyCb cb);

    EngineInterface* engine_;
    ConversationService* conversation_;
    int level_;
    Analysis last_analysis_;
    int generation_ = 0;
};

}  // namespace vgo
