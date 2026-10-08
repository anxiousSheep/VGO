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

SkillMeta Skill::frontmatter() const {
    SkillMeta m;
    m.id = id;
    m.name = name;
    m.description = description;
    m.applies_to = applies_to;
    m.version = version;
    m.last_score = last_score;
    return m;
}

SkillRegistry SkillRegistry::builtin() {
    SkillRegistry r;
    {
        Skill s;
        s.id = "life-death";
        s.name = "死活基础";
        s.description = "两眼活、一眼死、真眼假眼、眼位大小";
        s.applies_to = {QuestionClass::LifeDeath};
        s.prompt =
            "死活常识：一块棋要活，必须做出两只真眼（或具备双活/劫活等特殊形态）。\n"
            "只有一个真眼、或全是假眼的棋，会被对方点眼杀死。判断死活时先数眼位、\n"
            "再看是否有连接或延展的空间。常用死活手法：点眼（点入对方眼位要点）、扑（送吃让"
            "对方气紧成假眼）、做眼、破眼。眼位有大小：大眼位可能靠延展做出两眼，小眼位则"
            "可能被点杀。判断「能活吗」要数清真眼数与眼位空间，不要虚构具体的眼位坐标。\n"
            "【示例】\n"
            "问：这块棋能不能活？\n"
            "答：先数真眼数：做出两只真眼就能活，只有一只真眼或全是假眼会被点杀；再看眼位"
            "空间是否够大。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "move-quality";
        s.name = "落点对比";
        s.description = "用胜率与领先目数变化评估一手棋的好坏";
        s.applies_to = {QuestionClass::MoveQuality};
        s.prompt =
            "评估一手棋的好坏，看两点：落子前后的胜率变化（winrate，百分点）和领先目数变化\n"
            "（scoreLead，目）。胜率明显下降、或领先目数明显缩水，就是坏棋（恶手/缓手）；\n"
            "反之是好棋。常见恶手类型：缓手（没抢到急所、下得慢）、随手（局部下完全局脱节）、"
            "贪吃（吃小丢大）、后手（该先手的地方落后手）。回答时优先引用提供的事实数据，用"
            "「胜率从 X% 变到 Y%」的口吻，不要编造数据。\n"
            "【示例】\n"
            "问：这步棋怎么样？\n"
            "答：引用胜率与目数变化判断好坏，并说明是缓手、随手还是贪吃。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "counting";
        s.name = "数子与形势";
        s.description = "数子法领地计算、贴目与领先目数";
        s.applies_to = {QuestionClass::WhoIsWinning};
        s.prompt =
            "围棋数子：活棋的棋子数加上围住的交叉点（目）为一方领地，死子归对方。\n"
            "黑方要贴目（通常 7.5 目，让子局贴目减半）。比较双方领地与贴目后得出领先目数与"
            "胜率。回答「谁领先」时直接引用提供的领先目数与胜率，不要自行估算。\n"
            "判断形势还要看厚薄与发展潜力：厚势有辐射价值、孤棋是负资产、未定型处存在变数，"
            "领先目数相近时这些因素会决定胜负走向。\n"
            "【示例】\n"
            "问：现在谁领先？\n"
            "答：引用提供的领先目数与胜率作答，并提示厚薄与未定型处可能改变形势。\n"
            "问：形势如何？\n"
            "答：先看领先目数与胜率，再结合厚薄与潜力综合判断。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "ko";
        s.name = "打劫规则";
        s.description = "打劫、劫材与提劫的规则";
        s.applies_to = {QuestionClass::Other};
        s.prompt =
            "打劫：一方提子后，另一方不能立即提回，必须先在别处找劫材、对方应一手后才能提回。\n"
            "涉及「打劫」「劫材」「提劫」的问题，请解释这一规则，并提醒用户找劫材。\n"
            "【示例】\n"
            "问：打劫的规则是什么？\n"
            "答：一方提劫后对方不能立即提回，须先在别处找劫材，对方应一手后才能提回。\n"
            "问：什么是劫材？\n"
            "答：别处能迫使对方应手的棋，用来「买」到提回劫的权利。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "terms";
        s.name = "围棋术语";
        s.description = "打吃、提子、连接、延展、扳、断、气、眼等术语";
        s.applies_to = {};  // all
        s.prompt =
            "常用围棋术语：打吃（把对方打成只剩一口气）、提子（吃掉无气的子）、连接（把己方\n"
            "两块棋连成一块）、延展（加强己方一块棋）、扳（贴住对方棋子拐头）、断（切断对方\n"
            "两块棋）、气（棋子相邻的空点）、眼（围成的空点）、尖（斜对角小步）、飞（跨两路"
            "跳）、虎（两子间做虎口护断）、长（直线延伸）、双打吃（一手同时打吃两块）、征子"
            "（连续打吃追吃）、枷（虚罩困死）。用这些术语解释时保持口语化。\n"
            "【示例】\n"
            "问：什么是打吃？\n"
            "答：把对方棋子打成只剩一口气，对方若不接就会被提掉。\n"
            "问：什么是气？\n"
            "答：棋子相邻的空点，气越多棋子越安全，气被紧完就被提掉。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "fuseki-joseki";
        s.name = "布局与定式";
        s.description = "星/小目/三三布局常识、常用定式与选择逻辑";
        s.applies_to = {QuestionClass::WhereToPlay, QuestionClass::Other};
        s.prompt =
            "布局与定式常识：星位（四线交叉点）重视外势与速度，易成大模样；小目（三四路）兼"
            "顾实地与外势，取地扎实；三三（三三路）直接取角地，实地最稳但位置低、外势不足。"
            "开局先占角、再挂角或守角、后拆边，抢占大场。常见定式：星位小飞挂、一间低夹、"
            "双飞燕；小目小飞挂、一间高挂、托退定式。选择定式看全局配合：外势厚实则选夹击"
            "型取势，需实地则选托退，定式选择要与周边棋子呼应，避免局部下完却全局脱节。\n"
            "【示例】\n"
            "问：开局第一手该下哪？\n"
            "答：优先占空角：想取势抢速度下星位，想兼顾实地下小目，想稳取角地下三三。\n"
            "问：这个定式怎么选？\n"
            "答：看全局配合：己方厚势多则选夹击取势，需实地则选托退，定式要与周边棋子呼应。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "middle-game";
        s.name = "中盘攻防";
        s.description = "攻防方向/急所/厚薄判断";
        s.applies_to = {QuestionClass::WhereToPlay, QuestionClass::WhyThisMove};
        s.prompt =
            "中盘看攻防方向：抢占急所（双方必争的要点）、分断对方弱棋、补强自身薄棋。攻击要"
            "间接——通过威胁对方弱棋获得利益，而不是硬吃；防守要治孤，让对方无法借攻击获利。"
            "判断厚薄：厚势能辐射周边、不怕战斗，薄棋（孤棋、断点、气紧）易被攻击。落子前问"
            "三件事：这手是攻还是守、有没有抢到急所、与全局是否呼应。宁可下得厚一点，也不要"
            "留下孤棋。\n"
            "【示例】\n"
            "问：现在该攻还是该守？\n"
            "答：先看薄厚：己方有孤棋或断点就先补强，对方有弱棋就施加攻击，同时抢占急所。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "tesuji-shape";
        s.name = "手筋与棋形";
        s.description = "扳/断/尖/飞/虎/枷/征、好形/愚形";
        s.applies_to = {QuestionClass::MoveQuality, QuestionClass::WhyThisMove};
        s.prompt =
            "手筋（妙手）是效率极高的好棋，常以扳、断、尖、飞、虎、枷、征、渡等手法命中对方"
            "棋形弱点。棋形好坏看两点：断点多少与气松紧——好形（虎口、连接完整、气宽）结构"
            "稳固，愚形（弯三角、刀把五、团子）效率低。补棋先补断点，吃棋先紧气或断。判断一"
            "手棋是否妙，看它是否一子多用（既补强自己又威胁对方），或命中对方形薄之处。\n"
            "【示例】\n"
            "问：这手棋妙在哪？\n"
            "答：它一子多用：既补强了自己的断点，又威胁了对方的薄棋，还限制了对方的眼位。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "endgame";
        s.name = "官子";
        s.description = "官子大小、先手官子";
        s.applies_to = {QuestionClass::WhereToPlay, QuestionClass::Other};
        s.prompt =
            "收官先抢大官子、后收小官子，先手官子优先于后手官子（先手官子白赚一手）。判断官"
            "子大小看目数与先后手：先手官子价值翻倍计算；双方先手最紧急，单方先手次之，逆收"
            "要算清。常见大官子：一、二路扳粘、大飞伸腿、角部曲三等。收官要冷静，按价值从大"
            "到小依次收，不要因小失大。\n"
            "【示例】\n"
            "问：收官先收哪？\n"
            "答：先收双方先手的大官子，再收单方先手的，最后按目数从大到小收后手官子。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "ko-fight";
        s.name = "劫争与劫材";
        s.description = "劫材价值、劫争转换";
        s.applies_to = {QuestionClass::Other};
        s.prompt =
            "劫争的价值与转换：打劫前先算清劫的价值（赢了劫能得到多少目、多大利益），再盘点"
            "双方劫材是否充足。劫争转换：一方提劫、另一方在别处得利后放弃劫，形成利益交换。"
            "大劫（关系生死或大目数）值得付出大代价，小劫不值得。判断该不该打劫：赢劫收益大"
            "且己方劫材足够时打，否则避劫或先补。\n"
            "【示例】\n"
            "问：这个劫该不该打？\n"
            "答：先算劫的价值与双方应对：赢劫收益大且己方应对充足就打，不足就避劫或先补。";
        r.upsert(s);
    }
    {
        Skill s;
        s.id = "handicap";
        s.name = "让子局策略";
        s.description = "让子局策略";
        s.applies_to = {QuestionClass::Other};
        s.prompt =
            "让子局中黑方（被让子方）起点占优，白方（让子方）需发力追赶。黑方策略：守住优势，"
            "厚实地占角取地，避免复杂战斗，把局面简化导向终局；不要贪大模样，重视连接与实地。"
            "白方策略：主动制造复杂局面、挑起战斗，利用黑方偏厚的心理进攻弱棋；多下高位、取"
            "势，扩大棋盘复杂性以寻找翻盘机会。让子越多，黑方越应求稳、白方越应求变。\n"
            "【示例】\n"
            "问：让子局该怎么下？\n"
            "答：黑方求稳守优势，白方求变找战斗；黑方避复杂、白方造复杂。";
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

std::vector<SkillMeta> SkillRegistry::frontmatter() const {
    std::vector<SkillMeta> out;
    out.reserve(skills_.size());
    for (const Skill& s : skills_) out.push_back(s.frontmatter());
    return out;
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

bool SkillRegistry::proposeRewrite(const std::string& id, const std::string& candidate) {
    const std::string c = trim(candidate);
    if (c.empty()) return false;
    for (Skill& s : skills_) {
        if (s.id != id) continue;
        // 采纳时会追加到现有正文，合并后不得超体积上限。
        const std::size_t merged =
            s.prompt.size() + (s.prompt.empty() ? 0 : 1) + c.size();
        if (merged > kMaxSkillBodyChars) return false;
        s.pending.push_back(c);
        return true;
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

std::string buildRewritePrompt(const Skill& s, const std::vector<std::string>& misses) {
    std::ostringstream os;
    os << "你是一名围棋教学知识包编辑。请改写技能「" << s.name << "」的知识正文，"
       << "使其更好地覆盖下列曾经答错的题目：\n";
    for (const std::string& m : misses) os << "- " << m << '\n';
    os << "\n当前正文：\n"
       << s.prompt << "\n\n"
       << "要求：保持风格口语化、面向棋手，不编造数据。只输出改写后的正文，"
       << "不要加任何解释或标题。";
    return os.str();
}

std::string extractRewriteBody(const std::string& reply) {
    const std::string fence = "```";
    const std::size_t open = reply.find(fence);
    if (open == std::string::npos) return trim(reply);
    std::size_t start = open + fence.size();
    // 跳过开围栏行（可带语言标注，如 ```markdown）到行尾。
    const std::size_t nl = reply.find('\n', start);
    if (nl != std::string::npos && nl < reply.find(fence, start)) start = nl + 1;
    const std::size_t close = reply.find(fence, start);
    const std::string body =
        (close == std::string::npos) ? reply.substr(start) : reply.substr(start, close - start);
    return trim(body);
}

}  // namespace vgo
