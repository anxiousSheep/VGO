#include "joseki.h"

#include <algorithm>
#include <cctype>

#include "gtp.h"
#include "json.h"

namespace vgo {

namespace {

std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// 内置条目用「GTP 坐标 + 绝对颜色」直接构造（UR 帧、19 路）。
JosekiMove mv(const std::string& gtp, Color c, const std::string& note) {
    JosekiMove m;
    m.by = c;
    m.note = note;
    gtpParsePoint(gtp, 19, &m.p);
    return m;
}

Color parseColor(const std::string& s) {
    const std::string v = lower(s);
    if (v == "black" || v == "b") return Color::Black;
    if (v == "white" || v == "w") return Color::White;
    return Color::Empty;
}

}  // namespace

bool parseCorner(const std::string& s, Corner* out) {
    const std::string v = lower(s);
    if (v == "ur") { *out = Corner::UR; return true; }
    if (v == "ul") { *out = Corner::UL; return true; }
    if (v == "bl") { *out = Corner::BL; return true; }
    if (v == "br") { *out = Corner::BR; return true; }
    return false;
}

const char* cornerName(Corner c) {
    switch (c) {
        case Corner::UR: return "UR";
        case Corner::UL: return "UL";
        case Corner::BL: return "BL";
        case Corner::BR: return "BR";
    }
    return "UR";
}

std::vector<JosekiMove> projectToCorner(const std::vector<JosekiMove>& seq, int size,
                                        const std::string& corner) {
    Corner c = Corner::UR;
    parseCorner(corner, &c);
    const Symmetry s = cornerSymmetry(c);
    std::vector<JosekiMove> out;
    out.reserve(seq.size());
    for (const JosekiMove& m : seq) {
        JosekiMove r = m;
        r.p = applySymmetry(s, m.p, size);
        out.push_back(std::move(r));
    }
    return out;
}

JosekiLibrary JosekiLibrary::builtin() {
    JosekiLibrary lib;
    {
        // 点三三（星位三三入侵）——评审修正后的 UR 帧坐标（R17 = 右上角三三）。
        JosekiEntry e;
        e.id = "hoshi-sansan-invasion";
        e.name = "点三三";
        e.aliases = {"三三入侵", "点33", "点三·三"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "对方星位占角，己方直接点三三夺角";
        e.choices = "黑外围有配合时可选连扳强封；白需注意点三三时机过早会亏损";
        e.source = "builtin";
        e.sequence = {
            mv("R17", Color::White, "直接点入三三，意图夺角"),
            mv("Q17", Color::Black, "挡，必然"),
            mv("Q18", Color::White, "爬，求活的关键"),
            mv("P17", Color::Black, "长"),
            mv("S17", Color::White, "再爬"),
            mv("S16", Color::Black, "扳，封锁"),
            mv("R18", Color::White, "扳活角，黑得外势"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 星位小飞挂·一间跳。
        JosekiEntry e;
        e.id = "hoshi-small-knight-approach";
        e.name = "星位小飞挂";
        e.aliases = {"小飞挂", "星小飞挂"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "对方星位占角，己方小飞挂角试探";
        e.choices = "黑可一间跳守角，也可一间夹取势；白再拆边或点三三转换";
        e.source = "builtin";
        e.sequence = {
            mv("Q16", Color::Black, "星位占角"),
            mv("O17", Color::White, "小飞挂角"),
            mv("Q15", Color::Black, "一间跳守角"),
            mv("R14", Color::White, "拆二，安定"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 星位一间高挂·点角转换。
        JosekiEntry e;
        e.id = "hoshi-one-space-high-approach";
        e.name = "星位一间高挂";
        e.aliases = {"一间高挂"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "对方星位占角，己方一间高挂（高位）施加压力";
        e.choices = "黑可尖顶或飞应；白可点三三转换取地";
        e.source = "builtin";
        e.sequence = {
            mv("Q16", Color::Black, "星位占角"),
            mv("O18", Color::White, "一间高挂"),
            mv("Q17", Color::Black, "小飞应"),
            mv("P15", Color::White, "拆边"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 双飞燕。
        JosekiEntry e;
        e.id = "hoshi-double-approach";
        e.name = "双飞燕";
        e.aliases = {"双挂"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "对方星位占角，己方两侧同时挂角夹击";
        e.choices = "黑以「立」或「尖」治孤；白得角或外势需权衡";
        e.source = "builtin";
        e.sequence = {
            mv("Q16", Color::Black, "星位占角"),
            mv("O17", Color::White, "一侧小飞挂"),
            mv("R14", Color::Black, "另一侧小飞挂（双飞燕）"),
            mv("Q15", Color::White, "点角试探"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 小目小飞挂·秀策尖。
        JosekiEntry e;
        e.id = "komoku-shusaku";
        e.name = "小目小飞挂·秀策尖";
        e.aliases = {"秀策尖", "小目小飞挂"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "己方小目占角，对方小飞挂角";
        e.choices = "秀策尖取厚势，重视全局；也可一间夹取势或托退取地";
        e.source = "builtin";
        e.sequence = {
            mv("Q17", Color::Black, "小目占角"),
            mv("O16", Color::White, "小飞挂角"),
            mv("P16", Color::Black, "秀策尖，厚实"),
            mv("P15", Color::White, "长"),
            mv("Q15", Color::Black, "扳"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 小目托退定式。
        JosekiEntry e;
        e.id = "komoku-tsuke-hiki";
        e.name = "小目托退定式";
        e.aliases = {"托退"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "己方小目占角，对方小飞挂角后选择托退取地";
        e.choices = "托退取实地，虎口接后安定；对方得外势";
        e.source = "builtin";
        e.sequence = {
            mv("Q17", Color::Black, "小目占角"),
            mv("O16", Color::White, "小飞挂角"),
            mv("O17", Color::Black, "托"),
            mv("P16", Color::White, "退"),
            mv("O18", Color::Black, "虎口接"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 小目一间夹。
        JosekiEntry e;
        e.id = "komoku-one-space-pincer";
        e.name = "小目一间夹";
        e.aliases = {"一间夹"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "己方小目占角，对方小飞挂角后主动夹击取势";
        e.choices = "夹击取外势，对方点三三转换取地";
        e.source = "builtin";
        e.sequence = {
            mv("Q17", Color::Black, "小目占角"),
            mv("O16", Color::White, "小飞挂角"),
            mv("O18", Color::Black, "一间夹"),
            mv("Q18", Color::White, "点三三转换"),
        };
        lib.upsert(std::move(e));
    }
    {
        // 三三占角·一间跳。
        JosekiEntry e;
        e.id = "sansan-one-space-jump";
        e.name = "三三占角";
        e.aliases = {"三三"};
        e.corner = "UR";
        e.size = 19;
        e.trigger = "己方三三占角取地，对方挂角后选择方向";
        e.choices = "三三取地最稳但位置低；对方挂角时选一间跳或小飞取势";
        e.source = "builtin";
        e.sequence = {
            mv("R17", Color::Black, "三三占角，实地最稳"),
            mv("Q15", Color::White, "挂角"),
            mv("P17", Color::Black, "一间跳"),
            mv("Q18", Color::White, "拆边"),
        };
        lib.upsert(std::move(e));
    }
    return lib;
}

bool JosekiLibrary::fromJsonText(const std::string& text, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    Json j;
    if (!parseJson(text, &j) || !j.isObject()) return fail("非法 JSON");

    JosekiEntry e;
    const Json* v = j.get("id");
    if (!v || !v->isString() || v->string.empty()) return fail("缺少 id");
    e.id = v->string;
    v = j.get("name");
    if (v && v->isString()) e.name = v->string;
    v = j.get("corner");
    e.corner = (v && v->isString()) ? v->string : "UR";
    {
        Corner c;
        if (!parseCorner(e.corner, &c)) return fail("非法 corner（须为 UR/UL/BL/BR）");
    }
    v = j.get("size");
    e.size = (v && v->isNumber()) ? static_cast<int>(v->number) : 19;
    if (e.size < 2) return fail("size 非法");
    v = j.get("trigger");
    if (v && v->isString()) e.trigger = v->string;
    v = j.get("choices");
    if (v && v->isString()) e.choices = v->string;
    v = j.get("source");
    if (v && v->isString()) e.source = v->string;
    v = j.get("aliases");
    if (v && v->isArray()) {
        for (const Json& a : v->array)
            if (a.isString()) e.aliases.push_back(a.string);
    }
    v = j.get("sequence");
    if (!v || !v->isArray() || v->array.empty()) return fail("缺少 sequence");
    for (const Json& m : v->array) {
        if (!m.isObject()) return fail("sequence 元素须为对象");
        const Json* mv = m.get("move");
        const Json* by = m.get("by");
        if (!mv || !mv->isString()) return fail("sequence 缺少 move");
        Point p;
        if (!gtpParsePoint(mv->string, e.size, &p)) return fail("非法坐标：" + mv->string);
        const Color c = by && by->isString() ? parseColor(by->string) : Color::Empty;
        if (c == Color::Empty) return fail("sequence 缺少合法 by（black/white）");
        JosekiMove jm;
        jm.p = p;
        jm.by = c;
        const Json* note = m.get("note");
        if (note && note->isString()) jm.note = note->string;
        e.sequence.push_back(std::move(jm));
    }
    upsert(std::move(e));
    if (err) err->clear();
    return true;
}

bool JosekiLibrary::upsert(const JosekiEntry& e) {
    if (e.id.empty()) return false;
    for (JosekiEntry& x : entries_) {
        if (x.id == e.id) {
            x = e;
            return true;
        }
    }
    entries_.push_back(e);
    return true;
}

const JosekiEntry* JosekiLibrary::find(const std::string& query) const {
    const std::string q = lower(query);
    if (q.empty()) return nullptr;
    for (const JosekiEntry& e : entries_)
        if (lower(e.id) == q) return &e;
    for (const JosekiEntry& e : entries_)
        if (lower(e.name) == q) return &e;
    for (const JosekiEntry& e : entries_)
        for (const std::string& a : e.aliases)
            if (lower(a) == q) return &e;
    for (const JosekiEntry& e : entries_) {
        if (contains(lower(e.id), q) || contains(lower(e.name), q)) return &e;
        for (const std::string& a : e.aliases)
            if (contains(lower(a), q)) return &e;
    }
    return nullptr;
}

std::string JosekiLibrary::directoryIndex() const {
    std::string s;
    for (const JosekiEntry& e : entries_) {
        s += e.name;
        s += "（";
        s += e.id;
        for (const std::string& a : e.aliases) s += "/" + a;
        s += "）\n";
    }
    return s;
}

}  // namespace vgo
