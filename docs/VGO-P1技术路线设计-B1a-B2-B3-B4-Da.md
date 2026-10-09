# VGO · P1 批技术路线设计（B1a / B2 / B3 / B4 / D-a）

| 项 | 内容 |
|---|---|
| 依据 | `docs/VGO-设计优化评审报告.md` v1.1（§2 P1、§3 Batch 2、§5.2 决定②） |
| 范围 | Batch 2：B1a 几何着法 / B2 阶段+手顺 / B3 SSE 流式 / B4 质量评估（离线）/ D-a 画像侧重 |
| 版本 | v0.1 |
| 日期 | 2026-10-08 |
| 状态 | 已实现（Batch 2 落地，CTest 全量通过） |
| 前置 | P0 批（A1-A4）已落地；A2 的多轮记忆、A3 的分档契约在本批被复用 |

**设计纪律**（同 P0 批）：① CTest 只进离线密闭纯逻辑；② 未配置 LLM 降级链不劣化；③ 无 Qt、无 I/O 进 `src/ai/`；④ B4 不进 CTest（见 §4）。

---

## 0. 公共接口变更总览

| 变更 | 位置 | 性质 |
|---|---|---|
| `FactType` 增 7 个着法/形状枚举 | `src/ai/explain.h` | 追加枚举值（无破坏） |
| `extractBoardFacts` 增发 Cut + 形状事实（签名不变） | `src/ai/explain.cpp` | 行为扩展 |
| 新增 `GamePhase`/`classifyPhase` | `src/ai/phase.h/.cpp`（新） | 纯函数 |
| `ConversationRequest` 增 `phase` / `move_sequence` 字段 | `src/ai/conversation.h` | 追加字段 |
| `HttpConversationService` 覆写 `answerStream`/`supportsStreaming` | `conversation_http/` | 新增实现 |
| 新增 `winHttpPostStream`（增量回调） | `winhttp_transport.h/.cpp` | 新增函数 |
| 新增 `SseParser`（纯逻辑） | `conversation_http/` 或 `src/ai/` | 新增 |
| `ChatPanel` 增 `abortAiStream()` | `src/ui/ChatPanel.h/.cpp` | 新增方法 |
| 新增 `quality_judge.h/.cpp`（纯逻辑）+ `tools/quality_eval.cpp`（离线） | `src/ai/` + `tools/` | 新增 |
| `ConversationRequest` 增 `weakness_directive`；`AiAdvisor` 增 `setWeaknessProvider` | `conversation.h` / `advisor.h` | 追加字段/方法 |
| `buildWeaknessDirective`（纯逻辑） | `src/ai/learner/` 或新文件 | 新增 |

---

## 1. B1a · 几何着法识别（`extractBoardFacts` 升级）✅

### 1.1 范围与判定性

本轮**只做 B1a 几何着法**：扳 / 断 / 尖 / 小飞 / 大飞 / 跳 / 虎。全部可由「落点 + 邻接关系 + 相对位置」在 `extractBoardFacts(before, after, move, captured, mover)` 内判定，**签名不变、无引擎、无 Qt**。征子/枷吃（B1b）、意图标注（B1c）单独立项（见 Batch 3 文档 §立项说明）。

### 1.2 判定规则（几何定义，均相对落点 (x,y) 与 `after` 棋盘）

约定 `hasFriend(dx,dy) = after.at(x+dx, y+dy) == mover`，`hasOpp(dx,dy) = after.at(x+dx, y+dy) == opponent`。

**主形状（按 Chebyshev 距离取最近己方子，确定性、可单测）**：

| 形状 | 判定 | 优先级 |
|---|---|---|
| 扳 Hane | 己方子在对角 (±1,±1)，且该己方子与落点共享的某正交邻点（`(x+dx,y)` 或 `(x,y+dy)`）为对方子（「贴住对方拐头」） | 对角中最先判 |
| 尖 Diagonal | 己方子在对角 (±1,±1)，且不满足扳 | 同上 |
| 小飞 SmallKnight | 己方子在 (±1,±2) 或 (±2,±1) | — |
| 大飞 LargeKnight | 己方子在 (±2,±3) 或 (±3,±2) | — |
| 跳 OneSpaceJump | 己方子在 (±2,0) 或 (0,±2) | — |

取 Chebyshev 距离最小的己方子确定「主形状」；距离并列时按上表顺序（扳>尖>小飞>大飞>跳）取先者；Chebyshev 距离 >3 无己方子则无主形状（维持 Placement）。

**断 Cut（独立可加，非主形状互斥项）**：落点 4 个正交邻点上，`after` 中属于**不同对方组**的数量 ≥ 2（把对方两块棋切开）。用 `distinctOppGroups(after, x, y, opp)`（对 `explain.cpp` 现有 `distinctOwnGroups` 的镜像实现）。

**虎 TigerMouth（独立可加）**：存在一个方向 d，使得落点 + 两个与落点、彼此皆对角相邻的己方子（如面向西：`(x-1,y-1)` 与 `(x-1,y+1)`），且二者之间的正交点（如 `(x-1,y)`）为空（虎口）。四个方向分别判：西/东（dx=∓1，口在 `(x∓1,y)`）、北/南（dy=∓1，口在 `(x,y∓1)`）。

### 1.3 数据模型

`explain.h` 的 `FactType` 增：

```cpp
Cut,         // 断：把对方两块棋切开
Hane,        // 扳：贴住对方拐头
Diagonal,    // 尖
SmallKnight, // 小飞
LargeKnight, // 大飞
OneSpaceJump,// 跳（一间跳）
TigerMouth,  // 虎（虎口）
```

`StructuralFact` 结构不变（`stones` 承载涉及的子，`liberties` 保持原语义；形状事实可留空 `stones`）。

### 1.4 `renderExplanation` 升级

`renderExplanation`（explain.cpp L109-143）在现有「提子/打吃/连接/延展/自紧气」基础上追加：断 → 「切断对方两块棋」；扳 → 「扳（贴住对方拐头）」；尖 → 「尖（小尖）」；小飞/大飞/跳/虎 → 对应短语。最终「普通落子」兜底仅在**无任何信号**时触发。目标输出升级为「黑尖顶，守角兼攻白拆二」级别（注：攻/守意图属 B1c，本轮几何层只供给「尖/扳/断」等着法与形状，意图由后续 B1c 补）。

### 1.5 交叉影响（必做）

`classifyBlunderArea`（learner_model.cpp，D3.2）按 `FactType` 映射弱项区，**新枚举必须补映射**，否则落默认分支：建议 Cut/Hane → `Fighting`，Diagonal/SmallKnight/LargeKnight/OneSpaceJump → `Joseki`，TigerMouth → `ShapeJudgment`。此映射影响 D3 画像质量，不得遗漏。

### 1.6 B1a 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/explain.h` | `FactType` 增 7 值 |
| `src/ai/explain.cpp` | `distinctOppGroups` 助手 + `extractBoardFacts` 形状/断/虎判定 + `renderExplanation` 短语 |
| `src/ai/learner/learner_model.cpp` | `classifyBlunderArea` 新枚举映射 |
| `tests/test_explain.cpp`（扩） | 见下 |

**单测（纯逻辑，核心）**：每个形状一个「经典局部形」断言 `extractBoardFacts` 输出对应 FactType——例如：① 扳：`(2,2)黑` + `(3,2)白` + 落 `(3,3)黑` → Hane；② 断：两白组对角相邻、落点夹中间 → Cut；③ 小飞：`(2,2)` + 落 `(4,3)` → SmallKnight；④ 跳：`(2,2)` + 落 `(4,2)` → OneSpaceJump；⑤ 虎：两对角己方子 + 空口 + 落点 → TigerMouth；⑥ 负例：无己方子 → 无形状。用手摆局部板（`Board` + `set`）而非真实对局，保证 hermetic。

**验收**：`ctest -R explain` 全绿；`renderExplanation` 对含形状局面输出不再只是「普通落子」。

---

## 2. B2 · 局面阶段 + 手顺进 prompt ✅

### 2.1 阶段判断（纯函数）

新增 `src/ai/phase.h/.cpp`：

```cpp
enum class GamePhase { Fuseki, MiddleGame, Endgame };
// 按手数 + 领地固化度（ownership）启发式判段，纯逻辑、无 Qt。
GamePhase classifyPhase(int move_number, int size, const OwnershipMap& ownership);
const char* phaseLabel(GamePhase p);  // "布局" / "中盘" / "官子"
```

启发式（尺寸自适应；阈值为**初值**，需按 9/13/19 路真实对局校准后在 `test_phase` 锁定）：
- `Fuseki`：`move_number < 2 * size`（9 路 18、19 路 38；原 `max(3*size,24)` 在 9 路为 27 手偏长）。
- `Endgame`：ownership 非空且「固化度」`fraction(|ownership| > 0.8) > 0.55`（**主信号**，领地大面积定型），兜底 `move_number > size * size / 2`（9 路 40、19 路 180，可达；原 `4*size²/3` 在 19 路为 481 手几乎不可达）。
- 其余 `MiddleGame`。

ownership 为空（fallback 引擎）时只用 `move_number` 阈值：`< 阈值1 → Fuseki`，`> 阈值2 → Endgame`，中间 `MiddleGame`。

### 2.2 手顺进 prompt

`Game::moves()`（game.h L58）现成。`AiAdvisor::deliverAnswer` 取**最近 10 手**（不足则全部）渲染成「黑 H6、白 G5、…」串（复用 `gtpPoint`），连同 `phaseLabel` 写入 `ConversationRequest` 两个新字段：

```cpp
std::string phase;          // "布局"/"中盘"/"官子"
std::string move_sequence;  // "黑 H6、白 G5、…"，空 = 无历史
```

`buildUserPrompt`（http_conversation.cpp）在「形势」行后追加：

```
局面阶段：中盘
最近手顺：黑 H6、白 G5、黑 F7、…
```

讲解因此获得「前文」：「接上一步挂角，这手拆二生根」。

### 2.3 B2 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/phase.h/.cpp`（新） | `GamePhase`/`classifyPhase`/`phaseLabel` |
| `src/ai/conversation.h` | `ConversationRequest` 增 `phase`/`move_sequence` |
| `src/ai/advisor.cpp` | `deliverAnswer` 填 phase + 手顺 |
| `conversation_http/http_conversation.cpp` | `buildUserPrompt` 渲染阶段/手顺 |

**单测（纯逻辑）**：`classifyPhase` 手数阈值（0/30/300 手 × 9/19 路）、ownership 固化度阈值、ownership 为空回退；`deliverAnswer` 手顺串格式与截断（>10 手只取最近 10）。`buildUserPrompt` 含「局面阶段」「最近手顺」字段。

---

## 3. B3 · SSE 流式（恢复 `answerStream`）✅

### 3.1 传输层：`winHttpPostStream`

`winhttp_transport.h` 新增（现有 `winHttpPost` 读循环 L138-153 已在逐块 `WinHttpReadData`，加回调即可）：

```cpp
using ChunkCallback = std::function<void(const std::string& chunk)>;
// 与 winHttpPost 同参数 + 增量回调；每读一块即回调 onChunk(该块字节)。
WinHttpResult winHttpPostStream(const std::string& url, const std::string& body,
                                const std::string& contentType, const std::string& authHeader,
                                int timeoutMs, const ChunkCallback& onChunk);
```

非 Windows 分支（L163-171）同样返回「WinHTTP unavailable」。

### 3.2 SSE 解析（纯逻辑，可单测）

新增 `SseParser`（放 `src/ai/` 或 `conversation_http/`，无 Qt、无 I/O）：

```cpp
// OpenAI 兼容 SSE：累积原始字节，析出 content 增量、tool_calls 增量、[DONE]。
class SseParser {
public:
    void feed(const std::string& bytes);              // 追加原始字节（含跨块半行）
    std::vector<std::string> drainContentDeltas();     // choices[0].delta.content
    std::vector<QJsonObject> drainToolCallDeltas();    // choices[0].delta.tool_calls（增量）
    bool done() const;                                 // 看到 data: [DONE]
private:
    std::string buffer_;  // 未完整事件的行缓存（跨 WinHttpReadData 块）
    // …累积 delta、finish_reason、[DONE] 状态…
};
```

要点：`feed` 按 `\n` 分行缓存、以 `\n\n` 或空行界定一个事件；解析 `data: {...}` JSON 的 `choices[0].delta`；`[DONE]` 单独识别。测试用拼接好的 SSE 样例（含跨块半行、多 delta、`[DONE]`）。

### 3.3 `answerStream` 覆写：逐轮流式、仅最终回合可见

`HttpConversationService` 覆写 `supportsStreaming() → true` 与 `answerStream(req, DeltaCallback)`：

- 组装 messages（复用 A2 的历史 + 摘要，与 `answer()` 同源）。
- **逐轮流式**：每轮 `body["stream"]=true`，worker 线程 `winHttpPostStream` + `SseParser`，按 `generation_` 代际丢弃过期增量。
- **stream+tools 的 4xx 兜底（防弱兼容端点流式整体失效）**：若某轮已声明 `tools` 且流式请求返回 4xx（端点不支持 stream+tools 组合），该回合**降级为 `stream=false` 重试**（走非流式 `doChat` 路径），其余回合仍流式——避免弱端点上整条 `answerStream` 失效。`status 5xx`/超时仍走 C3 的 failover/重试（见 P2 文档 §4），4xx 不重试。
- **工具回合与最终回合的边界**（评审报告 v1.1 §B3 边界约束的落地）：
  - 每轮累积 `round_content`（本回合 content 增量）与 `round_tool_calls`。
  - 一轮结束（`[DONE]` 或 `finish_reason`）时：
    - 若 `round_tool_calls` 非空 → 这是**工具回合**：丢弃 `round_content`（通知 UI `abortAiStream()`），执行工具、`buildToolFollowup` 回填，流式进入下一轮；
    - 若为空 → **最终回合**：把 `round_content` 作为流式内容送 `deltaCb`（`false`），收尾 `deltaCb("", true)`。
  - 由于工具回合的 content 通常为空，绝大多数情况下 UI 只在最终回合开始渲染；少数「模型边答边调工具」的端点，`abortAiStream()` 兜底。
- 跨线程：worker 线程把 SseParser 析出的增量经 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 批量（每 30–50ms 或每 K 条）回主线程，避免逐 delta 跨线程开销。

### 3.4 UI 接线

`ChatPanel` 增 `abortAiStream()`（丢弃当前流式部分并复位 `streaming_`）；`MainWindow` 在 `answerStream` 回调里 `beginAiStream()`（首个 delta）→ `appendAiStream(delta)` → `endAiStream()`（done），工具回合收到 abort 时调 `abortAiStream()`。`beginAiStream/appendAiStream/endAiStream`（ChatPanel.cpp L225-258）已实现，仅需接线 + 增 abort。

### 3.5 B3 文件改动与测试

| 文件 | 改动 |
|---|---|
| `winhttp_transport.h/.cpp` | 新增 `winHttpPostStream` |
| `conversation_http/` 或 `src/ai/` | 新增 `SseParser`（纯逻辑） |
| `conversation_http/http_conversation.h/.cpp` | 覆写 `answerStream`/`supportsStreaming` |
| `src/ui/ChatPanel.h/.cpp` | 增 `abortAiStream()` |
| `src/ui/MainWindow.cpp` | 接线 `answerStream` |

**单测（纯逻辑）**：`SseParser` 跨块半行、多 delta 顺序、`[DONE]`、`tool_calls` 增量解析、畸形 JSON 容错。**离线冒烟**（对齐 L16 曾用的本地 HTTP 服务）：本地 endpoint 起一个固定 SSE 响应，验证 `answerStream` 端到端（增量回调顺序 + done）。**手测**：真实远端 LLM 下「为什么下这里」逐字渲染、工具问题不闪中间态。

---

## 4. B4 · 讲解质量评估（离线 judge 工具，**不进 CTest**）✅

### 4.1 原则（评审报告 v1.1 决定①）

- LLM-as-judge 依赖真实强模型调用，**违反 §10「CTest 离线密闭」纪律**，不进 release gate。
- CTest 只守 `src/ai/` 的**确定性部分**：judge prompt 构造、rubric 结构、打分结果解析器。
- 实际打分走离线工具 `tools/quality_eval`（对齐 `tools/integration_check.cpp` 形态，手动触发）。

### 4.2 纯逻辑层：`src/ai/quality_judge.h/.cpp`

```cpp
// 4 维 rubric，每维 1..5 分。
struct QualityRubric {
    double correctness = 0.0;   // 正确性（对照引擎真值，不编造）
    double professionalism = 0.0;  // 专业性（领域深度、术语准确）
    double tone = 0.0;          // 语气（人性化、面向棋手、无机械感）
    double structure = 0.0;     // 结构（符合 A3 三档契约）
};
struct QualityCase {
    std::string question;         // 提问
    std::string reference_truth;  // 引擎真值摘要（ground truth，供 judge 对照）
    std::string reply;            // 待打分回答（模型实际产出）
    std::string phase;            // 可选：局面阶段（影响结构档）
};
struct JudgeVerdict {
    QualityRubric scores;
    std::string note;   // judge 一句话理由
    bool valid = false; // 解析成功
};

// judge system prompt（4 维定义 + 打分标准 + 输出 JSON schema）。
std::string buildJudgeSystemPrompt();
// 单条打分 prompt（含真值 + 回答 + 要求输出 {"correctness":..,"professionalism":..,"tone":..,"structure":..,"note":".."}）。
std::string buildJudgePrompt(const QualityCase& c);
// 从 judge 回复解析 4 维分数（剥离围栏、解析 JSON、钳制 1..5、容错畸形）。
bool parseJudgeVerdict(const std::string& reply, JudgeVerdict* out);
```

### 4.3 离线工具：`tools/quality_eval.cpp`

流程（对齐 integration_check 的 `QEventLoop` + `runWithTimeout` 阻塞模式）：

1. 读 `ModelManager` 的强模型配置（judge 用强模型，与被评模型可不同）。
2. 加载 golden Q&A 集（JSONL 文件 + 内置最小集）。
3. 对每道题：若 `reply` 为空则用 `AiAdvisor` 现场生成（走真实 harness），再调强模型 `buildJudgePrompt` 打分、`parseJudgeVerdict`。
4. 打印 4 维均值表 + 每维最低分样本，作为 A1-A3 迭代的**人工验收线**。

不写进任何 CTest/CI。golden 集文件格式（每行一个 `QualityCase`）与内置最小集在 `quality_judge.cpp` 提供（内置集纯数据，可 CTest 校验格式）。

### 4.4 B4 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/quality_judge.h/.cpp`（新） | rubric/prompt/parser（纯逻辑） |
| `tools/quality_eval.cpp`（新） | 离线打分驱动 |
| `CMakeLists.txt` | 注册 `vgo_quality_eval` 目标（不进 CTest） |

**CTest（纯逻辑）**：`buildJudgePrompt` 含 4 维字段与真值；`parseJudgeVerdict` 正常/围栏包裹/畸形/缺字段 4 类输入。**离线人工**：跑 `vgo_quality_eval` 得 4 维基线，作为后续 A 系列迭代验收。

---

## 5. D-a · 画像 → 讲解侧重 ✅

### 5.1 纯逻辑映射

新增 `buildWeaknessDirective`（放 `src/ai/learner/`，复用 `WeakArea` 枚举与 `topWeakAreas`）：

```cpp
// 把 Top-N 弱项映射成「讲解侧重指令」（注入 system prompt），空 = 无侧重。
std::string buildWeaknessDirective(const std::vector<std::pair<WeakArea, double>>& topWeak);
```

映射规则（每弱项一条指令，示例）：
- `LifeDeath` → 「用户最近死活偏弱，涉及攻杀/做活的讲解务必带气数与眼位提醒。」
- `Ko` → 「用户最近在打劫上吃亏，涉及劫争的讲解务必说明劫材与得失。」
- `Counting` → 「用户数目偏弱，涉及形势的讲解务必逐块说明归属。」
- `Fighting` → 「用户战斗判断偏弱，讲解攻防时点明方向与厚薄。」
- `Endgame` / `Joseki` / `ShapeJudgment` → 对应侧重。

### 5.2 接线

- `ConversationRequest` 增 `std::string weakness_directive;`。
- `buildSystemPrompt`（http_conversation.cpp）在 `coach_memory` 之后追加 `weakness_directive`（措辞为「教学侧重（仅自然融入，不突兀）：…」）。
- `AiAdvisor` 增 `setWeaknessProvider(std::function<std::string()>)`；`deliverAnswer` 填 `req.weakness_directive = weakness_fn_ ? weakness_fn_() : ""`。
- `MainWindow` 接线：`setWeaknessProvider([this]{ return buildWeaknessDirective(learner_->model().topWeakAreas(3)); })`（隐私开关关闭/无数据时为空）。

与 D3.6 `coach_memory` 的关系：`coach_memory` 是「跨局事实摘要」（一句），`weakness_directive` 是「教学策略指令」（行为导向）；二者并列注入，不合并，保持语义清晰。

### 5.3 D-a 文件改动与测试

| 文件 | 改动 |
|---|---|
| `src/ai/learner/`（新文件或 learner_model.cpp） | `buildWeaknessDirective` |
| `src/ai/conversation.h` | `ConversationRequest` 增 `weakness_directive` |
| `src/ai/advisor.h/.cpp` | `setWeaknessProvider` + 填字段 |
| `conversation_http/http_conversation.cpp` | `buildSystemPrompt` 追加侧重 |
| `src/ui/MainWindow.cpp` | 接线 provider |

**单测（纯逻辑）**：`buildWeaknessDirective` 空向量→空；各 `WeakArea`→对应指令；Top-N 顺序；`buildSystemPrompt` 含侧重段。手测：低段位/弱项用户提问后，讲解自然带上对应侧重。

---

## 6. 测试矩阵（Batch 2 汇总）

| 层 | 覆盖 | 密闭性 |
|---|---|---|
| `tests/test_explain.cpp`（扩） | B1a 七形状判定 + renderExplanation | ✅ 纯逻辑 |
| `tests/test_phase.cpp`（新） | B2 阶段判定阈值/回退 | ✅ 纯逻辑 |
| `tests/test_sse.cpp`（新） | B3 SseParser 跨块/多 delta/[DONE]/tool_calls/容错 | ✅ 纯逻辑 |
| `tests/test_quality_judge.cpp`（新） | B4 prompt/parser | ✅ 纯逻辑 |
| `tests/test_weakness.cpp`（新） | D-a 映射 | ✅ 纯逻辑 |
| 离线工具 | B3 本地 SSE 冒烟；B4 `vgo_quality_eval` 打分 | 手动 |
| 手测 | B1a 真实局面形状；B2 三阶段 prompt；B3 逐字渲染；D-a 侧重自然融入 | 手动 |

**跨批验收**：Batch 2 完成后，用 `vgo_quality_eval` 的 4 维分数对比 Batch 1 基线（专业性/语气应有可见提升），作为客观验收线。

---

## 7. 风险与边界

| 风险 | 对策 |
|---|---|
| B1a 形状判定误报（几何启发式 vs 战术语义） | 经典局部形 golden 单测锁死；误报可接受（grounding 宁多勿错，校验闸门兜底数字）；不宣称战术正确 |
| B1a 新 FactType 未映射 `classifyBlunderArea` | §1.5 明确为必做，单测覆盖 |
| B2 阶段阈值初值偏粗 | 按 9/13/19 路真实对局校准后在 `test_phase` 锁定；仅作 prompt 提示，不参与真值 |
| B3 工具回合内容泄漏到 UI | 逐轮缓冲 + `abortAiStream()`；`SseParser` 单测 |
| B3 跨线程 delta 洪泛 | 批量回主线程（30–50ms 或每 K 条） |
| B4 judge 本身幻觉/偏差 | judge 用强模型、prompt 锚定真值、4 维分列、保留 note；仅作人工验收不自动门禁 |
| D-a 指令与 coach_memory 重复/冲突 | 二者语义分离（事实摘要 vs 行为指令），措辞区分 |

---

## 8. 文件改动清单（实现顺序）

1. **B1a**：`explain.h` 枚举 → `explain.cpp` 判定/渲染 → `learner_model.cpp` 映射。
2. **B2**：`phase.h/.cpp` → `conversation.h` 字段 → `advisor.cpp` 填充 → `http_conversation.cpp` 渲染。
3. **B3**：`winhttp_transport` 增量回调 → `SseParser` → `answerStream` 覆写 → `ChatPanel.abortAiStream` → `MainWindow` 接线。
4. **B4**：`quality_judge.h/.cpp` → `tools/quality_eval.cpp` → CMake 注册（不进 CTest）。
5. **D-a**：`buildWeaknessDirective` → `conversation.h` 字段 → `advisor` provider → `http_conversation` 注入 → `MainWindow` 接线。
6. 文档同步：`docs/长期任务清单.md`（L10/L9/L12 相关进展）、`VGO-设计优化评审报告.md` Batch 2 状态。
