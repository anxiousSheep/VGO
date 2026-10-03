#pragma once

#include <QString>

#include "ai/learner/learner_model.h"

namespace vgo {

// D3.3: persists the cross-game LearnerModel to a per-user jsonl file, and
// provides the privacy trio (enable / export / clear). Owns file I/O only —
// the profile logic stays in ai/learner/. The privacy toggle travels with the
// data in the same file, so a fresh process reads the same policy.
class LearnerStore {
public:
    // The default path under ModelManager::dataDir() (per-user AppData).
    static QString defaultPath();

    explicit LearnerStore(QString path);

    void load();  // no-op if the file is missing or malformed
    void save();  // writes {enabled, profile} as one JSON line

    LearnerModel& model() { return model_; }
    const LearnerModel& model() const { return model_; }

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // Plaintext profile JSON, for the user to export.
    QString exportJson() const;

    // Reset the profile and remove the file.
    void clear();

    QString path() const { return path_; }

private:
    QString path_;
    LearnerModel model_;
    bool enabled_ = true;
};

}  // namespace vgo
