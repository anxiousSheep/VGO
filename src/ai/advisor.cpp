#include "advisor.h"

#include <chrono>
#include <sstream>
#include <utility>

#include "agent/agent_tools.h"
#include "engine_pool.h"
#include "joseki.h"
#include "lifedeath.h"
#include "lifedeath_shape.h"
#include "lifedeath_verify.h"
#include "move_shape.h"
#include "ownership.h"
#include "phase.h"
#include "strength.h"
#include "verify/claim_extractor.h"
#include "verify/claim_verifier.h"
#include "verify/leak_gate.h"
#include "verify/term_gate.h"

namespace vgo {

namespace {

// D5.2 投机预生成缓存上限：只保留最近几个局面的讲解，避免长对局缓存无限膨胀。
constexpr std::size_t kMaxExplainCache = 8;

// Wall-clock ms since epoch, for trace timestamps (纯逻辑层，无 Qt)。
std::int64_t nowEpochMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// G1：推荐点（引擎推荐 / what-if 落点）着法几何术语渲染（程序判定），供 prompt 注入。
std::string renderRecommendedShape(const Board& board, const GroundedFacts& facts,
                                   Color to_move) {
    Point sp{-1, -1};
    Color sp_mover = to_move;
    if (facts.move_eval && facts.move_eval->valid && facts.move_eval->p.x >= 0) {
        sp = facts.move_eval->p;
        sp_mover = facts.move_eval->mover;
    } else if (facts.analysis.valid && !facts.analysis.best.is_pass &&
               facts.analysis.best.p.x >= 0) {
        sp = facts.analysis.best.p;
    }
    if (sp.x < 0) return {};
    const ShapeFact sf = classifyMove(board, sp, sp_mover);
    if (sf.term.empty() || sf.term == "普通落子") return {};
    return sf.relation + "，术语为「" + sf.term + "」";
}

// 把校验结果压缩成 trace 里的一档：none / pass / corrected / unverifiable。
const char* verifySummary(const std::vector<ClaimVerdict>& verdicts) {
    if (verdicts.empty()) return "none";
    bool unverifiable = false;
    bool corrected = false;
    for (const ClaimVerdict& v : verdicts) {
        if (v.disposition == ClaimDisposition::Unverifiable) unverifiable = true;
        else if (v.disposition == ClaimDisposition::Corrected) corrected = true;
    }
    if (unverifiable) return "unverifiable";
    if (corrected) return "corrected";
    return "pass";
}

// D2.5：把真值压成一行可读摘要，写进坏样本供后续复盘（统一转成黑方视角）。
std::string truthSummary(const GroundedFacts& facts, double score_black, Color to_move) {
    if (facts.analysis.valid) {
        const double blackLead = (to_move == Color::Black) ? facts.analysis.score_lead
                                                           : -facts.analysis.score_lead;
        const double blackWinrate = (to_move == Color::Black)
                                        ? facts.analysis.winrate
                                        : 1.0 - facts.analysis.winrate;
        std::ostringstream os;
        os << "黑领先 " << blackLead << " 目 / 黑胜率 " << (blackWinrate * 100.0) << "%";
        return os.str();
    }
    if (!facts.analysis.ownership.empty()) {
        std::ostringstream os;
        os << "归属领先 " << ownershipLead(facts.analysis.ownership) << " 目";
        return os.str();
    }
    std::ostringstream os;
    os << "规则计分黑领先 " << score_black << " 目";
    return os.str();
}

// D2 "Verify-then-Speak"：把模型回复里的断言抽出、对照真值校验，三档处置——
// 通过 / 小偏差（追加「已按引擎数据修正」标注）/ 不可验证（置 unsure 灰显）。
// 无任何真值时（模板解说 / 离线）为 no-op，不劣化现有可用性。
void verifyAndAnnotate(ConversationReply& r, const Board& board,
                       const GroundedFacts& facts, double score_black,
                       Color to_move, int size) {
    // G4-3 泄露闸门（硬保险，先于 truth/claims 判断）：回答正文泄露定式判定书 / 仲裁的
    // 内部字段名时，追加标注并置 unsure 灰显。与 D2 数字校验并列，模型「说不听」也拦得住。
    if (leakGateViolation(r.text)) {
        r.text += "\n\n（注：回答含内部字段，已标记为待确认。）";
        r.unsure = true;
        return;
    }
    // G1-3 术语断言闸门：程序已对推荐点判定出术语时，若正文把它改写成了另一个着法术语，
    // 追加标注并置 unsure（治「R17 说成扳」）。
    {
        Point rec{-1, -1};
        Color rec_mover = to_move;
        if (facts.analysis.valid && !facts.analysis.best.is_pass && facts.analysis.best.p.x >= 0) {
            rec = facts.analysis.best.p;
        }
        if (rec.x >= 0) {
            const ShapeFact sf = classifyMove(board, rec, rec_mover);
            if (!sf.term.empty() && sf.term != "普通落子" &&
                termGateViolation(r.text, sf.term)) {
                r.text += "\n\n（注：回答中的着法术语与程序判定不一致，已标记为待确认。）";
                r.unsure = true;
            }
        }
    }
    const bool has_truth = facts.analysis.valid || !facts.analysis.ownership.empty() ||
                           !facts.dead_groups.empty() || facts.move_eval.has_value() ||
                           facts.life_death.has_value();
    if (!has_truth) return;

    const std::vector<Claim> claims = ClaimExtractor{}.extract(r.text, size);
    if (claims.empty()) return;

    std::vector<ClaimVerdict> verdicts =
        ClaimVerifier{}.verify(claims, board, facts.analysis.ownership,
                               facts.analysis, score_black, to_move);
    bool corrected = false;
    bool unverifiable = false;
    for (const ClaimVerdict& v : verdicts) {
        if (v.disposition == ClaimDisposition::Corrected) corrected = true;
        else if (v.disposition == ClaimDisposition::Unverifiable) unverifiable = true;
    }
    // D2.4：把校验后的断言（含 source / disposition）挂到回复上，供 UI hover 溯源。
    r.verdicts = std::move(verdicts);
    if (corrected) r.text += "\n\n（注：以上部分数据已按引擎真值修正。）";
    if (unverifiable) r.unsure = true;
}

}  // namespace

AiAdvisor::AiAdvisor(EngineInterface* engine, ConversationService* conversation, int level)
    : engine_(engine),
      conversation_(conversation),
      level_(level),
      last_joseki_(std::make_shared<JosekiDialogState>()) {}

AiAdvisor::~AiAdvisor() = default;

void AiAdvisor::resetJosekiDialogState() {
    *last_joseki_ = JosekiDialogState{};
}

void AiAdvisor::ask(const Game& game, const std::string& question, ReplyCb cb) {
    if (!conversation_) return;
    const QuestionClass cls = classifyQuestion(question);
    // D5.2：命中投机预生成的「为什么下这里」缓存时直接返回，近乎零延迟。
    // A2 守卫：会话已有历史时旁路缓存——「那白棋不应呢」这类追问必须带上下文重新生成。
    if (cls == QuestionClass::WhyThisMove && !conversation_->hasHistory()) {
        const auto it = explain_cache_.find(positionHash(game.board()));
        if (it != explain_cache_.end()) {
            cb(it->second);
            return;
        }
    }
    // 讲解增强（N1）：落点质量走 what-if，死活走 ownership 判死，二者异步查引擎。
    if (cls == QuestionClass::MoveQuality) {
        handleMoveQuality(game, question, cb);
        return;
    }
    if (cls == QuestionClass::LifeDeath) {
        handleLifeDeath(game, question, cb);
        return;
    }
    // 「我该下哪」为用户执子方建议；「谁领先」用当前局面分析。二者都查引擎拿真实分析。
    const bool needEngine = (cls == QuestionClass::WhereToPlay || cls == QuestionClass::WhoIsWinning);

    if (needEngine && !game.is_over() && engine_) {
        const Color to_move = (user_color_ != Color::Empty && cls == QuestionClass::WhereToPlay)
                                  ? user_color_
                                  : game.current();
        const int gen = ++generation_;
        const Game snapshot = game;  // stable copy for the async callback

        // 「谁领先」用满血形势判断引擎（analyze），保证与「形势」显示口径一致、
        // 数字准确；「我该下哪」才用落子引擎（requestMove）给一手建议。
        if (cls == QuestionClass::WhoIsWinning) {
            EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
            // D5.4：提问属 Interactive 优先级，优先于形势刷新与恶手量化。
            analyzePrioritized(eng, snapshot.board(), to_move,
                               strengthForLevel(analysis_level_), EnginePriority::Interactive,
                               [this, gen, question, snapshot, cb](const Analysis& a) {
                                   if (gen != generation_) return;  // stale / cancelled
                                   GroundedFacts f = currentFacts(snapshot);
                                   f.analysis = a;
                                   deliverAnswer(snapshot, question, std::move(f), cb);
                               });
            return;
        }

        engine_->requestMove(game.board(), to_move, strengthForLevel(level_, kyu_hint_),
                             [this, gen, question, snapshot, cb](EngineResult r) {
                                 if (gen != generation_) return;  // stale / cancelled
                                 GroundedFacts f = currentFacts(snapshot);
                                 if (r.analysis.valid) {
                                     f.analysis = r.analysis;  // 用引擎完整分析
                                 } else if (!r.move.is_pass) {
                                     MoveCandidate c;
                                     c.p = r.move.p;
                                     f.analysis.valid = true;
                                     f.analysis.best = c;
                                     f.analysis.candidates.push_back(c);
                                 }
                                 deliverAnswer(snapshot, question, std::move(f), cb);
                             });
        return;
    }
    // 其余问题（改写说法 / 多跳 / 未命中专用路径）：直接同步应答。工具快照来自
    // prewarmAnalysis 在局面变化时（Background 优先级、与形势开关解耦）维护的
    // last_analysis_，不按题阻塞预取——否则就是 MCP 模式（C++ 替模型预计算）回潮。
    deliverAnswer(game, question, currentFacts(game), cb);
}

void AiAdvisor::explainLastMove(const Game& game, ReplyCb cb) {
    // D5.2：命中投机预生成缓存时直接返回（已过校验闸门，无需再校验）。
    // A2 守卫：会话已有历史时旁路缓存，避免追问串到旧答。
    if (!conversation_->hasHistory()) {
        const auto it = explain_cache_.find(positionHash(game.board()));
        if (it != explain_cache_.end()) {
            cb(it->second);
            return;
        }
    }
    deliverAnswer(game, "为什么下这里", currentFacts(game), cb);
}

void AiAdvisor::prewarmExplanation(const Game& game) {
    if (!speculative_explain_ || !conversation_) return;
    const std::string key = positionHash(game.board());
    if (explain_cache_.count(key)) return;  // 已有缓存，跳过
    const Game snapshot = game;
    GroundedFacts facts = currentFacts(snapshot);
    // 后台生成讲解并缓存；deliverAnswer 内部的 generation 校验会丢弃过期的结果，
    // 因此新一手的投机结果不会覆盖旧局面的缓存。
    deliverAnswer(snapshot, "为什么下这里", std::move(facts),
                  [this, key](ConversationReply r) {
                      if (explain_cache_.size() >= kMaxExplainCache) {
                          explain_cache_.erase(explain_cache_.begin());
                      }
                      explain_cache_[key] = std::move(r);
                  });
}

void AiAdvisor::analyzePosition(const Game& game, AnalysisCb cb) {
    // 形势判断用专用满血引擎（analysis_engine_），未设置时回退到落子引擎。
    EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) {
        cb(Analysis{});
        return;
    }
    const int gen = ++generation_;
    const Color to_move = game.current();
    // 形势判断用独立强度档位（analysis_level_，默认职业7段），与对局难度解耦，
    // 保证判断正确；落子仍按 level_ 的难度。D5.4：形势刷新为 Background 优先级，
    // 可被更新的刷新合并，不与提问争抢。只传棋盘、不复制整局 Game——收官时 Game
    // 含近 300KB 手数历史，逐手复制会放大内存压力。
    analyzePrioritized(eng, game.board(), to_move, strengthForLevel(analysis_level_),
                       EnginePriority::Background,
                       [this, gen, cb](const Analysis& a) {
                           if (gen != generation_) return;  // stale / cancelled
                           last_analysis_ = a;              // cache as grounding
                           cb(a);
                       });
}

void AiAdvisor::prewarmAnalysis(const Game& game) {
    // D1.8：局面级快照预热——用 Background 优先级（与形势开关解耦、可被更新的预热合并）
    // 刷新 last_analysis_，供模型工具（analyze_position / ownership_map / dead_groups /
    // life_death_solve）按需取真值。不走 generation_：预热不得取消在途提问。
    EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng || game.is_over()) return;
    const int gen = ++prewarm_generation_;
    analyzePrioritized(eng, game.board(), game.current(), strengthForLevel(analysis_level_),
                       EnginePriority::Background,
                       [this, gen](const Analysis& a) {
                           if (gen != prewarm_generation_) return;  // 有更新的预热，丢弃旧结果
                           last_analysis_ = a;  // cache as grounding（工具快照真相来源）
                       });
}

void AiAdvisor::cancel() {
    ++generation_;
    if (conversation_) conversation_->cancel();
    if (move_evaluator_) move_evaluator_->cancel();
    if (life_death_verifier_) life_death_verifier_->cancel();
    // D5.4：取消分析池（丢弃排队的形势刷新 / 恶手量化 + 取消在途分析）。守卫：
    // 当分析引擎回退到落子引擎（无 KataGo）时不要取消落子引擎——那是 controller 的职责。
    if (analysis_engine_ && analysis_engine_ != engine_) analysis_engine_->cancel();
}

MoveEvaluator* AiAdvisor::evaluator() {
    if (!move_evaluator_) {
        move_evaluator_ =
            std::make_unique<MoveEvaluator>(analysis_engine_ ? analysis_engine_ : engine_);
        // 与形势判断同源强度：避免恶手量化用弱引擎、与形势领先口径不一致。
        move_evaluator_->setStrength(strengthForLevel(analysis_level_));
    }
    return move_evaluator_.get();
}

LifeDeathVerifier* AiAdvisor::verifier() {
    if (!life_death_verifier_) {
        life_death_verifier_ = std::make_unique<LifeDeathVerifier>(
            analysis_engine_ ? analysis_engine_ : engine_, strengthForLevel(analysis_level_));
    }
    return life_death_verifier_.get();
}

void AiAdvisor::evaluateMove(const Game& game, const Point& p, MoveEvaluator::Callback cb) {
    const int gen = ++generation_;
    evaluator()->evaluate(game, p, [this, gen, cb](const MoveEvaluation& e) {
        if (gen != generation_) return;
        cb(e);
    });
}

void AiAdvisor::evaluateLastMove(const Game& game, MoveEvaluator::Callback cb) {
    const int gen = ++generation_;
    evaluator()->evaluateLastMove(game, [this, gen, cb](const MoveEvaluation& e) {
        if (gen != generation_) return;
        cb(e);
    });
}

void AiAdvisor::handleMoveQuality(const Game& game, const std::string& question, ReplyCb cb) {
    const Game snapshot = game;
    const int gen = ++generation_;
    const auto deliver = [this, gen, snapshot, question, cb](const MoveEvaluation& e) {
        if (gen != generation_) return;
        GroundedFacts f = currentFacts(snapshot);
        f.move_eval = e;
        deliverAnswer(snapshot, question, std::move(f), cb);
    };
    const PointMatch m = findPointInQuestion(question, game.size());
    if (m.found) {
        // 提问中明确给了落点：作为「假想手」在副本上试下再评估。
        evaluator()->evaluate(snapshot, m.p, deliver);
        return;
    }
    if (game.has_last_move()) {
        // 无落点：评估「最近一手」（已在棋盘上，撤掉再比较前后）。
        evaluator()->evaluateLastMove(snapshot, deliver);
        return;
    }
    deliverAnswer(snapshot, question, currentFacts(snapshot), cb);
}

void AiAdvisor::handleLifeDeath(const Game& game, const std::string& question, ReplyCb cb) {
    EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
    if (!eng) {
        deliverAnswer(game, question, currentFacts(game), cb);
        return;
    }
    const Game snapshot = game;
    const Color to_move = game.current();
    const int gen = ++generation_;
    // D5.4：死活提问属 Interactive 优先级，优先于形势刷新与恶手量化。
    analyzePrioritized(eng, game.board(), to_move, strengthForLevel(analysis_level_),
                       EnginePriority::Interactive,
                       [this, gen, snapshot, question, cb](const Analysis& a) {
                           if (gen != generation_) return;
                           GroundedFacts f = currentFacts(snapshot);
                           f.analysis = a;
                           groundFromAnalysis(snapshot, a, &f);  // D1.8：死棋/死活与 analysis 同源
                           // L4 引擎验证：常型已给出要点时，用 probe 双向验证并覆盖结论。
                           if (f.life_death && !f.life_death->group.empty() &&
                               (!f.life_death->kill_points.empty() ||
                                !f.life_death->live_points.empty())) {
                               const LifeDeathResult pattern = *f.life_death;
                               const Color defender =
                                   snapshot.board().at(pattern.group[0].x, pattern.group[0].y);
                               verifier()->verify(
                                   snapshot.board(), defender, pattern.group,
                                   pattern.kill_points, pattern.live_points,
                                   [this, gen, snapshot, question, cb, f = std::move(f),
                                    pattern](LifeDeathResult v) mutable {
                                       if (gen != generation_) return;
                                       v.shape = pattern.shape;
                                       v.eye_space = pattern.eye_space;
                                       f.life_death = v;
                                       deliverAnswer(snapshot, question, std::move(f), cb);
                                   });
                               return;
                           }
                           deliverAnswer(snapshot, question, std::move(f), cb);
                       });
}

// D1.8：由同一份归属分析统一派生死棋清单与死活结论，保证工具、硬事实、校验闸门口径一致
// （避免「analysis 新鲜、dead_groups 陈旧」的混源）。
void AiAdvisor::groundFromAnalysis(const Game& game, const Analysis& a, GroundedFacts* f) {
    if (!a.ownership.empty()) {
        f->dead_groups = deadGroups(game.board(), a.ownership);
        f->life_death = analyzeLifeDeath(game.board(), a.ownership);
        // L3：对最大死棋块跑常型识别，填 shape/status/要点（纯逻辑，无引擎）。
        if (!f->life_death->group.empty()) {
            const ShapeAnalysis sa = analyzeShape(game.board(), f->life_death->group);
            f->life_death->shape = sa.shape;
            f->life_death->status = sa.status;
            f->life_death->eye_space = sa.eye_space;
            f->life_death->kill_points = sa.kill_points;
            f->life_death->live_points = sa.live_points;
            if (!sa.shape.empty()) f->life_death->verification = LDVerify::PatternOnly;
        }
    }
}

GroundedFacts AiAdvisor::currentFacts(const Game& game) const {
    GroundedFacts f;
    f.analysis = last_analysis_;
    if (!last_analysis_.ownership.empty()) {
        f.dead_groups = deadGroups(game.board(), last_analysis_.ownership);
    }
    if (game.move_number() > 0 && game.has_last_move()) {
        const Color mover = opponent(game.current());
        Game before = game;
        before.undo();
        f.facts = extractBoardFacts(before.board(), game.board(),
                                    Move{game.last_move(), false},
                                    game.captures(mover) - before.captures(mover),
                                    mover);
    }
    return f;
}

void AiAdvisor::deliverAnswer(const Game& game, const std::string& question,
                              GroundedFacts facts, ReplyCb cb) {
    ConversationRequest req;
    req.question = question;
    req.facts = std::move(facts);
    req.board_context = boardToAscii(game.board());
    req.score_black = game.score_black();
    req.move_number = game.move_number();
    req.size = game.size();
    req.to_move = game.current();
    req.last_mover = opponent(game.current());
    req.user_color = user_color_;
    req.last_move = game.last_move();
    req.has_last_move = game.has_last_move();
    req.hint = isHintRequest(question);
    req.in_demo = demo_active_ctx_;
    req.demo_moves = demo_moves_;
    req.profile = PlayerProfile{level_, true};
    // D3.6：注入跨局教练记忆（如「最近在打劫上吃亏」），供 LLM 系统提示词引用。
    req.coach_memory = coach_memory_fn_ ? coach_memory_fn_() : std::string{};
    // B2：局面阶段 + 最近手顺进 prompt，让讲解获得「前文」。
    req.phase =
        phaseLabel(classifyPhase(game.move_number(), game.size(), req.facts.analysis.ownership));
    req.move_sequence = buildMoveSequence(game.moves(), game.size(), 10);
    // D-a：讲解侧重指令（弱项 → 教学策略），注入 system prompt。
    req.weakness_directive = weakness_fn_ ? weakness_fn_() : std::string{};
    // 程序确定性输出：本局已提示的恶手预警，让 LLM 讲解与 UI 已提示内容一致。
    req.blunder_notes = blunder_notes_fn_ ? blunder_notes_fn_() : std::vector<BlunderNote>{};
    // A.5：注入定式目录索引（几十行），让 LLM 知道词典里有什么、词典外别硬编。
    req.joseki_directory = joseki_ ? joseki_->directoryIndex() : std::string{};
    // G3-2：注入上一次定式判定（程序解析的指代事实），供「演示这个定式」消歧。
    req.last_joseki_context = renderJosekiDialogState(*last_joseki_);
    // G1：注入推荐点的着法几何术语（程序判定），LLM 只转述、勿自行判断。
    req.recommended_move_shape = renderRecommendedShape(game.board(), req.facts, req.to_move);
    // D1：把当前局面的确定性能力注册为工具，交给模型自主调用（而非关键词分发）。
    {
        AgentToolContext tool_ctx;
        tool_ctx.game = game;
        tool_ctx.ownership = req.facts.analysis.ownership;
        tool_ctx.analysis = req.facts.analysis;
        tool_ctx.joseki = joseki_;
        tool_ctx.joseki_verdicts = joseki_verdicts_;
        tool_ctx.last_joseki = last_joseki_;  // G3-2：本轮 joseki_lookup 判定后回写，供下一轮消歧
        conversation_->setTools(buildAgentTools(tool_ctx));
    }
    // D2：校验闸门需要真值与棋盘快照（facts 已 move 进 req，先复制一份）。
    const GroundedFacts verify_facts = req.facts;
    const Board verify_board = game.board();
    const double verify_score = req.score_black;
    const Color verify_to_move = req.to_move;
    const int verify_size = req.size;
    const int gen = ++generation_;
    const std::int64_t start_ms = nowEpochMs();
    conversation_->answer(
        req, [this, gen, cb, verify_facts, verify_board, verify_score, verify_to_move,
              verify_size, start_ms, question](ConversationReply r) mutable {
            if (gen != generation_) return;  // stale / cancelled
            const std::string raw_text = r.text;  // 校验前原样保留（供坏样本回看）
            verifyAndAnnotate(r, verify_board, verify_facts, verify_score, verify_to_move,
                              verify_size);
            // D2.4：挂上 grounding 分析的最佳变化，供 UI「跳 PV」。
            if (verify_facts.analysis.valid && !verify_facts.analysis.best.pv.empty())
                r.pv = verify_facts.analysis.best.pv;
            // D5.6：落一条可观测 trace（position hash / route / latency / verify / model）。
            if (trace_sink_) {
                AgentTraceRecord rec;
                rec.ts_ms = nowEpochMs();
                rec.position_hash = positionHash(verify_board);
                rec.route = trace_route_;
                rec.model = trace_model_;
                rec.latency_ms = static_cast<int>(nowEpochMs() - start_ms);
                rec.verify = verifySummary(r.verdicts);
                trace_sink_(rec);
            }
            // A4：反馈上下文透出（👎 沉淀坏样本用；模板/离线回复可为空）。
            const std::string truth = truthSummary(verify_facts, verify_score, verify_to_move);
            r.feedback_position_hash = positionHash(verify_board);
            r.feedback_truth_summary = truth;
            r.feedback_model = trace_model_;
            r.feedback_question = question;
            // D2.5：校验拦截到「修正/不可验证」时，逐条沉淀坏样本（反哺 D4 评估闭环）。
            if (bad_sample_sink_) {
                for (const ClaimVerdict& v : r.verdicts) {
                    if (v.disposition != ClaimDisposition::Corrected &&
                        v.disposition != ClaimDisposition::Unverifiable)
                        continue;
                    BadSample s;
                    s.ts_ms = nowEpochMs();
                    s.position_hash = positionHash(verify_board);
                    s.question = question;
                    s.reply_text = raw_text;
                    s.disposition = (v.disposition == ClaimDisposition::Corrected)
                                        ? "corrected"
                                        : "unverifiable";
                    s.truth_summary = truth;
                    s.model = trace_model_;
                    bad_sample_sink_(s);
                }
            }
            cb(std::move(r));
        });
}

void AiAdvisor::askStream(const Game& game, const std::string& question,
                          ConversationService::DeltaCallback cb) {
    if (!conversation_) return;
    using Ev = ConversationService::StreamEvent;
    const QuestionClass cls = classifyQuestion(question);
    // D5.2 投机缓存命中（WhyThisMove + 无历史）：单 delta 直达。
    if (cls == QuestionClass::WhyThisMove && !conversation_->hasHistory()) {
        const auto it = explain_cache_.find(positionHash(game.board()));
        if (it != explain_cache_.end()) {
            cb(Ev{Ev::Kind::Delta, it->second.text});
            cb(Ev{Ev::Kind::Done, {}, it->second});
            return;
        }
    }
    // 落点质量 / 死活仍走异步 what-if / 死活，结果以单 delta 送达（非逐字）。
    if (cls == QuestionClass::MoveQuality) {
        handleMoveQuality(game, question, [cb](const ConversationReply& r) {
            cb(Ev{Ev::Kind::Delta, r.text});
            cb(Ev{Ev::Kind::Done, {}, r});
        });
        return;
    }
    if (cls == QuestionClass::LifeDeath) {
        handleLifeDeath(game, question, [cb](const ConversationReply& r) {
            cb(Ev{Ev::Kind::Delta, r.text});
            cb(Ev{Ev::Kind::Done, {}, r});
        });
        return;
    }
    const bool needEngine = (cls == QuestionClass::WhereToPlay || cls == QuestionClass::WhoIsWinning);
    if (needEngine && !game.is_over() && engine_) {
        const Color to_move = (user_color_ != Color::Empty && cls == QuestionClass::WhereToPlay)
                                  ? user_color_
                                  : game.current();
        const int gen = ++generation_;
        const Game snapshot = game;

        if (cls == QuestionClass::WhoIsWinning) {
            EngineInterface* eng = analysis_engine_ ? analysis_engine_ : engine_;
            analyzePrioritized(eng, snapshot.board(), to_move,
                               strengthForLevel(analysis_level_), EnginePriority::Interactive,
                               [this, gen, question, snapshot, cb](const Analysis& a) {
                                   if (gen != generation_) return;
                                   GroundedFacts f = currentFacts(snapshot);
                                   f.analysis = a;
                                   deliverAnswerStream(snapshot, question, std::move(f), cb);
                               });
            return;
        }

        engine_->requestMove(game.board(), to_move, strengthForLevel(level_, kyu_hint_),
                             [this, gen, question, snapshot, cb](EngineResult r) {
                                 if (gen != generation_) return;
                                 GroundedFacts f = currentFacts(snapshot);
                                 if (r.analysis.valid) {
                                     f.analysis = r.analysis;
                                 } else if (!r.move.is_pass) {
                                     MoveCandidate c;
                                     c.p = r.move.p;
                                     f.analysis.valid = true;
                                     f.analysis.best = c;
                                     f.analysis.candidates.push_back(c);
                                 }
                                 deliverAnswerStream(snapshot, question, std::move(f), cb);
                             });
        return;
    }
    deliverAnswerStream(game, question, currentFacts(game), cb);
}

void AiAdvisor::deliverAnswerStream(const Game& game, const std::string& question,
                                    GroundedFacts facts, ConversationService::DeltaCallback cb) {
    using Ev = ConversationService::StreamEvent;
    ConversationRequest req;
    req.question = question;
    req.facts = std::move(facts);
    req.board_context = boardToAscii(game.board());
    req.score_black = game.score_black();
    req.move_number = game.move_number();
    req.size = game.size();
    req.to_move = game.current();
    req.last_mover = opponent(game.current());
    req.user_color = user_color_;
    req.last_move = game.last_move();
    req.has_last_move = game.has_last_move();
    req.hint = isHintRequest(question);
    req.in_demo = demo_active_ctx_;
    req.demo_moves = demo_moves_;
    req.profile = PlayerProfile{level_, true};
    req.coach_memory = coach_memory_fn_ ? coach_memory_fn_() : std::string{};
    req.phase =
        phaseLabel(classifyPhase(game.move_number(), game.size(), req.facts.analysis.ownership));
    req.move_sequence = buildMoveSequence(game.moves(), game.size(), 10);
    // D-a：讲解侧重指令（弱项 → 教学策略），注入 system prompt。
    req.weakness_directive = weakness_fn_ ? weakness_fn_() : std::string{};
    // 程序确定性输出：本局已提示的恶手预警，让 LLM 讲解与 UI 已提示内容一致。
    req.blunder_notes = blunder_notes_fn_ ? blunder_notes_fn_() : std::vector<BlunderNote>{};
    // A.5：注入定式目录索引（几十行），让 LLM 知道词典里有什么、词典外别硬编。
    req.joseki_directory = joseki_ ? joseki_->directoryIndex() : std::string{};
    // G3-2：注入上一次定式判定（程序解析的指代事实），供「演示这个定式」消歧。
    req.last_joseki_context = renderJosekiDialogState(*last_joseki_);
    // G1：注入推荐点的着法几何术语（程序判定），LLM 只转述、勿自行判断。
    req.recommended_move_shape = renderRecommendedShape(game.board(), req.facts, req.to_move);
    {
        AgentToolContext tool_ctx;
        tool_ctx.game = game;
        tool_ctx.ownership = req.facts.analysis.ownership;
        tool_ctx.analysis = req.facts.analysis;
        tool_ctx.joseki = joseki_;
        tool_ctx.joseki_verdicts = joseki_verdicts_;
        tool_ctx.last_joseki = last_joseki_;  // G3-2：本轮 joseki_lookup 判定后回写，供下一轮消歧
        conversation_->setTools(buildAgentTools(tool_ctx));
    }
    const GroundedFacts verify_facts = req.facts;
    const Board verify_board = game.board();
    const double verify_score = req.score_black;
    const Color verify_to_move = req.to_move;
    const int verify_size = req.size;
    const int gen = ++generation_;
    const std::int64_t start_ms = nowEpochMs();

    auto text = std::make_shared<std::string>();
    conversation_->answerStream(req, [this, gen, cb, verify_facts, verify_board, verify_score,
                                      verify_to_move, verify_size, start_ms, question,
                                      text](Ev e) mutable {
        if (gen != generation_) return;
        switch (e.kind) {
            case Ev::Kind::Delta:
                *text += e.text;
                cb(std::move(e));
                break;
            case Ev::Kind::Abort:
                text->clear();
                cb(std::move(e));
                break;
            case Ev::Kind::Done: {
                ConversationReply r;
                r.text = *text;
                const std::string raw_text = r.text;
                verifyAndAnnotate(r, verify_board, verify_facts, verify_score, verify_to_move,
                                  verify_size);
                // D2.4：挂 PV 供证据点击跳变；A4：反馈上下文（与 deliverAnswer 同源）。
                if (verify_facts.analysis.valid && !verify_facts.analysis.best.pv.empty())
                    r.pv = verify_facts.analysis.best.pv;
                r.feedback_position_hash = positionHash(verify_board);
                r.feedback_truth_summary =
                    truthSummary(verify_facts, verify_score, verify_to_move);
                r.feedback_model = trace_model_;
                r.feedback_question = question;
                // D5.6 trace / D2.5 坏样本（与 deliverAnswer 同源，保证流式不劣化）。
                if (trace_sink_) {
                    AgentTraceRecord rec;
                    rec.ts_ms = nowEpochMs();
                    rec.position_hash = positionHash(verify_board);
                    rec.route = trace_route_;
                    rec.model = trace_model_;
                    rec.latency_ms = static_cast<int>(nowEpochMs() - start_ms);
                    rec.verify = verifySummary(r.verdicts);
                    trace_sink_(rec);
                }
                if (bad_sample_sink_) {
                    const std::string truth =
                        truthSummary(verify_facts, verify_score, verify_to_move);
                    for (const ClaimVerdict& v : r.verdicts) {
                        if (v.disposition != ClaimDisposition::Corrected &&
                            v.disposition != ClaimDisposition::Unverifiable)
                            continue;
                        BadSample s;
                        s.ts_ms = nowEpochMs();
                        s.position_hash = positionHash(verify_board);
                        s.question = question;
                        s.reply_text = raw_text;
                        s.disposition = (v.disposition == ClaimDisposition::Corrected)
                                            ? "corrected"
                                            : "unverifiable";
                        s.truth_summary = truth;
                        s.model = trace_model_;
                        bad_sample_sink_(s);
                    }
                }
                // 最终回合：把完整回复（含 verdicts / 反馈上下文）交给 UI 渲染。
                cb(Ev{Ev::Kind::Done, {}, std::move(r)});
                break;
            }
        }
    });
}

}  // namespace vgo
