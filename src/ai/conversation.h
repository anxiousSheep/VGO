#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "explain.h"
#include "go/go_types.h"

namespace vgo {

enum class QuestionClass { WhyThisMove, WhereToPlay, WhoIsWinning, Other };

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
    PlayerProfile profile;
};

struct ConversationReply {
    std::string text;
    bool unsure = false;
    std::vector<Point> highlight;  // points to highlight on the board
};

class ConversationService {
public:
    virtual ~ConversationService() = default;
    using Callback = std::function<void(ConversationReply)>;
    virtual void answer(const ConversationRequest& req, Callback cb) = 0;
    virtual void cancel() {}
};

}  // namespace vgo
