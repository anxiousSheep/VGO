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

Point pt(const std::string& gtp) {
    Point p;
    gtpParsePoint(gtp, 19, &p);
    return p;
}

JosekiStone stone(const std::string& gtp, Color c, const std::string& role,
                  const std::string& label) {
    JosekiStone s;
    s.point = pt(gtp);
    s.color = c;
    s.role = role;
    s.label = label;
    return s;
}

JosekiZone zone(const std::string& name, std::vector<Point> points, const std::string& meaning) {
    JosekiZone z;
    z.name = name;
    z.points = std::move(points);
    z.meaning = meaning;
    return z;
}

Color parseColor(const std::string& s) {
    const std::string v = lower(s);
    if (v == "black" || v == "b") return Color::Black;
    if (v == "white" || v == "w") return Color::White;
    return Color::Empty;
}

// when 谓词：string（"zone_friendly_majority:right_flank"）或 object（{"zone_…":"…"}）
// 一律规范化为字符串（4 保留字）。解析失败返回 false。
bool parseWhen(const Json& v, std::string* out) {
    if (v.isString()) {
        *out = v.string;
        return true;
    }
    if (v.isObject() && v.object.size() == 1) {
        const std::string k = v.object[0].first;
        const Json& val = v.object[0].second;
        if (k == "ladder_favorable") {
            *out = "ladder_favorable";
            return true;
        }
        if (k == "zone_friendly_majority" || k == "zone_enemy_majority") {
            *out = k + ":" + val.asString();
            return true;
        }
    }
    return false;
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
                                        Corner c) {
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

Color josekiInvader(const JosekiEntry& e) {
    for (const JosekiVariant& v : e.variants)
        if (!v.sequence.empty()) return v.sequence[0].by;
    if (!e.preconditions.required_stones.empty())
        return opponent(e.preconditions.required_stones[0].color);
    return Color::Empty;
}

JosekiLibrary JosekiLibrary::builtin() {
    JosekiLibrary lib;
    {
        // 点三三（星位三三入侵）——旗舰条目，示范完整 v2 schema。
        // 注：文档 §4.1 示例把 anchor 标成 R16，但「对方星位占角」的星位是 4-4 = Q16；
        // 此处按「星位 = 4-4」修正，坐标帧由 test_joseki_v2 的四角往返测试锁死。
        JosekiEntry e;
        e.id = "hoshi-sansan-invasion";
        e.name = "点三三";
        e.aliases = {"三三入侵", "点33", "点三·三"};
        e.size = 19;
        e.applies_to_formation = "对方星位占角";
        e.source = "builtin";
        e.preconditions.required_stones = {
            stone("Q16", Color::Black, "corner_anchor", "对方星位子"),
        };
        e.preconditions.required_empty = {
            pt("R17"), pt("Q17"), pt("S17"), pt("Q18"), pt("S18"),
        };
        e.preconditions.zones = {
            {"left_flank",
             zone("left_flank",
                  {pt("Q14"), pt("Q13"), pt("Q12"), pt("R14"), pt("R13"), pt("R12")},
                  "左侧翼")},
            {"right_flank",
             zone("right_flank",
                  {pt("S14"), pt("S13"), pt("S12"), pt("T14"), pt("T13"), pt("T12")},
                  "右侧翼")},
        };
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "挡后长（常型）";
            v.when = "default";
            v.direction_rule = "block_toward_friendly_side";
            v.sequence = {
                mv("R17", Color::White, "点三三，夺角"),
                mv("Q17", Color::Black, "挡（方向由 direction_rule 镜像决定）"),
                mv("Q18", Color::White, "爬，求活关键"),
            };
            e.variants.push_back(std::move(v));
        }
        {
            JosekiVariant v;
            v.id = "reverse-block";
            v.name = "反向挡（右侧有己方接应时取势）";
            v.when = "zone_friendly_majority:right_flank";
            v.sequence = {};  // 空序列：只作策略建议，不进演示线
            e.variants.push_back(std::move(v));
        }
        {
            JosekiVariant v;
            v.id = "ladder-variant";
            v.name = "含征子变化";
            v.when = "ladder_favorable";
            v.ladder_check = true;
            v.sequence = {};  // 空序列：B1b 落地前只作策略建议
            e.variants.push_back(std::move(v));
        }
        e.alternatives_if_rejected = {"approach-knight", "approach-high"};
        e.teaching.idea = "星位角空虚时直接点三三夺实地，对方得外势";
        e.teaching.caution =
            "点三三过早让外势朝向对方接应方向时亏损；征子关系不利时部分变化不成立";
        lib.upsert(std::move(e));
    }
    {
        // 星位小飞挂·一间跳。
        JosekiEntry e;
        e.id = "hoshi-small-knight-approach";
        e.name = "星位小飞挂";
        e.aliases = {"小飞挂", "星小飞挂"};
        e.size = 19;
        e.applies_to_formation = "对方星位占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q16", Color::Black, "corner_anchor", "对方星位子")};
        e.preconditions.required_empty = {pt("O17"), pt("Q15"), pt("R14")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "一间跳守角";
            v.sequence = {
                mv("O17", Color::White, "小飞挂角"),
                mv("Q15", Color::Black, "一间跳守角"),
                mv("R14", Color::White, "拆二，安定"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "小飞挂角试探，对方一间跳守角后己方拆边安定";
        e.teaching.caution = "挂角过早时对方可能一间夹取势，需留意右侧接应";
        lib.upsert(std::move(e));
    }
    {
        // 星位一间高挂·小飞应。
        JosekiEntry e;
        e.id = "hoshi-one-space-high-approach";
        e.name = "星位一间高挂";
        e.aliases = {"一间高挂"};
        e.size = 19;
        e.applies_to_formation = "对方星位占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q16", Color::Black, "corner_anchor", "对方星位子")};
        e.preconditions.required_empty = {pt("O18"), pt("Q17"), pt("P15")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "小飞应";
            v.sequence = {
                mv("O18", Color::White, "一间高挂（高位施加压力）"),
                mv("Q17", Color::Black, "小飞应"),
                mv("P15", Color::White, "拆边"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "一间高挂高位施压，对方小飞应后己方拆边";
        e.teaching.caution = "高挂让位，对方可尖顶或飞应，需权衡外势与实地";
        lib.upsert(std::move(e));
    }
    {
        // 双飞燕（两侧同时挂角）。
        JosekiEntry e;
        e.id = "hoshi-double-approach";
        e.name = "双飞燕";
        e.aliases = {"双挂"};
        e.size = 19;
        e.applies_to_formation = "对方星位占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q16", Color::Black, "corner_anchor", "对方星位子")};
        e.preconditions.required_empty = {pt("O17"), pt("R15"), pt("Q17")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "尖应对双挂";
            v.sequence = {
                mv("O17", Color::White, "一侧小飞挂"),
                mv("R15", Color::White, "另一侧小飞挂，双飞燕成形"),
                mv("Q17", Color::Black, "尖，治孤"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "两侧同时挂角夹击，对方尖应对，己方得角或外势需权衡";
        e.teaching.caution = "双挂取势但局部易被对方整形，需看全局厚薄";
        lib.upsert(std::move(e));
    }
    {
        // 小目小飞挂·秀策尖。
        JosekiEntry e;
        e.id = "komoku-shusaku";
        e.name = "小目小飞挂·秀策尖";
        e.aliases = {"秀策尖", "小目小飞挂"};
        e.size = 19;
        e.applies_to_formation = "己方小目占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q17", Color::Black, "corner_anchor", "己方小目子")};
        e.preconditions.required_empty = {pt("O16"), pt("P16"), pt("P15")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "秀策尖";
            v.sequence = {
                mv("O16", Color::White, "小飞挂角"),
                mv("P16", Color::Black, "秀策尖，厚实"),
                mv("P15", Color::White, "长"),
                mv("Q15", Color::Black, "扳"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "小目挂角后秀策尖取厚势，重视全局";
        e.teaching.caution = "秀策尖偏缓，也可一间夹取势或托退取地";
        lib.upsert(std::move(e));
    }
    {
        // 小目托退定式。
        JosekiEntry e;
        e.id = "komoku-tsuke-hiki";
        e.name = "小目托退定式";
        e.aliases = {"托退"};
        e.size = 19;
        e.applies_to_formation = "己方小目占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q17", Color::Black, "corner_anchor", "己方小目子")};
        e.preconditions.required_empty = {pt("O16"), pt("O17"), pt("P16"), pt("O18")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "托退取地";
            v.sequence = {
                mv("O16", Color::White, "小飞挂角"),
                mv("O17", Color::Black, "托"),
                mv("P16", Color::White, "退"),
                mv("O18", Color::Black, "虎口接"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "托退取实地，虎口接后安定；对方得外势";
        e.teaching.caution = "托退实地坚实但位置低，需看外势得失";
        lib.upsert(std::move(e));
    }
    {
        // 小目一间夹。
        JosekiEntry e;
        e.id = "komoku-one-space-pincer";
        e.name = "小目一间夹";
        e.aliases = {"一间夹"};
        e.size = 19;
        e.applies_to_formation = "己方小目占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("Q17", Color::Black, "corner_anchor", "己方小目子")};
        e.preconditions.required_empty = {pt("O16"), pt("O18"), pt("Q18")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "夹击取势";
            v.sequence = {
                mv("O16", Color::White, "小飞挂角"),
                mv("O18", Color::Black, "一间夹"),
                mv("Q18", Color::White, "点三三转换"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "夹击取外势，对方点三三转换取地";
        e.teaching.caution = "夹击后对方点三三转换，需判断角地与外势的得失";
        lib.upsert(std::move(e));
    }
    {
        // 三三占角·一间跳。
        JosekiEntry e;
        e.id = "sansan-one-space-jump";
        e.name = "三三占角";
        e.aliases = {"三三"};
        e.size = 19;
        e.applies_to_formation = "己方三三占角";
        e.source = "builtin";
        e.preconditions.required_stones = {stone("R17", Color::Black, "corner_anchor", "己方三三子")};
        e.preconditions.required_empty = {pt("Q15"), pt("P17"), pt("Q18")};
        {
            JosekiVariant v;
            v.id = "main";
            v.name = "一间跳";
            v.sequence = {
                mv("Q15", Color::White, "挂角"),
                mv("P17", Color::Black, "一间跳"),
                mv("Q18", Color::White, "拆边"),
            };
            e.variants.push_back(std::move(v));
        }
        e.teaching.idea = "三三取地最稳但位置低；对方挂角时选一间跳或小飞取势";
        e.teaching.caution = "三三位置低，需尽早向中腹出头";
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
    v = j.get("size");
    e.size = (v && v->isNumber()) ? static_cast<int>(v->number) : 19;
    if (e.size < 2) return fail("size 非法");
    v = j.get("applies_to_formation");
    if (v && v->isString()) e.applies_to_formation = v->string;
    v = j.get("source");
    if (v && v->isString()) e.source = v->string;
    v = j.get("aliases");
    if (v && v->isArray()) {
        for (const Json& a : v->array)
            if (a.isString()) e.aliases.push_back(a.string);
    }

    // —— preconditions ——
    const Json* pre = j.get("preconditions");
    if (pre && pre->isObject()) {
        const Json* rs = pre->get("required_stones");
        if (rs && rs->isArray()) {
            for (const Json& s : rs->array) {
                if (!s.isObject()) return fail("required_stones 元素须为对象");
                const Json* mp = s.get("point");
                const Json* cl = s.get("color");
                if (!mp || !mp->isString()) return fail("required_stones 缺少 point");
                JosekiStone st;
                st.point = pt(mp->string);
                st.color = cl && cl->isString() ? parseColor(cl->string) : Color::Empty;
                if (st.color == Color::Empty) return fail("required_stones 缺少合法 color");
                const Json* role = s.get("role");
                if (role && role->isString()) st.role = role->string;
                const Json* label = s.get("label");
                if (label && label->isString()) st.label = label->string;
                e.preconditions.required_stones.push_back(std::move(st));
            }
        }
        auto parsePts = [&](const Json* arr, const char* what, std::vector<Point>* out) -> bool {
            if (!arr) return true;
            if (!arr->isArray()) return fail(std::string(what) + " 须为数组");
            for (const Json& p : arr->array) {
                if (!p.isString()) return fail(std::string(what) + " 坐标须为字符串");
                Point q;
                if (!gtpParsePoint(p.string, e.size, &q))
                    return fail("非法坐标：" + p.string);
                out->push_back(q);
            }
            return true;
        };
        if (!parsePts(pre->get("required_empty"), "required_empty",
                      &e.preconditions.required_empty))
            return false;
        if (!parsePts(pre->get("forbidden"), "forbidden", &e.preconditions.forbidden))
            return false;
        const Json* zones = pre->get("zones");
        if (zones && zones->isObject()) {
            for (const auto& kv : zones->object) {
                const Json& zj = kv.second;
                JosekiZone z;
                z.name = kv.first;
                if (zj.isObject()) {
                    const Json* meaning = zj.get("meaning");
                    if (meaning && meaning->isString()) z.meaning = meaning->string;
                    if (!parsePts(zj.get("points"), "zone.points", &z.points)) return false;
                }
                e.preconditions.zones[kv.first] = std::move(z);
            }
        }
    }

    // —— variants ——
    v = j.get("variants");
    if (!v || !v->isArray() || v->array.empty()) return fail("缺少 variants");
    for (const Json& vj : v->array) {
        if (!vj.isObject()) return fail("variants 元素须为对象");
        JosekiVariant var;
        const Json* id = vj.get("id");
        if (!id || !id->isString() || id->string.empty()) return fail("variants 缺少 id");
        var.id = id->string;
        const Json* name = vj.get("name");
        if (name && name->isString()) var.name = name->string;
        const Json* when = vj.get("when");
        if (when && !when->isNull() && !parseWhen(*when, &var.when))
            return fail("非法 when 谓词");
        const Json* lc = vj.get("ladder_check");
        var.ladder_check = lc && lc->asBool(false);
        const Json* dr = vj.get("direction_rule");
        if (dr && dr->isString()) var.direction_rule = dr->string;
        const Json* seq = vj.get("sequence");
        if (seq && seq->isArray()) {
            for (const Json& m : seq->array) {
                if (!m.isObject()) return fail("sequence 元素须为对象");
                const Json* mp = m.get("move");
                const Json* by = m.get("by");
                if (!mp || !mp->isString()) return fail("sequence 缺少 move");
                JosekiMove jm;
                if (!gtpParsePoint(mp->string, e.size, &jm.p))
                    return fail("非法坐标：" + mp->string);
                jm.by = by && by->isString() ? parseColor(by->string) : Color::Empty;
                if (jm.by == Color::Empty) return fail("sequence 缺少合法 by（black/white）");
                const Json* note = m.get("note");
                if (note && note->isString()) jm.note = note->string;
                var.sequence.push_back(std::move(jm));
            }
        }
        e.variants.push_back(std::move(var));
    }

    // —— alternatives / teaching ——
    v = j.get("alternatives_if_rejected");
    if (v && v->isArray()) {
        for (const Json& a : v->array)
            if (a.isString()) e.alternatives_if_rejected.push_back(a.string);
    }
    const Json* teaching = j.get("teaching");
    if (teaching && teaching->isObject()) {
        const Json* idea = teaching->get("idea");
        if (idea && idea->isString()) e.teaching.idea = idea->string;
        const Json* caution = teaching->get("caution");
        if (caution && caution->isString()) e.teaching.caution = caution->string;
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
