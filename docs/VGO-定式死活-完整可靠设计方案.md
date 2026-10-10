# VGO · 定式与死活 完整可靠设计方案（v1 策略 + v2 可靠性 整合版）

| 项 | 内容 |
|---|---|
| 范围 | 定式演示/应用（Joseki）、死活常型判断（L3）、死活题生成与求解（L4） |
| 整合自 | `VGO-L3-L4详细解决方案.md`（v1，策略与数据设计）+ `VGO-定式死活-可靠性增强方案v2.md`（v2，可靠性架构） |
| 背景 | v1 定式最小闭环实测两处失败：① 空角点三三（前提失明）；② 接应子/路径子不改变定式行为（上下文失明）。L3/L4 已全量回滚，本文是回陆的唯一依据文档 |
| 读者 | 实施 Agent / 工程评审者。本文自包含，标注了代码锚点、数据 schema、测试矩阵与验收 Done |
| 版本 | v3.0（整合定稿） |

---

## 第一部分 · 历史教训与可靠性原则

### 1.1 症状与根因（为什么 v1 必然失败）

实测现象：让 AI 演示点三三，它从空盘下一整个开局后才有概率点三三；修复后，又在空无一物的角部直接点三三；路径上/路径外有接应子时行为毫无变化。四层根因：

1. **LLM 没有定式知识源**：`resources/skills/` 仅 stub 包，`sgf_lookup_position` 只返回本局手顺，无定式词典。LLM 只能靠自身常识硬猜坐标，而本地 7B 模型的围棋常识最弱。
2. **演示工具只能固定"一手"，不能表达"一条线"**：`demonstrate_go_moves` 仅 `point`+`plies` 参数，固定一手后 `collectAiMoves()`（MainWindow.cpp L1532-1551）逐手由 KataGo 自择优——从空盘出发走的就是布局而非定式。
3. **前提条件是 prompt 文本，不是机器谓词**：v1 词典的 `trigger: "对方星位占角…"` 只是注入 prompt 的自然语言，程序链路（`joseki_lookup` → `demonstrate_go_moves sequence`）上**没有任何一处对真实 Board 做前提校验**。7B 模型在"用户要求演示"的顺从压力下忽略前提是大概率事件。
4. **单条 canonical 线覆盖不了四维上下文**：

   | 上下文维度 | 对定式的影响 | v1 的处理 |
   |---|---|---|
   | 前提子（对方星位占角） | 决定定式是否**存在** | 无 → 症状① |
   | 路径占用（序列点上有子） | 定式**无法原样走出** | `Game probe` 报错，无备选行为 |
   | 侧翼接应（左右邻子） | **挡的方向镜像**、变局部策略（连扳/长/改挂角/夹击） | 无 → 症状② |
   | 全局配置（外势朝向、征子） | 定式**便宜了哪方** | 无 |

### 1.2 本质结论

> VGO 全部既有设计（Grounding → Verify-then-Speak → 校验闸门 → 坏样本回收）建立在同一条纪律上：**真值由程序供给，结论由程序校验，LLM 只负责叙述**。v1 是第一次违反该纪律——把适用性判断外包给了 LLM。回滚正确；本方案把纪律找回来。

修复方向不是"让模型学棋"，而是**把定式与死活常型做成新的可校验真值源**。

### 1.3 五条可靠性原则（全文贯穿，评审与验收的准绳）

- **P1 前提即代码**：任何定式/常型的前提条件必须是对真实 `Board` 求值的机器谓词，不是给模型的提示词。**LLM 永远不做适用性判断。**
- **P2 知识即变体树**：词典条目不是一条线，而是「前提 + 变体集 + 方向规则 + 教学注释」，每个变体有自己的适用条件。
- **P3 局部规则、全局仲裁**：方向/前提这类**局部确定性判断**用规则（可单测）；"此局面该定式便不便宜"这类**全局判断**不写成规则，一律交 KataGo 仲裁——规则清单随"多面情况"无限增长的路是脆的，不走。
- **P4 三模式分流**：实战应用 / 教学演示 / 结构化拒绝，由程序按谓词求值结果分流，LLM 只负责把分流结果讲清楚。空角想看定式是**合法需求**——走教学模式摆前提再演示，而不是在真实棋盘上硬下。
- **P5 无引擎降级为"只教不战"**：未接 KataGo 时只允许教学模式（标准形演示），实战应用一律诚实拒绝——降级链上**安全优先于功能**。

---

## 第二部分 · 总体架构

```
用户提问「点三三 / 定式 / 这块棋死活 / 来道死活题」
  │
  ▼
S0 共享基础设施（先行，全部纯逻辑 + 替身引擎可测）
  · D4 对称变换工具（canonicalize / 角部投影）
  · 谓词引擎（board_predicates）
  · probe 协议（双向 what-if 引擎验证，只复制 Board）
  │
  ├── 线 A：定式 v2（五层）
  │     L1 谓词求值 + 上下文分类 → L2 词典 v2 → L3 变体选择/方向解析
  │     → L4 引擎仲裁（胜率差 ≤3pp）→ L5 三模式执行（apply/textbook/refuse）
  │
  ├── 线 B：死活常型 L3（四层）
  │     L1 眼位提取/假眼识别 → L2 常型库（8 对称 bitmask 查表）
  │     → L3 要点候选排序 → L4 引擎验证（双向 probe，四态合成）
  │
  └── 线 C：死活题 L4
        题库（生成器为主，JSON 模型）→ 三层判题（树内秒回→树外引擎裁判≤12 手→诚实兜底）
        → 三级提示（复用 L3 要点）→ UI 标签页 → 画像/复习联动
```

三条线共享 S0 设施；定式线 L4 仲裁与死活线 L4 验证共用同一 probe 协议。

---

## 第三部分 · S0 共享基础设施（F0 可靠性框架）

> 这是回滚后**最先落地**的部分，三条线全部依赖它。全部纯逻辑、无 Qt、无 I/O，进 CTest。

### 3.1 对称变换工具（`src/ai/symmetry.h/.cpp`）

- 二面体群 D4：8 对称（4 旋转 × 2 镜像）。
- 用途 A（定式）：角部映射用 **4 对称**子群，**UR（右上）帧为 canonical 基准**，词典全部存 UR 帧 19 路 GTP 坐标，加载时 `canonicalize()` 防御规范化一次，运行时投影到目标角。
- 用途 B（死活常型）：形状规范化用**全 8 对称**——眼位点集 → 平移归一 → bitmask → 8 对称取最小哈希 → 查表。
- **坐标帧往返测试必须进 CTest**（v1 工程稿曾出现 UR 标注配 BL 帧坐标的自相矛盾，照抄四角即镜像错位）。

### 3.2 谓词引擎（`src/ai/board_predicates.h/.cpp`）

```cpp
struct PrecondReport {
    bool satisfied = false;
    std::vector<std::string> missing;      // 缺什么（人读，供 LLM 叙述："右上角没有对方星位子"）
    std::vector<Point> occupied_path;      // 路径占用点
};
PrecondReport checkPreconditions(const Board& b, const JosekiEntry& e, Corner c);

struct CornerContext {
    int left_friendly = 0, left_enemy = 0;
    int right_friendly = 0, right_enemy = 0;
    bool path_clear = true;
};
CornerContext classifyContext(const Board& b, const JosekiEntry& e, Corner c, Color invader);
```

谓词种类（封闭集合，不允许扩张成 DSL）：`required_stones`（指定点指定色，含 role/label）、`required_empty`、`forbidden`、`zone_friendly_majority` / `zone_enemy_majority`（侧翼分区计数）、`path_clear`、`ladder_favorable`（旗标，见 §4.4）。

### 3.3 probe 协议（双向 what-if 引擎验证，`src/ai/probe.h/.cpp`）

从原 L3-B 抽出的公共依赖，定式仲裁与死活验证共用：

- 复用 `EnginePool::analyzePrioritized`（engine_pool.h L80，Interactive 优先级）与 `MoveEvaluator` 的"棋盘副本试下"模式；
- **只复制 `Board`，不复制整局 `Game`**（evaluate.cpp L107 的教训：收官时 Game 含 ~300KB 历史）；
- 异步、有界（交换手数上限、超时诚实降级）；结果按 position hash 缓存（复用 explain_cache 模式）；
- 测试用**替身引擎**（确定性假 analyze），保证 CTest hermetic。

### 3.4 演示工具三模式拆分（MainWindow 层）

`demonstrate_go_moves` 重构为三段可组合原语 + `mode` 参数（`apply`/`textbook`/`auto`，缺省 auto）：

| 原语 | 职责 |
|---|---|
| `setupPreconditions()` | 在 **Variation 试下板**上程序化放置前提子（如 anchor 星位子），复用 `enterDemoAndPlace`（MainWindow.cpp L1448）的 probe 校验；**从不写真实 Game**（N2 既有约束），退出即弃 |
| `playFixedSequence(sequence)` | 逐手 `variation_.commitStep` 摆固定线（v1 已有，保留），每手走 `Game probe(b, to_move)` 合法性校验（L1543 模式） |
| `collectAiMoves(plies)` | 固定线摆完后的引擎续下（既有，L1532） |

`auto` 分流逻辑：L1 谓词求值 → satisfied 走 apply 流，否则走 refuse 流（见 §4.5）。

---

## 第四部分 · 线 A：定式 v2（五层）

### 4.1 词典 v2（`resources/joseki/*.json`，双源：代码内嵌 builtin + 热更）

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

硬性约定：

- **坐标基准**：全部 UR 帧 19 路 GTP；`size` 与当前棋盘不符 → 工具层诚实回退「该定式适用于 19 路棋盘」；
- **`when` 子句是微型谓词**，仅 4 个保留字：`default` / `zone_friendly_majority` / `zone_enemy_majority` / `ladder_favorable`，由 L1 求值，**不允许自由文本**；
- 首批 **12-15 条**（星位：点三三、小飞挂、一间高挂、双飞燕；小目：小飞挂·秀策尖、托退、一间夹；三三/高目常见应接），每条 1-2 变体，**质量优先于数量**。

### 4.2 L1 谓词求值与上下文分类

见 §3.2。对目标角跑 `checkPreconditions` + `classifyContext`，产出 `PrecondReport` 与 `CornerContext`。

### 4.3 L3 变体选择与方向解析（纯逻辑）

- 变体筛选：`when` 谓词求值 → 可用变体集（0/1/多个）；
- 方向规则求值：`block_toward_friendly_side` → 比较两翼 zone 己方计数 → 选镜像，用 S0 对称投影把 canonical 线映射到选定方向；
- 0 个可用变体 → 结构化拒绝（带 `missing` 明细）；多个 → 交 L4 仲裁排序，或由 LLM 向用户陈列选择。

### 4.4 L4 引擎仲裁（异步，实战模式默认开启）

复用 S0 probe 设施：

1. 副本摆完选定变体线 → 记录终局胜率 `W_joseki`（入侵方视角）；
2. 同局面引擎自选最优一手 → 对应胜率 `W_best`；
3. **判定**：`W_best - W_joseki ≤ 3pp` →「定式成立（KataGo 验证）」；> 3pp →「此局面下该定式不便宜」，改荐 `alternatives_if_rejected` 或引擎最优手，**并照常提供教学模式**；
4. `ladder_check: true` 的变体：B1b（征子模块）落地前**一律强制引擎仲裁**，引擎否决即弃用该变体——**规则层不碰征子**；
5. 无 KataGo → 按 P5 拒绝实战应用，仅教学演示（标注"未经引擎验证"）。

### 4.5 L5 三模式执行

| 模式 | 触发 | 行为 |
|---|---|---|
| **apply 实战应用** | 前提满足（L1）+ 变体选定（L3）+ 引擎认可（L4） | 在当前角逐手摆线，逐步注释，附「KataGo 验证：胜率 X%→Y%」 |
| **textbook 教学演示** | 任何时候可用（含空盘） | `setupPreconditions()` 先摆理想角前提（announce「这是标准形教学演示，已摆出黑星位占角的前提局面」）→ 走线 + 逐手注释 + `teaching.idea/caution` |
| **refuse 结构化拒绝** | 用户要求实战但前提不满足/引擎否决 | 返回结构化原因（`missing`/`occupied_path`/引擎差距），LLM 叙述：「右上角是空角，点三三的前提是对方星位占角，当前不成立。要看标准形我可以教学演示。」并自动附 textbook 选项 |

### 4.6 工具与 prompt 契约

- **`joseki_lookup`** 从「返回一条线」升级为「返回**适用性判定书**」：`{preconditions: PrecondReport, applicable_variants: [...], context: CornerContext, modes_available: [apply/textbook], engine_verdict(若已仲裁)}`——LLM 拿到的每一步结论都是程序算好的，它只负责翻译成人话；
- **新增 `joseki_advise(corner)`**（实战咨询）：对指定角返回「可用定式变体 + 引擎胜率差 + 备选下法」排序列表，覆盖"改变局部策略"场景（有接应时也许该挂不该点）；
- system prompt 注入定式**目录索引**（id+name+aliases，几十条省 token）；能力声明更新为：

> 「定式的适用性由工具判定，你只负责解释工具的判定结果；工具说不可用就如实说明并给替代，不得自行认定定式成立；词典未收录的名字一律回答'这个定式我还没收录'。」

---

## 第五部分 · 线 B：死活常型 L3（四层）

> **与本次失败无关**：两个症状全部出在定式回放链路；死活常型是局部确定性识别 + 引擎验证，设计不变，仅排期后移。

### 5.1 目标与非目标

| 目标（做） | 非目标（不做） |
|---|---|
| 识别围合眼位的死活常型（直三/曲三/丁四/方四/刀五/花五/花六/板六/直四曲四等） | 不做通用 tsumego 求解器（全树搜索是研究级工程） |
| 给出要点（杀棋点/做活点），「为什么做不活」能指到具体点 | 开放局面（无围合眼位）不硬出常型结论——诚实回落引擎验证或 Unknown |
| 引擎 what-if 验证结论 | 不做深算路（v1 为 1-ply 双向探测） |
| 解锁 B6 死活类「要点」档、支撑 L4 题目生成与提示 | 盘角曲四、双活、劫活 v1 识别为特殊/未决，v2 处理 |

### 5.2 L1：眼位提取与假眼识别（纯逻辑）

给定目标棋块 `G`（`Board::group()` / `Board::liberties()` 现成）：

1. 收集 `G` 的气与紧邻空点；对每个相邻空点 flood-fill 得连通空区 `R`；
2. **围合判定**：`R` 的边界子全部（或 ≥90%）为 `G` 方颜色 → `R` 是眼位区域；混入少量对方子 → 标记 `contaminated` 降置信；棋盘边/角天然围合；
3. **假眼识别**（单子眼位）：单点眼 `p` 为真眼 ⟺ 对角邻点己方控制数达标——**中央 ≥3/4，边 ≥2/3，角 ≥2/2**；对角对方子可被立即提掉的按己方计；
4. **直判活**：≥2 个单子真眼，或眼位匹配活形（直四/曲四/板六）→ `Alive, PatternOnly`。

### 5.3 L2：常型库（数据驱动，双源）

形状规范化：眼位点集 → 平移归一 → bitmask → 8 对称最小哈希 → 查表。数据文件 + 内嵌双源（`resources/lifeshapes/*.json` 热更 + 代码内嵌，与 skills 同构）：

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

> ⚠️ 回陆必修（此前工程稿评审发现的必修 3）：常型示例 pattern 曾把**梅花五标成"刀五"且含负坐标**——F2 回陆时先修数据再合入，canonical 坐标必须非负且平移归一。

### 5.4 L3：要点候选排序（纯逻辑）

1. 常型命中：直接用表内要点（nakade 类常型杀/做同点，双方视角共用）；
2. 未命中启发式（按优先级）：**分割势最大点**（试占后眼位被切成两块的点数乘积最大）→ 区域形心（腰带点）→ 角部二·1；
3. 输出 ≤3 个候选，供引擎验证与提示系统。

### 5.5 L4：引擎验证协议（复用 S0 probe）

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

```cpp
enum class LDStatus { Alive, Dead, Unsettled, Unknown };
enum class LDVerify { PatternOnly, EngineVerified, None };

struct LifeDeathResult {          // explain.h L43-48，向后兼容追加
    // —— 现有字段保留 ——
    std::vector<Point> group;
    bool alive = true;
    std::vector<Point> saving_moves;
    std::string reason;
    // —— L3 新增 ——
    LDStatus status = LDStatus::Unknown;   // alive 字段由其派生
    LDVerify verification = LDVerify::None;
    std::string shape;                     // 常型名（"刀五"），空=未识别
    std::vector<Point> eye_space;          // 眼位区域（UI 高亮）
    std::vector<Point> kill_points;        // 杀棋要点（已验证优先）
    std::vector<Point> live_points;        // 做活要点（已验证优先）
};
```

| 位置 | 改动 |
|---|---|
| `AiAdvisor::groundFromAnalysis`（advisor.cpp L312） | 对 dead_groups 及用户点名棋块跑 L1/L2 识别，填新字段 |
| `AiAdvisor::handleLifeDeath`（L289） | 死活提问：先常型 → 需要时触发引擎验证（Interactive）再 `deliverAnswer` |
| `life_death_solve` 工具（agent_tools.cpp L65-118） | 返回新增 `shape/kill_points/live_points/status/verification` |
| `renderLifeDeath`（explain.cpp L187） | 升级文案：「这块棋是**刀五**，白走到 **X** 点即死；黑补 **Y** 可活（KataGo 已验证）」 |
| B6 递进提示（P2 批） | 死活类 tier-2「要点」= `kill_points`/`live_points`，**L3 落地即解锁** |

---

## 第六部分 · 线 C：死活题 L4

### 6.1 题源三路（按优先级）

| 路 | 内容 | 本期 |
|---|---|---|
| ① 内置题库 | **生成器为主**：`tools/tsumego_gen.cpp` 用 L3 常型库在角/边摆常型局面，自动生成题面+解树，引擎离线验证后入库；手工精选 20-40 题（古典《玄玄棋经》公有领域可重录；**现代 SGF 汇编有版权风险**，首批以自研为主） | ✅ |
| ② 对局挖掘 | 复用恶手采集管线（`maybeWarnBlunder`，MainWindow.cpp L1611+）：某块棋随后死亡 → 回溯死亡前局部 → 裁切 → 引擎验证可杀/可活且解唯一 → 入个人题库（"你自己的死活题"） | Phase C |
| ③ 引擎合成 | KataGo self-play 批量挖矿 | v2 暂缓 |

### 6.2 题目数据模型（JSON，不用 SGF 树）

**决策**：JSON（`resources/tsumego/*.json` + `board_snapshot` 复用 D3.5 快照格式）。理由：`parseSgf` 只取主变线、跳过变分支（sgf.h 注释明示），扩展 SGF 树解析属 L7——用 JSON 避免 L4 被 L7 阻塞；SGF 导入作为 v2 与 L7 同做。

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
TsumegoJudge judgeTsumegoMove(const TsumegoProblem& p,
                              const std::vector<Move>& path, Move user);
```

1. **树内判题（无引擎，秒回）**：`good` → 自动从 `by:对方` 分支走出应一手；`bad` → 判负展示 `comment`；叶节点且目标达成 → `Solved`；
2. **树外引擎裁判（异步，Interactive）**：`OffTree` → 副本试下用户手 → KataGo 强档应手 → 有界交换（≤12 手或目标棋块归属稳定）→ 终态 `deadGroups` 对照 `goal` 判胜负；**歧义/超时诚实回答**「这手棋超出题库变化，引擎也一时判不清」；
3. **提示三级**：档 1 方向（「要点在眼位中心一带」）→ 档 2 要点（L3 的 `kill_points`/`live_points` 区域高亮，不给精确点）→ 档 3 答案（演示解树主线）。

### 6.4 UI（新「死活题」标签页）

- 题库浏览：按主题/难度筛选，显示完成状态与最佳成绩；
- 答题棋盘：限制交互（只能落子，复用 D3.5 动态重建棋盘模式）；树/引擎自动应手；
- 答案演示：复用 N2 `VariationController` + `VariationPanel` 逐手走解树（零新增基础设施）；
- 提示按钮三档递进；失败重试/下一题；每主题统计（尝试/通过率）。

### 6.5 难度标定与画像联动

- v1 静态标签：主题 + 解树深度（直三 ~18k，刀五/花五 ~12k，角部对杀 ~7k），生成器自动打标；v2 用户通过率回流校准（入 `LearnerStore`）；
- 答题结果写画像：死活主题失败 → `recordBlunder(BlunderEvent{WeakArea::LifeDeath, severity})` 同源加重；错题入 SM-2 复习队列，隔日重现于「每日一题」队列尾部；隐私开关关闭时不落任何统计。

---

## 第七部分 · 多面情况处理矩阵（验收即按此表逐行测）

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

## 第八部分 · 测试与验收（可靠性是第一需求）

### 8.1 CTest（纯逻辑，全部 hermetic）

| 测试 | 覆盖 |
|---|---|
| `test_symmetry.cpp` | D4 群封闭性、**坐标帧往返锁死**、canonicalize 幂等 |
| `test_board_predicates.cpp` | 前提谓词（有/缺 anchor、路径占用、forbidden）、zone 分类、方向规则求镜像 |
| `test_joseki_v2.cpp` | 词典加载（含坐标帧防御）、变体筛选（when 四谓词）、拒绝报告字段齐全 |
| `test_demo_modes.cpp`（替身引擎） | 三模式分流（#1/#2/#3/#5/#8）、教学模式先摆前提再走线、apply 引擎否决降级 |
| `test_lifedeath_shape.cpp` | 九常型逐个识别+要点、假眼三档对角规则、两真眼直判活、方四直判死、污染眼位降置信、对称等价 |
| `test_tsumego_judge.cpp` / `test_tsumego_load.cpp` | 树内四路（good/bad/OffTree/Solved）、路径回溯、JSON schema 校验、快照重建 |

### 8.2 离线（不进 CTest）

- 定式仲裁准确率对拍：构造"定式宜/不宜"各 10 局真实配置，KataGo 深度复核仲裁结论；
- 死活常型对拍：精选 20-30 真实局面（含劫/双活/开放形），常型+probe vs KataGo 深度分析；
- `tools/tsumego_gen` 产物批量引擎复核（每题"首手要点成立"）。

### 8.3 对抗性手测（本次失败的直接教训，必做）

- 空盘「直接给我点三三」「现在点三三怎么样」→ 必须走 #1/#2，**不得硬下**；
- 前提满足但路径故意摆一子 → 必须 #5；
- 左右翼分别摆接应子 → 方向必须镜像正确（#4）；
- 全程开 trace 面板核对：每次分流依据来自谓词求值，而非模型自述；
- 完整做 5 道死活题（对/错/树外/提示/答案演示全路径）。

### 8.4 验收 Done 定义

1. 矩阵 14 行逐行通过；对抗提示无一次"空角硬点三三"；
2. 「为什么做不活」能答「这是刀五，对方走 X 点即死，你补 Y 可活（已验证/按常型）」；
3. 首批 ≥40 题入库且引擎复核通过；树内判题秒回、树外裁判有界；提示三档不提前泄答案；错题入复习队列；
4. CTest 全绿且无新增联网测试；未配置 KataGo/LLM 的降级路径（只教不战 + 常型直判 + 树内判题）完整可用。

---

## 第九部分 · 回陆顺序与排期

```
F0 可靠性框架（S0 全部：对称工具 + 谓词引擎 + probe 协议 + 演示三模式拆分 + 替身引擎测试）
  │  无依赖，~1 周
  ├──→ F1 定式 v2（词典 12-15 条 + joseki_lookup 判定书 + joseki_advise + L4 仲裁 + 矩阵 #1-#10）
  │      ~1.5 周
  └──→ F2 L3 回陆（L1 眼位/假眼 + L2 常型库 + L3 要点 + 工具/render 接入 + 引擎验证；先修常型数据标注）
         2-3 周；落地即解锁 B6 死活要点档
         └──→ F3 L4 回陆（题库+生成器 ≥40 题 + 三层判题 + 三级提示 + UI + 画像联动）
                L4-A 2-3 周（依赖 F2）→ L4-B 1-2 周（裁判复用 S0 probe）→ L4-C 持续（对局挖掘、难度 v2、SGF 导入依赖 L7）
```

建议把 L4 从 P2 **升格为 P1**（用户实测痛点 + F2 的直接消费者 + 教学闭环关键拼图）。

## 第十部分 · 风险与对策

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

---

## 附 · 纪律清单（任何实现不得违反）

1. CTest 只进离线密闭纯逻辑（引擎验证用替身引擎）；
2. 未配置 KataGo/LLM 时降级链不劣化：定式只教不战、死活常型直判、死活题树内判题全部可用；
3. 技能/数据双源：代码内嵌 builtin + `resources/` 热更；
4. 纯逻辑层无 Qt、无 I/O；
5. **适用性判断不进 prompt，只进谓词**（P1，本次失败换来的新纪律）；
6. 冲突以引擎为准，坏样本落 `bad_samples.jsonl` 回收。
