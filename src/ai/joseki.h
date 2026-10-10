#pragma once

#include <map>
#include <string>
#include <vector>

#include "go/go_types.h"
#include "symmetry.h"

namespace vgo {

// ---------------------------------------------------------------------------
// 定式词典 v2（A.1，五层模型的数据层）。D1（=P1 前提即代码）/ D2（=P2 知识即变体树）
// 的落点：每条定式 = 前提（机器谓词）+ 变体集（每个变体有独立 `when` 谓词）+ 方向规则
// + 教学注释。LLM 永远不做适用性判断——谓词对真实 Board 求值，见 board_predicates.h。
// ---------------------------------------------------------------------------

// 定式序列中的一手：落点 + 执子方（绝对黑白）+ 讲解词。纯数据，Qt-free。
struct JosekiMove {
    Point p{-1, -1};
    Color by = Color::Empty;
    std::string note;
};

// 前提中的一颗指定子（指定点 + 指定色 + 语义标注）。
struct JosekiStone {
    Point point{-1, -1};
    Color color = Color::Empty;
    std::string role, label;
};

// 侧翼分区（left_flank / right_flank），供方向规则与 `when` 谓词求值。
struct JosekiZone {
    std::string name;         // "left_flank" / "right_flank"
    std::vector<Point> points;
    std::string meaning;
};

struct JosekiPreconditions {
    std::vector<JosekiStone> required_stones;   // 指定点指定色（如 对方星位子）
    std::vector<Point> required_empty;          // 必须为空的路径点
    std::vector<Point> forbidden;               // 禁止被占的点
    std::map<std::string, JosekiZone> zones;    // left_flank / right_flank
};

// 一个变体。`when` 是微型谓词（仅 4 保留字，见 board_predicates.h evaluateWhen）：
//   "default" | "zone_friendly_majority:<zone>" | "zone_enemy_majority:<zone>"
//   | "ladder_favorable"
// 空 `sequence` 的变体只作策略建议（由 joseki_advise 陈列），不进 apply/textbook 演示线。
struct JosekiVariant {
    std::string id, name;
    std::string when = "default";     // 见上
    bool ladder_check = false;        // 征子变体：B1b 落地前强制引擎仲裁
    std::string direction_rule;       // "block_toward_friendly_side" | ""
    std::vector<JosekiMove> sequence; // canonical UR 帧
};

struct Teaching {
    std::string idea, caution;
};

struct JosekiEntry {
    std::string id, name, applies_to_formation, source;
    std::vector<std::string> aliases;
    int size = 19;                       // 适用棋盘尺寸；lookup 按此闸校验
    JosekiPreconditions preconditions;
    std::vector<JosekiVariant> variants;
    std::vector<std::string> alternatives_if_rejected;
    Teaching teaching;
};

// 定式词典：代码内嵌 builtin + 热更 JSON（与 SkillRegistry 同构，D6 双源）。
class JosekiLibrary {
public:
    static JosekiLibrary builtin();

    // 解析单条定式 JSON（v2 schema，见 resources/joseki/*.json）。非法 JSON / 坐标 /
    // 字段报错写 err，返回 false。
    bool fromJsonText(const std::string& text, std::string* err);
    bool upsert(const JosekiEntry& e);

    // id 精确 → name 精确 → alias 精确 → 子串/包含（大小写不敏感）。查无返回 nullptr。
    const JosekiEntry* find(const std::string& query) const;

    std::size_t size() const { return entries_.size(); }
    const std::vector<JosekiEntry>& all() const { return entries_; }

    // 目录索引（id + name + aliases，几十行，省 token，注入 system prompt）。
    std::string directoryIndex() const;

private:
    std::vector<JosekiEntry> entries_;
};

// UR 帧序列 → 目标角（4 对称投影），颜色不变。`size` 为定式适用的棋盘尺寸。
std::vector<JosekiMove> projectToCorner(const std::vector<JosekiMove>& seq, int size,
                                        Corner c);

// 解析 "UR"/"UL"/"BL"/"BR"（大小写不敏感）→ Corner；非法返回 false。
bool parseCorner(const std::string& s, Corner* out);

// Corner → 短名（"UR"/"UL"/"BL"/"BR"）。
const char* cornerName(Corner c);

// 定式的「入侵方」颜色：首变体首手的 `by`；若无变体序列，取首颗 required_stone 的对方。
Color josekiInvader(const JosekiEntry& e);

}  // namespace vgo
