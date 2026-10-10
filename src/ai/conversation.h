#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "agent/tool_registry.h"
#include "explain.h"
#include "go/go_types.h"
#include "record.h"
#include "verify/claim.h"

namespace vgo {

enum class QuestionClass {
    WhyThisMove,  // 为什么下这里（无落点，讲最近一手）
    WhereToPlay,  // 我该下哪 / 建议 / 指点
    WhoIsWinning, // 谁领先 / 形势 / 目数
    MoveQuality,  // 为什么这步不好 / 这步行吗 / X 点如何（what-if 量化）
    LifeDeath,    // 为什么做不活 / 这块棋死没死 / 能活吗
    Other,
};

inline std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

inline QuestionClass classifyQuestion(const std::string& text) {
    const std::string q = lower_ascii(text);
    // 死活类（最优先）：做不活 / 死没死 / 能活吗 / 这块棋
    if (contains(q, "做不活") || contains(q, "死活") || contains(q, "死没死") ||
        contains(q, "能活吗") || contains(q, "救活") || contains(q, "这块棋") ||
        contains(q, "这块") || contains(q, "活棋") || contains(q, "被吃") ||
        contains(q, "死棋") || contains(q, "眼睛") || contains(q, "眼") ||
        contains(q, "life") || contains(q, "dead") || contains(q, "alive"))
        return QuestionClass::LifeDeath;
    // 落点质量（先于 WhyThisMove，避免「为什么下这里不好」被误判）：
    if (contains(q, "不好") || contains(q, "行吗") || contains(q, "行不行") ||
        contains(q, "好不好") || contains(q, "怎么样") || contains(q, "如何") ||
        contains(q, "值得") || contains(q, "亏") || contains(q, "赚") ||
        contains(q, "坏棋") || contains(q, "好棋") || contains(q, "恶手") ||
        contains(q, "妙手") || contains(q, "坏手") || contains(q, "是不是") ||
        contains(q, "对不对") || contains(q, "对错") || contains(q, "quality"))
        return QuestionClass::MoveQuality;
    if (contains(q, "领先") || contains(q, "形势") || contains(q, "谁赢") ||
        contains(q, "优势") || contains(q, "领先多少") || contains(q, "score") ||
        contains(q, "目数") || contains(q, "胜负"))
        return QuestionClass::WhoIsWinning;
    if (contains(q, "下哪") || contains(q, "哪里") || contains(q, "怎么下") ||
        contains(q, "建议") || contains(q, "指点") || contains(q, "走哪") ||
        contains(q, "下一手") || contains(q, "where") || contains(q, "play"))
        return QuestionClass::WhereToPlay;
    if (contains(q, "为什么") || contains(q, "为何") || contains(q, "为啥") ||
        contains(q, "why") || contains(q, "这手") || contains(q, "意思") ||
        contains(q, "想干嘛") || contains(q, "什么意思"))
        return QuestionClass::WhyThisMove;
    return QuestionClass::Other;
}

// QuestionClass <-> stable string (used by Skill "applies" lists).
inline const char* questionClassToString(QuestionClass q) {
    switch (q) {
        case QuestionClass::WhyThisMove:  return "why-this-move";
        case QuestionClass::WhereToPlay:  return "where-to-play";
        case QuestionClass::WhoIsWinning: return "who-is-winning";
        case QuestionClass::MoveQuality:  return "move-quality";
        case QuestionClass::LifeDeath:    return "life-death";
        default:                          return "other";
    }
}

inline bool questionClassFromString(const std::string& s, QuestionClass* out) {
    if (s == "why-this-move") *out = QuestionClass::WhyThisMove;
    else if (s == "where-to-play") *out = QuestionClass::WhereToPlay;
    else if (s == "who-is-winning") *out = QuestionClass::WhoIsWinning;
    else if (s == "move-quality") *out = QuestionClass::MoveQuality;
    else if (s == "life-death") *out = QuestionClass::LifeDeath;
    else if (s == "other") *out = QuestionClass::Other;
    else return false;
    return true;
}

// 半提示模式（M3b.1）：求指点/给方向，而不是直接报坐标。
inline bool isHintRequest(const std::string& text) {
    const std::string q = lower_ascii(text);
    return contains(q, "指点") || contains(q, "提示") || contains(q, "方向");
}

struct PlayerProfile {
    int level = 5;
    bool teaching = true;
};

struct ConversationRequest {
    std::string question;
    GroundedFacts facts;
    std::string board_context;  // ASCII board, for language models
    double score_black = 0.0;   // Game::score_black() (positive => black ahead)
    int move_number = 0;
    int size = 0;
    Color to_move = Color::Black;
    Color last_mover = Color::Empty;
    Color user_color = Color::Empty;  // 用户执子颜色；Empty=无（摆棋模式）
    Point last_move{-1, -1};    // 最近一手的坐标（供 LLM 定位）
    bool has_last_move = false;
    bool hint = false;          // 半提示模式：只给方向，不给具体坐标（M3b.1）
    // 试下模式上下文：让 LLM 知道当前已有一个进行中的棋盘演示及其走法，
    // 从而正确判断「继续（reset=false）」还是「重新（reset=true）」。
    bool in_demo = false;
    std::string demo_moves;     // 如 "白 H6、黑 G5、白 F6"
    PlayerProfile profile;
    std::string coach_memory;   // D3.6 跨局教练记忆（如「最近在打劫上吃亏」），可为空
    std::string phase;          // B2 局面阶段："布局"/"中盘"/"官子"
    std::string move_sequence;  // B2 最近手顺："黑 H6、白 G5、…"，空 = 无历史
    std::string weakness_directive;  // D-a 教学侧重指令（注入 system prompt），可为空
    // 定式目录索引（id + name + aliases，几十行），注入 system prompt 供 LLM 知道
    // 词典里有什么、避免编造词典外的定式（A.5）。
    std::string joseki_directory;
    // G3-2：上一次定式判定（程序解析的指代事实，如「点三三（可用，apply 模式）」），
    // 注入 prompt 让「演示这个定式」的「这个」由程序消歧，而非让 LLM 凭记忆猜名。空 = 无。
    std::string last_joseki_context;
    // 程序已给出的确定性输出（如局中恶手预警），让 LLM 的讲解与 UI 已提示内容一致。
    std::vector<BlunderNote> blunder_notes;
};

struct ConversationReply {
    std::string text;
    bool unsure = false;
    std::vector<Point> highlight;  // points to highlight on the board
    // D2.4 证据溯源：校验后的断言（含 source / disposition），供 UI hover 看来源。
    std::vector<ClaimVerdict> verdicts;
    // D2.4 跳 PV：grounding 分析的最佳变化（逐点），点击证据时在棋盘上高亮。
    std::vector<Point> pv;
    // A4：反馈上下文（👎 沉淀坏样本用；模板/离线回复可为空）。
    std::string feedback_position_hash;
    std::string feedback_truth_summary;
    std::string feedback_model;
    std::string feedback_question;
};

class ConversationService {
public:
    virtual ~ConversationService() = default;
    using Callback = std::function<void(ConversationReply)>;
    // Streaming update (B3): `Delta` carries incremental text; `Done` signals the
    // final round completed; `Abort` tells the caller to discard everything streamed
    // for the current round (a tool round emitted content before revealing
    // tool_calls, or a router fallback re-asks the strong model). Exactly one kind
    // is meaningful per event.
    struct StreamEvent {
        enum class Kind { Delta, Done, Abort };
        Kind kind = Kind::Delta;
        std::string text;  // only for Kind::Delta
        // For Done: the fully-assembled, verified reply (verdicts / pv / feedback
        // context), so the UI can render evidence rows + the A4 feedback bar.
        ConversationReply reply;
    };
    using DeltaCallback = std::function<void(const StreamEvent&)>;
    virtual void answer(const ConversationRequest& req, Callback cb) = 0;
    virtual bool supportsStreaming() const { return false; }
    // Default: synthesize a single delta from a non-streaming answer, so the
    // template conversation works for the demo flow too (N3 fallback).
    virtual void answerStream(const ConversationRequest& req, DeltaCallback cb) {
        answer(req, [cb](const ConversationReply& r) {
            cb(StreamEvent{StreamEvent::Kind::Delta, r.text});
            cb(StreamEvent{StreamEvent::Kind::Done, {}});
        });
    }
    virtual void cancel() {}

    // A2：会话是否已有历史（多轮追问时投机缓存需旁路，避免串答）。模板/离线无记忆。
    virtual bool hasHistory() const { return false; }

    // D1: inject a position-bound tool registry before answering. The model may
    // then call any of these tools (function-calling) instead of a keyword
    // classifier deciding what to compute. Default no-op (template conversation
    // ignores tools).
    virtual void setTools(ToolRegistry tools) {}
};

}  // namespace vgo
