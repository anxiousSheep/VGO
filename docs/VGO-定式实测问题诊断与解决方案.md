# VGO · F1 实测问题诊断与解决方案（点三三第二轮测试）

| 项 | 内容 |
|---|---|
| 输入 | 用户第二轮实测：中国流 + 白小飞挂被黑一间夹局面下的五组现象 |
| 回源 | 最新 master（b8eebbc，F0/F1 已合入）：joseki_apply.cpp / agent_tools.cpp joseki_lookup / joseki.cpp builtin 词典 / http_conversation.cpp 分级契约 |
| 总判断 | **v2 可靠性架构（谓词→变体→仲裁→门禁）工作正常**：判定书输出正确、refuse 理由正确、apply 门禁闭合生效。五个现象的根因全部在**架构之外的三层**：① grounding 缺空间/着法事实（几何缺陷、术语幻觉）；② 词典数据不充盈（zones 失明、变体空壳、线截断）；③ 呈现层契约不严（内部参数泄露、口头禅、指代消歧）。**不需要回滚，需要五个针对性补丁** |
| 版本 | v1.0 |

---

## 现象 ①：形状判断错误（R17 说成"扳"，还引申出"逢扳必断/逢扳必扳"）

### 根因（代码证据）

**着法几何识别（B1a）从未落地**。`joseki_lookup` 与 `extractBoardFacts` 均不供给任何着法术语——R17 相对黑 M17（隔一路、不同线）是"点入"而非"扳"（扳=贴身接触后拐），这个判断目前**完全由 7B 模型对两串坐标做心算**。心算失败 → 术语瞎猜 → 为自圆其说再引用错误棋谚（"逢扳必扳"）。此前评审已把 B1a 拆为独立项（几何着法：扳/断/尖/飞/跳/虎，纯局部几何），本次实测证明它是**讲解可信度的第一阻塞项**。

### 解决方案 G1：着法几何分类器（B1a 落地，纯逻辑）

新增 `src/ai/move_shape.h/.cpp`（纯逻辑，进 CTest）：

```cpp
// 对候选着 p 相对参照子 q 的几何关系分类。
// 参照子自动选取：距 p 最近的异色子（或调用方指定）。
enum class MoveShape {
    Contact,    // 贴（扳/挡/顶：正交相邻）
    Diagonal,   // 尖/觑（对角相邻）
    OneGap,     // 隔一路（飞/挂/点：曼哈顿距离 2 且平行/斜行）
    TwoGap,     // 隔二路（跳/拆二）
    Extension,  // 同线延伸（长/爬：沿边线离开参照子）
    Other
};
struct ShapeFact {
    MoveShape shape;
    std::string term;        // "扳" / "点" / "小飞挂" / "跳" ...
    std::string relation;    // 人读："R17 与黑 M17 隔一路平行，属点入三线"
};
ShapeFact classifyMove(const Board& b, Point p, Color mover);
```

接入三处：
1. **"该下哪"回答链路**（advisor 的 move-quality 路径）：对引擎推荐点预标注 `ShapeFact`，作为硬事实注入 prompt——LLM 的职责从"判断这是什么手"降级为"转述程序给的术语"；
2. **演示逐手注释**：`demonstrate_go_moves` 每手落子后自动追加 `classifyMove` 结果，note 与术语冲突时以程序为准；
3. **ClaimVerifier 新增术语断言**：LLM 回答中对当前着点使用的术语若与 `classifyMove` 不一致 → 打回重写（regex 提取"扳/断/尖/飞/跳/虎/点/挂/夹"）。

**验收**：同局面问"我该下哪" → 回答为"点入三三"而非"扳"；LLM 自造术语被闸门拦截。

## 现象 ②：棋盘几何位置判断缺陷（天元错、坐标空间感差）

### 根因

与 ①同源：grounding 事实清单里**没有任何空间描述**（区域/线数/相对星位与天元的方位）。LLM 只拿到裸坐标串，被要求自行完成"O17 在哪、离天元多远"的空间推理。

### 解决方案 G2：`describe_point` 空间事实（纯逻辑，与 G1 同文件）

```cpp
struct PointGeo {
    std::string region;   // "右上角" / "上边" / "中央（距天元 2 路）"
    int line;             // 第几线（1=一线…）
    std::string nearby;   // "距星位 Q16 一路" / "即天元"
};
PointGeo describePoint(const Board& b, Point p);
```

- **注入策略**：`extractBoardFacts` 对"局面中所有子的坐标 + 引擎推荐点"批量输出 `PointGeo` 行——每颗子一行 `M17=黑，三线，上边偏右，距右上星位 3 路`。token 成本约每子 15 字，布局阶段 <30 子，可控；
- **"该下哪"与演示链路**同样注入推荐点的 `PointGeo`；
- 单测：四角/边/天元/三三/小目的 `describePoint` 全枚举锁死（19 路与 9 路各一套 golden）。

**验收**：问"天元在哪/这颗子在什么位置"类问题，回答引用注入事实；推荐点讲解中的方位描述（"上边""三线"）与程序一致。

## 现象 ③：指代/记忆错误（问"适合哪些定式"答对了点三三，说"演示这个定式"却拿小飞挂开讲）

### 根因（代码证据）

回源确认：`joseki_lookup` 对**所有**词条（含不适用者）返回完整判定书——小飞挂虽 `satisfied=false`，其 `occupied_path:[O17]`、`strategy_only` 变体、教学注释仍占据大量输出篇幅。下一轮"演示这个定式"时，LLM 要从上一轮 tool 结果里自行消歧"这个"= 点三三——**多轮指代消歧又被外包给了模型**（与 v1 把前提判断外包给模型同构，这次是轻得多的版本）。

### 解决方案 G3：定式指代由程序解析，不让 LLM 凭记忆选名

1. **判定书瘦身**：`joseki_lookup` 增加顶层 `summary`（程序生成的人话一句话，如「点三三：前提满足，可实战演示」），并对**不可用**词条只返回 `{name, summary, applicable: false, reason一句话}`，**不再输出** missing/occupied_path/variants 全量字段——不可用条目在上下文里的存在感降到最低，指代歧义源头直接消除；
2. **对话状态注入**：advisor 维护 `last_joseki_lookup_`（id/name/是否可用），识别到"演示这个定式/就它了"类请求时，把 `上一次定式判定：点三三（可用，apply 模式）` 作为硬事实注入当轮 prompt；
3. **工具描述补丁**：`demonstrate_go_moves` 描述加一句"演示目标定式若在上一轮判定书中已出现，优先使用该定式名，不要自行更换"。

**验收**：完整复现测试剧本（问适用 → 说"演示这个"）→ 演示的是点三三；LLM 换定式名时程序注入纠正。

## 现象 ④：泄露内部参数（"satisfied = false"、"occupied_path: [O17]"原样吐出）

### 根因（代码证据）

`joseki_lookup` 的返回是**裸结构化 JSON**（agent_tools.cpp L277-279 直接序列化 `satisfied`/`occupied_path` 字段名），工具描述只说"你只负责解释判定结果"，**没有禁止转述字段名**。7B 模型把"解释"执行成"逐字段朗读"。

### 解决方案 G4：人话层 + 转述禁令 + 泄露闸门（三层防御）

1. **工具返回加 `summary`**（与 G3 共用）：程序预生成人话——「小飞挂目前不适用：第一步 O17 已被占（就是白棋刚下的那一手）」。prompt 契约改为"**向用户只使用 summary 的表述，禁止转述字段名、JSON 键名或数值参数**"；
2. **工具描述明示**：`joseki_lookup` description 追加"输出中的 preconditions/context/engine_verdict 等字段是给你理解的内部数据，禁止原文出现给用户"；
3. **ClaimVerifier 泄露闸门**（确定性正则，进 CTest）：回答正文匹配 `satisfied|occupied_path|w_joseki|w_best|favorable|strategy_only|verdict` 任一 token → 判违规打回。这一条是硬保险，模型说不听也拦得住。

**验收**：复现场景 → 回答全自然语言；闸门单测含 6 个 token 的正反例。

## 现象 ⑤：一间夹存在时，演示仍把白棋压在二线（黑外势厚所得更多）

### 根因（代码证据，三条）

1. **zones 对夹击位失明**：`hoshi-sansan-invasion` 的 `left_flank`/`right_flank` 全部是 **四线五线点（Q12-Q14、S12-T14）**，而一间夹的夹击子（M17 一带）在**三线**——`classifyContext` 统计两翼时根本数不到这颗子，`direction_rule: block_toward_friendly_side` 拿的是残缺计数，**挡的方向可能选错**（正确棋理：挡向夹击子一侧，让外势与夹子呼应）；
2. **变体空壳**：reverse-block / ladder-variant 两条变体 `sequence` 均为空（`strategy_only`），`firstDemonstrable`（joseki_apply.cpp L20-28）只能兜底退回 main——**夹击场景没有任何专属变体**；
3. **线截断**：main 变体只有 3 手（点、挡、爬），apply 摆完即停，定式远未收束——"白蹲二线被压制"的观感来自**半截定式**，且没有引擎续下完成后续。仲裁摆同一条 3 手线评估，评估的也只是一个残缺中间态。

### 解决方案 G5：词典数据修订 + apply 续下闭环

1. **zones 扩域**：`left_flank`/`right_flank` 各补三线区（左侧 `Q17,Q16…` 挂角/夹击位带，右侧对称），使 `classifyContext` 能看见夹击子与挂角子；同时在 `teaching.caution` 写明"对方有夹击子时，挡的方向应朝夹击子一侧使外势呼应"；
2. **补夹击变体**：新增 `when: {"zone_enemy_majority": "left_flank"}`（或对应翼）的完整变体线——"有夹击时的点三三"（白点入 → 黑朝夹子方向挡 → 白爬活角 → 黑取厚势与夹子连片），sequence 补全至定式收束（8-12 手）；main 线同样补全；
3. **apply 摆线后接引擎续下**：`demonstrate_go_moves` apply 分支在 `playFixedSequence` 后自动 `collectAiMoves(plies)` 续下至定式收束（复用既有原语，参数如 plies=6），避免半截定式直接停在白蹲二线的中间态——**这正是 v2 方案 §5.4 的原始设计，落地时被省掉了，补回**；
4. **仲裁同步升级**：`arbitrateJoseki` 的 step 1 摆线改为"固定线 + 引擎续下 N 手"后的终局胜率，评估的才是定式的完整结果而非中间态。

**验收**：一间夹局面演示点三三 → 黑挡向夹击侧、外势与夹子连片、白活角，讲解明确指出"黑取势白取地，此局面两分"；仲裁 verdict 的胜率差基于完整线。

## 现象 ⑥（附带）：口头禅"一句话结论"

### 根因

回源确认：prompt 正文**不含**"一句话结论"字样（仅代码注释与 quality_judge 内部字段），这是模型对分级结构契约（http_conversation.cpp L57"先用一句话给出明确结论"，**每轮注入**）的标签化复读——指令被模型当作输出模板复述。

### 解决方案 G6（一行修）

契约措辞改为"先直接给出明确结论"，并追加一句"不要把结构说明中的任何字样（如'一句话''结构'）当作标题或引导语复述"。若仍有残留，泄露闸门（G4-3）正则加 `一句话结论`。

---

## 落地顺序与工作量

| 序 | 内容 | 类型 | 工作量 |
|---|---|---|---|
| G6 | 契约措辞 + 口头禅 | prompt | 0.5 天（立即可做） |
| G4 | summary 层 + 转述禁令 + 泄露闸门 | 契约+闸门 | 1-2 天 |
| G3 | 判定书瘦身 + last_joseki 状态注入 | 工具+advisor | 2 天 |
| G5 | zones 扩域 + 夹击变体 + 线补全 + apply 续下 + 仲裁升级 | 数据+管线 | 3-4 天 |
| G1+G2 | move_shape + describe_point + 三处接入 + 术语断言 | 纯逻辑新模块 | 4-5 天（即 B1a 落地） |

G6→G4→G3 可先行合入；G5 与 G1/G2 并行。全部完成前，现象 ①② 部分缓解（G5 的逐手注释暂不含术语）。

## 纪律对齐

- G1/G2 纯逻辑无 Qt 无 I/O，进 CTest；G4-3 泄露闸门是确定性正则，进 CTest；
- 降级链不变：无引擎时 G1/G2/G3/G4/G6 全部可用（纯 prompt/纯逻辑），G5 的 apply 续下依赖引擎，降级为 textbook（既有 P5）；
- 所有修复**不触碰谓词引擎/仲裁/门禁**——本轮实测恰恰证明这三层是对的，守住"程序判适用性、引擎判得失、LLM 只叙述"的分工，把越界的两处（指代消歧 G3、术语自造 G1）收回到程序侧。
