#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "go/board.h"
#include "joseki.h"
#include "probe.h"
#include "symmetry.h"

namespace vgo {

// ---------------------------------------------------------------------------
// 定式 L4 仲裁 + L5 三模式执行（D4/P4 三模式分流 + D5/P5 无引擎降级 + §4.4.1 apply
// 门禁闭合）。纯逻辑、Qt-free、可单测；Qt 层（handleDemoTool）只消费这里的决策。
// ---------------------------------------------------------------------------

// L4 引擎仲裁结果（异步产出，advisor 层按 position hash 缓存后注入工具环路）。
struct JosekiVerdict {
    bool computed = false;    // false = 未仲裁 / 无引擎 / 摆线非法
    bool favorable = false;   // true = 定式成立（W_best - W_joseki <= 3pp）
    double w_joseki = 0.0;    // 入侵方视角：摆完定式线后的胜率
    double w_best = 0.0;      // 入侵方视角：引擎自选最优一手的胜率
    std::string note;         // 人读："定式成立（引擎验证，胜率差 1.2pp）"
};

// 共享 verdict 存储（§4.4.1 方案 A 的「等价共享 verdict 存储」）：advisor 异步路径仲裁后
// 按局面快照缓存 verdict；同步工具环路（joseki_lookup / handleDemoTool）据此读取，形成
// 「预仲裁注入」的数据通路。无 KataGo 时保持为空 → apply 安全降级 textbook（P5）。
class JosekiVerdictStore {
public:
    void put(const Board& board, const JosekiVerdict& v);
    bool get(const Board& board, JosekiVerdict* out) const;
    void clear();
    std::size_t size() const { return map_.size(); }

private:
    std::map<std::string, JosekiVerdict> map_;
};

// L5 三模式执行决策。
enum class DemoAction { Apply, Textbook, Refuse };

struct DemoDecision {
    DemoAction action = DemoAction::Refuse;
    std::vector<JosekiMove> sequence;  // 投影+方向镜像后的可摆线（Apply/Textbook 时非空）
    std::string variant_id;
    std::string announce;              // textbook 前提声明 / refuse 原因
    std::string note;                  // 胜率标注
};

// 三模式分流（纯逻辑，可单测）。`mode`：apply / textbook / auto（缺省或空 = auto）。
// apply 门禁闭合（§4.4.1）：L4 仲裁是异步的，工具环路只消费已注入 verdict——
// 无 verdict 时请求 apply 一律降级 textbook，绝不未经仲裁摆线。
DemoDecision decideJosekiDemo(const Board& b, const JosekiEntry& e, Corner c, Color invader,
                              const std::string& mode, const JosekiVerdict* verdict);

// L4 引擎仲裁（异步，复用 S0.3 Probe）：在 board 副本上摆完 projected_seq → 读
// W_joseki（入侵方视角）；同局面引擎自选最优 → 读 W_best；胜率差 <= 3pp 判「成立」。
// 无引擎 / 序列为空 / 摆线非法 → 立即回调 computed=false。
void arbitrateJoseki(Probe& probe, const Board& board, Color invader,
                     const std::vector<JosekiMove>& projected_seq,
                     std::function<void(const JosekiVerdict&)> cb);

// 判定阈值（§4.4）：引擎最优与定式线的胜率差 <= 3 个百分点视为「定式成立」。
constexpr double kJosekiMargin = 0.03;

}  // namespace vgo
