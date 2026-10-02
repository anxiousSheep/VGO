#include "skill.h"

#include <algorithm>
#include <sstream>

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

}  // namespace

SkillRegistry SkillRegistry::builtin() {
    SkillRegistry r;
    {
        Skill s;
        s.id = "life-death";
        s.name = "死活基础";
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

std::string SkillRegistry::toText(const Skill& s) {
    std::ostringstream os;
    os << "id: " << s.id << '\n';
    os << "name: " << s.name << '\n';
    os << "applies:";
    for (QuestionClass q : s.applies_to) os << ' ' << questionClassToString(q);
    os << '\n';
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
            } else if (t.rfind("applies:", 0) == 0) {
                for (const std::string& tok : split(t.substr(8), ' ')) {
                    QuestionClass q;
                    if (questionClassFromString(tok, &q)) s.applies_to.push_back(q);
                }
            }
        } else {
            body += line;
            body += '\n';
        }
    }
    if (s.id.empty()) return false;
    s.prompt = body;
    if (!s.prompt.empty() && s.prompt.back() == '\n') s.prompt.pop_back();
    *out = std::move(s);
    return true;
}

}  // namespace vgo
