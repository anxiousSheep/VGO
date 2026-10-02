#pragma once

#include <functional>
#include <memory>
#include <string>

#include "analysis.h"
#include "conversation.h"
#include "engine.h"
#include "evaluate.h"
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

    // Request a full positional analysis (winrate / score lead / ownership) of
    // `game`'s current position. Powers the 形势 display. Invalid when the
    // engine provides no ownership (e.g. the fallback engine). The returned
    // winrate/score_lead are from the current player's perspective.
    using AnalysisCb = std::function<void(const Analysis&)>;
    void analyzePosition(const Game& game, AnalysisCb cb);

    // Record the engine analysis of the last played move (used as grounding).
    void noteAnalysis(const Analysis& a) { last_analysis_ = a; }

    // Invalidate any in-flight request and bump the generation counter.
    void cancel();

    int level() const { return level_; }
    void setLevel(int l) { level_ = l; }

    // 用户执子颜色（人机对弈时由 controller 设置；摆棋模式保持 Empty）。
    void setUserColor(Color c) { user_color_ = c; }
    Color userColor() const { return user_color_; }

    // 形势判断专用引擎（满血 KataGo，独立于落子引擎）。未设置时回退到 engine_。
    void setAnalysisEngine(EngineInterface* e) { analysis_engine_ = e; }

    // 形势判断强度档位（职业7/8/9段，见 strength.h 的 kAnalysisLevel*）。
    void setAnalysisLevel(int l) { analysis_level_ = l; }

    // 试下模式上下文：注入 LLM prompt，让模型知道已有进行中的演示及其走法。
    void setDemoState(bool active, const std::string& moves) {
        demo_active_ctx_ = active;
        demo_moves_ = moves;
    }

    // What-if evaluate a single move (N1.2). Wrapped for the UI (blunder alerts).
    void evaluateMove(const Game& game, const Point& p, MoveEvaluator::Callback cb);

private:
    GroundedFacts currentFacts(const Game& game) const;
    void deliverAnswer(const Game& game, const std::string& question,
                       GroundedFacts facts, ReplyCb cb);
    void handleMoveQuality(const Game& game, const std::string& question, ReplyCb cb);
    void handleLifeDeath(const Game& game, const std::string& question, ReplyCb cb);
    MoveEvaluator* evaluator();

    EngineInterface* engine_;
    EngineInterface* analysis_engine_ = nullptr;  // 形势判断专用，可空
    ConversationService* conversation_;
    int level_;
    int analysis_level_ = kAnalysisLevelNormal;  // 形势判断强度档位
    Color user_color_ = Color::Empty;
    Analysis last_analysis_;
    std::unique_ptr<MoveEvaluator> move_evaluator_;
    int generation_ = 0;
    bool demo_active_ctx_ = false;   // 试下模式上下文（供 LLM 判断继续/重置）
    std::string demo_moves_;
};

}  // namespace vgo
