# VGO 设计优化评审报告

| 项 | 内容 |
|---|---|
| 评审对象 | [anxiousSheep/VGO](https://github.com/anxiousSheep/VGO)（master，2026-10-08） |
| 版本 | v1.1（含技术路线确认附录 §5，B1/B4/C1 等 11 处已按核对结论修订） |
| 已知问题 | LLM 讲解「不够专业、不够人性化」 |
| 评审方法 | 全量阅读 `src/ai/`、`src/ai/conversation_http/`、`src/ai/agent/`、`src/ai/verify/`、`src/ai/learner/`、`resources/skills/` 与全部 docs；全部代码引用经二次核对 |
| 结论先行 | 项目 harness（工具环路 / 校验闸门 / 画像 / 编排）已是 2026 水准；**讲解质量差的根因不在架构，而在「知识源、会话记忆、教学契约」三处内容层欠债**。另有 17 项可优化设计，按 P0/P1/P2 分批 |

---

## 1. 根因诊断：为什么讲解「不专业、不人性化」

以下每条都有代码级证据，按对问题的影响排序。（6 条诊断经独立核对全部成立。）

### 1.1 知识源几乎为空（「不专业」的第一根因）

`resources/skills/` 共 5 个文件、**合计 36 行**，全部围棋领域知识约 700 字：

| 文件 | 行数 | 实际知识量 |
|---|---|---|
| counting.md | 7 | 数子法一句话 + 贴目 7.5 |
| ko.md | 6 | 打劫规则一句话 |
| life-death.md | 8 | 「两眼活、一眼死」 |
| move-quality.md | 8 | 「胜率降就是坏棋」 |
| terms.md | 7 | 8 个术语单词表 |

**缺失整个专业体系**：布局与定式、中盘作战方向、手筋与棋形、官子、打劫进阶、让子局常识——全部为空白。LLM 的专业天花板 = 700 字知识包 + 模型自身常识，而项目默认场景（本地 Ollama 7B 级）的围棋常识恰恰最弱。L10 已把 Skills 迭代列为 P0 持续任务，但当前存量本质是 **stub，不是可迭代的知识库**。

> 注：技能存在**双源**——`resources/skills/*.md`（热更源，经 `MainWindow::loadSkills()` L1789 `fromText` 加载）与 `SkillRegistry::builtin()`（skill.cpp L97 内嵌，同时是 `skill_eval.cpp` golden 集 / CTest 的测试数据源）。任何扩容必须两处同步，见 A1。

### 1.2 对话完全失忆（「不人性化」的第一根因）

`HttpConversationService::answer()`（http_conversation.cpp L312-320）每次调用重建 `messages = [system, user]`——**没有任何多轮历史、没有摘要**。后果：

- 用户无法追问「那白棋不应呢？」「刚才那手呢？」——指代全部失效；
- AI 无法承接教学递进（上次讲到哪、用户哪里没听懂）；
- `coach_memory`（D3.6）是唯一跨轮信息，但它只是一句话，且只讲弱项不讲对话。

人性化对话的最低门槛是多轮上下文，当前为零。这是比「措辞生硬」更深一层的问题。

### 1.3 教学契约单薄，且被硬性压缩

`buildSystemPrompt()`（http_conversation.cpp L28-79）：

- 人设仅 3 句话（「耐心、友善的围棋老师」），**没有讲解结构约定**（结论→依据→变化→建议）；
- 语气联动只有两档阈值（`level <= 6` / `level >= 12`），中间档与「初学者/高手」的词汇表、讲解深度没有分级设计；
- 指令「回答要简短、口语化」+ `max_tokens_ = 1024`（http_conversation.h L72）——**「简短」与「专业细致」直接冲突**，模型被训练成给三行摘要，自然显得不专业。

### 1.4 事实抽取词汇贫瘠，grounding 撑不起专业讲解

`extractBoardFacts()`（explain.cpp）只识别 6 类结构事实（explain.h L12-19：提子 / 打吃 / 连接 / 延展 / 自紧气 / 普通落子）。**没有 扳、断、尖、飞、跳、虎、枷吃、征子 等着法识别，更没有意图标注**（攻击 / 防守 / 围空 / 破空 / 求活）。

LLM 拿到的 grounding 是「这是一手普通落子」，它能做的只有复述——讲解的专业度上限被 grounding 的贫瘠直接锁死。Verify-then-Speak（D2）保证了「不编造」，但也意味着：**事实层不供给的，模型就只能说正确的废话**。

### 1.5 无流式、无局面纵深

- **流式未接线**：`doChat` 写死 `body["stream"]=false`（http_conversation.cpp L345），`answerStream` 在全仓库**没有任何调用方**（L16 文档亦承认；README 的「流式讲解」属超前宣称）。`ChatPanel::beginAiStream/appendAiStream/endAiStream`（ChatPanel.cpp L225-247）已定义但从未被调用，逐字渲染链路是现成的。所有讲解非流式整段返回，远端 LLM 延迟 5-20 秒白板，「老师在讲」的拟人感为零。
- **prompt 无纵深**：`buildUserPrompt()` 只有 ASCII 棋盘 + 最近一手坐标——`Game::moves()`（game.h L58）现成但未用，没有手顺历史、没有局面阶段（布局 / 中盘 / 官子）判断。模型不知道「这盘棋是怎么走到这里的」。

### 1.6 评估闭环只测路由，不测讲解质量

D4.5 / D5.7 的 golden 集评估的是**分类命中率与检索命中率**；坏样本管道（D2.5）只接**校验闸门**（数字错没错），不接人。没有任何机制度量「讲得好不好」——没有质量 rubric，聊天面板也没有 👍/👎 反馈入口。**不可度量的东西无法迭代**。

---

## 2. 优化建议（按优先级）

> 已与现有 Roadmap（L1-L16、D1-D5）去重；标注「承接」的项是对已有规划的细化与提前。

### P0 · 讲解质量直接修复（预计 1-2 周）

#### A1. Skills 扩容 + few-shot 化（承接 L10，本报告第 1 优先级）

新建 6 个知识包，存量 5 个全部加厚，每包目标 **300-800 字正文 + 1-2 个 few-shot 问答示例**：

| 新包 | 覆盖 | 服务的提问 |
|---|---|---|
| fuseki-joseki.md | 星/小目/三三布局常识、5-8 个常用定式及选择逻辑 | 「开局怎么下」「这个定式怎么选」 |
| middle-game.md | 攻击与防守的方向判断、急所 vs 大场、厚薄 | 「现在该攻还是该守」 |
| tesuji-shape.md | 扳/断/尖/飞/虎/枷/征 + 好形/愚形 | 「这手棋妙在哪」「形怎么补」 |
| endgame.md | 官子大小比较、先手官子概念 | 「收官先收哪」 |
| ko-fight.md | 劫材价值、劫争转换 | 「这个劫该不该打」 |
| handicap.md | 让子局白方策略常识 | 让子对弈联动 |

实施要点：

- **双源同步**：新增包同时落到 `resources/skills/*.md`（热更源）与 `SkillRegistry::builtin()`（skill.cpp 内嵌源，也是 `skill_eval` / CTest 的数据源），漏一处则 CTest 测不到新包；
- **golden 集扩充**：`builtinGoldenQuestions()`（skill_eval.cpp L58）每包补 ≥2 条 golden Q&A，守住 ≥90% 命中率 gate；
- **体积配比**：`select()` 注入预算 4096 字、`validateSkill` 上限 8192 字（skill.h L11/L67），300-800 字/包天然兼容，但 few-shot 并入 body，需控制「正文 + 示例」配比，避免单包示例膨胀顶满 8192 挤压其他包注入；
- few-shot 是本地小模型（Ollama 7B）输出稳定性的最大杠杆（L10.3 已列出，建议提前）。

#### A2. 多轮会话记忆

- **会话级状态放 `HttpConversationService`**（服务对象跨回合存活，`generation_`/`cancel()` 已存在），比塞进 `ConversationRequest` 更干净；
- **历史只保留最终 user/assistant 回合**：`doChat` 工具环路会产生 assistant/tool 中间消息（`buildToolFollowup`），这些中间态**不得**进入跨轮 history，否则污染后续上下文；
- **滑动窗口**（最近 6-8 轮）+ 超窗**摘要压缩**（一条 system 消息承载「前情提要」），与 token 预算联动；
- **D5.2 缓存兼容**：`explain_cache_` 只按 `positionHash` 键（advisor.cpp L102-107），多轮语境下「那白棋不应呢」的语义随轮次变化——history 非空时禁用缓存直返，或将缓存键纳入会话轮次；
- 新对局 / 切换模式时清空 session（复用 `cancel()` 语义扩展）。

#### A3. 讲解输出契约（分级教学脚本）

把「回答要简短」替换为**按难度分三档的结构契约**：

- **新手档**：一句话结论 → 一个类比/术语解释 → 一个最简变化（≤3 手）→ 一句鼓励；
- **进阶档**：结论 → 数据依据（胜率/目数变化）→ 关键变化 → 练习建议；
- **高手档**：要点式，直接给坐标与数据。

`max_tokens_` 从单一成员改为 `doChat` 内按 `req.profile.level` 分档（512 / 1024 / 768），取代一刀切 1024——`buildSystemPrompt` 已拿到 level，改动面小、纯逻辑可单测。契约写进 system prompt + A1 的 few-shot 双重锚定。

#### A4. 用户反馈环（小而有界的改动）

聊天面板每条 AI 回复加 👍/👎，差评连同上下文沉淀进 `BadSampleStore`，作为 D4.6「模型提议改写」的输入。范围如实说明（非「零新代码」）：

- `BadSample.disposition` 是字符串字段，新增 `"user_downvote"` 零成本，`BadSampleStore` 复用；
- **新增**：`ChatPanel` 👍/👎 按钮 + `feedbackGiven(bool)` 信号（现不存在）；
- **新增**：last-reply 上下文留存——构造 BadSample 需要「上一条回复原文 + 局面 hash + 真值摘要」，这些目前只在 `deliverAnswer` 回调闭包里短暂存在，需在 `ChatPanel`/`MainWindow` 保留；
- 只做差评是合理取舍：👍 正反馈在 D4.6 管道无现成落点。

### P1 · 纵深与体验（预计 2-4 周）

#### B1. 着法语义识别升级——拆为三项，本轮只做 B1a

grounding 加深是讲解专业度的上限解锁项，但三种着法的可判定性本质不同，**不得打包排期**：

| 子项 | 内容 | 可判定性 | 排期 |
|---|---|---|---|
| **B1a 几何着法** | 扳 / 断 / 尖 / 小飞 / 大飞 / 跳 / 虎 | 纯局部几何（相对位置 + 邻接关系），低风险，`extractBoardFacts` 内可完成 | **本轮** |
| **B1b 征子 / 枷吃** | 征子（ladder）/ 枷吃（net） | 征子本质是「反复打吃能否逃脱」的**前瞻搜索**，单看一手前后局面无法判定；枷吃可识形但「可靠判定」需短搜索确认被枷子无法脱出。需小型模拟或模式库 | 单独立项，中工作量 |
| **B1c 意图标注** | 攻击 / 防守 / 围空 / 破空 / 求活 | 依赖 KataGo ownership / analysis，而 `extractBoardFacts(before, after, move, captured, mover)` 签名里没有这些数据——需扩展签名，或把标注挪到 `AiAdvisor::currentFacts`（那里才有 analysis）。**不是纯逻辑层能闭环的** | 随 B1b 或之后，另走一层 |

B1a 落地后 `renderExplanation` 即可从「这是一手普通落子」升级到「黑尖顶，守角兼攻白拆二」级别的事实供给，且纯逻辑无 Qt，与现有分层哲学一致。

#### B2. 局面阶段 + 手顺进 prompt

`buildUserPrompt` 增加：① 阶段判断（布局 / 中盘 / 官子，按手数 + 领地固化度的简单启发式即可，纯逻辑）；② 最近 8-12 手坐标序列（`Game::moves()` 直接可取，不必走工具）。改动集中在 `deliverAnswer` 填 `ConversationRequest` 新字段 + `buildUserPrompt` 拼接，收敛可控。讲解因此能有「前文」：「接上一步挂角，这手拆二生根」。

#### B3. 恢复 SSE 流式（L16 后续项 #1，建议提前）

`winhttp_transport` 读循环（L138-153）补 `WinHttpReadData` 增量回调 + SSE 解析，恢复 `answerStream`；`ChatPanel::beginAiStream/appendAiStream/endAiStream` 现成，接线即可。感知延迟从「5-20 秒白板」变为「即刻开讲」，是人性化体验收益最大的单项工程改动。

**边界约束**：流式只适用于**最终非工具回合**——`doChat` 有工具循环，中间 tool-call 回合必须缓冲不渲染（`toolCalls.isEmpty()` 且非 router 回退的最终回答回合才流式输出），否则逐字渲染会暴露中间态。

#### B4. 讲解质量评估——离线集成工具 + 人工验收（修订）

~~LLM-as-judge 进 CTest release gate~~ 的路线**与项目纪律冲突，已修订**：

- **冲突点**：设计文档 §10 明确 golden 集进 CTest 必须**离线密闭**（`harness_eval`/`skill_eval` 能进 CTest 正因为是纯逻辑：分类命中、TF-IDF 检索）；LLM-as-judge 依赖真实强模型调用，进 gate 会让无网络/无密钥环境下构建失败，也违背开箱即用降级链。
- **正确路线**：
  - `src/ai/` 纯逻辑层：judge prompt 构造、rubric 结构（正确性 / 专业性 / 语气 / 结构 4 维）、打分结果解析器——可单测，**CTest 只守这部分确定性内容**（rubric 定义合法 + 解析器正确）；
  - 实际打分走**离线集成工具**（对齐 `tools/vgo_integration_check` 形态，手动触发），不进构建管线；
  - 质量分数作为 A1-A3 每轮迭代的**人工验收线**（与 §3 验收建议口径一致）。

#### B5. 情境化主动教学（承接 L12 CommentaryPolicy）

- 主动点评触发器：胜率波动 > 15pp / 用户走出恶手（M3b 已有预警，可升级为点评）/ 妙手；
- 快捷提问 chips 随局面变化：布局期给「这个定式怎么选」、战斗期给「这块棋死了吗」、官子期给「先收哪个官」——降低提问门槛，也塑造「AI 懂棋」的观感。
- **成本控制**：与 L12「话痨/成本」同源对立，触发阈值必须保守（宁可漏评不可话痨），主动点评纳入 token 预算守护（D5.5）。

#### B6. 递进式提示（承接 L9）

半提示从一档升级为三档递进：方向（「往中腹看」）→ 要点（「要点在断」）→ 答案。`req.hint` 从 bool 扩为 tier(1/2/3) 可行，但**按问题类别区分「要点」来源**：

- 落点 / 形势类：可用引擎候选点顶替，本轮可做；
- 死活类：「要点」依赖 L3（眼形/要点死活求解，⬜ P1 未做），本轮降级为「方向 → 答案」两档，或待 L3 落地。

### P2 · 架构与生态（持续）

#### C1. 关键词路由的现状与可选架构改造（修订）

现状订正：`WhyThisMove` **已经不走**专用引擎调用——它只做一次投机缓存检查就落到 `deliverAnswer` → 工具环路。真正仍走 C++ 关键词预判 + 专用异步引擎的是 `WhereToPlay`（`requestMove`）、`WhoIsWinning`（`analyzePrioritized`），以及 `MoveQuality` / `LifeDeath` 的异步求值（advisor.cpp L98-165）。

这四者是 D1.9 明确标注的**必要例外**：`Tool::invoke` 是同步契约、引擎是异步，因此「统一到工具环路」的真正前提是**给 Tool 引入异步回调契约**——这是架构改造，不是顺手清理。取舍：

- **接受现状**（推荐）：D1.9 的同步/异步分工已有文档背书，关键词分类作为无工具端点的降级路径继续保留；
- **立项改造**：若未来工具种类继续增长、异步能力增多（如演示、死活搜索），单独立项做 async tool，不与 P0/P1 批次混排。

#### C2. prompt cache 分层

可行：system prompt 按「稳定 → 半稳定 → 动态」重排。两点现实约束：

- 稳定前缀**并不天然稳定**：`buildSystemPrompt` 按 `user_color` 分两支、按 level 追加语气文本，byte-identical 前缀只在「同一模式 + 同一档位」内成立——要吃到缓存需先把人设骨架与语气联动分层解耦；
- 价值**仅限云端点**（OpenAI / DeepSeek 有 prefix cache），本地 Ollama 无成本收益。

定位为纯降本项，优先级不高于 C3。

#### C3. 多提供商 failover

单 `baseUrl_` 扩为端点列表 + 超时/5xx 重试，保留 `generation_` 取消语义即可。注意主端点超时要设得激进（如 15-20s），否则 failover 总延迟反而伤体验。与 D5.3 模型路由正交。

#### C4. 跨平台抽象

`winhttp_transport.cpp` 已有 `#ifdef Q_OS_WIN` + 非 Windows stub（L8-11 / L163-171），抽传输层与密钥层接口（libcurl + Secret Service / Keychain 备选实现）是顺理成章，与 L16「另接 libcurl」衔接一致。纯重构、无路线错误，就是工作量大，维持 P2/持续。

#### C5. i18n

UI 文案与 prompt 模板全部硬编码中文。UI 侧抽 `tr()`、prompt 模板抽资源文件，为英文版留口。机械但面广，维持 P2。

#### C6. 棋谱库位置索引（文档已挂「另行推进」，建议立项）

`sgf_lookup_position` 目前只返回本局手顺。外延为「典型局面词典」（定式 / 常见布局的 SGF 索引），直接服务布局期讲解专业度。**真正的难点不在 SGF 读写（L5 已 ✅），而在局面匹配**：把当前角部局面哈希到定式索引需要 pattern-hash 索引 + 语料建设，本质是数据工程，不是简单字典。

### 画像驱动（穿插 P1，随批落地）

- **D-a. 画像 → 讲解侧重**：`LearnerModel::topWeakAreas()`（learner_model.h L92）已有 7 维弱项向量，把 `coach_memory` 从单句扩为「弱项 → 讲解策略指令」映射（如「死活弱 → 战斗讲解必带气数提醒」），写进 `buildSystemPrompt`。纯逻辑映射、可单测，是 D3.6 的自然延伸，**低成本建议随 Batch 2 落地**；
- **D-b. 每日一题 → 讲解联动**：`ReviewItem` 已带 `board_snapshot`/`blunder_move`/`lead_before`/`lead_blunder`（D3.5 字段齐全），答题失败后一键「让 AI 讲透这题」：从快照重建 `Game(Board, to_move)` 构 `ConversationRequest`。注意快照局面的 analysis 未预热，「讲透」会触发一次异步 analyze，属正常；
- **D-c. 练习路径**：基于弱项向量生成周练习计划，调度是纯逻辑零风险，但题源依赖 L4（死活题生成，⬜ P2），L4 落地后闭环。

---

## 3. 落地路线（三批）

| 批次 | 内容 | 预期效果 | 工作量 |
|---|---|---|---|
| **Batch 1（1-2 周）** | A1 Skills 扩容（双源同步 + golden 扩充）+ A2 多轮记忆 + A3 输出契约 + A4 反馈环 | 讲解「专业且像人」的最小闭环 | 中 |
| **Batch 2（2-4 周）** | B1a 几何着法 + B2 阶段/手顺 + B3 SSE 流式（仅最终回合）+ B4 质量评估（离线工具）+ D-a 画像侧重 | 专业度上限解锁 + 拟人感；迭代从此可度量 | 中大 | ✅ 已落地（2026-10-09，见 `VGO-P1技术路线设计-B1a-B2-B3-B4-Da.md`） |
| **Batch 3（持续）** | B5 主动教学 + B6 递进提示（落点类先行）+ C2/C3 降本可靠 + D-b 联动；B1b 征/枷、B1c 意图、C4 跨平台、C6 局面词典视立项排期 | 从「答得好」到「教得好」 | 持续 |

**验收建议**：Batch 1 完成后，用 D2.5 积存的 `bad_samples.jsonl` + A4 新增的用户反馈回放对比，以 B4 离线 rubric 分数（专业性 / 语气两维）作为人工验收线。

## 4. 不建议做的事（与项目既有纪律对齐）

- **不接 MCP、不引 Python 侧链 / 重型 Agent 框架**——现有轻量 Tool Registry 已够用，本报告所有建议都在该框架内完成；
- **不牺牲开箱即用**：所有 LLM 增强必须保持「未配置时回退模板解说」的降级链不劣化；CTest 只进离线密闭内容（B4 修订的核心动机）；
- **不做大而全的围棋知识图谱**：Skills 按「问题驱动」扩容（哪类提问答不好补哪包），避免无锚点的知识建设；
- **不把架构改造混进体验批次**：async tool 契约（C1）、跨平台（C4）单独立项，不阻塞 A/B 批次。

---

## 5. 附录：技术路线确认记录（v1.1）

独立核对结论：**§1 的 6 条根因诊断全部属实**（代码引用逐一验证通过）。以下为核对驱动的修订点与两项拍板决定。

### 5.1 修订点（11 处）

| 项 | 原文表述 | 修订后 | 原因 |
|---|---|---|---|
| A1 | 未提双源 | 补「双源同步 + golden 集扩充 + few-shot 体积配比」 | `builtin()`（skill.cpp L97）与 `resources/skills/*.md` 是两处数据源，漏一处 CTest 测不到 |
| A2 | 缓存键兼容一点 | 补「历史只存最终回合，工具中间态不入 history」「session 状态放 service 层」 | `buildToolFollowup` 的 assistant/tool 消息会污染跨轮上下文 |
| A4 | 「几乎零新代码」 | 改为「小而有界」：👍/👎 按钮、feedbackGiven 信号、last-reply 上下文留存均为新增 | 回复原文/局面 hash/真值摘要目前只在 deliverAnswer 闭包里短暂存在 |
| B1 | 打包为「局部几何可判定」一项 | 拆 B1a/B1b/B1c（见 5.2 决定 ②） | 征子需前瞻搜索、枷吃需短搜索确认、意图依赖 ownership 不在签名内 |
| B3 | 未提流式边界 | 补「仅最终非工具回合流式，中间 tool-call 回合缓冲」 | 否则逐字渲染暴露工具环路中间态 |
| B4 | 「LLM-as-judge 并列进 CTest release gate」 | 改为「离线集成工具 + 人工验收；CTest 只守 rubric 结构与解析器」 | 违反 §10 离线密闭纪律与降级链（见 5.2 决定 ①） |
| B5 | 未提成本 | 补「阈值保守 + 纳入 token 预算守护」 | 与 L12 话痨/成本控制同源对立 |
| B6 | 三档笼统可行 | 按问题类别区分：死活类「要点」档被 L3 阻塞，本轮降级 | L3 眼形/要点求解未做（⬜ P1） |
| C1 | 「WhyThisMove/WhereToPlay/WhoIsWinning 仍走 C++ 关键词预判 + 专用引擎调用」，统一是「清理残留」 | 订正现状（WhyThisMove 已在工具环路上）；统一的前提是 async Tool 契约，属架构改造，建议接受 D1.9 现状或单独立项 | 原文现状描述有误、工作量低估 |
| C2 | 未提前提 | 补「人设/语气分层解耦才有 byte-identical 前缀」「价值仅限云端点」 | 前缀按 user_color/level 分叉；Ollama 无成本收益 |
| C6 | 「外延为词典工具」 | 补「难点在 pattern-hash 局面匹配 + 语料，本质是数据工程」 | SGF 读写 L5 已完成，匹配才是瓶颈 |

### 5.2 两项拍板决定

**决定 ①：B4 不进入 CTest。** 质量评估走「离线集成工具 + 人工 rubric 验收」。CTest 只包含确定性内容：rubric 结构合法性、judge 输出解析器正确性（`src/ai/` 纯逻辑、可单测）。实际打分对齐 `tools/vgo_integration_check` 形态手动触发。若未来要在 CI 做自动化质量回归，需改用密闭代理（如参考答案的确定性相似度）——但那就不是 LLM-as-judge 了，另行论证。

**决定 ②：B1 拆分，本轮只做 B1a 几何着法（扳/断/尖/飞/跳/虎）。** 征子/枷吃（B1b，需搜索或模式库）与意图标注（B1c，需 ownership、扩 `extractBoardFacts` 签名或挪至 `AiAdvisor::currentFacts`）单独立项，不进 Batch 2 排期承诺。

### 5.3 已验证的关键代码事实（供实现引用）

- 技能双源：`SkillRegistry::builtin()`（skill.cpp L97）、热更 `MainWindow::loadSkills()`（L1789，`fromText` L1808）、菜单「重载技能知识」（L376-378）
- 体积约束：`kMaxSkillBodyChars = 8192`（skill.h L11）、`select()` 注入预算默认 4096（skill.h L67）
- golden 集：`builtinGoldenQuestions()`（skill_eval.cpp L58），命中率 ≥90% 为 release gate
- 流式现状：`body["stream"]=false`（http_conversation.cpp L345）；`ChatPanel::beginAiStream/appendAiStream/endAiStream`（ChatPanel.cpp L225-247）已定义无调用方
- 传输层平台抽象：`winhttp_transport.cpp` `#ifdef Q_OS_WIN`（L8-11）+ stub（L163-171）
- 画像：`topWeakAreas(int n = 3)`（learner_model.h L92）、7 维 `weak_[]`（L122）
- 手顺：`Game::moves()`（game.h L58）、`moveLog()`（L73，含已撤销手）
