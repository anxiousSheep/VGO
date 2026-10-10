# VGO · P0 批技术路线设计（A1–A4）

| 项 | 内容 |
|---|---|
| 依据 | `../../VGO-设计优化评审报告.md` v1.1（§5 技术路线确认）、`VGO-P0任务拆解-A1-A4.md` |
| 范围 | Batch 1：A1 Skills 扩容 / A2 多轮记忆 / A3 输出契约 / A4 反馈环（不含 B/C/D 批次） |
| 版本 | v0.1 |
| 日期 | 2026-10-08 |
| 状态 | 已实现（代码 + CTest 全绿；待真实 LLM 人工验收，见各任务 🧪） |

**设计纪律**（承自评审报告 §4 与 P0 拆解，实现与测试都必须守住）：

1. **CTest 只进离线密闭纯逻辑**——所有联网/引擎/强模型依赖一律走离线集成工具（对齐 `tools/vgo_integration_check`），不进 release gate。
2. **未配置 LLM 时降级链不劣化**——`TemplateConversation` 与开箱即用路径回归无变化。
3. **技能双源同步**——`resources/skills/*.md` 与 `SkillRegistry::builtin()` 两处必须一致。
4. **无 Qt、无 I/O 进 `../../../src/ai`**——平台层（持久化 / 面板）归 `../../../src/ui`。

---

## 0. 公共接口变更总览（先看爆炸半径）

A2/A3/A4 会触碰共享类型，先列出，便于评审一次看清影响面：

| 变更 | 位置 | 性质 | 影响 |
|---|---|---|---|
| `ConversationService` 增 `virtual bool hasHistory() const` | `../../../src/ai/conversation.h` | 新增虚函数，默认 `false` | 无破坏，子类可覆写 |
| `ConversationReply` 增 4 个反馈元数据字段 | `../../../src/ai/conversation.h` | 追加字段 | 无破坏（默认空） |
| `HttpConversationService::doChat` 增 `int max_tokens` 形参 | `conversation_http/http_conversation.h/.cpp` | 私有方法签名变更 | 仅内部递归，无外部调用方 |
| `max_tokens_` 默认值 1024 → 0（=自动分档） | `conversation_http/http_conversation.h` | 语义变更 | `setMaxTokens` 目前无调用方（已核实），安全 |
| 纯函数 `buildConversationMessages` / `compressSession` / `buildStructureContract` / `maxTokensForLevel` | `conversation_http/http_conversation.h` | 新增、暴露供单测 | 沿用 `buildUserPrompt`/`buildToolFollowup` 既有可测模式 |

---

## 1. A1 · Skills 扩容 + few-shot 🧪

### 1.1 目标与产物

- 新增 6 个知识包、加厚存量 5 个，每包 **300–800 字正文 + 1–2 个 few-shot 问答示例**。
- 双源同步 + golden 集扩充，`test_skill_eval` 命中率 ≥90% gate 不回退。

### 1.2 文件格式与 few-shot 约定

沿用 `Skill::fromText` 现有解析格式（`skill.cpp` L366-411）：**frontmatter 无前导 `---`**，字段（`id/name/description/applies/version`）在前，随后一行 `---` 分隔正文——`fromText` 把**首个** `---` 当作 header/body 分隔符，若文件以 `---` 开头，`id:` 会被解析进正文、`s.id` 为空、整包静默加载失败（与存量 5 包格式对齐）。**few-shot 是正文的一部分**（`fromText` 不解析它，只是内容约定），采用 `【示例】` 标记分隔：

```markdown
id: fuseki-joseki
name: 布局与定式
description: 星/小目/三三布局常识、常用定式与选择逻辑
applies: where-to-play other
version: 1
---
（正文 300–800 字：星/小目/三三的取舍、5–8 个常用定式、选择逻辑）

【示例】
问：开局第一手该下哪？
答：（按 A3 进阶档结构：结论→数据依据→关键变化→练习建议）

问：这个定式怎么选？
答：（同上）
```

- `applies` 取值 = `questionClassToString` 的 6 个串（`why-this-move`/`where-to-play`/`who-is-winning`/`move-quality`/`life-death`/`other`），空格分隔；空 = 全类。
- few-shot 必须与 A3 三档结构契约一致（见 §3.3 双锚定），否则 system prompt 里"契约说 A、示例说 B"会互相打架。

### 1.3 新增包与 `applies` 映射

| id | 覆盖 | applies | 服务的提问 |
|---|---|---|---|
| `fuseki-joseki` | 布局/定式 | `where-to-play other` | 「开局怎么下」「这个定式怎么选」 |
| `middle-game` | 攻防方向/急所/厚薄 | `where-to-play why-this-move` | 「现在该攻还是该守」 |
| `tesuji-shape` | 扳/断/尖/飞/虎/枷/征、好形/愚形 | `move-quality why-this-move` | 「这手妙在哪」「形怎么补」 |
| `endgame` | 官子大小、先手官子 | `where-to-play other` | 「收官先收哪」 |
| `ko-fight` | 劫材价值、劫争转换 | `other` | 「这个劫该不该打」 |
| `handicap` | 让子局策略 | `other` | 让子对弈联动 |

存量 5 包（`counting`/`ko`/`life-death`/`move-quality`/`terms`）加厚至同规格（正文 + few-shot）。

### 1.4 双源同步（易漏，独立成步）

两处数据源，**漏一处则 CTest 用 `builtin()` 测不到新包**：

1. `resources/skills/*.md` —— 热更源，`MainWindow::loadSkills()`（L1789-1812）从 3 个目录 `fromText` 加载并 `upsert`。
2. `SkillRegistry::builtin()`（`skill.cpp` L97-161）—— 内嵌源，`skill_eval`/CTest 的数据源。

实现时两者内容必须逐字一致；`ko-fight` 与现有 `ko` 重叠，决定"独立成包"（`ko` 保留规则一句话，`ko-fight` 讲劫材价值/转换）还是"加厚 `ko`"，**选独立成包**以免单个包膨胀顶满 8192。

### 1.5 golden 集扩充

`builtinGoldenQuestions()`（`skill_eval.cpp` L58-73）每新包补 ≥2 条，覆盖"换种问法"的检索命中：

```cpp
{"开局第一手该下哪",   QuestionClass::WhereToPlay, "fuseki-joseki"},
{"这个定式怎么选",      QuestionClass::Other,        "fuseki-joseki"},
{"现在该攻还是该守",    QuestionClass::Other,        "middle-game"},
{"这手棋妙在哪",        QuestionClass::WhyThisMove,  "tesuji-shape"},
{"收官先收哪",          QuestionClass::Other,        "endgame"},
{"这个劫该不该打",      QuestionClass::Other,        "ko-fight"},
...
```

每条 `expected_skill` 必须能被 `retrieve(question, 1)` 的 Top-1 命中（TF-IDF 词面匹配）；选词时优先用包 id/name/description 里出现的词，避免加包导致 `top1_hit_rate` 回退。**注意 `cls` 必须与 `classifyQuestion(question)` 实际输出一致**——`evaluateSkills`（skill_eval.cpp L28）会断言分类命中率，「现在该攻还是该守」「收官先收哪」这类被分类器判为 `Other` 的题若标成 `WhereToPlay` 会直接拖低 gate；`cls` 只影响分类断言，`expected_skill` 的检索断言（`retrieve`）与 `cls` 无关，改 `cls` 不影响技能命中验证。

### 1.6 体积/预算交互（关键约束）

`select(cls, question, 3)` 是**类别白名单优先、再补足检索 top-k、受 4096 字预算裁剪**（`skill.cpp` L251-273）。A1 加包后要重点盯 `avg_injected`（`SkillEvalReport`，每题为 token 代理）：

- `where-to-play` 类会白名单注入 `terms`（空=全类）+ `fuseki-joseki` + `middle-game` + `endgame` = **4 包**；若每包逼近 800 字，4 包 ≈3200 字，检索 top-k 基本被预算裁掉，语义检索失效。
- **缓解**：让新包正文控制在 600 字左右、few-shot 简短（2 个示例合计 ≤200 字），并观察 `avg_injected` 不显著上升。若仍挤爆，属 D4 范畴（`select` 优先级/预算重设计），**不在 A1 解决**，仅记录。

### 1.7 A1 文件改动与测试

| 文件 | 改动 |
|---|---|
| `resources/skills/*.md` | +6 新包、5 包加厚 |
| `../../../src/ai/skill.cpp` | `builtin()` 同步 6 新 + 5 加厚 |
| `../../../src/ai/skill_eval.cpp` | `builtinGoldenQuestions()` +~12 条 |
| 测试 | `ctest -R skill` 全绿；`validateSkill` 对 11 包全过；`avg_injected` 不恶化 |

无头文件/签名变更（纯内容 + 测试数据）。

**验收**：`ctest -R skill` 全绿 → 菜单「重载技能知识」（MainWindow.cpp L376）热更新生效 → 本地 Ollama 7B 抽测「开局怎么下」「这个劫该不该打」各 3 次，few-shot 输出结构稳定。

---

## 2. A2 · 多轮会话记忆 🧪

### 2.1 核心数据模型（放 service 层）

`HttpConversationService` 新增（会话状态，跨回合存活，`generation_`/`cancel()` 已具备）：

```cpp
// A2 多轮会话：只存最终 user/assistant 回合；工具环路中间消息（buildToolFollowup
// 产生的 assistant/tool）绝不入历史。
struct SessionTurn {
    QString user;       // 原始自然语言提问（非 buildUserPrompt 全文）
    QString assistant;  // 最终回答文本
};
std::vector<SessionTurn> session_;
QString session_summary_;          // 超窗压缩后的「前情提要」，可为空
static constexpr int kSessionWindowTurns = 6;  // 滑动窗口：最近 6 轮

void clearSession();               // 新对局 / 切模式调用
bool hasHistory() const override;  // 覆写 ConversationService 虚函数
```

**关键设计点**：history 存的是**原始提问**与**最终回答**，不是 `buildUserPrompt` 全文。理由：旧回合的棋盘/事实已陈旧（局面每手都在变），重发旧 prompt 既臃肿又误导；history 只为"指代连续性"（上一问讲到了哪），当前局面的 grounding 由本轮 `buildUserPrompt` 全新供给。

### 2.2 消息组装（抽纯函数，可单测）

`answer()` 组装改为：

```cpp
messages = [ {system: buildSystemPrompt(...)} ]
           + (summary 非空 ? [ {system: "前情提要：" + summary_} ] : [])
           + 窗口内每轮 [ {user: t.user}, {assistant: t.assistant} ]
           + [ {user: buildUserPrompt(req, cls)} ]
```

抽纯函数（沿用 `buildToolFollowup` 的可测模式，Qt-Core-only，暴露进 header）：

```cpp
QJsonArray buildConversationMessages(const QJsonObject& system,
                                     const QString& summary,
                                     const std::vector<SessionTurn>& turns,
                                     const QJsonObject& user);
```

### 2.3 窗口裁剪与摘要压缩（纯逻辑）

```cpp
// 规则式摘要（不额外调 LLM）：被裁轮次压成「问题句 + 结论首句」。
// 返回时 *turns 已裁到最近 max_turns 轮，*summary 累积了裁掉的前情。
void compressSession(std::vector<SessionTurn>* turns, QString* summary, int max_turns);
```

- 初版摘要用**规则式**（保留各轮问题句 + 结论首句，截断至固定长度），避免再调一次 LLM 的延迟与成本。
- 与 token 预算联动：窗口 token 估算超预算时先裁最旧轮（`buildConversationMessages` 前先 `compressSession`）。

### 2.4 历史写入时机（关键正确性）

`answer()` 包装回调，在**最终回答**处写一条历史（工具环路的中间回合不写、失败/降级回答不写）：

```cpp
const QString rawQuestion = QString::fromStdString(req.question);
auto wrapped = [this, rawQuestion, cb = std::move(cb)](ConversationReply r) mutable {
    if (!r.unsure && !r.text.empty()) {          // 只记成功的最终回答
        session_.push_back({rawQuestion, QString::fromStdString(r.text)});
        compressSession(&session_, &session_summary_, kSessionWindowTurns);
    }
    cb(std::move(r));
};
doChat(messages, 0, nowMs() + max_loop_ms_, /*max_tokens*/ mt, std::move(wrapped));
```

- 包装后的 `wrapped` 在工具环路/路由回退中只被**最终终点**调用一次（`doChat` 重入时传的是同一个 cb，见 http_conversation.cpp L396/L429），故不会重复记历史。
- `r.unsure`（校验不可验证灰显 / 传输失败）不记历史，避免污染后续上下文。

### 2.5 D5.2 投机缓存守卫（正确性必做）

`explain_cache_` 按 `positionHash` 键（advisor.cpp L102-107），投机预生成是**无上下文**的。一旦会话有历史，"为什么下这里"必须带上下文重新生成，否则「那白棋不应呢」这类追问会串到旧答。

- `ConversationService` 增 `virtual bool hasHistory() const { return false; }`，`HttpConversationService` 覆写。
- `AiAdvisor::ask()` 与 `explainLastMove()` 的缓存直返加守卫：`if (cls == WhyThisMove && !conversation_->hasHistory()) { …cache… }`。
- 首问（无历史）仍享零延迟缓存；会话开始后缓存旁路——语义正确，代价是会话内的追问不走投机缓存，属可接受。

### 2.6 会话生命周期

| 事件 | 动作 |
|---|---|
| `answer()` 收到成功最终回答 | `session_.push_back` + `compressSession` |
| 新对局 / 切换模式（人机↔摆棋↔每日一题） | `MainWindow` 调 `clearSession()` |
| `cancel()` | **不清 session**（cancel 是单请求取消，不是清对话） |
| 未配置 LLM（`TemplateConversation`） | `hasHistory()` 默认 false，无记忆、无劣化 |

### 2.7 A2 文件改动与测试

| 文件 | 改动 |
|---|---|
| `../../../src/ai/conversation.h` | `ConversationService` 增 `hasHistory()` |
| `conversation_http/http_conversation.h/.cpp` | 增 session 状态 + 纯函数 + 组装 + 写入 |
| `../../../src/ai/advisor.cpp` | 缓存直返加 `!hasHistory()` 守卫 |
| `../../../src/ui/MainWindow.cpp` | 新对局/切模式调 `clearSession()` |
| `../../../tests/test_conversation_memory.cpp`（新） | 见下 |

**单测（纯逻辑，无 transport）**：`buildConversationMessages` 组装顺序（system→摘要→窗口→user）；`compressSession` 窗口裁剪 + 摘要累积；中间态不入历史（直接测 SessionTurn 结构，不含 tool 消息）。D5.2 守卫测：`hasHistory()` 真/假时 `ask()` 是否走缓存（用注入的 ConversationService stub）。

**手测**：追问链「我该下哪」→「为什么」→「那白棋不应呢」指代可解析；投机预生成在新一手后无串答。

---

## 3. A3 · 分级讲解输出契约 🧪

### 3.1 三档结构契约（纯函数 + system prompt 注入）

抽两个纯函数（暴露进 header 供单测）：

```cpp
// A3：按 level 分三档的结构契约文本（含语气 + 结构 + 长度约定）。
QString buildStructureContract(int level);
// A3：按 level 分档的 max_tokens。新手/进阶/高手 = 512/1024/768。
int maxTokensForLevel(int level);
```

分档阈值（补上当前缺失的中间档）：

| 档 | level | max_tokens | 结构契约 |
|---|---|---|---|
| 新手 | `<= 6` | 512 | 一句话结论 → 一个类比/术语解释 → 一个最简变化（≤3 手）→ 一句鼓励 |
| 进阶 | `7..11` | 1024 | 结论 → 数据依据（胜率/目数变化）→ 关键变化 → 练习建议 |
| 高手 | `>= 12` | 768 | 要点式，直接给坐标与数据 |

`buildSystemPrompt`（http_conversation.cpp L28-79）：
- 保留人设（耐心/友善/客观）与"不编造"事实约束；
- **删除**"回答要简短、口语化"一刀切，删除现有两档语气分支（L39-43、L52-56），改为 `s += buildStructureContract(req.profile.level);`。

### 3.2 max_tokens 分档

- `max_tokens_` 默认值 1024 → **0（=自动）**；`setMaxTokens(0)` = 自动，`setMaxTokens(n>0)` = 显式覆盖。已核实 `setMaxTokens` 目前无调用方，改默认安全。
- `answer()` 解析 `const int mt = max_tokens_ > 0 ? max_tokens_ : maxTokensForLevel(req.profile.level);`，作为新形参传入 `doChat`，贯穿递归（工具选择回合与最终回合同 cap）。
- **修订（2026-10-09）**：初值 512/1024/768 过紧，讲解正文 + 注入 Skills + `<claims>` JSON 常超限触发 `finish_reason=length` 被截断。已上调为 **1024/2048/1024**，并在 `doChat`/`doChatStream` 增 `finish_reason=length` 检测：命中且未达 4096 上限时以足额上限重试一次（`kLengthRetryTokens=4096`）。`max_tokens` 只是上限，模型仍按 `stop` 自然结束，不会更啰嗦。

### 3.3 双锚定

A1 的 few-shot 示例按三档风格撰写，与 `buildStructureContract` 措辞一致——system prompt 的"结构契约"与 few-shot 的"示例格式"共同锚定输出结构，避免单锚定在小模型（Ollama 7B）上失效。

### 3.4 A3 文件改动与测试

| 文件 | 改动 |
|---|---|
| `conversation_http/http_conversation.h` | 暴露 `buildStructureContract`/`maxTokensForLevel`；`max_tokens_` 默认 0；`doChat` 增形参 |
| `conversation_http/http_conversation.cpp` | `buildSystemPrompt` 用契约；`answer` 解析 mt 并传入 |
| `../../../tests/test_http_tools.cpp`（或新） | 见下 |

**单测（纯逻辑）**：`buildStructureContract(5/9/13)` 返回三档不同契约且含关键结构词；`maxTokensForLevel(5/9/13)=512/1024/768`；`buildSystemPrompt` 在三分档下不再含"简短"字样。人工 rubric：同局三档各 5 问，结构符合率 ≥80%（B4 离线工具就位前的表格打分）。

---

## 4. A4 · 用户反馈环 🧪

### 4.1 反馈入口的现实约束（设计修正）

`ChatPanel` 是 **`QTextBrowser` 纯文本日志**（ChatPanel.cpp L57-83），消息不是独立气泡控件，**"每条气泡加按钮"需要把 ChatPanel 从 QTextBrowser 迁到消息列表控件**——那是 L15 界面打磨的范畴，**本期不做**。

本期采用**"上一条回复反馈"**：在输入行加 👍/👎 两个按钮，作用于**最近一条 AI 回复**；发送新问题后置灰、收到新回复后重新启用。这与 A4-2"last-reply 上下文留存"的单一回复范围天然一致。

```cpp
// ChatPanel 新增
void setFeedbackEnabled(bool on);   // 收到新回复后 true；发送新问题后 false
signals:
    void feedbackGiven(bool up);    // true=👍 false=👎
```

### 4.2 last-reply 上下文留存（透出 `ConversationReply` 元数据）

`ConversationReply`（conversation.h L123-131）追加 4 个字段：

```cpp
struct ConversationReply {
    // …现有字段…
    // A4：反馈上下文（👎 沉淀坏样本用；模板/离线回复可为空）
    std::string feedback_position_hash;
    std::string feedback_truth_summary;
    std::string feedback_model;
    std::string feedback_question;
};
```

`AiAdvisor::deliverAnswer`（advisor.cpp L337-416）已有 `positionHash(verify_board)`、`truthSummary(...)`、`trace_model_`、`question`，在 `cb(std::move(r))` 前写这 4 个字段即可——**零新增计算**。`TemplateConversation` 不填（空），差评仍可落盘（question + text 足以供人复盘）。

### 4.3 差评落盘（复用 D2.5 管道）

`MainWindow` 收到回复后存 `last_reply_meta_`（从 `ConversationReply` 反馈字段取），`chat_->setFeedbackEnabled(true)`；`feedbackGiven(false)` 时：

```cpp
BadSample s;
s.ts_ms = 当前时间;
s.position_hash = last_reply_meta_.position_hash;   // 可能空（模板回复）
s.question    = last_reply_meta_.question;
s.reply_text  = last_reply_meta_.text;
s.disposition = "user_downvote";
s.truth_summary = last_reply_meta_.truth_summary;
s.model       = last_reply_meta_.model;
bad_sample_store_->append(s);   // 复用已建的 store（MainWindow.cpp L84）
```

- `BadSample.disposition` 是 `std::string`（bad_sample.h L18），新增 `"user_downvote"` 值**零字段扩展**。建议在 `bad_sample.h` 加命名常量（`kDispositionUserDownvote`）替代魔法串，与 `"corrected"`/`"unverifiable"` 并列。
- 👍 暂不落盘（D4.6 无正反馈落点，本期不做）。

### 4.4 反哺改写（依赖 D4.6 驱动端，需注意）

已核实 `buildRewritePrompt(s, misses)` / `proposeRewrite(id, candidate)`（skill.cpp）**当前无任何 UI 调用方**——D4.6 的"模型提议改写"逻辑已写，但驱动端（读 golden 失分 + 坏样本 → 调 `buildRewritePrompt`）尚未接线。

因此 A4-4 分层处理：
- **本期可闭环**：`user_downvote` 样本进入 `bad_samples.jsonl`，在坏样本查看处可见（与 `corrected`/`unverifiable` 并列），供**人工**驱动改写。
- **自动改写**：需待 D4.6 驱动端就位后，把 `user_downvote` 的 `question` 并入 `misses` 喂给 `buildRewritePrompt`。本期只留接口约定（`misses` 输入源含 `user_downvote`），不阻塞 A4。

### 4.5 A4 文件改动与测试

| 文件 | 改动 |
|---|---|
| `../../../src/ai/conversation.h` | `ConversationReply` 加 4 反馈字段 |
| `../../../src/ai/advisor.cpp` | `deliverAnswer` 填反馈字段 |
| `../../../src/ai/verify/bad_sample.h` | 加 `kDispositionUserDownvote` 常量 |
| `src/ui/ChatPanel.h/.cpp` | 👍/👎 按钮 + `setFeedbackEnabled` + `feedbackGiven` 信号 |
| `../../../src/ui/MainWindow.cpp` | 连 `feedbackGiven` → 构 `BadSample` → `bad_sample_store_->append` |

**测试**：`bad_sampleToJsonl`/`parseBadSample` 往返带 `disposition="user_downvote"`（纯逻辑）；UI 手测：差评后 `bad_samples.jsonl` 新增记录且字段齐全、按钮置灰防重复。

---

## 5. 测试矩阵（Batch 1 汇总）

| 层 | 覆盖 | 密闭性 |
|---|---|---|
| `tests/test_skill_eval` | A1 golden 扩充后命中率 ≥90%、`avg_injected` 不恶化 | ✅ 纯逻辑 |
| `tests/test_conversation_memory`（新） | A2 组装顺序 / 窗口裁剪 / 中间态不入历史 / D5.2 守卫 | ✅ 纯逻辑（stub ConversationService） |
| `tests/test_http_tools`（扩） | A3 三档契约 / max_tokens 分档；A4 BadSample 往返 | ✅ 纯逻辑 |
| CTest 全量 | 无新增联网/引擎测试 | ✅ |
| 人工 | A1 Ollama few-shot 稳定性；A2 追问链指代；A3 结构符合率；A4 差评落盘 | 手动 |

**跨任务 Done 定义**（承 P0 拆解 §跨任务验收）：`ctest --output-on-failure` 全绿；模板解说回归无劣化；用 `bad_samples.jsonl` + A4 新反馈回放对比前后（专业性/语气，为 B4 积累基准）；文档同步（`../../长期任务清单.md` L10 进展 + skills 迭代日志）。

---

## 6. 状态机：一次多轮问答的完整数据流

```
用户提问 req.question
  → AiAdvisor::ask  (hasHistory()? 缓存守卫)
      └ deliverAnswer: 填 ConversationRequest + ConversationReply 反馈元数据
  → HttpConversationService::answer
      ├ 组装 [system + 摘要? + 历史窗口 + user(buildUserPrompt)]
      └ doChat(mt=分档max_tokens)
           ├ (工具回合) buildToolFollowup 追加 assistant/tool —— 不入 session_
           └ 最终回答 → wrapped cb:
                ├ 写 session_.push_back({rawQuestion, finalText}) + compressSession
                └ AiAdvisor 校验闸门 → ConversationReply(含反馈字段) → MainWindow
                     └ ChatPanel.appendAi + setFeedbackEnabled(true)
                          └ 👎 → BadSample(user_downvote) → bad_sample_store_
```

---

## 7. 风险与边界

| 风险 | 对策 |
|---|---|
| A1 加包挤爆 `select()` 4096 预算、语义检索失效 | 包正文 ≤600 字、few-shot 简短；观察 `avg_injected`；超限归 D4 另立 |
| A2 工具中间态误入历史 | `SessionTurn` 结构只存 user/assistant，写入点仅在最终 cb，单测锁死 |
| A2 投机缓存串答 | `!hasHistory()` 守卫（§2.5），单测 + 手测回归 |
| A2 摘要规则式丢信息 | 初版够用；后续可升级 LLM 摘要（需走离线工具，不进 CTest） |
| A3 自动 max_tokens 与未来配置冲突 | `setMaxTokens(n>0)` 仍是显式覆盖，配置启用即退自动 |
| A4 per-bubble 按钮不可行 | 明确降级为"上一条回复反馈"，气泡化迁 L15 |
| A4 自动改写无驱动端 | 本期只落盘 + 人工；自动改写依赖 D4.6 驱动端 |

---

## 8. 文件改动清单（实现顺序）

1. **A1**（内容层，可先行，与 A3 同人同批）：`resources/skills/*.md` → `skill.cpp::builtin()` → `skill_eval.cpp::builtinGoldenQuestions()`。
2. **A3**（纯逻辑，与 A1 双锚定）：`http_conversation.h/.cpp` 契约 + max_tokens 分档。
3. **A2**（记忆）：`conversation.h` 虚函数 → `http_conversation.h/.cpp` session → `advisor.cpp` 守卫 → `MainWindow.cpp` clearSession。
4. **A4**（反馈）：`conversation.h` 字段 → `advisor.cpp` 填字段 → `bad_sample.h` 常量 → `ChatPanel` 按钮 → `MainWindow` 落盘。
5. 文档同步：`../../长期任务清单.md` L10、skills 迭代日志。
