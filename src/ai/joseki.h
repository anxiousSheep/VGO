#pragma once

#include <string>
#include <vector>

#include "go/go_types.h"
#include "symmetry.h"

namespace vgo {

// 定式序列中的一手：落点 + 执子方（绝对黑白）+ 讲解词。纯数据，Qt-free（A.2）。
struct JosekiMove {
    Point p{-1, -1};
    Color by = Color::Empty;
    std::string note;
};

// 一条定式。`sequence` 存 UR 帧（右上角）GTP 坐标解析后的内部点；颜色为绝对黑白。
struct JosekiEntry {
    std::string id, name, corner, trigger, choices, source;
    int size = 19;                        // 适用棋盘尺寸；lookup 按此闸校验
    std::vector<std::string> aliases;
    std::vector<JosekiMove> sequence;
};

// 定式词典：代码内嵌 builtin + 热更 JSON（与 SkillRegistry 同构，D1 双源）。
class JosekiLibrary {
public:
    static JosekiLibrary builtin();

    // 解析单条定式 JSON（见 resources/joseki/*.json）。非法 JSON / 坐标 / 字段
    // 报错写 err，返回 false。
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

// UR 帧序列 → 目标角（8 对称投影），颜色不变。`size` 为定式适用的棋盘尺寸。
std::vector<JosekiMove> projectToCorner(const std::vector<JosekiMove>& seq, int size,
                                        const std::string& corner);

// 解析 "UR"/"UL"/"BL"/"BR"（大小写不敏感）→ Corner；非法返回 false。
bool parseCorner(const std::string& s, Corner* out);

// Corner → 短名（"UR"/"UL"/"BL"/"BR"）。
const char* cornerName(Corner c);

}  // namespace vgo
