# VGO · L3/L4 技术路线与实现方案（v2.0，可靠性重写版）

| 项 | 内容 |
|---|---|
| 输入 | `docs/VGO-定式死活-完整可靠设计方案.md`（v3.0 专家报告，可靠性整合定稿） |
| 产出 | 三条线（定式 v2 / L3 死活常型 / L4 死活题）+ 共享基础设施 S0 的工程级实现路线 |
| 基线 | v1.1 路线及其失败实现已回滚；本文是**回陆的唯一依据文档** |
| 版本 | v2.1 |
| 修订 | v2.0：依据专家报告 v3.0 全量重写。定式线从「最小闭环」升级为「五层 v2（谓词 + 变体树 + 引擎仲裁 + 三模式）」，新增 `board_predicates` 与 `probe` 共享设施，纳入 14 行多面情况矩阵与回陆顺序 F0–F3。v2.1（评审后）：M1 修正「回滚不干净」误述（§1.2 重写 + 开工前 `git diff d29b96f` 核实门）；M2 新增 §4.4.1 apply 门禁闭合（预仲裁注入，无 verdict 一律降级 textbook）；另采纳空序列变体只作建议、probe 双向汇合防竞态、替身引擎列 F0 首件 |

---

## 0. 目的与读法

本文把专家报告 v3.0 的「做什么」落成「改哪个文件、加哪个函数、接哪个管线、写哪些测试」。三条线**全部复用既有管线，不新增管线**；纯逻辑落在 Qt-free 的 `vgo_ai`，数据加载与 UI 落在 Qt 层——这正是项目「纯逻辑可单测 + 数据可热更」的既有纪律。

**核心设计决策（全文贯穿，即专家报告的可靠性原则 P1–P5 的工程化表述）**：

- **D1（=P1 前提即代码）**：定式/常型的适用条件必须是对真实 `Board` 求值的**机器谓词**，不是注入 prompt 的自然语言。**LLM 永远不做适用性判断。** 本次失败的直接教训——v1 把「是否适用」外包给了模型，导致空角硬点三三、路径子/接应子失明。
- **D2（=P2 知识即变体树）**：定式词典条目不是一条线，而是「前提 + 变体集 + 方向规则 + 教学注释」，每个变体有自己的 `when` 谓词。
- **D3（=P3 局部规则、全局仲裁）**：方向/前提这类**局部确定性判断**用规则（可单测）；「此局面该定式便不便宜」这类**全局判断**不写成规则，一律交 KataGo 仲裁——规则清单随多面情况无限增长的路是脆的，不走。
- **D4（=P4 三模式分流）**：`apply` 实战应用 / `textbook` 教学演示 / `refuse` 结构化拒绝，由程序按谓词求值结果分流，LLM 只负责把分流结果讲清楚。空角想看定式是**合法需求**——走 textbook 摆前提再演示，而不是在真实棋盘上硬下。
- **D5（=P5 无引擎降级为「只教不战」）**：未接 KataGo 时只允许 textbook（标准形演示），实战应用一律诚实拒绝；死活只出 `PatternOnly`。
- **D6 数据双源**：所有数据（定式词典、常型库、题目）都做成「代码内嵌 builtin + `resources/**/*.json` 热更」双源，与 `SkillRegistry::builtin()` + `MainWindow::loadSkills()` 同构。
- **D7 同步/异步边界**：`Tool::invoke` 是同步的（`agent_tools.h:29-32` 已明示 `evaluate_move` 因异步被排除）。因此**纯逻辑（谓词/L1/L2/L3、判题）进同步工具，引擎验证（定式仲裁、死活 probe、树外裁判）只进 advisor 的异步路径**，作为硬事实注入 prompt。
- **D8 只复制 Board，不复制 Game**：所有 what-if 试下复用 `evaluate.cpp` 的教训（收官 Game 含 ~300KB 历史）。

---

## 1. 代码基线回源（接入点锚定 + 回滚后处置）

> **定位纪律**：行号仅为本次回源快照，会随提交漂移；实现时一律**以符号名（函数/类型）定位**，行号只作参考。⚠️ 因回滚不干净（见 §1.2），下表行号取自**带 v1.1 残留的工作区**，非干净 master——`buildSystemPrompt` 在 `origin/master` 为 L28、残留工作区为 L82 即是一例。

### 1.1 接入点锚定

| 关注点 | 位置 | 现状（已验证） |
|---|---|---|
| 死活纯逻辑 | `src/ai/lifedeath.h/.cpp` | 仅 `deadGroups` / `savingCandidates` / `analyzeLifeDeath`，无眼位/常型/假眼 |
| 死活结果模型 | `src/ai/explain.h:57` `LifeDeathResult` | 已含 L3 新字段 `status/verification/shape/eye_space/kill_points/live_points`（失败尝试已加，保留） |
| 死活渲染 | `src/ai/explain.cpp` `renderLifeDeath` | 一句话文案，无常型名/要点 |
| 死活提问异步路径 | `src/ai/advisor.cpp` `handleLifeDeath` / `groundFromAnalysis` | Interactive 优先级 analyze → 填 `dead_groups` + `life_death` |
| 同步工具注册 | `src/ai/agent/agent_tools.cpp` `buildAgentTools` | `life_death_solve` / `sgf_lookup_position` 等 |
| 工具上下文 | `src/ai/agent/agent_tools.h:15` `AgentToolContext` | `{Game, OwnershipMap, Analysis, joseki}`（`joseki` 字段失败尝试已加） |
| 演示工具 | `src/ui/MainWindow.cpp:1688` `handleDemoTool` | 只支持 `point/color/plies/reset/undo_to`（无 `sequence`/`mode`） |
| AI 续下 | `src/ui/MainWindow.cpp:1635` `collectAiMoves` | 逐手 `requestDemoMoveSync`，引擎自择优（根因 2） |
| 演示落子校验 | `src/ui/MainWindow.cpp:1551` `enterDemoAndPlace` | `Game probe(b, play)` + `variation_.commitStep` |
| 固定线助手 | `src/ui/MainWindow.h:89` `playFixedSequence` | 失败尝试已加（逐手摆定线），**保留复用** |
| 恶手→每日一题 | `src/ui/MainWindow.cpp:1754` `maybeWarnBlunder` | `reviewItemFromBlunder` + `addReviewItem` |
| 技能热更 | `src/ui/MainWindow.cpp` `loadSkills` | 三目录 `*.md` → `SkillRegistry::fromText` → `http->setSkills` |
| 系统提示词 | `src/ai/conversation_http/http_conversation.cpp:82` `buildSystemPrompt` | 能力声明 + claims 契约 + 技能注入点 |
| 工具声明 | `src/ai/conversation_http/http_conversation.cpp` `buildTools` | `demonstrate_go_moves` 硬编码 + ToolRegistry 追加 |
| 引擎接口/优先级 | `src/ai/engine.h:18` `EngineInterface` + `src/ai/engine_pool.h:80` `analyzePrioritized` | Interactive/Background/Idle 已就绪 |
| 模式枚举 | `src/ai/game_setup.h:11` `GameMode` | 已含 `Tsumego`（失败尝试已加，保留） |
| 快照重建 | `src/ai/learner/daily_puzzle.h:24` `boardFromSnapshot` | `Board::snapshot()` 格式 `'X'/'O'/'.'` 行主序 |
| 学习画像 | `src/ai/learner/learner_model.h` | `WeakArea{...LifeDeath, Joseki...}` / `recordBlunder` / `addReviewItem` |
| SGF | `src/ai/sgf.h` `parseSgf` | **只读主变线**（变分支跳过）→ L4 用 JSON 不用 SGF 树的根因 |
| 谓词引擎 / probe 协议 | **不存在** | 需新增（v3.0 的核心新设施） |

### 1.2 回滚真相与残留处置（开工 F0 前必修）

**回滚并不干净**：回滚动作是 `git reset` 到 `d29b96f`（"修复：AI 输出被 max_tokens 截断"），只把 3 个失败提交（`2d7266f` 文档 v1.0、`a067fc5` 文档 v1.1、`9438927` 实现）从**历史**移除；但 `reset`（mixed）**把它们的改动原样留在了工作区**——追踪文件并未回到 master 状态。实测工作区相对 `d29b96f` 仍有 **~532 行未提交残留，横跨 12 个 `src/` 追踪文件**（另含 `CMakeLists.txt`、`README.md`），另有大量未跟踪新文件。§1.1 的行号即取自这个「带残留的工作区」而非干净 master，**一律只作参考、以符号名定位**。

**开工 F0 前必须 `git diff d29b96f` 逐文件核对**，把残留分成「v3.0 保留」与「v1.1 丢弃」两堆，确认无未登记残留（重点：`MainWindow.cpp` +213、`agent_tools.cpp` +167、`advisor.cpp` +50、`http_conversation.cpp` +30）：

| 残留（相对 d29b96f） | v1.1 内容 | v3.0 处置 |
|---|---|---|
| `src/ai/symmetry.h/.cpp`（新） | 8 对称 + `canonicalize` + 角投影 | **复用**，补坐标帧往返 CTest（§3.1） |
| `src/ai/joseki.h/.cpp`（新） | 单条 `sequence` 词典（无前提/变体） | **重写**为变体树词典（§4.1）；`projectToCorner` 保留 |
| `src/ai/lifedeath_shape.h/.cpp`（新） | L1/L2/L3 纯逻辑 | **复用**，先修常型数据标注（§5.2） |
| `src/ai/lifedeath_verify.h/.cpp`（新） | L4 双向 probe（内联） | **重构**：抽出共享 `probe`（§3.3），定式仲裁复用 |
| `src/ai/tsumego.h/.cpp`（新） | 题模型 + 判题 + 加载 | **复用**（v3.0 L4 与 v1.1 一致） |
| `tests/test_symmetry.cpp` 等 6 个（新） | v1.1 测试 | `test_symmetry/test_lifedeath_shape/test_lifedeath_verify/test_tsumego_*` 保留增强；`test_joseki.cpp` 重写为 `test_joseki_v2.cpp` |
| `resources/joseki/*.json`（新） | 单条线 schema | **重写**为变体树 schema（§4.1） |
| `resources/lifeshapes/*.json`、`resources/tsumego/*.json`（新） | 常型/题数据 | 常型数据修正后复用；题目复用 |
| `tools/tsumego_gen.cpp`（新） | 生成器 | 复用，补「根要点 == vital 反投影」自检（§6.3） |
| `src/ai/explain.h`（+16） | `LifeDeathResult` 新字段 | **保留**（v3.0 §5.6 需要） |
| `src/ai/game_setup.h`（+7） | `GameMode::Tsumego` | **保留**（v3.0 §6.4 需要） |
| `src/ai/agent_tools.h`（+5） | `AgentToolContext.joseki` 字段 | **保留字段、升级语义**：单序列词典 → 判定书/verdict（§4.4.1） |
| `src/ai/agent_tools.cpp`（+167） | `joseki_lookup` 返回单序列 | **重写**为返回适用性判定书（§4.6） |
| `src/ai/http_conversation.cpp`（+30） | `joseki_lookup` 能力声明、`joseki_directory` 注入、`sequence` 参数声明 | `joseki_directory`/`sequence` **保留**；能力声明**改写**为判定书语义；补 `mode` 参数（§4.6） |
| `src/ui/MainWindow.h/.cpp`（+23/+213） | `playFixedSequence`、`loadJoseki`、tsumego UI、`handleDemoTool` sequence 处理 | `playFixedSequence`/`loadJoseki`/tsumego UI **保留**；`handleDemoTool` **重构**为三模式（§3.4） |
| `src/ai/advisor.h/.cpp`（+10/+50） | `setJosekiLibrary`、死活接入 | `setJosekiLibrary` **保留**；补预仲裁注入路径（§4.4.1） |
| `src/ai/conversation.h`（+3） | `joseki_directory` 字段 | **保留** |
| `src/ai/explain.cpp`（+29） | `renderLifeDeath` 升级 | **保留** |
| `tests/test_agent.cpp`（+15） | `joseki_lookup` 测试 | **重写**（对齐判定书返回） |

---

## 2. 三条线依赖总图

```
S0 共享基础设施（先行，全部纯逻辑 + 替身引擎可测）
  · 对称变换工具（复用残留） + 谓词引擎（新增） + probe 协议（新增，从 lifedeath_verify 抽取）
  · 演示工具三模式拆分（MainWindow 层）
        │
        ├──────────────► 线 A：定式 v2（五层）
        │                  L1 谓词求值 + 上下文分类 → L2 词典 v2 → L3 变体选择/方向解析
        │                  → L4 引擎仲裁（胜率差 ≤3pp）→ L5 三模式执行（apply/textbook/refuse）
        │
        └──────────────► 线 B：死活常型 L3（四层）
                             L1 眼位提取/假眼 → L2 常型库（8 对称 bitmask 查表）
                             → L3 要点候选 → L4 引擎验证（双向 probe，四态合成）
                             └──► 支撑线 C 生成器与提示
        │
        └──────────────► 线 C：死活题 L4
                             题库（生成器为主，JSON）→ 三层判题（树内→树外引擎裁判→诚实兜底）
                             → 三级提示（复用 L3 要点）→ UI 标签页 → 画像/复习联动
```

三条线共享 S0 设施；定式线 L4 仲裁与死活线 L4 验证共用同一 `probe` 协议。

---

## 3. 共享基础设施 S0（先行，A/B/C 共用）

> 回陆后**最先落地**，三条线全部依赖它。全部纯逻辑、无 Qt、无 I/O，进 CTest。

### 3.1 对称变换工具 `src/ai/symmetry.h/.cpp`（复用残留，补测试）

正方形的 8 个对称（二面体群 D4）：4 旋转 × 2 镜像。内点 `(x,y)` 0-indexed，`x` 列（左→右）、`y` 行（上→下），`size = N`。残留文件已实现，接口基本满足 v3.0，**只需补测试**：

```cpp
enum class Symmetry { Id, Rot90, Rot180, Rot270, MirrorX, MirrorY, MirrorDiag, MirrorAntiDiag };
Point applySymmetry(Symmetry s, Point p, int size);
enum class Corner { UR, UL, BL, BR };
Symmetry cornerSymmetry(Corner target);   // UR→UR:Id；UR→UL:MirrorX；UR→BL:Rot180；UR→BR:MirrorY
struct CanonicalShape { std::vector<Point> points; Symmetry used; int dx, dy; /* + 逆变换元数据 */ };
CanonicalShape canonicalize(const std::vector<Point>& pts);
std::string shapeKey(const std::vector<Point>& canonical_pts);
Point canonicalToBoard(const CanonicalShape& cs, Point canonical);  // 反投影（常型要点回棋盘）
```

- **用途 A（定式）**：角部映射用 **4 对称**子群，**UR（右上）帧为 canonical 基准**，词典全部存 UR 帧 19 路 GTP 坐标，加载时 `canonicalize()` 防御规范化一次，运行时 `cornerSymmetry(target)` 投影到目标角；`by` 颜色不变。
- **用途 B（常型）**：形状规范化用**全 8 对称**——眼位点集 → 平移归一 → 8 对称取最小哈希 → 查表；命中后把库内 canonical 要点用逆变换反投影回真实棋盘。
- **坐标帧往返测试必须进 CTest**（v1 工程稿曾出现 UR 标注配 BL 帧坐标的自相矛盾，照抄四角即镜像错位）。

### 3.2 谓词引擎 `src/ai/board_predicates.h/.cpp`（**新增**）

这是 v3.0 最核心的新设施，根治 v1「前提失明」的直接落点：

```cpp
struct PrecondReport {
    bool satisfied = false;
    std::vector<std::string> missing;      // 缺什么（人读，供 LLM 叙述："右上角没有对方星位子"）
    std::vector<Point> occupied_path;      // 路径占用点（人读："R17 已有子"）
};
PrecondReport checkPreconditions(const Board& b, const JosekiEntry& e, Corner c);

struct CornerContext {
    int left_friendly = 0, left_enemy = 0;
    int right_friendly = 0, right_enemy = 0;
    bool path_clear = true;
};
CornerContext classifyContext(const Board& b, const JosekiEntry& e, Corner c, Color invader);
```

**谓词种类（封闭集合，不允许扩张成 DSL）**：`required_stones`（指定点指定色，含 role/label）、`required_empty`、`forbidden`、`zone_friendly_majority` / `zone_enemy_majority`（侧翼分区计数）、`path_clear`、`ladder_favorable`（旗标，见 §4.4）。

- 输入是**真实 `Board`**（不是 prompt 文本）与 canonical 条目；`Corner` 参数决定对称投影到哪个角再求值。
- 该引擎同时驱动定式线 L1 与演示 `auto` 分流（§3.4），是 P1「前提即代码」的唯一实现点。

### 3.3 probe 协议 `src/ai/probe.h/.cpp`（**新增**，从 `lifedeath_verify` 抽取）

从原 L3-B 抽出的公共依赖，定式仲裁（线 A L4）与死活验证（线 B L4）共用：

- 复用 `EnginePool::analyzePrioritized`（`engine_pool.h:80`，Interactive 优先级）与 `MoveEvaluator` 的「棋盘副本试下」模式；
- **只复制 `Board`，不复制整局 `Game`**（`evaluate.cpp` 的教训：收官 Game 含 ~300KB 历史）；
- 异步、有界（交换手数上限、超时诚实降级）；结果按 position hash 缓存（复用 explain_cache 模式）；
- 测试用**替身引擎**（确定性假 analyze），保证 CTest hermetic；
- **双向汇合语义（防竞态）**：死活验证的攻/守两方向 probe 独立异步，靠「世代号 + 请求计数」汇合——每次 `verify` 递增 `generation_`，两方向回调各自携带发起时的世代号，**只有两个回调都带当前世代才合成四态结论**，迟到/过期回调直接丢弃。残留 `lifedeath_verify` 的 `generation_` 模式即此用途，抽出到 `probe` 后保留。

```cpp
class Probe {
public:
    using Callback = std::function<void(const ProbeResult&)>;
    // 在副本上落 probe_move → analyze → 返回 {winrate, dead_groups}；超时/无引擎诚实降级。
    void probe(const Board& board, Color to_move, const Point& probe_move, Callback cb);
    void cancel();
};
```

`lifedeath_verify` 重构为 `Probe` 的消费者（不再内联分析调用）；定式仲裁新写一个消费者。

### 3.4 演示工具三模式拆分（MainWindow 层）

`demonstrate_go_moves` 重构为三段可组合原语 + `mode` 参数（`apply`/`textbook`/`auto`，缺省 `auto`）：

| 原语 | 职责 |
|---|---|
| `setupPreconditions()` | 在 **Variation 试下板**上程序化放置前提子（如 anchor 星位子），复用 `enterDemoAndPlace` 的 probe 校验；**从不写真实 Game**（N2 既有约束），退出即弃 |
| `playFixedSequence(sequence)` | 逐手 `variation_.commitStep` 摆固定线（失败尝试已实现，`MainWindow.h:89`，保留），每手走 `Game probe(b, to_move)` 合法性校验 |
| `collectAiMoves(plies)` | 固定线摆完后的引擎续下（既有，`MainWindow.cpp:1635`） |

`auto` 分流逻辑：L1 谓词求值 → `satisfied` 走 apply 流，否则走 refuse 流（§4.5）。`textbook` 强制先 `setupPreconditions()` 再走线；`apply` 在当前角摆线。

> **apply 门禁（见 §4.4.1）**：`apply` 分支不得在工具环路内自行求值引擎仲裁——仲裁是异步的，`handleDemoTool` 只消费已注入的 `engine_verdict`，**无 verdict 一律降级 textbook，绝不摆线**。

---

## 4. 线 A：定式 v2（五层）

> 范围：常见定式词典 + 前提校验 + 引擎仲裁 + 三模式演示。**不做**棋谱库位置索引（C6 完整版维持立项）。

### 4.1 词典 v2 `resources/joseki/*.json`（双源，schema 重写）

每文件一条定式。schema 从「一条 `sequence`」升级为「前提 + 变体集 + 方向规则 + 教学注释」：

```json
{
  "id": "hoshi-sansan-invasion",
  "name": "点三三",
  "aliases": ["三三入侵", "点33", "点三·三"],
  "size": 19,
  "applies_to_formation": "对方星位占角",
  "preconditions": {
    "required_stones": [
      {"point": "R16", "color": "black", "role": "corner_anchor", "label": "对方星位子"}
    ],
    "required_empty": ["R17", "Q17", "S17", "Q16", "S16", "R15"],
    "forbidden": [],
    "zones": {
      "left_flank":  {"points": ["Q14","Q13","Q12","R14","R13","R12"], "meaning": "左侧翼"},
      "right_flank": {"points": ["S14","S13","S12","T14","T13","T12"], "meaning": "右侧翼"}
    }
  },
  "variants": [
    {
      "id": "main",
      "name": "挡后长（常型）",
      "when": "default",
      "direction_rule": "block_toward_friendly_side",
      "sequence": [
        {"move": "R17", "by": "white", "note": "点三三，夺角"},
        {"move": "Q17", "by": "black", "note": "挡（方向由 direction_rule 镜像决定）"},
        {"move": "Q18", "by": "white", "note": "爬，求活关键"}
      ]
    },
    {
      "id": "reverse-block",
      "name": "反向挡（右侧有己方接应时取势）",
      "when": {"zone_friendly_majority": "right_flank"},
      "sequence": []
    },
    {
      "id": "ladder-variant",
      "name": "含征子变化",
      "when": {"ladder_favorable": true},
      "ladder_check": true,
      "sequence": []
    }
  ],
  "alternatives_if_rejected": ["approach-knight", "approach-high"],
  "teaching": {
    "idea": "星位角空虚时直接点三三夺实地，对方得外势",
    "caution": "点三三过早让外势朝向对方接应方向时亏损；征子关系不利时部分变化不成立"
  },
  "source": "builtin"
}
```

**硬性约定**：

- **坐标基准**：全部 UR 帧 19 路 GTP；`size` 与当前棋盘不符 → 工具层诚实回退「该定式适用于 19 路棋盘」；
- **`when` 子句是微型谓词**，仅 4 个保留字：`default` / `zone_friendly_majority` / `zone_enemy_majority` / `ladder_favorable`，由 L1 求值，**不允许自由文本**；
- 首批 **12-15 条**（星位：点三三、小飞挂、一间高挂、双飞燕；小目：小飞挂·秀策尖、托退、一间夹；三三/高目常见应接），每条 1-2 变体，**质量优先于数量**；
- **空 `sequence` 变体语义**：`sequence: []` 的变体（如 `reverse-block` / `ladder-variant`）**只作策略建议，不进演示线**——它表达「此上下文该改变局部策略」，由 `joseki_advise` 陈列给 LLM 叙述，不产生可逐手摆的定式线。凡进 apply/textbook 演示的变体必须 `sequence` 非空；空序列变体命中时只改写 `applicable_variants` 的「建议」而非「摆线」。

纯逻辑结构（`joseki.h` 重写）：

```cpp
struct JosekiStone { Point point; Color color; std::string role, label; };
struct JosekiZone { std::string name; std::vector<Point> points; std::string meaning; };
struct JosekiPreconditions {
    std::vector<JosekiStone> required_stones;
    std::vector<Point> required_empty;
    std::vector<Point> forbidden;
    std::map<std::string, JosekiZone> zones;   // left_flank / right_flank
};
struct JosekiVariant {
    std::string id, name, when;                 // "default" | "zone_friendly_majority:right_flank" | ...
    bool ladder_check = false;
    std::string direction_rule;                 // "block_toward_friendly_side" | ""
    std::vector<JosekiMove> sequence;           // canonical UR 帧
};
struct Teaching { std::string idea, caution; };
struct JosekiEntry {
    std::string id, name, applies_to_formation, source;
    std::vector<std::string> aliases;
    int size = 19;
    JosekiPreconditions preconditions;
    std::vector<JosekiVariant> variants;
    std::vector<std::string> alternatives_if_rejected;
    Teaching teaching;
};
class JosekiLibrary { /* builtin() / fromJsonText() / upsert() / find() / directoryIndex() */ };
std::vector<JosekiMove> projectToCorner(const std::vector<JosekiMove>& seq, int size, Corner c);
```

### 4.2 L1 谓词求值与上下文分类

见 §3.2。对目标角跑 `checkPreconditions` + `classifyContext`，产出 `PrecondReport` 与 `CornerContext`。

### 4.3 L3 变体选择与方向解析（纯逻辑）

- 变体筛选：`when` 谓词求值 → 可用变体集（0/1/多个）；
- 方向规则求值：`block_toward_friendly_side` → 比较两翼 zone 己方计数 → 选镜像，用 S0 对称投影把 canonical 线映射到选定方向；
- 0 个可用变体 → 结构化拒绝（带 `missing` 明细）；多个 → 交 L4 仲裁排序，或由 LLM 向用户陈列选择。

### 4.4 L4 引擎仲裁（异步，实战模式默认开启）

复用 S0 `probe` 设施：

1. 副本摆完选定变体线 → 记录终局胜率 `W_joseki`（入侵方视角）；
2. 同局面引擎自选最优一手 → 对应胜率 `W_best`；
3. **判定**：`W_best - W_joseki ≤ 3pp` →「定式成立（KataGo 验证）」；> 3pp →「此局面下该定式不便宜」，改荐 `alternatives_if_rejected` 或引擎最优手，**并照常提供教学模式**；
4. `ladder_check: true` 的变体：B1b（征子模块）落地前**一律强制引擎仲裁**，引擎否决即弃用该变体——**规则层不碰征子**；
5. 无 KataGo → 按 P5 拒绝实战应用，仅教学演示（标注"未经引擎验证"）。

### 4.4.1 apply 门禁闭合（预仲裁注入，必修）

apply 的三闸（L1 谓词 + L3 变体 + L4 仲裁）里，前两闸是纯逻辑可同步求值，但 **L4 仲裁是异步的**（D7：引擎验证只进 advisor 异步路径）。而 LLM 的实际调用是**同步工具环路**（`joseki_lookup` → `handleDemoTool`），`handleDemoTool` 走到 apply 分支时不能自己等仲裁。若让工具在无仲裁结果时照摆线，就形成「LLM 工具直达 apply、未经仲裁」的绕过路径——v1 失败换形态复活，**必须闭合**。

**方案 A（预仲裁注入）**：

1. **仲裁只发生在 advisor 异步路径**（不违反 D7）：advisor 在投递给 LLM 之前，对「用户点名定式且 L1/L3 判定可用」的局面跑 `Probe` 仲裁，产出 `engine_verdict`（按 position hash 缓存）；
2. **注入工具上下文**：把 `engine_verdict` 注入 `AgentToolContext.joseki`（或等价共享 verdict 存储），使同步工具环路能读到已算好的结论；
3. **`handleDemoTool(mode=apply)` 只消费已注入的 verdict**：有 verdict 且「成立」→ 摆线 + 标注胜率；有 verdict 且「不便宜」→ refuse + 荐 alternatives；**无 verdict → 一律降级 textbook（安全默认），绝不摆线**；
4. 无 KataGo → 无 verdict 可注入 → apply 永远落到 textbook（与 P5 一致）。

**新增测试（CTest，替身引擎）**：无 verdict 注入 + 请求 apply → 必须降级 textbook，**不得摆线**；有 verdict「成立」→ 摆线；有 verdict「不便宜」→ refuse。

### 4.5 L5 三模式执行

| 模式 | 触发 | 行为 |
|---|---|---|
| **apply 实战应用** | 前提满足（L1）+ 变体选定（L3）+ 引擎认可（L4） | 在当前角逐手摆线，逐步注释，附「KataGo 验证：胜率 X%→Y%」 |
| **textbook 教学演示** | 任何时候可用（含空盘） | `setupPreconditions()` 先摆理想角前提（announce「这是标准形教学演示，已摆出黑星位占角的前提局面」）→ 走线 + 逐手注释 + `teaching.idea/caution` |
| **refuse 结构化拒绝** | 用户要求实战但前提不满足/引擎否决 | 返回结构化原因（`missing`/`occupied_path`/引擎差距），LLM 叙述：「右上角是空角，点三三的前提是对方星位占角，当前不成立。要看标准形我可以教学演示。」并自动附 textbook 选项 |

### 4.6 工具与 prompt 契约

- **`joseki_lookup`** 从「返回一条线」升级为「返回**适用性判定书**」：`{preconditions: PrecondReport, applicable_variants: [...], context: CornerContext, modes_available: [apply/textbook], engine_verdict(若已仲裁)}`——LLM 拿到的每一步结论都是程序算好的，它只负责翻译成人话；
- **新增 `joseki_advise(corner)`**（实战咨询，异步路径）：对指定角返回「可用定式变体 + 引擎胜率差 + 备选下法」排序列表，覆盖"改变局部策略"场景（有接应时也许该挂不该点）；
- system prompt 注入定式**目录索引**（id+name+aliases，几十条省 token，复用 `ConversationRequest.joseki_directory` 注入点 + `AiAdvisor::setJosekiLibrary`）；能力声明更新为：

> 「定式的适用性由工具判定，你只负责解释工具的判定结果；工具说不可用就如实说明并给替代，不得自行认定定式成立；词典未收录的名字一律回答'这个定式我还没收录'。」

### 4.7 测试与验收

- `tests/test_joseki_v2.cpp`（CTest，重写自 `test_joseki.cpp`）：词典加载（含坐标帧防御、`when` 四谓词解析）、变体筛选、`projectToCorner` 8 对称往返（含 **UR→四角→UR 恒等**，锁死坐标帧）、`size` 不匹配拒绝、`fromJsonText` 非法 JSON 报错、`directoryIndex` 只含索引。
- `tests/test_board_predicates.cpp`（CTest）：前提谓词（有/缺 anchor、路径占用、forbidden）、zone 分类、方向规则求镜像。
- `tests/test_demo_modes.cpp`（CTest，替身引擎）：三模式分流（空角 textbook、空角实战 refuse、前提满足 apply、路径占用 refuse、无引擎降级）、教学模式先摆前提再走线、apply 引擎否决降级、**无 verdict 注入 + 请求 apply → 必须降级 textbook 且不摆线（apply 门禁闭合，§4.4.1）**。

---

## 5. 线 B：死活常型 L3（四层）

> **与本次失败无关**：两个症状全部出在定式回放链路；死活常型是局部确定性识别 + 引擎验证，设计不变，仅排期后移。残留文件 `lifedeath_shape` / `lifedeath_verify` 基本可复用。

### 5.1 目标与非目标

| 目标（做） | 非目标（不做） |
|---|---|
| 识别围合眼位的死活常型（直三/曲三/丁四/方四/刀五/花五/花六/板六/直四曲四等） | 不做通用 tsumego 求解器（全树搜索是研究级工程） |
| 给出要点（杀棋点/做活点），「为什么做不活」能指到具体点 | 开放局面（无围合眼位）不硬出常型结论——诚实回落引擎验证或 Unknown |
| 引擎 what-if 验证结论 | 不做深算路（v1 为 1-ply 双向探测） |
| 解锁 B6 死活类「要点」档、支撑 L4 题目生成与提示 | 盘角曲四、双活、劫活 v1 识别为特殊/未决，v2 处理 |

### 5.2 L1 眼位提取与假眼识别（纯逻辑，复用 `lifedeath_shape`）

给定目标棋块 `G`（`Board::group()` / `Board::liberties()` 现成）：

1. 收集 `G` 的气与紧邻空点；对每个相邻空点 flood-fill 得连通空区 `R`；
2. **围合判定**：`R` 的边界子全部（或 ≥90%）为 `G` 方颜色 → `R` 是眼位区域；混入少量对方子 → 标记 `contaminated` 降置信；棋盘边/角天然围合；
3. **假眼识别**（单子眼位）：单点眼 `p` 为真眼 ⟺ 对角邻点己方控制数达标——**中央 ≥3/4，边 ≥2/3，角 ≥2/2**；对角对方子可被立即提掉的按己方计；
4. **直判活**：≥2 个单子真眼，或眼位匹配活形（直四/曲四/板六）→ `Alive, PatternOnly`。

### 5.3 L2 常型库（数据驱动，双源，复用 + 修数据）

形状规范化：眼位点集 → 平移归一 → 8 对称最小哈希 → 查表。数据文件 + 内嵌双源（`resources/lifeshapes/*.json` 热更 + `LifeShapeLibrary::builtin()`）：

| 常型 | 眼位点 | 状态 | 要点（杀=做同点） | 讲解词 |
|---|---|---|---|---|
| 直三 | 3 一线 | 先手死 | 中点 | 「直三，一点正中即死」 |
| 曲三 | 3 拐弯 | 先手死 | 弯中点 | 「曲三，要害在弯处」 |
| 丁四 | 4 丁字 | 先手死 | 丁字中心 | 「丁四，点中心成刀五/曲三杀」 |
| 方四 | 4 方块 | **死形**（先走也不活） | — | 「方四是死形，不用走了」 |
| 直四 | 4 一线 | 活 | — | 「直四是活棋」 |
| 曲四 | 4 拐弯 | 活（角上盘角曲四特例 v2） | — | 「曲四是活棋」 |
| 刀五（刀把五） | 5 | 先手死 | 刀把中心 | 「刀五，一点聚杀」 |
| 梅花五（花五） | 5 | 先手死 | 花心 | 「花五，点花心」 |
| 葡萄六（花六） | 6 | 先手死 | 中心 | 「花六，中心是要点」 |
| 板六 | 6 二路线 | 活（角上板六特例 v2） | — | 「板六是活棋」 |

未知形状 → `Unknown`，进 L3 启发式 + L4 引擎验证。

> ⚠️ **回陆必修（此前工程稿评审发现的必修 3）**：常型示例 pattern 曾把**梅花五标成"刀五"且含负坐标**——回陆时先修数据再合入，canonical 坐标必须非负且平移归一。十字（中心+四臂）是**花五**而非刀五，刀五是 2×3 缺角。`fromJsonText` 必须对 `pattern` 做防御性 `canonicalize()` 再入库，`vital` 同帧变换。

### 5.4 L3 要点候选排序（纯逻辑，复用）

1. 常型命中：直接用表内要点（nakade 类常型杀/做同点，双方视角共用）；
2. 未命中启发式（按优先级）：**分割势最大点**（试占后眼位被切成两块的点数乘积最大）→ 区域形心（腰带点）→ 角部二·1；
3. 输出 ≤3 个候选，供引擎验证与提示系统。

### 5.5 L4 引擎验证协议（复用 S0 probe）

```
攻方 probe（≤3 要点候选 v）:
    副本 Board 攻方落 v → analyze → deadGroups() 含 G？ → 成立记 kills_at = v
守方 probe（≤3 守点候选 d，来自要点 + savingCandidates）:
    副本 Board 守方落 d → analyze → deadGroups() 不含 G？ → 成立记 lives_at = d
```

**结论合成**：

| kills_at | lives_at | 结论 | 讲解 |
|---|---|---|---|
| 有 | 有 | **先手死**（Unsettled-先后手相关） | 「对方走 X 你死，你先走 Y 可活——谁先来谁赚」 |
| 有 | 无 | **死**（EngineVerified） | 「白走 X 即死，任何防守都救不活」 |
| 无 | 有 | **活**（EngineVerified） | 「你走 Y 即活」 |
| 无 | 无 | **未决** | 诚实回答「这个局部引擎一时判不清」 |

- 常型直判与引擎冲突时**以引擎为准**，冲突样本落 `bad_samples.jsonl` 反哺常型库；
- 劫/双活：v1 不判定，probe 结果振荡时落 `Unsettled` 标注「疑似劫/特殊型」；
- 无 KataGo → 只出 L1/L2 结论，`verification=PatternOnly`，措辞"按常型判断……（未经验证）"。

### 5.6 数据模型与接入点

`LifeDeathResult`（`explain.h:57`）的新字段**失败尝试已加，保留**（`status/verification/shape/eye_space/kill_points/live_points`）。接入点：

| 位置 | 改动 |
|---|---|
| `AiAdvisor::groundFromAnalysis`（advisor.cpp） | 对 dead_groups 及用户点名棋块跑 `analyzeShape`，填新字段 |
| `AiAdvisor::handleLifeDeath` | 死活提问：先常型 → 需要时 `LifeDeathVerifier::verify`（Interactive）再 `deliverAnswer` |
| `life_death_solve` 工具（agent_tools.cpp） | 返回新增 `shape/kill_points/live_points/status/verification`；anchor 路径改为纯 Board 即可跑（无引擎降级 PatternOnly），不再因 ownership 为空而硬失败 |
| `renderLifeDeath`（explain.cpp） | 升级文案：「这块棋是**刀五**，白走到 **X** 点即死；黑补 **Y** 可活（KataGo 已验证）」 |
| B6 递进提示（P2 批） | 死活类 tier-2「要点」= `kill_points`/`live_points`，**L3 落地即解锁** |

### 5.7 测试

- `tests/test_lifedeath_shape.cpp`（CTest）：九常型逐个识别+要点、假眼三档对角规则、两真眼直判活、方四直判死、污染眼位降置信、对称等价。
- `tests/test_lifedeath_verify.cpp`（CTest，替身引擎）：覆盖四态结论表（重构到 `probe` 后仍四态齐全）。

---

## 6. 线 C：死活题 L4

### 6.1 题源三路（按优先级）

| 路 | 内容 | 本期 |
|---|---|---|
| ① 内置题库 | **生成器为主**：`tools/tsumego_gen.cpp` 用 L3 常型库在角/边摆常型局面，自动生成题面+解树，引擎离线验证后入库；手工精选 20-40 题（古典《玄玄棋经》公有领域可重录；**现代 SGF 汇编有版权风险**，首批以自研为主） | ✅ |
| ② 对局挖掘 | 复用恶手采集管线（`maybeWarnBlunder`，MainWindow.cpp:1754）：某块棋随后死亡 → 回溯死亡前局部 → 裁切 → 引擎验证可杀/可活且解唯一 → 入个人题库（"你自己的死活题"） | Phase C |
| ③ 引擎合成 | KataGo self-play 批量挖矿 | v2 暂缓 |

### 6.2 题目数据模型（JSON，不用 SGF 树）

**决策**：JSON（`resources/tsumego/*.json` + `board_snapshot` 复用 `Board::snapshot()` 格式）。理由：`parseSgf` 只取主变线、跳过变分支（`sgf.h` 注释明示），扩展 SGF 树解析属 L7——用 JSON 避免 L4 被 L7 阻塞；SGF 导入作为 v2 与 L7 同做。残留 `tsumego.h` 的 `TsumegoNode`/`TsumegoProblem`/`TsumegoLibrary` 结构满足要求，**复用**。

```json
{
  "id": "nakade-blade5-01",
  "theme": "nakade",
  "size": 9,
  "snapshot": ".....XX...",
  "to_move": "black",
  "goal": "kill",
  "target_group": ["D4", "E4", "F4", "E5", "F5"],
  "solution_tree": [
    {"move": "E4", "verdict": "good", "comment": "刀五要点，一点聚杀",
     "children": [
       {"move": "D5", "by": "white", "children": [
         {"move": "C5", "verdict": "good", "comment": "白做不出两眼"}]}]},
    {"move": "D5", "verdict": "bad", "comment": "走错，白 E4 做活",
     "children": [{"move": "E4", "by": "white", "children": []}]}
  ],
  "difficulty": "15k",
  "source": "builtin"
}
```

### 6.3 判题（三层，先树后引擎，诚实兜底）

```cpp
enum class TsumegoJudge { Correct, Wrong, OffTree, Solved };
TsumegoJudgement judgeTsumegoMove(const TsumegoProblem& p,
                                  const std::vector<std::pair<Color, Move>>& path, Move user);
```

1. **树内判题（无引擎，秒回）**：`good` → 自动从 `by:对方` 分支走出应一手；`bad` → 判负展示 `comment`；叶节点且目标达成 → `Solved`；
2. **树外引擎裁判（异步，Interactive）**：`OffTree` → 副本试下用户手 → KataGo 强档应手 → 有界交换（≤12 手或目标棋块归属稳定）→ 终态 `deadGroups` 对照 `goal` 判胜负；**歧义/超时诚实回答**「这手棋超出题库变化，引擎也一时判不清」；
3. **提示三级**：档 1 方向（「要点在眼位中心一带」）→ 档 2 要点（L3 的 `kill_points`/`live_points` 区域高亮，不给精确点）→ 档 3 答案（演示解树主线）。

生成器自检（纯逻辑可进 CTest）：每题目标棋块存在、解树根为要点、**解树根要点与 `LifeShapeLibrary` vital 反投影一致**（锁死常型数据错误的第二道闸）、JSON schema 合法。**引擎验证不进 CTest**。首批 40-60 题（自研生成为主 + 手工精选公有领域古典题）。

### 6.4 UI（新「死活题」标签页）

- `game_setup.h:11` 的 `GameMode::Tsumego` 已加（失败尝试保留）；`MainWindow.cpp` 标签页索引映射加 `case 4`。
- 完全镜像「每日一题」既有模式（`daily_game_/daily_problem_/daily_ready_/daily_solved_` 与 `enterDailyPuzzle/onDailyAnswer/skipDailyProblem`），新增 `tsumego_game_/tsumego_problem_/tsumego_path_` 等成员。
- **答题棋盘**：复用 `boardFromSnapshot` 动态重建棋盘，限制交互（只能落子）；树/引擎自动应手。
- **题库浏览**：主题/难度筛选 + 完成状态（存 `LearnerStore`）。
- **答案演示**：复用 `VariationController` + `VariationPanel` 逐手走解树（现成试下状态机，零新增基础设施）。
- **提示按钮**：三档递进（§6.3）。

### 6.5 难度标定与画像联动

- v1 静态标签：主题 + 解树深度（直三 ~18k，刀五/花五 ~12k，角部对杀 ~7k），生成器自动打标；v2 用户通过率回流校准（入 `LearnerStore`）。
- 答题结果写画像：死活主题失败 → `recordBlunder(BlunderEvent{WeakArea::LifeDeath, severity})` 同源加重；错题入 SM-2 复习队列，隔日重现于「每日一题」队列尾部；隐私开关关闭时不落任何统计。

### 6.6 测试

- `tests/test_tsumego_judge.cpp`（CTest）：good/bad/OffTree/Solved 四路、路径回溯、多分支。
- `tests/test_tsumego_load.cpp`（CTest）：JSON 解析、`boardFromSnapshot` 重建、字段校验、生成器产物自检。

---

## 7. 多面情况处理矩阵（验收即按此表逐行测）

| # | 场景 | 谓词求值 | 系统行为 |
|---|---|---|---|
| 1 | 空盘/空角，「演示点三三」 | 前提不满足 | textbook：摆 anchor + 走线 + 声明"标准形教学演示" |
| 2 | 空角，「在这点三三」（实战语义） | 前提不满足 | refuse：「空角不适用，前提=对方星位占角」+ 附 textbook 选项 |
| 3 | 对方星位占角、路径净、无侧翼子 | 满足，default 变体 | 引擎仲裁 → 成立则 apply 主线 |
| 4 | 前提满足 + 右侧己方接应 | `zone_friendly_majority=right` | 选 reverse-block 变体/主线镜像（L3 规则）→ 仲裁 → apply |
| 5 | 前提满足 + 路径点被占 | `path_clear=false` | refuse：「R17 已有子，标准定式走不出」+ 建议引擎分析当前局面 |
| 6 | 前提满足但全局不宜（外势送对方厚势） | 满足 | L4 仲裁差距 >3pp →「此局面点三三不便宜」+ 荐 alternatives/引擎最优 + textbook 仍可看 |
| 7 | 含征子变体且 B1b 未落地 | `ladder_check` | 强制引擎仲裁，否决即弃该变体（规则层不碰征子） |
| 8 | 未接 KataGo | — | P5：实战一律拒绝，只 textbook（标注未验证）；死活只 PatternOnly |
| 9 | 9 路棋盘查 19 路定式 | size 不符 | 「该定式适用于 19 路棋盘」（上游诚实回退，不到演示层） |
| 10 | 词典未收录的名字 | 查无 | 「这个定式我还没收录」 |
| 11 | 死活：围合眼位命中常型 | shape 匹配 | 常型直判 + 要点，标注 PatternOnly 或 EngineVerified |
| 12 | 死活：开放局面/未知形状 | 无匹配 | Unknown → 引擎 probe 兜底 → 仍未决则诚实说"判不清" |
| 13 | 死活题：树内 good/bad | — | 秒回 + 自动应手/判负讲评 |
| 14 | 死活题：树外一手 | OffTree | 引擎裁判 ≤12 手有界交换；超时诚实兜底 |

---

## 8. 测试矩阵汇总

| 测试 | 层级 | 位置 | 网络/引擎 |
|---|---|---|---|
| `test_symmetry.cpp` | 纯逻辑 | CTest（vgo_ai_tests） | 无 |
| `test_board_predicates.cpp` | 纯逻辑 | CTest | 无 |
| `test_joseki_v2.cpp` | 纯逻辑 | CTest | 无 |
| `test_demo_modes.cpp` | 纯逻辑（替身引擎） | CTest | 无（替身） |
| `test_lifedeath_shape.cpp` | 纯逻辑 | CTest | 无 |
| `test_lifedeath_verify.cpp` | 纯逻辑（替身引擎） | CTest | 无（替身） |
| `test_tsumego_judge.cpp` / `test_tsumego_load.cpp` | 纯逻辑 | CTest | 无 |
| 生成器产物自检 | 纯逻辑 | CTest（`test_tsumego_load.cpp` 内） | 无 |
| 离线对拍（死活 20-30 局） | 集成 | `tools/integration_check.cpp` 扩展 | KataGo（离线，不进 CTest） |
| 定式仲裁准确率对拍（宜/不宜各 10 局） | 集成 | 离线 | KataGo（离线） |
| 题库引擎复核 | 集成 | `tools/tsumego_gen.cpp` + KataGo | KataGo（离线） |
| 点三三回归 / 完整做 5 题 | 手测 | 验收清单 | KataGo + LLM |

**铁律**：CTest 全绿且**无新增联网测试**；未配置 KataGo/LLM 的降级路径（只教不战 + 常型直判 + 树内判题）完整可用。

---

## 9. 回陆顺序与排期

```
F0 可靠性框架（S0 全部：**① 替身引擎（全部替身测试的隐藏关键路径，最先交付）** → 对称工具补测 + 谓词引擎 + probe 协议 + 演示三模式拆分 + apply 门禁闭合 + 替身测试）
  │  无依赖，~1 周
  ├──→ F1 定式 v2（词典 12-15 条 + joseki_lookup 判定书 + joseki_advise + L4 仲裁 + 矩阵 #1-#10）
  │      ~1.5 周
  └──→ F2 L3 回陆（L1 眼位/假眼 + L2 常型库 + L3 要点 + 工具/render 接入 + 引擎验证；先修常型数据标注）
         2-3 周；落地即解锁 B6 死活要点档
         └──→ F3 L4 回陆（题库+生成器 ≥40 题 + 三层判题 + 三级提示 + UI + 画像联动）
                L4-A 2-3 周（依赖 F2）→ L4-B 1-2 周（裁判复用 S0 probe）→ L4-C 持续（对局挖掘、难度 v2、SGF 导入依赖 L7）
```

建议把 L4 从 P2 **升格为 P1**（用户实测痛点 + F2 的直接消费者 + 教学闭环关键拼图）。

---

## 10. 风险与对策

| 风险 | 对策 |
|---|---|
| 变体/zones 数据建模工作量膨胀 | 首批 12-15 条、每条 1-2 变体；`when` 谓词锁死 4 保留字，不扩张成 DSL |
| 引擎仲裁增加延迟 | Interactive 优先级 + 并发池；textbook 不仲裁（秒回）；结果按 position hash 缓存 |
| 谓词覆盖不全导致误拒/误放 | 误拒可接受（诚实降级 textbook/引擎分析）；误放由 L4 仲裁兜底；坏样本照常回收 |
| 常型库覆盖度有限（只对围合眼位） | 开放局面诚实落 Unknown + probe 兜底；呈现永远标 verification 来源，不装懂 |
| ownership 中盘噪声导致 probe 抖动 | 双向 probe 投票；阈值沿用 0.8；冲突落坏样本回收 |
| 盘角曲四/双活/劫误判 | v1 显式识别为特殊/Unsettled，不给死活结论；v2 专项 |
| 教学模式摆前提污染真实对局 | 前提摆设只发生在 Variation 试下板（从不写真实 Game），退出即弃 |
| 用户把「教学演示」误当「当前可用」 | textbook 每次强制 announce 前提声明；UI 演示面板标题区分「标准形演示」与「实战定式」 |
| 题库版权 | 首批自研生成器 + 公有领域古典题手工重录；不搬运现代 SGF 汇编 |
| 残留文件照单全收回退 | 回陆按 §1.2 处置表逐项复用/重写/删除，`test_joseki.cpp` 必须重写为 `test_joseki_v2.cpp` |

---

## 11. 里程碑验收清单（Done 定义）

1. **多面情况矩阵 14 行逐行通过**；对抗提示无一次「空角硬点三三」——空盘「直接给我点三三」「现在点三三怎么样」必须走 textbook/refuse，不得硬下。
2. **定式 v2**：前提满足时 apply 主线 + 引擎仲裁标注；空角走 textbook（先摆前提再走线 + announce）；路径占用/前提缺失走 refuse 并附替代；左右翼接应子使方向镜像正确；**apply 门禁闭合——无预仲裁 verdict 时请求 apply 一律降级 textbook，绝不未经仲裁摆线**。
3. **L3**：九常型单测全绿；「为什么做不活」能答「这是刀五，对方走 X 点即死，你补 Y 可活（已验证/按常型）」；B6 死活要点档可用。
4. **L4**：首批 ≥40 题入库且引擎复核通过；树内判题秒回、树外引擎裁判有界；提示三档递进不提前泄答案；错题入复习队列。
5. **CTest 全绿且无新增联网测试**；未配置 KataGo/LLM 的降级路径（只教不战 + 常型直判 + 树内判题）完整可用。

---

## 附 A：新增/改动文件清单

**新增（纯逻辑，进 `vgo_ai`）**
- `src/ai/symmetry.h/.cpp`（复用残留，补测试）
- `src/ai/board_predicates.h/.cpp`（**新增**，谓词引擎）
- `src/ai/probe.h/.cpp`（**新增**，共享 probe 协议）
- `src/ai/joseki.h/.cpp`（**重写**，变体树词典）
- `src/ai/lifedeath_shape.h/.cpp`（复用残留，修常型数据）
- `src/ai/lifedeath_verify.h/.cpp`（**重构**，消费 probe）
- `src/ai/tsumego.h/.cpp`（复用残留）

**新增（工具/离线）**
- `tools/tsumego_gen.cpp`（复用残留，补自检）

**新增（数据资源，热更）**
- `resources/joseki/*.json`（**重写**为变体树 schema）、`resources/lifeshapes/*.json`（修数据）、`resources/tsumego/*.json`

**改动（Qt-free）**
- `src/ai/explain.h`（`LifeDeathResult` 扩展，保留）、`src/ai/explain.cpp`（`renderLifeDeath`）
- `src/ai/advisor.h/.cpp`（`setJosekiLibrary`、`groundFromAnalysis`、`handleLifeDeath`、`joseki_advise` 异步路径）
- `src/ai/agent/agent_tools.h/.cpp`（`AgentToolContext.joseki`、`joseki_lookup` 判定书、`life_death_solve` 升级）
- `src/ai/conversation.h`（`ConversationRequest.joseki_directory`）

**改动（Qt 层）**
- `src/ai/conversation_http/http_conversation.cpp`（能力声明 + 目录注入 + `sequence`/`mode` 工具声明）
- `src/ui/MainWindow.h/.cpp`（`loadJoseki`、`setupPreconditions`、`playFixedSequence`、`handleDemoTool` 扩展三模式、死活题标签页、`GameMode::Tsumego`）
- `src/ai/game_setup.h`（`GameMode::Tsumego` 保留）

**改动（构建/测试）**
- `CMakeLists.txt`（新增 `board_predicates/probe` 源 + 测试目标 `test_symmetry/test_board_predicates/test_joseki_v2/test_demo_modes/test_lifedeath_shape/test_lifedeath_verify/test_tsumego_*`）

## 附 B：纪律清单（任何实现不得违反）

1. CTest 只进离线密闭纯逻辑（引擎验证用替身引擎）；
2. 未配置 KataGo/LLM 时降级链不劣化：定式只教不战、死活常型直判、死活题树内判题全部可用；
3. 技能/数据双源：代码内嵌 builtin + `resources/` 热更；
4. 纯逻辑层无 Qt、无 I/O；
5. **适用性判断不进 prompt，只进谓词**（P1，本次失败换来的新纪律）；
6. 冲突以引擎为准，坏样本落 `bad_samples.jsonl` 回收。
