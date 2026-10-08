# VGO · P2 批技术路线设计（B5 / B6 / C2 / C3 / D-b）+ 立项说明

| 项 | 内容 |
|---|---|
| 依据 | `docs/VGO-设计优化评审报告.md` v1.1（§2 P2、§3 Batch 3、§5.2） |
| 范围 | Batch 3：B5 主动教学 / B6 递进提示 / C2 缓存分层 / C3 failover / D-b 每日一题联动；另附 B1b / B1c / C4 / C6 立项说明 |
| 版本 | v0.1 |
| 日期 | 2026-10-08 |
| 状态 | 设计稿（待评审） |
| 前置 | P0（A1-A4）、P1（B1a/B2/B3/B4/D-a）；B6 部分依赖 L3（见 §2） |

**设计纪律**（同前两批）：① CTest 只进离线密闭纯逻辑；② 未配置 LLM 降级链不劣化；③ 无 Qt、无 I/O 进 `src/ai/`。

---

## 0. 公共接口变更总览

| 变更 | 位置 | 性质 |
|---|---|---|
| `CommentaryEvent`/`classifyCommentary`（纯逻辑） | `src/ai/evaluate.h/.cpp` 或新文件 | 新增 |
| `chipsForPhase(GamePhase)` | `src/ai/phase.h`（复用 B2） | 新增 |
| `hint_tier` 取代 `hint` 布尔；`hintTierFromText` | `src/ai/conversation.h` | 字段语义变更 + 新增 |
| `buildHintDirective(tier, cls)` | `conversation_http/` | 新增 |
| `buildSystemPrefix`（稳定前缀分层） | `conversation_http/` | 新增 + `buildSystemPrompt` 重排 |
| `HttpConversationService::setEndpoints` + 重试 | `conversation_http/` | 新增 |
| `LlmSettings` 增备选端点字段 | `ui/model/`（ModelManager） | 配置扩展 |
| `AiAdvisor` 增 `explainDailyProblem` | `advisor.h/.cpp` | 新增 |

---

## 1. B5 · 情境化主动教学（承接 L12 CommentaryPolicy）

### 1.1 点评触发器（纯逻辑）

在 `evaluate.h` 扩展（`isBlunder` 已含 winrate_drop=0.15/lead_drop=3.0 阈值）：

```cpp
enum class CommentaryEvent { None, Blunder, Brilliant, Swing };
// 据一手棋的胜负率/领先变化三分：Blunder（掉 >0.15）/ Brilliant（涨 >0.15）/
// Swing（局面级胜率波动 >0.15，即 15 个百分点，非单手而是连续几手累积）/ None。
CommentaryEvent classifyCommentary(const MoveEvaluation& e);
```

- `Blunder`/`Brilliant` 复用 `MoveEvaluator::evaluateLastMove` 的 `MoveEvaluation`（已有，D3.2 blunder 采集同源）。
- `Swing`（局势反转）需**连续手数胜率序列**：在 `AiAdvisor` 或 UI 层维护最近 N 手胜率滑动窗口，波动超阈值触发。属 P2 增强，初版可只做 `Blunder`/`Brilliant` 两档。

### 1.2 主动点评 + 成本控制（与 L12 同源对立，务必保守）

- 触发时生成一句点评：复用 `AiAdvisor`，构造问题（如「刚这手棋怎么样」/「这一步妙在哪」）走既有 harness（含校验闸门）。
- **节流**：每局主动点评上限（如 ≤5 条）+ 最小间隔（≥1 手）；**纳入 D5.5 token 预算守护**，超预算即停。
- 触发阈值**宁可漏评不可话痨**（评审报告 v1.1 §B5 明确）。
- 妙手/恶手已有 UI 预警（M3b），点评是其在聊天面板的自然语言升级。

### 1.3 快捷提问 chips 随局面变化

`phase.h` 增纯函数：

```cpp
// 按局面阶段返回快捷提问 chips（布局/中盘/官子各 3-4 条）。
std::vector<std::string> chipsForPhase(GamePhase phase);
```

映射（复用 B2 的 `classifyPhase`）：布局 → 「开局怎么下」「这个定式怎么选」；中盘 → 「这块棋死了吗」「现在该攻还是守」「这手妙在哪」；官子 → 「先收哪个官」「谁领先」。

`ChatPanel` 的 `quickRow`（ChatPanel.cpp L139-147，当前静态 5 按钮）改由 `MainWindow` 按 `classifyPhase` 结果动态重建（局面阶段变化时刷新）。纯逻辑 `chipsForPhase` 可单测；UI 重建机械。

### 1.4 B5 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/evaluate.h/.cpp` | `CommentaryEvent`/`classifyCommentary` |
| `src/ai/phase.h/.cpp` | `chipsForPhase` |
| `src/ai/advisor.*` | 胜率滑动窗口 + 点评触发（或放 UI） |
| `src/ui/ChatPanel.*` | quickRow 动态化 |
| `src/ui/MainWindow.cpp` | 阶段变化 → 刷新 chips；点评节流/预算 |

**单测（纯逻辑）**：`classifyCommentary` 三档阈值；`chipsForPhase` 三阶段内容。手测：走一局，恶手/妙手触发点评、chips 随阶段切换、点评不话痨。

---

## 2. B6 · 递进式提示（承接 L9）

### 2.1 三档递进模型

`ConversationRequest` 的 `bool hint` → `int hint_tier`（0=无提示，1=方向，2=要点，3=答案）：

```cpp
// 从自然语言判断提示档位（纯逻辑）："求指点/提示/方向"=1；"要点/关键"=2；"直接说/告诉我"=3。
int hintTierFromText(const std::string& text, int current_tier);
```

三档语义：
- **档 1 方向**：只给方向（「往中腹看」），不给坐标——现有 `moveHint(p, size)`（explain.cpp L209-221）即此档。
- **档 2 要点**：给「要点/关键手位置/断点」但不给完整答案——**按问题类别区分来源**（评审报告 v1.1 §B6）：
  - 落点/形势类：引擎候选点（`req.facts.analysis.candidates`，已有）作要点。
  - 死活类：依赖 L3（眼形/要点求解，⬜ P1 未做）→ 本轮**降级**为「方向→答案」两档，或待 L3。
- **档 3 答案**：完整作答（即 `hint_tier=0` 的默认行为）。

### 2.2 `buildHintDirective`（纯逻辑）

```cpp
// 按档位 + 问题类别生成「只给方向/只给要点/直接作答」的 prompt 指令。
QString buildHintDirective(int tier, QuestionClass cls);
```

`buildUserPrompt`（http_conversation.cpp L130-132 现有 `if (req.hint)` 分支）改为按 `hint_tier` 注入对应指令；档 2 死活类降级为档 1 措辞（含注释说明依赖 L3）。

### 2.3 交互流（UI）

`MainWindow` 维护「当前提示档位」：首次「求指点」→ 档 1；用户追加「要点」→ 档 2（`hintTierFromText` 升级）；「直接告诉我」→ 档 3。chips 提供「提示」入口，逐次递进。

### 2.4 B6 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/conversation.h` | `hint` → `hint_tier`；`hintTierFromText` |
| `conversation_http/http_conversation.cpp` | `buildHintDirective` + `buildUserPrompt` 按档注入 |
| `src/ai/advisor.cpp` | `deliverAnswer` 填 `hint_tier` |
| `src/ai/template_conversation.cpp` | `req.hint` → 档位映射（见下） |
| `src/ui/MainWindow.cpp` | 递进交互 |

**模板层降级映射（必做，否则编译断 + 打断「未配置 LLM」降级链）**：`template_conversation.cpp` L15 `const bool hint = req.hint;` 与 L35 `if (hint)` 在 `hint` 改名 `hint_tier` 后编译断。改为 `const bool hint = (req.hint_tier >= 1 && req.hint_tier < 3);`——档 1/2 = 现有半提示（方向），档 3/0 = 完整作答；模板无「要点」能力，档 2 与档 1 同视为方向提示。

**单测（纯逻辑）**：`hintTierFromText` 升级判定；`buildHintDirective` 三档 × 类别（落点/死活）措辞；死活类档 2 降级；`TemplateConversation` 在 tier 1/2 给方向、tier 3/0 给坐标。手测：递进提示不提前泄露答案。

---

## 3. C2 · prompt cache 分层

### 3.1 目标与前提（价值仅限云端点）

把 system prompt 排成「稳定前缀 → 半稳定 → 动态」，对齐 OpenAI/DeepSeek 的 prefix cache。**本地 Ollama 无成本收益**，属纯降本项，优先级不高于 C3。

### 3.2 稳定前缀的成立条件（关键）

`buildSystemPrompt` 当前按 `user_color` 分两支、按 level 追加语气文本，**byte-identical 前缀只在「同一模式 + 同一档位」内成立**（评审报告 v1.1 §C2）。要吃到缓存需把**人设骨架**与**语气/契约联动**分层：

```cpp
// 稳定层：人设骨架（不随 user_color/level 变化，byte-identical 前缀）。
QString buildSystemPrefix(bool human_vs_ai);  // 仅人机/摆棋两个稳定值
```

设计分层顺序（system message 内部）：

1. **稳定层**（人设骨架 + skills frontmatter 常驻，D4.2）：`buildSystemPrefix(mode)`。同模式内逐字节一致。
2. **半稳定层**（语气/结构契约 A3、coach_memory、weakness_directive）：随 level/画像变化，但同档位内稳定。
3. **动态层**（skills body，`select` 检索结果）：随提问变化。

用户 prompt（棋盘/事实）是**独立消息**，其动态性不破坏 system 前缀缓存。

### 3.3 C2 文件改动与测试

| 文件 | 改动 |
|---|---|
| `conversation_http/http_conversation.cpp` | `buildSystemPrefix` + `buildSystemPrompt` 重排为三层层序 |
| 无 | 无新类型 |

**单测（纯逻辑）**：`buildSystemPrefix` 同模式两次调用字节一致；`buildSystemPrompt` 三层顺序（稳定在前）；人机/摆棋两模式前缀不同但各自稳定。

**验收**：云端点观察两次请求 system 前缀是否复用（依赖 provider 的 cache 命中指标，属离线观测，不进 CTest）。

---

## 4. C3 · 多提供商 failover

### 4.1 端点级 failover

`HttpConversationService` 增端点列表：

```cpp
void setEndpoints(std::vector<QString> endpoints);  // 首个为主，其余备选
```

`doChat` 中 `winHttpPost`（或 `winHttpPostStream`）返回后：`!res.ok`（超时/网络不可用）或 `status >= 500` 时，按序尝试下一端点（**有界重试 ≤ 2 次**）；`status 4xx`（鉴权/参数错）**不重试**（重试无意义）。全程保留 `generation_` 取消语义（任一环节 cancel 即弃结果）。

### 4.2 配置扩展

`ModelManager` 的 `LlmSettings`（运行时配置，MainWindow.cpp L504 读取）增备选端点字段（如 `baseUrlAlt` 或 `endpoints` 列表）；`MainWindow::createAiServices` 读配置后 `setEndpoints`。默认空 = 单端点（现有行为不变）。

### 4.3 超时策略

主端点超时设**激进**（15–20s，评审报告 v1.1 §C3），保证主端点不可用时 failover 总延迟仍可接受；备选端点可略长。与现有 `timeout_ms_`（120s）解耦为「主超时」与「备超时」。

### 4.4 C3 文件改动与测试

| 文件 | 改动 |
|---|---|
| `conversation_http/http_conversation.h/.cpp` | `setEndpoints` + 重试逻辑 |
| `ui/model/`（ModelManager） | `LlmSettings` 增备选端点 |
| `src/ui/MainWindow.cpp` | 读配置 → `setEndpoints` |

**单测（纯逻辑）**：把重试决策抽成纯函数 `nextEndpoint(status, ok, idx)`，测 5xx 重试 / 4xx 不重试 / 超时重试 / 耗尽。**离线冒烟**：本地起「主端点 500 + 备端点 200」，验证 failover 端到端。

---

## 5. D-b · 每日一题 → 讲解联动

### 5.1 数据就位情况

`ReviewItem`（learner_model.h L54-71）已含 `board_snapshot` / `blunder_move` / `lead_before` / `lead_blunder` / `to_move` / `size`；`boardFromSnapshot(snap, size, &board)`（daily_puzzle.h L24）已能重建棋盘。快照局面现成，缺的是「讲透」入口。

### 5.2 「讲透」流程

1. `MainWindow` 每日一题标签页加「让 AI 讲透这题」按钮（答题后可用）。
2. 从 `ReviewItem.board_snapshot` 重建 `Game(Board, to_move)`（`Game(const Board&, Color, double)` 构造，game.h L21）。
3. `AiAdvisor` 增 `explainDailyProblem(const ReviewItem&, ReplyCb)`：
   - 重建 Game；构造问题（如「上一手为什么在这个点失误？更好的点在哪？为什么？」）；
   - **真相来源**：快照局面的 `analysis` 未预热（`prewarmAnalysis` 只覆盖当前对局），故 `explainDailyProblem` 需**异步 analyze 该快照棋盘**（`analyzePrioritized` 或 `analysis_engine_->analyze`，Interactive 优先级）取 `analysis`，再 `deliverAnswer`；
   - 若引擎不可用，退化为用 `lead_before`/`lead_blunder`（ReviewItem 存好的领先基准）作为规则级真值，走模板/降级讲解。

**共享态干扰（必做，否则与在局问答互相抢占）**：`explainDailyProblem` 的异步 analyze 与在局 `ask` **共用 `generation_` 与 `last_analysis_`**——在局提问递增 `generation_` 会丢弃每日题的 analyze 结果，写 `last_analysis_` 会污染在局 grounding 快照。因此：① 每日题 analyze 用**独立代际计数器**（如 `daily_gen_`，与 `prewarm_generation_` 同模式），不依赖 `generation_`；② 结果**直填** `GroundedFacts f; f.analysis = a;` 再 `deliverAnswer(game, question, std::move(f), cb)`，**不写 `last_analysis_`**；③ 抢占/排队策略：每日题讲透与在局问答并发时走 `analyzePrioritized` 的 Interactive 优先级排队，或 UI 层在讲透期间禁用新提问。
4. 复用 D3.5 快照字段，无新持久化。

### 5.3 D-b 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/advisor.h/.cpp` | `explainDailyProblem` |
| `src/ui/MainWindow.cpp` | 每日一题「讲透」按钮 + 接线 |

**单测（纯逻辑）**：`explainDailyProblem` 的 Game 重建（`boardFromSnapshot` + `Game`）与问题构造。**手测**：答题后「讲透」触发一次异步 analyze，回答引用该局面的引擎真值，不串到当前对局局面。

---

## 6. 立项说明（B1b / B1c / C4 / C6，视立项排期）

> 以下四项评审报告 v1.1 明确「视立项排期」，本批不承诺交付，仅立项评估。

### 6.1 B1b · 征子 / 枷吃识别

- **性质**：征子（ladder）本质是「反复打吃能否逃脱」的**前瞻搜索**；枷吃（net）可识形但「可靠判定」需短搜索确认被枷子无法脱出。均**非局部几何可判定**。
- **路线**：在 `src/ai/` 增纯逻辑搜索（小深度 ladder 模拟 / 枷吃脱出检查），或引入局部模式库。无引擎、无 Qt。
- **依赖**：无（纯逻辑）；但需 golden 局部形 + 战术对拍。
- **工作量**：中。建议在 B1a 稳定后单独立项。

### 6.2 B1c · 意图标注（攻击/防守/围空/破空/求活）

- **性质**：依赖 KataGo ownership / analysis，而 `extractBoardFacts(before, after, move, captured, mover)` **签名里没有这些数据**——需扩展签名，或把标注挪到 `AiAdvisor::currentFacts`（那里才有 `last_analysis_`）。
- **路线**：在 `currentFacts` 层用 ownership + 落点相对领地关系打意图标签，作为 `GroundedFacts` 新字段进 prompt。
- **依赖**：KataGo ownership（已具备，L13 实测通过）。
- **工作量**：中。与 B1a 解耦（B1a 纯几何、B1c 依赖引擎），建议独立。

### 6.3 C4 · 跨平台抽象

- **性质**：WinHTTP / DPAPI / MSVC 使项目 Windows-only。抽传输层与密钥层接口（libcurl + Secret Service / Keychain 备选）。
- **现状**：`winhttp_transport.cpp` 已有 `#ifdef Q_OS_WIN`（L8-11）+ 非 Windows stub（L163-171），是现成的抽象缝。
- **路线**：定义 `Transport` / `SecretStore` 接口，WinHTTP 与 libcurl 双实现；与 L16「连接复用」一并做。
- **工作量**：大（纯重构，无路线错误）。维持 P2/持续。

### 6.4 C6 · 棋谱库位置索引（典型局面词典）

- **性质**：`sgf_lookup_position` 目前只返回本局手顺（D1.7）。外延为「定式/布局 SGF 索引」。
- **真正的难点**：不在 SGF 读写（L5 已 ✅），而在**局面匹配**——把当前角部局面哈希到定式索引，需 pattern-hash 索引 + 语料建设，本质是数据工程。
- **路线**：角部规范化（旋转/镜像等价）+ 局面哈希 → 定式词典查找；语料来源需立项建设。
- **工作量**：大。建议作为独立数据项目，先出语料与匹配原型。

---

## 7. 测试矩阵（Batch 3 汇总）

| 层 | 覆盖 | 密闭性 |
|---|---|---|
| `tests/test_evaluate.cpp`（扩） | B5 `classifyCommentary` | ✅ |
| `tests/test_phase.cpp`（扩） | B5 `chipsForPhase` | ✅ |
| `tests/test_hint.cpp`（新） | B6 `hintTierFromText`/`buildHintDirective` | ✅ |
| `tests/test_http_tools.cpp`（扩） | C2 `buildSystemPrefix` 稳定、C3 `nextEndpoint` | ✅ |
| 离线冒烟 | C3 主备端点 failover | 手动 |
| 手测 | B5 点评节流/chips、B6 递进、D-b 讲透 | 手动 |

---

## 8. 风险与边界

| 风险 | 对策 |
|---|---|
| B5 主动点评话痨/烧 token | 节流（每局上限+间隔）+ 纳入 D5.5 预算，宁可漏评 |
| B5 chips 与分类路由耦合 | chips 只发自然语言问题，走既有 harness，无新分发 |
| B6 死活类「要点」被 L3 阻塞 | 明确降级为两档，L3 落地后补 |
| C2 稳定前缀脆弱（后续改动破坏 byte-identical） | `buildSystemPrefix` 单测锁字节一致；前缀后追加新内容必须加到半稳定/动态层 |
| C3 主端点超时过长拖累总延迟 | 主超时激进（15-20s）、重试有界 |
| D-b 快照局面 analysis 未预热 | `explainDailyProblem` 异步 analyze + 规则级降级 |

---

## 9. 文件改动清单（实现顺序）

1. **B6**（最独立、承接 L9）：`conversation.h` 档位 → `buildHintDirective` → `advisor` 填充 → UI 递进。
2. **B5**：`classifyCommentary`/`chipsForPhase` → 点评节流 → chips 动态化。
3. **C3**：`setEndpoints` + 重试 → `LlmSettings` 字段 → MainWindow 接线。
4. **C2**：`buildSystemPrefix` + `buildSystemPrompt` 重排（纯降本，可最后）。
5. **D-b**：`explainDailyProblem` → UI 按钮。
6. 立项：B1b / B1c / C4 / C6 各出立项说明（本文 §6），排期另议。
7. 文档同步：`docs/长期任务清单.md`（L9/L12 进展、L3/L4 关联）、评审报告 Batch 3 状态。
