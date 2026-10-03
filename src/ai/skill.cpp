#include "skill.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace vgo {

namespace {

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                     s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

bool isAsciiWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Split into tokens: ASCII words (lowercased) + CJK unigrams and bigrams. This
// is a small, dependency-free TF-IDF tokenizer for skill retrieval.
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::vector<std::string> cjk;  // buffered CJK logical chars (3-byte UTF-8)
    std::string ascii;

    auto flushAscii = [&]() {
        if (ascii.empty()) return;
        std::string w;
        for (char c : ascii)
            w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        out.push_back(w);
        ascii.clear();
    };
    auto flushCjk = [&]() {
        for (std::size_t i = 0; i < cjk.size(); ++i) out.push_back(cjk[i]);  // unigram
        for (std::size_t i = 0; i + 1 < cjk.size(); ++i)
            out.push_back(cjk[i] + cjk[i + 1]);  // bigram
        cjk.clear();
    };

    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0xE0 && c < 0xF0 && i + 2 < s.size()) {
            flushAscii();
            cjk.push_back(s.substr(i, 3));
            i += 3;
        } else if (isAsciiWordChar(static_cast<char>(c))) {
            flushCjk();
            ascii += static_cast<char>(c);
            ++i;
        } else {
            flushAscii();
            flushCjk();
            ++i;
        }
    }
    flushAscii();
    flushCjk();
    return out;
}

}  // namespace

SkillRegistry SkillRegistry::builtin() {
    SkillRegistry r;
    {
        Skill s;
        s.id = "life-death";
        s.name = "死活基础";
        s.description = "两眼活、一眼死、真眼假眼、眼位大小";
        s.applies_to = {QuestionClass::LifeDeath};
        s.prompt =
            "死活常识：一块棋要活，必须做出两只真眼（或具备双活/劫活等特殊形态）。"
            "只有一个真眼、或全是假眼的棋，会被对方点眼杀死。判断死活时先数眼位、"
            "再看是否有连接或延展的空间。请用「两眼活、一眼死」的朴素直觉解释，"
            "不要虚构具体的眼位坐标。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "move-quality";
        s.name = "落点对比";
        s.description = "用胜率与领先目数变化评估一手棋的好坏";
        s.applies_to = {QuestionClass::MoveQuality};
        s.prompt =
            "评估一手棋的好坏，看两点：落子前后的胜率变化（winrate，百分点）和领先目数变化"
            "（scoreLead，目）。胜率明显下降、或领先目数明显缩水，就是坏棋（恶手/缓手）；"
            "反之是好棋。回答时优先引用提供的事实数据，用「胜率从 X% 变到 Y%」的口吻，"
            "不要编造数据。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "counting";
        s.name = "数子与形势";
        s.description = "数子法领地计算、贴目与领先目数";
        s.applies_to = {QuestionClass::WhoIsWinning};
        s.prompt =
            "围棋数子：活棋的棋子数加上围住的交叉点（目）为一方领地，死子归对方。"
            "黑方要贴目（通常 7.5 目）。比较双方领地与贴目后得出领先目数与胜率。"
            "回答「谁领先」时直接引用提供的领先目数与胜率，不要自行估算。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "ko";
        s.name = "打劫规则";
        s.description = "打劫、劫材与提劫的规则";
        s.applies_to = {QuestionClass::Other};
        s.prompt =
            "打劫：一方提子后，另一方不能立即提回，必须先在别处找劫材、对方应一手后才能提回。"
            "涉及「打劫」「劫材」「提劫」的问题，请解释这一规则，并提醒用户找劫材。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "terms";
        s.name = "围棋术语";
        s.description = "打吃、提子、连接、延展、扳、断、气、眼等术语";
        s.applies_to = {};  // all
        s.prompt =
            "常用围棋术语：打吃（把对方打成只剩一口气）、提子（吃掉无气的子）、连接（把己方"
            "两块棋连成一块）、延展（加强己方一块棋）、扳（贴住对方棋子拐头）、断（切断对方"
            "两块棋）、气（棋子相邻的空点）、眼（围成的空点）。用这些术语解释时保持口语化。";
        r.upsert(s);
    }
    return r;
}

std::vector<const Skill*> SkillRegistry::forQuestion(QuestionClass q) const {
    std::vector<const Skill*> out;
    for (const Skill& s : skills_) {
        if (s.applies_to.empty() ||
            std::find(s.applies_to.begin(), s.applies_to.end(), q) != s.applies_to.end())
            out.push_back(&s);
    }
    return out;
}

std::vector<SkillMatch> SkillRegistry::retrieve(const std::string& query, int k) const {
    struct Doc {
        const Skill* skill;
        std::unordered_map<std::string, int> tf;
    };
    std::vector<Doc> docs;
    std::unordered_map<std::string, int> df;
    docs.reserve(skills_.size());

    for (const Skill& s : skills_) {
        const std::string text =
            s.id + " " + s.name + " " + s.description + " " + s.prompt;
        const std::vector<std::string> toks = tokenize(text);
        Doc d;
        d.skill = &s;
        std::unordered_map<std::string, int> uniq;
        for (const std::string& t : toks) {
            ++d.tf[t];
            uniq[t] = 1;
        }
        for (const auto& kv : uniq) ++df[kv.first];
        docs.push_back(std::move(d));
    }
    if (docs.empty()) return {};

    const double n = static_cast<double>(docs.size());
    auto idf = [&](const std::string& t) -> double {
        auto it = df.find(t);
        if (it == df.end()) return 0.0;
        return std::log((n + 1.0) / (1.0 + static_cast<double>(it->second))) + 1.0;
    };

    const std::vector<std::string> qtok = tokenize(query);
    std::unordered_map<std::string, int> qtf;
    for (const std::string& t : qtok) ++qtf[t];

    double qnorm = 0.0;
    for (const auto& kv : qtf) {
        const double w = static_cast<double>(kv.second) * idf(kv.first);
        qnorm += w * w;
    }
    qnorm = std::sqrt(qnorm);
    if (qnorm <= 0.0) return {};

    std::vector<SkillMatch> out;
    for (const Doc& d : docs) {
        double dot = 0.0;
        for (const auto& kv : qtf) {
            auto it = d.tf.find(kv.first);
            if (it == d.tf.end()) continue;
            const double w = static_cast<double>(kv.second) * idf(kv.first) *
                             static_cast<double>(it->second) * idf(kv.first);
            dot += w;
        }
        double dnorm = 0.0;
        for (const auto& kv : d.tf) {
            const double w = static_cast<double>(kv.second) * idf(kv.first);
            dnorm += w * w;
        }
        dnorm = std::sqrt(dnorm);
        if (dnorm <= 0.0) continue;
        const double score = dot / (qnorm * dnorm);
        if (score <= 0.0) continue;
        SkillMatch m;
        m.skill = d.skill;
        m.score = score;
        std::ostringstream os;
        os << "命中「" << d.skill->name << "」（相关度 " << score << "）";
        m.reason = os.str();
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(),
              [](const SkillMatch& a, const SkillMatch& b) { return a.score > b.score; });
    if (k >= 0 && static_cast<std::size_t>(k) < out.size())
        out.resize(static_cast<std::size_t>(k));
    return out;
}

std::vector<const Skill*> SkillRegistry::select(QuestionClass cls,
                                                const std::string& question, int topk,
                                                std::size_t budget_chars) const {
    std::vector<const Skill*> chosen;
    std::size_t used = 0;
    const auto fits = [&](const Skill* s) { return used + s->prompt.size() <= budget_chars; };
    // 类别白名单优先（问题类型匹配的知识包不裁），再补足语义检索 top-k（按 id 去重）。
    for (const Skill* s : forQuestion(cls)) {
        if (!fits(s)) break;
        chosen.push_back(s);
        used += s->prompt.size();
    }
    for (const SkillMatch& m : retrieve(question, topk)) {
        bool dup = false;
        for (const Skill* sk : chosen)
            if (sk == m.skill) { dup = true; break; }
        if (dup) continue;
        if (!fits(m.skill)) break;
        chosen.push_back(m.skill);
        used += m.skill->prompt.size();
    }
    return chosen;
}

const Skill* SkillRegistry::find(const std::string& id) const {
    for (const Skill& s : skills_)
        if (s.id == id) return &s;
    return nullptr;
}

bool SkillRegistry::upsert(const Skill& s) {
    if (s.id.empty()) return false;
    for (Skill& e : skills_) {
        if (e.id == s.id) {
            e = s;
            return true;
        }
    }
    skills_.push_back(s);
    return true;
}

bool SkillRegistry::setScore(const std::string& id, double score) {
    for (Skill& s : skills_) {
        if (s.id == id) {
            s.last_score = score;
            return true;
        }
    }
    return false;
}

bool SkillRegistry::addPending(const std::string& id, const std::string& candidate) {
    if (candidate.empty()) return false;
    for (Skill& s : skills_) {
        if (s.id == id) {
            s.pending.push_back(candidate);
            return true;
        }
    }
    return false;
}

bool SkillRegistry::adoptPending(const std::string& id) {
    for (Skill& s : skills_) {
        if (s.id != id) continue;
        if (s.pending.empty()) return false;
        for (const std::string& p : s.pending) {
            if (!s.prompt.empty()) s.prompt += '\n';
            s.prompt += p;
        }
        s.pending.clear();
        return true;
    }
    return false;
}

std::string SkillRegistry::toText(const Skill& s) {
    std::ostringstream os;
    os << "id: " << s.id << '\n';
    os << "name: " << s.name << '\n';
    os << "description: " << s.description << '\n';
    os << "applies:";
    for (QuestionClass q : s.applies_to) os << ' ' << questionClassToString(q);
    os << '\n';
    os << "version: " << s.version << '\n';
    os << "last_score: " << s.last_score << '\n';
    for (const std::string& p : s.pending) os << "pending: " << p << '\n';
    os << "---\n";
    os << s.prompt << '\n';
    return os.str();
}

bool SkillRegistry::fromText(const std::string& text, Skill* out) {
    if (!out) return false;
    Skill s;
    std::istringstream is(text);
    std::string line;
    bool in_body = false;
    std::string body;
    while (std::getline(is, line)) {
        const std::string t = trim(line);
        if (!in_body) {
            if (t == "---") {
                in_body = true;
                continue;
            }
            if (t.rfind("id:", 0) == 0) {
                s.id = trim(t.substr(3));
            } else if (t.rfind("name:", 0) == 0) {
                s.name = trim(t.substr(5));
            } else if (t.rfind("description:", 0) == 0) {
                s.description = trim(t.substr(12));
            } else if (t.rfind("applies:", 0) == 0) {
                for (const std::string& tok : split(t.substr(8), ' ')) {
                    QuestionClass q;
                    if (questionClassFromString(tok, &q)) s.applies_to.push_back(q);
                }
            } else if (t.rfind("version:", 0) == 0) {
                s.version = std::atoi(trim(t.substr(8)).c_str());
            } else if (t.rfind("last_score:", 0) == 0) {
                s.last_score = std::strtod(trim(t.substr(11)).c_str(), nullptr);
            } else if (t.rfind("pending:", 0) == 0) {
                const std::string c = trim(t.substr(8));
                if (!c.empty()) s.pending.push_back(c);
            }
        } else {
            body += line;
            body += '\n';
        }
    }
    if (s.id.empty()) return false;
    s.prompt = body;
    if (!s.prompt.empty() && s.prompt.back() == '\n') s.prompt.pop_back();
    // D4.7：单文件体积上限，防止一个超大 skill 拖垮 system prompt。
    if (s.prompt.size() > kMaxSkillBodyChars) return false;
    *out = std::move(s);
    return true;
}

bool validateSkill(const Skill& s, std::string* err) {
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (s.id.empty()) return fail("id 不能为空");
    if (s.prompt.empty()) return fail("正文不能为空");
    if (s.version < 1) return fail("version 必须 >= 1");
    if (s.prompt.size() > kMaxSkillBodyChars) return fail("正文超过体积上限");
    if (err) err->clear();
    return true;
}

}  // namespace vgo
