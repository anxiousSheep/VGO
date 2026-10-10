# VGO · F1 实测修复技术路线（G1–G6 落地设计）

| 项 | 内容 |
|---|---|
| 输入 | `VGO-定式实测问题诊断与解决方案.md`（点三三第二轮实测，六现象 G1–G6） |
| 回源 | master（b8eebbc）：`joseki_apply.cpp` / `board_predicates.cpp` / `agent_tools.cpp` joseki_lookup / `http_conversation.cpp` 分级契约 / `MainWindow.cpp` handleDemoTool / `explain.cpp` B1a / `probe.h` / `resources/joseki/hoshi-sansan-invasion.json`，并对照 `docs/archive/v0.2.1/`（P1 技术路线 B1a、定式死活完整方案 §4/§5） |
| 结论 | v2 可靠性架构（谓词→变体→仲裁→门禁）**判定正确，不回滚**。六现象的根因在架构外三层（grounding 缺事实 / 词典数据不充盈 / 呈现契约不严），以六个针对性补丁 G1–G6 修复，全部守住「程序判适用性、引擎判得失、LLM 只叙述」的分工 |
| 版本 | v1.0 |

---

## 0. 代码校准（读代码后对诊断文档的三处修正）

诊断文档总判断成立，但三处细节与实际代码不符，直接影响路线的可行性与工期：

| # | 文档说法 | 代码实况（回源） | 对路线的影响 |
|---|---|---|---|
| ① | "B1a 着法几何**从未落地**" | `explain.cpp` 的 `extractBoardFacts` 已产出 Hane/Cut/Diagonal/SmallKnight/LargeKnight/OneSpaceJump/TigerMouth（P1 §1 已实现，`FactType` 枚举在 `explain.h:12-26`） | G1 不是补空白，而是**新增一条与既有 B1a 正交的轴**：既有 B1a 是「已落子相对**己方**子」的主形状，且枚举里**没有「点入/挂/夹」**，表达不了「R17 相对黑 M17 隔一路点入」。要新建 `classifyMove(b,p,mover)`（候选点相对**最近异色子**），并抽取/复用 `explain.cpp` 的 `chebDist`/邻接原语，不要重写 |
| ② | "apply 摆完即停，续下被省掉了" | `MainWindow::handleDemoTool`（`MainWindow.cpp:1714-1725`）**已经** `playFixedSequence(seq)` 后接 `collectAiMoves(plies)` | 演示侧「摆线 + 引擎续下」**已存在**。真正缺的是**仲裁侧**：`arbitrateJoseki`（`joseki_apply.cpp:160-182`）step1 只摆 3 手定式线就 `analyzePosition`，无续下。G5-3 降级为「确认 + 调 plies 默认」，重心移到 G5-4 |
| ③ | "术语断言/泄露闸门……**打回重写**" | `verifyAndAnnotate`（`advisor.cpp:71-95`）只做「追加修正标注 + 置 unsure 灰显」，**没有 regenerate 回路** | 「打回重写」需要尚不存在的重试环。可行落法分两步：先做**确定性闸门纯函数 + 追加标注**（与现有 verify 语义一致，零新架构），再决定是否加「有界单次重试」（决策点 D1） |

---

## 1. 技术路线总图（按层次）

```
┌─ 纯逻辑新增（Qt-free，进 CTest）──────────────────────────────┐
│  move_shape.h/.cpp     classifyMove / describePoint  (G1+G2)   │
│  verify/leak_gate.h/.cpp  泄露闸门纯函数            (G4-3)     │
│  verify/term_gate.h/.cpp  术语断言纯函数            (G1-3)     │
└───────────────────────────────────────────────────────────────┘
┌─ 工具/呈现层（agent_tools.cpp + http_conversation.cpp）───────┐
│  joseki_lookup：+summary、不可用条目瘦身、转述禁令  (G3-1/G4-1/G4-2)│
│  buildTools：demonstrate_go_moves 描述补丁           (G3-3)    │
│  buildStructureContract：口头禅措辞                  (G6)      │
└───────────────────────────────────────────────────────────────┘
┌─ advisor 状态层（advisor.h/.cpp）─────────────────────────────┐
│  last_joseki 对话状态注入                           (G3-2)     │
│  verifyAndAnnotate 接入两个闸门                     (G4-3/G1-3)│
└───────────────────────────────────────────────────────────────┘
┌─ 数据层（resources/joseki/*.json）────────────────────────────┐
│  zones 扩域 + main 补全 + 夹击变体 + teaching.caution (G5-1/2) │
└───────────────────────────────────────────────────────────────┘
┌─ 管线层（joseki_apply.cpp + probe.h）─────────────────────────┐
│  仲裁续下 selfPlayThenAnalyze                       (G5-4)    │
└───────────────────────────────────────────────────────────────┘
```

**不变量**（沿用诊断文档纪律，符合 v2 原设计 §P3「局部规则、全局仲裁」）：全程不碰 `checkPreconditions`/`selectVariants`/`evaluateWhen`/`directionMirrors` 的判定语义、不碰 apply 门禁（`joseki_apply.cpp:89-94`）、不碰 ClaimVerifier 的数字校验。只加事实源、只收口两处越界（术语自造→程序、指代消歧→程序）。

---

## 2. 分阶段落地

### Phase 0 · G6 口头禅（0.5 天，独立，可立即合入）

- **改动**：`http_conversation.cpp:53-71` `buildStructureContract`。三档里「先用一句话给出明确结论 / 先给结论」改为「先直接给出明确结论」，并追加「不要把结构说明里的字样（如『一句话』『结构』）当作标题或引导语复述」。
- **测试**：`tests/test_http_tools.cpp`（`buildStructureContract` 已暴露可单测）补断言：输出不含「一句话结论」、含「先直接给出明确结论」。
- **验收**：复测原场景，回答不再以「一句话结论：」开头。

### Phase 1 · G4 泄露（1–2 天）

1. **纯函数闸门**：新增 `src/ai/verify/leak_gate.h/.cpp`
   ```cpp
   bool leakGateViolation(const std::string& text);
   // 正则匹配 satisfied|occupied_path|w_joseki|w_best|favorable|strategy_only|verdict
   ```
   进 `vgo_ai_tests`，6 个 token 正反例各一条。
2. **summary 层 + 转述禁令**：`agent_tools.cpp:270-291` `joseki_lookup` 返回 JSON 增加顶层 `summary`（程序预生成人话，如「小飞挂目前不适用：第一步 O17 已被占（就是白棋刚下的那一手）」）；`agent_tools.cpp:166-169` 的 `description` 追加「preconditions/context/engine_verdict 等字段是给你理解的内部数据，禁止原文出现给用户」。
3. **接入闸门**：`advisor.cpp:71-95` `verifyAndAnnotate` 末尾，若 `leakGateViolation(r.text)` → 追加标注（如「（注：回答含内部字段，已隐藏。）」）并 `r.unsure=true`。**先不加 retry 回路**（决策点 D1）。
4. **测试**：`tests/test_verify.cpp` 扩（或新 `test_leak_gate.cpp`）。

### Phase 2 · G3 指代消歧（2 天）

1. **判定书瘦身**：`agent_tools.cpp` `joseki_lookup` 里，当 `!pre.satisfied` 时返回体**收敛为** `{name, summary, applicable:false, reason}`——不再输出 `missing`/`occupied_path`/`context`/`applicable_variants`/`engine_verdict` 全量字段（`agent_tools.cpp:270-291` 是唯一改动点）。
2. **对话状态注入**（诊断文档未展开、需要新架构的点）：
   - 新增 `struct JosekiDialogState { std::string id, name; bool applicable; std::string mode; }`（放 `agent_tools.h` 或 `joseki_apply.h`）。
   - `AgentToolContext`（`agent_tools.h:17-26`）加 `std::shared_ptr<JosekiDialogState> last_joseki`（**可变**：`const` ctx 里共享指针指向的对象可变）。
   - `joseki_lookup` 的 `invoke` 返回前写入 `last_joseki`。
   - `AiAdvisor` 加成员 `std::shared_ptr<JosekiDialogState> last_joseki_`，在 `deliverAnswer`/`deliverAnswerStream`（`advisor.cpp:417-425` / `583-591`）构造 `tool_ctx` 时传入同一指针；`ConversationRequest`（`conversation.h`）加字段 `last_joseki_context`。
   - `buildUserPrompt`（`http_conversation.cpp:183`）非空时注入「上一次定式判定：点三三（可用，apply 模式）」作为硬事实。
3. **工具描述补丁**：`http_conversation.cpp:306-310` `demonstrate_go_moves` description 追加「演示目标定式若在上一轮判定书中已出现，优先使用该定式名，不要自行更换」。
4. **测试**：`tests/test_agent.cpp`（工具写状态）、`tests/test_advisor.cpp`（注入渲染）。失效时机见决策点 D2。

### Phase 3 · G5 数据 + 管线（3–4 天，可与 Phase 4 并行）

1. **zones 扩域**（`resources/joseki/hoshi-sansan-invasion.json:13-16`）：`left_flank`/`right_flank` 各补**三线/四线带**（挂角/夹击位，左侧 `P16/P17/O16/O17/N16/N17/M17…`，右侧对称到 S/T 边），使 `classifyContext`（`board_predicates.cpp:62-89`，已会统计任意色）能看到夹击子与挂角子。⚠️ **精确点集不手拍**：用中国流 + 一间夹实测局面写 golden 测试锁定（见验收）。`teaching.caution` 追加「对方有夹击子时，挡的方向应朝夹击子一侧使外势呼应」。
2. **补全序列 + 夹击变体**：`main` 变体 `sequence` 从 3 手补到 8–12 手（收束）；新增 `{"id":"pincer-variant","when":{"zone_enemy_majority":"left_flank"}, ...}` 完整变体线（`evaluateWhen` 已支持 `zone_enemy_majority`，`board_predicates.cpp:98`，纯数据即可）。
3. **G5-3 确认**：`MainWindow::handleDemoTool` 已 `playFixedSequence` + `collectAiMoves`，无需新写；只需确认 joseki 演示路径的 `plies` 默认值（`MainWindow.cpp:1701` 当前 4）够收束，必要时在 joseki 分支调高。
4. **仲裁续下（G5-4）**：`Probe`（`probe.h:27-51`）新增 `selfPlayThenAnalyze(board, to_move, plies, cb)`——内部 `engine_->requestMove` 逐手摆、末位 `analyzePosition`。`arbitrateJoseki` step1（`joseki_apply.cpp:160-182`）改为「摆固定线 + `selfPlayThenAnalyze(N)`」。**若 Phase3-2 已把序列补到收束，step1 摆全 line 即等价于续下，此项可降级为仅对仍会分支的变体启用**——这是本阶段的主要工期杠杆。
5. **测试**：`tests/test_joseki_v2.cpp` / `tests/test_demo_modes.cpp` 扩：夹击局面 `classifyContext` 计数正确、`selectVariants` 命中夹击变体、方向镜像朝夹击侧、`arbitrateJoseki` 用 `ArbEngine` 假引擎验证续下后 verdict 基于完整线。

### Phase 4 · G1+G2 着法几何 + 空间事实（4–5 天，= B1a 真正补全）

1. **新模块** `src/ai/move_shape.h/.cpp`（纯逻辑，Qt-free）：
   ```cpp
   enum class MoveShape { Contact, Diagonal, OneGap, TwoGap, Extension, Other };
   struct ShapeFact { MoveShape shape; std::string term; std::string relation; };
   ShapeFact classifyMove(const Board& b, Point p, Color mover);  // 参照 = 距 p 最近异色子
   struct PointGeo { std::string region; int line; std::string nearby; };
   PointGeo describePoint(const Board& b, Point p);
   ```
   复用 `explain.cpp` 的 `chebDist`/正交对角判定思想（建议把 `chebDist` 等几何原语上提到共享头避免重复；不动 `explain.cpp` 就重写这几个 5 行原语，成本极低）。
2. **三处接入**：
   - `advisor.cpp` 的 `handleMoveQuality`（move-quality 路径）：对引擎推荐点预标注 `ShapeFact` 注入 prompt；
   - `MainWindow.cpp` 演示逐手注释：`collectAiMoves`/`playFixedSequence` 每手落子后追加 `classifyMove` 结果；
   - `verify/term_gate.h/.cpp`：`termGateViolation(text, expectedTerm)`，`verifyAndAnnotate` 接入（先标注，retry 见 D1）。
3. **测试**：新 `tests/test_move_shape.cpp`：扳/断/尖/飞/跳/虎/点/挂/夹各一经典局部形；`describePoint` 四角/边/天元/三三/小目 **19 路 + 9 路各一套 golden 全枚举**。
4. **验收**：同局面「我该下哪」→ 回答「点入三三」而非「扳」。

---

## 3. 依赖、并行与交付顺序

```
G6 ──▶ G4 ──▶ G3 ──▶ [合入 #1]          （纯 prompt/纯逻辑，无引擎也能跑）
        G5 ───────────────────┐
        G1+G2 ────────────────┴─▶ [合入 #2]
```

- **降级链不变**：G5 的仲裁续下依赖 KataGo，无引擎时 `Probe::hasEngine()==false` → 仲裁 `computed=false` → apply 自动降级 textbook（既有 P5，`joseki_apply.cpp:148-153` 已处理），**无需改降级逻辑**。
- **纪律**：G1/G2/G4-3/G1-3 全部纯逻辑进 `vgo_ai_tests`（CMake 在 `CMakeLists.txt:131-169`，新 `.cpp` 往 `add_executable(vgo_ai_tests ...)` 追加即可）。
- **全部完成前**：现象 ①② 部分缓解（G5 的逐手注释暂不含术语）。

---

## 4. 测试与验收矩阵

| 现象 | 闸门/事实 | 单测 | 手测验收 |
|---|---|---|---|
| ① 术语幻觉 | `classifyMove` + `term_gate` | `test_move_shape.cpp` | 「我该下哪」→「点入三三」 |
| ② 空间感 | `describePoint` | `test_move_shape.cpp`（19/9 golden） | 方位描述与程序一致 |
| ③ 指代漂移 | 判定书瘦身 + `last_joseki` | `test_agent.cpp`/`test_advisor.cpp` | 复现「问适用→演示这个」→演示点三三 |
| ④ 泄露参数 | `summary` + `leak_gate` | `test_leak_gate.cpp`（6 token 正反例） | 回答全自然语言 |
| ⑤ 挡向/半截 | zones + 夹击变体 + 续下 | `test_joseki_v2.cpp`/`test_demo_modes.cpp` | 一间夹局面：黑挡向夹击侧、白活角、讲解「两分」 |
| ⑥ 口头禅 | 契约措辞 | `test_http_tools.cpp` | 不再「一句话结论：」 |

---

## 5. 待拍板决策点（默认值已给）

- **D1「打回重写」是否加 retry 回路**：默认**先不加**——闸门先做「追加标注 + unsure 灰显」（与现有 `verifyAndAnnotate` 一致，零新架构），跑通后再评估是否加「有界单次重试」（需在 `HttpConversationService` 增加一次 tool-message 回填重试，约 +1 天）。
- **D2 `last_joseki` 失效时机**：默认「同一局内跨轮次保留，换局/换模式 `clearSession` 时清空」；是否在非定式提问后也保留，影响指代命中率，需实测调。
- **D3 zones 精确点集**：默认按「三线/四线挂角 + 夹击带」扩域，但精确点用**中国流 + 一间夹实测局面的 golden 测试**锁死（不手拍）。

---

## 6. 纪律对齐

- G1/G2 纯逻辑无 Qt 无 I/O，进 CTest；G4-3 泄露闸门是确定性正则，进 CTest；
- 降级链不变：无引擎时 G1/G2/G3/G4/G6 全部可用（纯 prompt/纯逻辑），G5 的仲裁续下依赖引擎，降级为 textbook（既有 P5）；
- 所有修复**不触碰谓词引擎/仲裁判定语义/apply 门禁/数字校验**——本轮实测恰恰证明这四层是对的，守住「程序判适用性、引擎判得失、LLM 只叙述」的分工，把越界的两处（指代消歧 G3、术语自造 G1）收回到程序侧。
