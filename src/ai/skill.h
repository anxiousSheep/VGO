#pragma once

#include <string>
#include <vector>

#include "conversation.h"

namespace vgo {

// A domain-knowledge pack injected into the LLM system prompt to make up for a
// general model's weak Go background. Pure data, no Qt.
struct Skill {
    std::string id;                          // stable key, e.g. "life-death"
    std::string name;                        // display name
    std::vector<QuestionClass> applies_to;   // empty => applies to all questions
    std::string prompt;                      // injected knowledge body
};

class SkillRegistry {
public:
    // Built-in skills (life-death / move-quality / counting / ko / terms).
    static SkillRegistry builtin();

    // Skills that apply to question class `q`.
    std::vector<const Skill*> forQuestion(QuestionClass q) const;

    const Skill* find(const std::string& id) const;
    bool upsert(const Skill& s);   // add or replace by id; false if id empty
    void clear() { skills_.clear(); }
    std::size_t size() const { return skills_.size(); }
    const std::vector<Skill>& all() const { return skills_; }

    // Serialize a single skill to / from a human-editable text form
    // (id/name/applies headers + "---" + prompt body). Pure, no Qt, no I/O.
    static std::string toText(const Skill& s);
    static bool fromText(const std::string& text, Skill* out);

private:
    std::vector<Skill> skills_;
};

}  // namespace vgo
