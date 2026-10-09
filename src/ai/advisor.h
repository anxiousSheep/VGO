#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "agent/trace.h"
#include "analysis.h"
#include "conversation.h"
#include "engine.h"
#include "evaluate.h"
#include "go/game.h"
#include "verify/bad_sample.h"

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

    // B3：流式问答——与 ask 同源（接地 / 工具注入 / 校验闸门），但最终回合逐字送达。
    void askStream(const Game& game, const std::string& question,
                   ConversationService::DeltaCallback cb);

    // Explain the last move of `game`, grounding on the most recently noted
    // analysis (see noteAnalysis; invalid when no engine analysis exists).
    void explainLastMove(const Game& game, ReplyCb cb);

    // D5.2 投机预生成：AI 落子后后台预跑「为什么下这里」的讲解并缓存（key = 局面
    // hash），用户随后追问时近乎零延迟命中。仅在 setSpeculativeExplain(true) 时生效。
    void setSpeculativeExplain(bool on) { speculative_explain_ = on; }
    void prewarmExplanation(const Game& game);

    // Request a full positional analysis (winrate / score lead / ownership) of
    // `game`'s current position. Powers the 形势 display. Invalid when the
    // engine provides no ownership (e.g. the fallback engine). The returned
    // winrate/score_lead are from the current player's perspective.
    using AnalysisCb = std::function<void(const Analysis&)>;
    void analyzePosition(const Game& game, AnalysisCb cb);

    // Record the engine analysis of the last played move (used as grounding).
    void noteAnalysis(const Analysis& a) { last_analysis_ = a; }

    // D1.8：局面级快照预热——Background 优先级刷新 last_analysis_（与形势开关解耦），
    // 供模型工具（analyze_position / ownership_map / dead_groups / life_death_solve）按需取。
    void prewarmAnalysis(const Game& game);

    // Invalidate any in-flight request and bump the generation counter.
    void cancel();

    int level() const { return level_; }
    void setLevel(int l) { level_ = l; }
    // D3.4：新手难度爬坡的连续强度提示，使「我该下哪」建议与对局实际强度口径一致。
    void setKyuHint(double k) { kyu_hint_ = k; }

    // 用户执子颜色（人机对弈时由 controller 设置；摆棋模式保持 Empty）。
    void setUserColor(Color c) { user_color_ = c; }
    Color userColor() const { return user_color_; }

    // 形势判断专用引擎（满血 KataGo，独立于落子引擎）。未设置时回退到 engine_。
    void setAnalysisEngine(EngineInterface* e) { analysis_engine_ = e; }

    // 形势判断强度档位（职业7/8/9段，见 strength.h 的 kAnalysisLevel*）。
    // 同步更新 MoveEvaluator，使恶手量化的口径与形势判断一致。
    void setAnalysisLevel(int l) {
        analysis_level_ = l;
        if (move_evaluator_) move_evaluator_->setStrength(strengthForLevel(l));
    }

    // 试下模式上下文：注入 LLM prompt，让模型知道已有进行中的演示及其走法。
    void setDemoState(bool active, const std::string& moves) {
        demo_active_ctx_ = active;
        demo_moves_ = moves;
    }

    // What-if evaluate a single move (N1.2). Wrapped for the UI (blunder alerts).
    void evaluateMove(const Game& game, const Point& p, MoveEvaluator::Callback cb);

    // Evaluate the move already played (game.last_move()) — the correct
    // semantics for detecting a blunder on a move that is already on the board
    // (D3.2). Unlike evaluateMove, it does not re-play the point.
    void evaluateLastMove(const Game& game, MoveEvaluator::Callback cb);

    // D3.6：跨局教练记忆来源（UI 注入 LearnerModel::coachMemory；可为空）。
    void setCoachMemoryProvider(std::function<std::string()> fn) {
        coach_memory_fn_ = std::move(fn);
    }

    // D-a：讲解侧重来源（UI 注入 buildWeaknessDirective(topWeakAreas)；可为空）。
    void setWeaknessProvider(std::function<std::string()> fn) {
        weakness_fn_ = std::move(fn);
    }

    // 程序确定性输出来源：本局已提示的恶手预警（UI 注入，供 LLM 讲解上下文对齐）。
    void setBlunderNotesProvider(std::function<std::vector<BlunderNote>()> fn) {
        blunder_notes_fn_ = std::move(fn);
    }

    // D5.6 可观测：每次问答完成后，把一条 trace 记录交给 UI 落盘（纯逻辑层不碰 I/O）。
    void setTraceSink(std::function<void(const AgentTraceRecord&)> fn) {
        trace_sink_ = std::move(fn);
    }
    // trace 元信息（由 UI 在接线时提供：路由档位 + 模型 id）。
    void setTraceMeta(const std::string& route, const std::string& model) {
        trace_route_ = route;
        trace_model_ = model;
    }

    // D2.5 坏样本集：校验闸门拦截到「修正/不可验证」时，把样本交给 UI 落盘，
    // 反哺 D4 评估闭环（纯逻辑层不碰 I/O）。
    void setBadSampleSink(std::function<void(const BadSample&)> fn) {
        bad_sample_sink_ = std::move(fn);
    }

private:
    GroundedFacts currentFacts(const Game& game) const;
    void deliverAnswer(const Game& game, const std::string& question,
                       GroundedFacts facts, ReplyCb cb);
    void deliverAnswerStream(const Game& game, const std::string& question,
                             GroundedFacts facts, ConversationService::DeltaCallback cb);
    void handleMoveQuality(const Game& game, const std::string& question, ReplyCb cb);
    void handleLifeDeath(const Game& game, const std::string& question, ReplyCb cb);
    // D1.8：由同一份归属分析统一派生死棋清单与死活结论（工具/硬事实/校验同源）。
    void groundFromAnalysis(const Game& game, const Analysis& a, GroundedFacts* f);
    MoveEvaluator* evaluator();

    EngineInterface* engine_;
    EngineInterface* analysis_engine_ = nullptr;  // 形势判断专用，可空
    ConversationService* conversation_;
    int level_;
    double kyu_hint_ = 20.0;                  // D3.4 新手难度爬坡
    int analysis_level_ = kAnalysisLevelNormal;  // 形势判断强度档位
    Color user_color_ = Color::Empty;
    Analysis last_analysis_;
    std::unique_ptr<MoveEvaluator> move_evaluator_;
    int generation_ = 0;
    int prewarm_generation_ = 0;  // D1.8：预热请求代际（旧结果丢弃，不影响 generation_）
    bool demo_active_ctx_ = false;   // 试下模式上下文（供 LLM 判断继续/重置）
    std::string demo_moves_;
    std::function<std::string()> coach_memory_fn_;  // D3.6
    std::function<std::string()> weakness_fn_;      // D-a 讲解侧重
    std::function<std::vector<BlunderNote>()> blunder_notes_fn_;  // 本局恶手预警
    std::function<void(const AgentTraceRecord&)> trace_sink_;  // D5.6
    std::function<void(const BadSample&)> bad_sample_sink_;    // D2.5
    std::string trace_route_ = "template";
    std::string trace_model_;
    bool speculative_explain_ = false;                 // D5.2 投机预生成开关
    std::map<std::string, ConversationReply> explain_cache_;  // D5.2 key=局面 hash
};

}  // namespace vgo
