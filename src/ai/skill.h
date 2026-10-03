#pragma once

#include <string>
#include <vector>

#include "conversation.h"

namespace vgo {

// A domain-knowledge pack injected into the LLM system prompt (Skills 2.0, D4).
// Frontmatter fields (id / name / description / applies_to / version /
// last_score) are cheap to keep resident; `prompt` (the body) is loaded on
// demand. `version` + `last_score` give the evaluation loop a completion
// criterion (L10) and enable rollback. Pure data, no Qt.
struct Skill {
    std::string id;                          // stable key, e.g. "life-death"
    std::string name;                        // display name
    std::string description;                 // one-line summary (frontmatter)
    std::vector<QuestionClass> applies_to;   // empty => applies to all questions
    std::string prompt;                      // injected knowledge body
    int version = 1;
    double last_score = -1.0;                // -1 = never evaluated
};

// A retrieval hit with a score and a human-readable reason.
struct SkillMatch {
    const Skill* skill = nullptr;
    double score = 0.0;   // cosine similarity [0,1]
    std::string reason;   // why this skill matched (for the settings page)
};

class SkillRegistry {
public:
    // Built-in skills (life-death / move-quality / counting / ko / terms).
    static SkillRegistry builtin();

    // Skills that apply to question class `q`.
    std::vector<const Skill*> forQuestion(QuestionClass q) const;

    // Semantic retrieval top-k (embedding is the intended front-end; TF-IDF is
    // the always-available fallback). Matches against id + name + description +
    // body, and returns a hit reason per result.
    std::vector<SkillMatch> retrieve(const std::string& query, int k = 3) const;

    const Skill* find(const std::string& id) const;
    bool upsert(const Skill& s);   // add or replace by id; false if id empty
    void clear() { skills_.clear(); }
    std::size_t size() const { return skills_.size(); }
    const std::vector<Skill>& all() const { return skills_; }

    // Serialize a single skill to / from the YAML-frontmatter + body text form
    // (id/name/description/applies/version/last_score headers + "---" + body).
    // fromText accepts the older id/name/applies-only header for compatibility.
    static std::string toText(const Skill& s);
    static bool fromText(const std::string& text, Skill* out);

private:
    std::vector<Skill> skills_;
};

}  // namespace vgo
