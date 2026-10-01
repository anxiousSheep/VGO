#pragma once

#include "conversation.h"

namespace vgo {

// Offline conversation that answers a fixed set of questions from board facts
// and the score. No LLM. Used as the fallback when no language service is
// configured, and as a test double.
class TemplateConversation : public ConversationService {
public:
    void answer(const ConversationRequest& req, Callback cb) override;
};

}  // namespace vgo
