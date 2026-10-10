# VGO · P0 批任务拆解（A1-A4）—— 可执行清单

| 项 | 内容 |
|---|---|
| 依据 | `../../VGO-设计优化评审报告.md` v1.1（含 §5 技术路线确认） |
| 范围 | Batch 1：A1 Skills 扩容 / A2 多轮记忆 / A3 输出契约 / A4 反馈环 |
| 纪律 | ① CTest 只进离线密闭纯逻辑；② 未配置 LLM 时降级链不劣化；③ 技能双源必须同步 |
| 建议排期 | 1-2 周；A1 与 A3 强耦合（few-shot 双锚定），建议同人同批；A2 可并行 |

---

## A1 · Skills 扩容 + few-shot（承接 L10）

### A1-1 新增 6 个知识包（`resources/skills/*.md`）

每包：YAML frontmatter（`id/name/description/applies_to/version`）+ **300-800 字正文** + **1-2 个 few-shot 问答示例**。

- [ ] `fuseki-joseki.md`：星/小目/三三布局常识、5-8 个常用定式及选择逻辑 → `applies_to: where-to-play, other`
- [ ] `middle-game.md`：攻防方向、急所 vs 大场、厚薄 → `applies_to: where-to-play, why-this-move`
- [ ] `tesuji-shape.md`：扳/断/尖/飞/虎/枷/征 + 好形/愚形 → `applies_to: move-quality, why-this-move`
- [ ] `endgame.md`：官子大小比较、先手官子 → `applies_to: where-to-play, other`
- [ ] `ko-fight.md`：劫材价值、劫争转换（加厚现有 `ko.md` 或独立） → `applies_to: other`
- [ ] `handicap.md`：让子局策略常识 → `applies_to: other`
- [ ] 存量 5 包（counting/ko/life-death/move-quality/terms）加厚至同规格

**配比约束**：正文 + 示例合计 ≤ 8192 字（`kMaxSkillBodyChars`，skill.h L11）；注意 `select()` 注入预算 4096 字（skill.h L67）下单轮最多注入 3 包（`select(cls, question, 3)`），单包过大将挤占其他包。

### A1-2 双源同步（易漏，单独列任务）

- [ ] `SkillRegistry::builtin()`（skill.cpp L97-161）内嵌同批 6 包 + 5 包加厚内容
- [ ] 确认 `fromText` 解析新 frontmatter 字段无兼容问题（`fromText` 兼容旧式头，skill.h L89-91）

### A1-3 golden 集扩充（D4.5 闭环）

- [ ] `builtinGoldenQuestions()`（skill_eval.cpp L58）每个新包补 ≥2 条 golden Q&A（覆盖「换种问法」的检索命中）
- [ ] `test_skill_eval` 全绿，命中率 ≥90% gate 不回退

### A1-4 验收

- [ ] `ctest -R skill` 全绿；`validateSkill` 对全部新包通过
- [ ] 菜单「重载技能知识」（MainWindow.cpp L376）热更新生效，无需重启
- [ ] 人工抽测：本地 Ollama 7B 下「开局怎么下」「这个劫该不该打」各 3 次，few-shot 输出格式稳定

---

## A2 · 多轮会话记忆

### A2-1 session 状态（放 service 层）

- [ ] `HttpConversationService` 新增 `std::vector<QJsonObject> session_history_`——**只存最终 `user`/`assistant` 回合**；`doChat` 工具环路的 assistant/tool 中间消息（`buildToolFollowup`）**不写入**
- [ ] `answer()` 组装 messages 改为 `[system] + [摘要?] + session_history_ 窗口 + [user]`
- [ ] 新对局 / 切模式 / `cancel()` 时清空 session（与 `generation_` 语义对齐）

### A2-2 窗口与摘要

- [ ] 滑动窗口：最近 6-8 轮；超窗部分压成一条 system「前情提要」（初版可用规则式摘要：保留各轮问题句 + 结论首句，避免再调一次 LLM）
- [ ] 窗口 token 与 `max_tokens_` 预算联动：历史超长先裁最旧轮次

### A2-3 D5.2 缓存兼容（关键正确性）

- [ ] `AiAdvisor::ask()` 的 `explain_cache_` 直返（advisor.cpp L102-107）加守卫：**session 非空时禁用直返**（或缓存键纳入会话轮次）——否则「那白棋不应呢」会串到「为什么下这里」的旧答

### A2-4 验收

- [ ] 单测（纯逻辑）：mock transport 断言 messages 组装顺序、窗口裁剪、中间态不入历史
- [ ] 手测追问链：「我该下哪」→「为什么」→「那白棋不应呢」指代可解析
- [ ] 投机预生成在新一手后无串答（回归 D5.2 场景）

---

## A3 · 分级讲解输出契约

### A3-1 三档结构契约（system prompt）

- [ ] `buildSystemPrompt` 按 `req.profile.level` 分三档写契约：新手档（结论→类比/术语→≤3 手变化→鼓励）/ 进阶档（结论→数据依据→关键变化→练习建议）/ 高手档（要点式）
- [ ] 移除「回答要简短」一刀切指令，改为「按上述结构作答」

### A3-2 max_tokens 分档

- [ ] `max_tokens_` 单成员 → `doChat` 内按 `profile.level` 选档（512 / 1024 / 768）；保留 `setMaxTokens` 作显式覆盖

### A3-3 双锚定

- [ ] A1 的 few-shot 示例按三档风格撰写（与契约一致），形成 prompt 契约 + 示例双锚定

### A3-4 验收

- [ ] 单测：prompt 构造按 level 分档、token 档选择正确（纯逻辑）
- [ ] 人工 rubric 对比（B4 离线工具就位前可用表格人工打分）：同局三档各 5 问，结构符合率 ≥80%

---

## A4 · 用户反馈环（👎 → 坏样本 → 改写闭环）

### A4-1 UI

- [ ] `ChatPanel` 每条 AI 回复气泡加 👍/👎 按钮；新增 `feedbackGiven(bool up)` 信号；反馈后按钮置灰防重复

### A4-2 last-reply 上下文留存

- [ ] `MainWindow`（或 ChatPanel）保留上一条回复的 `{原文 text, position_hash, truth_summary, model, 问题}`——这些数据目前只在 `AiAdvisor::deliverAnswer` 回调闭包里短暂存在，需在回调处透出（如给 `ConversationReply` 挂可选元数据字段，或由 MainWindow 在收到回复时同步快照局面）

### A4-3 差评落盘

- [ ] 👎 时构造 `BadSample{disposition="user_downvote", ...}` 写 `BadSampleStore`（复用 D2.5 管道，字段零扩展）
- [ ] 👍 暂不落盘（D4.6 无正反馈落点，本期不做）

### A4-4 反哺改写

- [ ] D4.6 `proposeRewrite` 的输入源纳入 `user_downvote` 样本（与 corrected/unverifiable 并列）

### A4-5 验收

- [ ] 差评后 `bad_samples.jsonl` 新增记录，字段齐全（原文/hash/真值摘要/model）
- [ ] 坏样本面板（BadSampleStore 查看入口）可见 user_downvote 条目

---

## 跨任务验收（Batch 1 Done 定义）

1. `ctest --output-on-failure` 全绿（含扩充后的 skill_eval；无新增联网测试）；
2. 未配置 LLM 的开箱即用路径（模板解说）回归无劣化；
3. 用 D2.5 积存 `bad_samples.jsonl` + A4 新反馈回放，人工对比 Batch 1 前后回答（专业性/语气两维，为 B4 离线 rubric 积累基准数据）；
4. 文档同步：`../../长期任务清单.md` L10 进展、Skills 迭代日志（L10 产出约定）记录本轮变更。
