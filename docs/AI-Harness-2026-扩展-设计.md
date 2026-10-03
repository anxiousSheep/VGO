# VGO · AI Harness 2026 扩展 — 设计与任务计划

| 项 | 内容 |
|---|---|
| 版本 | v0.1 |
| 日期 | 2026-10-03 |
| 状态 | **进行中**：D1 **基本落地**——工具注册表 + 多工具 HTTP 回填 + D1.8 局面快照预热（`prewarmAnalysis` 替代 ask 阻塞预取）+ D1.9 prompt 收敛（Other 走工具按需取）已上线并通过单测；剩 D1.10 `AgentLoop` 接线（生产环路由 `doChat` depth 兜底）待收口；D2（校验闸门）已落地并通过单测；D5.1 降级明示 + D5.5 预算守护已上线；**D3 已落地**（blunder 采集 → LearnerStore 持久化 → 教练记忆注入 → **D3.4 动态难度 + 新手难度动态爬坡**：`estimateKyuRank` + `humanProfileForStrength` 细粒度 `rank_20k→…→rank_1d` 阶梯 + `NewGameDialog` 自动难度开关 + `noviceLevelLabel` 新手档动态标注，消除新手→业余1段断崖）；**D2.4 证据溯源（hover 看来源 + 跳 PV）与 D5.6 可观测（trace 落盘 + 本地面板）已落地**；**D5.4 分析并发池 + 优先级队列（`EnginePool`）与 D5.2 投机预生成已落地**；**D4 评估闭环已落地**（D4.4 `select` 组合去重 + 预算、D4.5 golden 集 + `SkillEval` 评估器纳入 CTest release gate、D4.6 pending 区 + 一键采纳 + 评分回写、D4.7 `validateSkill` 校验 + 体积上限）；**L13 真实 KataGo/LLM 集成实测已完成** |
| 相关代码 | `src/ai/`（conversation / advisor / skill / evaluate / lifedeath / ownership / variation / explain / session / engine / strength）、`src/ai/conversation_http/`、`src/ui/`、`resources/skills/` |
| 上游文档 | `docs/VGO-AI-Harness-2026-扩展路线.md`（权威意见）、`docs/长期任务清单.md`、`docs/archive/`（历史设计与技术路线） |

## 状态图例

- ⬜ 未开始
- 🔄 已有部分基础，需按本设计改造
- ✅ 已完成（构建 + 单测通过）
- 🧪 已实现但待与真实服务实测

---

## 目录

1. [概述与现状诊断](#1-概述与现状诊断)
2. [总体架构：Harness 分层](#2-总体架构harness-分层)
3. [D1 · Tool-Use Agent Loop](#3-d1--tool-use-agent-loop)
4. [D2 · Verify-then-Speak](#4-d2--verify-then-speak)
5. [D3 · 跨局学习者画像](#5-d3--跨局学习者画像)
6. [D4 · Skills 2.0](#6-d4--skills-20)
7. [D5 · 延迟与可靠性编排](#7-d5--延迟与可靠性编排)
8. [优先级、依赖与分批实施](#8-优先级依赖与分批实施)
9. [与既有文档 / 长期任务清单的关系](#9-与既有文档--长期任务清单的关系)
10. [落地约束与「不做的事」](#10-落地约束与不做的事)
11. [风险与对策](#11-风险与对策)

---

## 1. 概述与现状诊断

### 1.1 结论

VGO 的**围棋内核与 UI 已经相当扎实**：规则引擎、AI 状态机、模板解说、KataGo GTP 客户端、LLM 客户端（OpenAI 兼容 + SSE 流式）、形势显示、棋盘演示、Skills 1.0、模型自安装均已落地并通过单测。

真正停在 2024 年的是**模型周边的运行时（harness）**，集中体现在五个现状：

1. **关键词分路由**：`src/ai/conversation.h` 的 `classifyQuestion()` 用硬编码中文关键词 `contains()` 顺序判定（死活 → 落点质量 → 形势 → 落点 → 为什么），源码注释明确写着「先于 WhyThisMove，避免『为什么下这里不好』被误判」——靠写死顺序打补丁。系统能力集合 = 关键词命中集合。
2. **静态 facts 灌 prompt**：`AiAdvisor` 在调用 LLM **之前**由 C++ 决定「算什么、怎么算」（`handleMoveQuality()` / `handleLifeDeath()`），`GroundedFacts` 一次性全量拼进 prompt。`MoveEvaluator` / `LifeDeathAnalyzer` 等确定性能力对模型不可见、不可组合。
3. **单次同步调用**：`HttpConversationService` 单 `base_url` + 单 `model`；每次问答一次完整往返；讲解与 KataGo 分析串行。
4. **无记忆**：`PlayerProfile { int level = 5; bool teaching = true; }` 只有两个标量，且 `level` 来自难度滑块的静态设置（`GameSetup.level`），关掉应用 AI 完全不认识用户。
5. **无校验**：`ConversationReply { text, unsure, highlight }` 里有 `unsure` 却没有校验；`GroundedFacts` 各字段（`analysis` / `move_eval` / `life_death` / `dead_groups`）是 C++ 算出的真值，但模型输出不受任何约束（即长期任务清单里的 L11，现挂 P2）。

### 1.2 一句话目标

把「让模型自己决定调什么工具、让事实可被校验、让用户被记住、让知识可被度量、让延迟被编排」这五件事，用一层 **`src/ai/` 下无 Qt 的 C++ 编排**补齐。

---

## 2. 总体架构：Harness 分层

### 2.1 落地约束（必须遵守）

- **无 Qt、无 I/O**：harness 核心放 `src/ai/`，与现有分层哲学一致；下载 / 持久化 / 密钥等平台层仍归 `src/ui/model/`。
- **不接 MCP、不引 Python 侧链、不上重型 Agent 框架**：本项目 harness 的正确形态是一层自建的轻量 Tool Registry + 有界环路。
- **工具即 C++ 函数**：把**已有**的确定性能力（`EngineInterface` / `MoveEvaluator` / `LifeDeathAnalyzer` / `ownership` / `variation` / `record` / 规则引擎计分）注册成工具，**重写注册表、不重写能力**。
- **降级可回退**：端点不支持工具 / 校验失败 / LLM 不可用，一律回到现有 C++ dispatch 与模板解说路径，保证「开箱即用」不被破坏。

### 2.2 分层图

```
用户自然语言
  → Agent Loop（有界工具环路：模型规划、自主发起工具调用）   [D1]
        ├─ Tool Registry：analyze / evaluate / life_death / ownership /
        │                variation_pv / count_territory / sgf_lookup …
        │      └─ 复用既有确定性能力（无新算法）
        └─ 按需取用 facts（用到才注入，不再一次性灌满）
  → Verify-then-Speak（ClaimExtractor → ClaimVerifier 对照真值，三档处置）[D2]
  → 讲解渲染（每句可溯源 hover）
  ── 支撑层 ──
  Learner Model（跨局画像 / 弱项向量 / 动态难度 / 复习队列）   [D3]
  Skills 2.0（渐进披露 + 语义检索 + 可度量自进化）            [D4]
  编排与可观测（投机预生成 / 模型路由 / 明示降级 / trace）      [D5]
```

### 2.3 新增数据模型与模块归属

| 模块 | 位置 | Qt | 说明 |
|---|---|---|---|
| `Tool` / `ToolResult` / `ToolRegistry` | `src/ai/agent/` | 无 | 工具描述 + JSON schema + `invoke` |
| `AgentLoop`（有界环路 + trace） | `src/ai/agent/` | 无 | max_steps / 超时 / 取消 |
| `Claim` / `ClaimExtractor` / `ClaimVerifier` | `src/ai/verify/` | 无 | 断言抽取 + 对照真值校验 |
| `LearnerModel`（纯逻辑） | `src/ai/learner/` | 无 | 弱项向量 / 棋力估计 / SM-2 |
| `LearnerStore`（持久化） | `src/ui/model/` | 有 | jsonl 落盘；隐私开关 / 导出 / 清空 |
| `Skill` 2.0 + `SkillIndex`（检索） | `src/ai/` | 无 | YAML frontmatter + body + version + score |
| 路由 / 预算 / 观测 | `src/ai/`（逻辑）+ `src/ui/`（面板） | 混合 | 见 D5 |

---

## 3. D1 · Tool-Use Agent Loop

> 把「关键词分发」换成「模型调工具」。

### 3.1 现状证据

| 位置 | 事实 |
|---|---|
| `src/ai/conversation.h` | `classifyQuestion()` 是硬编码中文关键词 `contains()` 顺序判定；靠写死顺序打补丁（「先于 WhyThisMove」） |
| `src/ai/advisor.cpp` | `handleMoveQuality()` / `handleLifeDeath()`：在调用 LLM **之前** C++ 已决定要算什么、怎么算 |
| `MoveEvaluator` / `LifeDeathAnalyzer` | 确定性能力已存在，但对模型不可见、不可组合 |
| `src/ai/conversation_http/http_conversation.h` | 🔄 **已有单个 `ToolHandler`**（function-calling，用于 `demonstrate_go_moves`），但**没有**通用 Tool Registry、没有多工具选择、没有有界环路 |

### 3.2 本质

系统能力集合 = 关键词命中集合。用户换个说法就掉进 `QuestionClass::Other`；多跳问题（「右上角能活吗，另外中腹谁大」）必然漏一半。

| | 2024 范式（现状） | 2026 Harness |
|---|---|---|
| 谁决定算什么 | C++ 端 `if/else` 预判 | 模型规划，自主发起工具调用 |
| 上下文 | facts 一次性灌满 prompt | 按需取用，用到才注入 |
| 复合问题 | 静默降级为一类 | 单轮内多跳，逐步取证 |

### 3.3 设计

**核心类型（`src/ai/agent/tool.h`）**

```cpp
struct ToolResult { bool ok; std::string json; std::string error; };

struct Tool {
    std::string name;              // "evaluate_move"
    std::string description;       // 喂给模型的自然语言说明
    std::string json_schema;       // 参数 schema（JSON）
    std::function<ToolResult(const std::string& args_json)> invoke;  // 纯逻辑
};
```

**注册表（`src/ai/agent/tool_registry.h`）**：`registerTool` / `find(name)` / `all()` / `schemasForModel()`。

**首批注册的既有能力**（写注册表，不重写能力）：

| 工具名 | 绑定 | 现状 |
|---|---|---|
| `analyze_position` | `AiAdvisor::analyzePosition` | ✅ 已有 |
| `evaluate_move(point)` | `MoveEvaluator::evaluate`（异步） | ✅ 已有，但**不注册为同步工具**——`Tool::invoke` 是同步契约、引擎是异步，落点质量改由 `handleMoveQuality` 异步求值后注入 prompt |
| `evaluate_last_move` | `MoveEvaluator::evaluateLastMove`（异步） | ✅ 同上（`handleMoveQuality` 处理） |
| `life_death_solve(group)` | `LifeDeathAnalyzer`（`lifedeath.h`） | ✅ 已有 |
| `ownership_map` | `ownership.h` | ✅ 已有 |
| `variation_pv(n)` | `variation.h` | ✅ 已有 |
| `count_territory` | 规则引擎计分（`go/game.h`） | 需确认暴露 API |
| `sgf_lookup_position` | `record.h` + L5 SGF | ⬜ 依赖 L5 SGF 落地后接 |

**环路（`src/ai/agent/loop.h`）**

- `AgentLoop::run(question, tools, on_step, on_done)`：模型 → 工具调用 → 结果回填 → 再问模型，直到模型给出最终答案或到达边界。
- 有界：`max_steps = 4`、`max_wallclock_ms = 3000`；取消复用现有 `generation_` 语义（`AiAdvisor` / `AIMatchController` / `MoveEvaluator` 已有此计数器，环路内再包一层）。
- 每步落 `agent_trace.jsonl`：`{ts, position_hash, step, tool, args, latency, route}`，为 D5 评估留证据。
- **端点不支持工具时回退**现有 C++ `classifyQuestion()` dispatch 路径，保住 fallback 引擎 + 模板解说。

**HTTP 侧改造**：`HttpConversationService` 已支持 function-calling（`setToolHandler`），D1 需要把它从「单回调」升级为「按 `ToolRegistry` 提供的 schema 声明多工具 + 循环回填」，仍走 OpenAI 兼容 `tools` / `tool_calls` 协议。

**harness 形态的关键约定（MCP → harness 落地口径）**

- **真相来源 = 局面级快照，而非按题预计算**：工具（`analyze_position` / `ownership_map` / `dead_groups` / `life_death_solve`）读的是 `AgentToolContext` 捕获的快照。快照由 `AiAdvisor` 在**每手落子后**用 `EnginePriority::Background` 的 `prewarmAnalysis`（与形势开关解耦，可被更新的预热合并）维护到 `last_analysis_`；`ask` 只读快照、**不按题阻塞预取**。快照缺失时工具回 `需要 KataGo 归属分析`、模型退到规则计分 + 棋盘事实——这是允许的回退，不是错误。
- **async 能力是同步契约的必然例外，不算 MCP 残留**：`evaluate_move` / `evaluate_last_move` 因 `Tool::invoke` 同步、引擎异步，不注册为工具，仍由 `handleMoveQuality` 前置异步求值注入 prompt（D1.3 已记）。
- **关键词分发降级为兜底**：sync 能力（`count_territory` / `life_death_solve` / `ownership_map` / `dead_groups` / `analyze_position`）的主路径交给工具环路；async 能力（`MoveQuality` / `LifeDeath` / `WhereToPlay` / `WhoIsWinning`）因 `Tool::invoke` 同步契约，仍由 `classifyQuestion` 前置识别 + 异步求值注入（必要例外）；`classifyQuestion` 同时作为「端点不支持工具 / LLM 不可用」时的回退 dispatch，保住模板解说与 fallback 引擎。

### 3.4 任务计划

| ID | 任务 | 产出 | 状态 |
|---|---|---|---|
| D1.1 | 定义 `Tool` / `ToolResult` / `ToolRegistry`（纯逻辑、可单测） | `agent/tool.h` `agent/tool_registry.h` + 单测 | ✅ |
| D1.2 | 注册 `evaluate_move` 与 `life_death_solve` 两个既有能力（最小第一步；`evaluate_move` 随后被 D1.3 移除） | 注册表 + 单测 | ✅ |
| D1.3 | 注册其余既有能力（analyze / ownership / count_territory；`evaluate_*` 因引擎异步、`Tool::invoke` 同步而不注册为工具，改由 `handleMoveQuality` 异步求值注入 prompt） | 注册表 + 单测 | ✅ |
| D1.4 | `HttpConversationService` 多工具声明 + 循环回填（OpenAI `tools`/`tool_calls`） | `conversation_http` | ✅ |
| D1.5 | `AgentLoop` 有界环路（max_steps / 超时 / 取消 / 不支持工具时回退 dispatch）——**仅单测，生产未接线**：实际环路由 `doChat` 的 depth 上限兜底 | `agent/loop.h` + 单测 | 🔄 |
| D1.6 | 环路 trace 落 `agent_trace.jsonl` | `agent/loop.h` | 🔄 |
| D1.7 | `sgf_lookup_position`（依赖 L5 SGF 落地后接） | 注册表 | ⬜ |
| D1.8 | 移除 `ask` 的阻塞式 `Interactive` 预取（MCP 回潮回归）；新增 `prewarmAnalysis`（`EnginePriority::Background`、与形势开关解耦）维护工具快照 + 抽 `groundFromAnalysis` 统一 `dead_groups`/`life_death` 口径 | `advisor.*` + 单测 | ⬜ |
| D1.9 | 关键词分发降级为兜底（sync 能力主路径交工具环路，async 能力仍由 `handle*` 前置注入）；`buildUserPrompt` 收敛（Other 只注入无引擎基础事实，胜率/候选点/死棋清单改由工具按需取） | `advisor` + `conversation_http` + 单测 | ✅ |
| D1.10 | `AgentLoop` 接线进 `doChat`（补墙钟超时 + D1.6 trace）或删除死代码 | `agent/loop.*` + `conversation_http` | ⬜ |

**单测要点**：工具 JSON 参数解析（含非法坐标拒答）、环路终止 / 超时 / 取消、工具结果回填、多工具回填（每个 `tool_call` 都有对应 `tool` 消息）、「Other 问题不再触发阻塞引擎预取」回归。

### 3.5 验收标准

- 100 题 golden 集（含改写说法与多跳题）命中率显著高于现状关键词基线。
- 环路 p95 退出耗时 < 3s。
- 端点不支持工具时，行为与现有版本一致（不劣化）。
- harness 完成判据：sync 能力走工具、`classifyQuestion` 仅作「端点不支持工具」时的兜底；`Other` 问题不再触发阻塞引擎预取（快照由 `prewarmAnalysis` 局面级维护）。

**UX 提升**：用户第一次可以**用自己的话提问而不会被答非所问**。

---

## 4. D2 · Verify-then-Speak

> 把 Grounding 校验器从 P2 升格为一等门禁。

### 4.1 现状证据

- `ConversationReply { text, unsure, highlight }` 有 `unsure` 却没有校验字段。
- `GroundedFacts`（`analysis` / `move_eval` / `life_death` / `dead_groups`）是 C++ 算出的真值，但**模型输出不受任何约束**。
- L11「Grounding 校验器」现挂 P2 backlog，无完成计划。

> 教学产品的特殊性：**讲错比不讲更糟**。用户无法分辨「黑领先 5 目」是引擎真值还是模型编的。

| | 2024 范式（现状） | 2026 Harness |
|---|---|---|
| 真值关系 | 事实塞进 prompt，祈祷模型照说 | 结论交还确定性模块验证后才渲染 |
| 幻觉处置 | 无 | 三档：通过 / 自动修正并标注 / 降级标注 |
| 可信度 | 不可见 | 每句可溯源 hover |

### 4.2 设计

**输出结构化**：要求模型同时产出可机读 claims（轻量方案：回复末尾 `<claims>{...}</claims>` JSON；具备 strict schema 的端点走 schema 分支）。claims 形如：

```json
[{ "type": "score_lead", "value": 5.5, "color": "black" },
 { "type": "winrate", "value": 0.62, "color": "black" },
 { "type": "group_life", "point": "C3", "alive": false }]
```

**校验链（`src/ai/verify/`）**

| 组件 | 职责 |
|---|---|
| `ClaimExtractor` | 从回复抽出断言 `{坐标, 目数, 胜率, 所指棋块}`（含 `<claims>` 解析 + 无结构化输出时对正文的坐标/数字正则兜底） |
| `ClaimVerifier` | 对照 `Board` 状态 + KataGo `Analysis` 逐条校验；容差：目数 ±1.0、胜率 ±5pp、坐标须合法且与所指棋块一致 |

**三档处置**

| 档 | 条件 | 渲染 |
|---|---|---|
| 通过 | 断言与真值一致 | 正常渲染 |
| 小偏差 | 数值在容差外但在可修正范围 | **改写为引擎真值**并标「已按引擎数据修正」 |
| 不可验证 | 无对应真值（如模型自造坐标） | 该句灰显 + 复用 `unsure` 诚实条款 |

**证据挂载**：`ConversationReply` 扩展溯源字段（每条 claim 带 `source`：`KataGo 分析 / 规则引擎 / 模型推断` + 对应 PV / 分析快照），UI 上每句讲解 hover 显示来源、可一键跳对应 PV。

**坏样本回收**：拦截失败进 telemetry（D5），形成坏样本集，反哺 D4 的评估闭环与 L10。

### 4.3 任务计划

| ID | 任务 | 产出 | 状态 |
|---|---|---|---|
| D2.1 | `Claim` / `ClaimExtractor` / `ClaimVerifier`（纯逻辑 + 单测） | `verify/` | ✅ |
| D2.2 | 输出结构化：`<claims>` JSON（或 strict schema 分支）进 prompt | `conversation_http` + 模板 | ✅ |
| D2.3 | 三档处置接线：改写标注 / 灰显 + unsure / 正常渲染 | `AiAdvisor` + `ConversationReply` | ✅ |
| D2.4 | 证据挂载：`source` 字段 + hover 溯源 + 跳 PV | `ChatPanel` / `GobanWidget` | ✅ |
| D2.5 | 拦截失败进 telemetry（坏样本集） | 与 D5 观测共用 | 🔄 |

### 4.4 验收标准

- grounded claim 精确率 ≥ 98%；幻觉句未拦截率 < 1%。
- 未接 LLM 时（模板解说）校验闸门为 no-op，不影响可用性。

**UX 提升**：从「看着像真的」变成「可以信」。这是所有教育类 AI 的分水岭。

---

## 5. D3 · 跨局学习者画像

> 从两个标量到持久 Player Model。

### 5.1 现状证据

- `PlayerProfile { int level = 5; bool teaching = true; }`——只有两个标量。
- `level` 实际来自难度滑块静态设置（`GameSetup.level`），**并非对用户棋力的观测**。
- 关闭应用再打开，AI 完全不认识你。
- **难度档位粒度不均**：`strengthForLevel(1)` 的新手 = `rank_20k`（20 级），而 `strengthForLevel(2)` = 业余1段 `rank_1d`——新手与业余1段之间隔着约 20 个棋力等级，一步跨度过大，新手「毕业」即面对强得多的 1d，缺少中间过渡，不利于练习。

### 5.2 设计

| | 2024 范式（现状） | 2026 Harness |
|---|---|---|
| 用户状态 | 单次会话内、两个标量 | 跨局持久化、可解释的弱项向量 |
| 难度 | 用户自己拖滑块 | 由观测棋力自动标定（保留手动 override） |
| 复习 | 无 | 从**用户自己的恶手**生成的间隔重复队列 |

**组件（纯逻辑 `src/ai/learner/`，持久化在 `src/ui/model/`）**

- `LearnerModel`：弱项向量 + 滚动窗口（近 20 局）+ 真实棋力估计（平均目损 / 胜率偏离）。
- 每步异步 what-if（复用 `MoveEvaluator` + `isBlunder`，**仅在 UI 空闲 / 低优先级线程**跑，不阻塞行棋），产出 blunder 序列。
- 弱项打标签：按 `FactType`（`explain.h`，现有 Captures/PutsInAtari/Connects/Extends/Placement/SelfAtari）+ blunder 上下文（战斗 / 死活 / 官子 / 定式 / 形势判断 / 打劫 / 收官目数）。
- **动态难度**：由观测棋力自动标定 `level`（保留手动 override）。
- **新手难度动态爬坡**：针对 5.1 的 `rank_20k`→`rank_1d` 断崖，把「新手」档拆成细粒度 HumanSL 阶梯 `rank_20k → rank_15k → rank_10k → rank_7k → rank_5k → rank_3k → rank_1k → rank_1d`，据 `LearnerModel::estimateLevel()` 的**连续强度估计**（而非粗粒度 1..19 level）逐档逼近业余1段，让 AI 难度随新手进步平滑爬升、无断崖。
- **复习队列**：自有恶手 + 题源构成 SM-2（简化）间隔重复，接「今日一题」入口。
- **教练记忆注入**：跨局事实（「你最近三次在打劫判断上吃亏」）进 prompt。
- **隐私三件套**：开关 / 导出 / 清空（本地桌面应用必须做）；密钥沿用 DPAPI 机制（`src/ui/model/KeyStore`）。

### 5.3 任务计划

| ID | 任务 | 产出 | 状态 |
|---|---|---|---|
| D3.1 | `LearnerModel` 纯逻辑：弱项向量 / 棋力估计 / 滚动窗口 | `learner/learner_model.*` + 单测 | ✅ |
| D3.2 | blunder 采集：UI 空闲时异步 what-if（复用 `MoveEvaluator`） | `AiAdvisor` + UI 线程策略 | ✅ |
| D3.3 | `LearnerStore` 持久化（jsonl）+ 隐私开关 / 导出 / 清空 | `ui/model/` | ✅ |
| D3.4 | 动态难度：观测棋力自动标定 level + **新手难度动态爬坡**（`rank_20k→…→rank_1d` 细粒度阶梯，消除新手→业余1段断崖；保留手动 override） | `NewGameDialog` + `LearnerModel` + `strength.*` | ✅ |
| D3.5 | 复习队列：SM-2（简化）+「今日一题」入口 | `learner/` + `ui/` | ⬜ |
| D3.6 | 教练记忆注入 prompt | `http_conversation` + `template_conversation` | ✅ |

### 5.4 验收标准

- 连打 5 局后系统给出的 Top-3 弱项与人工复盘标注一致率 ≥ 70%。
- 重开应用后画像仍在，且不阻塞行棋（异步采集）。
- 自动难度开启时，新手连续进步 AI 难度沿 `rank_20k→…→rank_1d` 阶梯平滑爬升、无断崖。

**UX 提升**：**「它记得我」**——重开应用时最强烈的差异感，也是最容易被现有版本暴露的短板。

### 5.5 新手难度动态爬坡（D3.4 细化）

**问题**：`strength.cpp` 的 19 级表把「新手」固定映射到 `rank_20k`（20 级），相邻的「业余1段」直接跳到 `rank_1d`，中间隔着约 20 个棋力等级。新手要么一直面对偏弱的 20k，要么一步升到偏强的 1d，缺乏平滑过渡。

**设计**：

1. **细粒度 HumanSL 阶梯（`strength` 层）**：新增 `humanProfileForStrength(double kyu)`，把连续强度估计映射到新手带阶梯
   `rank_20k → rank_15k → rank_10k → rank_7k → rank_5k → rank_3k → rank_1k → rank_1d`（`kyu=20` → `kyu=-1`，`kyu<=0` 即 1d 及以上）。这样「新手」档内部可连续逼近业余1段。
2. **连续强度估计（`learner` 层）**：`LearnerModel` 在现有 `estimateLevel()`（粗粒度 1..19）之外，新增 `estimateKyuRank()`（或返回 `{level, kyu}` 的结构），据滚动窗口的 `avg_loss_per_move` 线性映射到连续 kyu 值。
3. **D3.4 接线（UI 层）**：新对局时若「自动难度」开启，用 `estimateKyuRank()` 反查阶梯得到 `human_profile`（业余段及以上仍走现有 1..19 表），取代「新手=固定 20k」；用户手动拖滑块始终 override。
4. **降级回退**：无历史画像时仍回退默认新手档（`rank_20k`），不劣化开箱即用。

**实现与待办**：

- 已实现：`estimateKyuRank` / `humanProfileForStrength` / `snapKyuRung`（纯逻辑）+ `noviceLevelLabel`（新手档动态标注「新手（X级）」，已过新手时改趣味提示词、控制长度）+ `NewGameDialog` 自动难度开关；`LearnerStore::load` 在文件缺失/清空时重置画像，清空学习数据后新手段位回到 20 级。
- **一致性修复**：`estimateLevel` 与 `estimateKyuRank` 改用同源对数映射（`avg_loss 1.0 → 0 kyu = 1d = 业余1段`，`20 → 20k`），修复旧线性映射下「毕业（kyu=0）被误判为职业9段（level 19）」的断崖；无历史默认新手（level 1 / 20k）。
- **炸鱼惩罚（反炸鱼）**：`smurfGuardedLevel`——强手（估计 `业余1段` 及以上）**手动**选择新手档时，有 5% 概率匹配职业 AI（采用形势判断 AI 的段位 `analysisLevel`）；系统**自动**选档不触发，以此惩罚「开小号虐新手」的炸鱼棋手。
- KataGo HumanSL 的 `rank_*` 档位已随 L13 真实集成实测确认（`rank_20k/1d/3d/5d/7d/9d`），阶梯中的 `15k/10k/7k/5k/3k/1k` 可在真实 KataGo 上进一步校准。
- 与 L1/L2（难度标定）共用同一阶梯：L2 的「更细难度标定」落地后，本阶梯即为其新手带部分；对数映射为 L1 精确标定前的合理启发式。

---

## 6. D4 · Skills 2.0

> 渐进式披露 + 语义检索 + 可度量自进化。

### 6.1 现状证据

- `Skill { id, name, applies_to, prompt }` + `SkillRegistry::forQuestion(QuestionClass)`——按类别白名单**整段注入**。
- 三个后果：注入量不可控、多 skill 同时命中无冲突解决、L10「持续优化 Skills」因没有度量而成为**永远完不成的持续任务**。
- 知识包现为 `resources/skills/*.md`（counting / ko / life-death / move-quality / terms）。

### 6.2 设计

| | 2024 范式（现状） | 2026 Harness |
|---|---|---|
| 选择方式 | QuestionClass 白名单 | 语义检索 top-k + 证据 |
| 加载粒度 | 整段进 prompt | frontmatter 常驻，body 按需加载 |
| 质量 | 人工主观修 md | golden 集打分，版本化可回滚 |

**格式升级（YAML frontmatter + body）**

```markdown
---
id: life-death
name: 死活基础
description: 两眼活、一眼死、真眼假眼、眼位大小
applies_to: [life-death]
version: 1
last_score: 0.71
---
(body)
```

- **渐进式披露**：常驻只加载 frontmatter，命中再读 body（省 token）。
- **检索升级**：embedding 语义检索优先，无 embedding 时 TF-IDF 兜底；结果携带命中理由。
- **组合与冲突解决**：多命中时按优先级 + token 预算裁剪，超限按检索分砍。
- **评估闭环**：golden Q&A 集（每题带 rubric + 引擎真值），跑 skill-on / skill-off 对比，产出每 skill 的 `hit-rate / Δquality` 指标表。
- **自进化**：低分样本沉淀进 skill 的 pending 区，支持一键采纳 / 模型提议改写——**L10 从此有了完成标准**。
- **版本与回滚**：文件带 `version` + 上次评估时间；设置页显示「该知识包上次评分 0.71」。
- **热更加固**：schema 校验 + 单文件体积上限 + 注入沙箱（防止一个 md 拖垮 prompt）。

### 6.3 任务计划

| ID | 任务 | 产出 | 状态 |
|---|---|---|---|
| D4.1 | skill 文件格式升级（YAML frontmatter + version + last_score） | `skill.*` + 解析单测 | ✅ |
| D4.2 | 渐进式披露：frontmatter 常驻、body 按需加载 | `skill.*` + 注入路径 | 🔄 |
| D4.3 | 检索升级：embedding 优先 / TF-IDF 兜底，带命中理由 | `skill.*` 或 `ai/skill_index.*` | ✅ |
| D4.4 | 组合与冲突解决：优先级 + token 预算裁剪 | `skill.*` | ✅ |
| D4.5 | 评估闭环：golden Q&A 集 + skill-on/off 对比 + 指标表 | `skill_eval.*` + `tests/` | ✅ |
| D4.6 | 自进化：pending 区 + 一键采纳 / 模型提议改写 | `skill.*` + 设置页 | ✅（pending/采纳/评分；模型提议改写待 L13） |
| D4.7 | 版本与回滚 + 热更加固（schema 校验 / 体积上限 / 沙箱） | `skill.*` + `SettingsDialog` | ✅（校验/体积上限；版本回滚 UI 待 L15） |

### 6.4 验收标准

- Top-1 命中率 ≥ 90%；平均 prompt token 下降 ≥ 30%；每个 skill 有可复跑评分。

**UX 提升**：讲解质量**随时间变好**，而不是靠作者手动改 markdown。

---

## 7. D5 · 延迟与可靠性编排

> 投机预生成 + 明示降级 + 可观测。

### 7.1 现状证据

- 单 `base_url` 单 `model`（`HttpConversationService`）；每问一次同步调用；讲解与 KataGo 分析串行。
- 接入失败时**静默回退到模板解说**，用户不知道自己拿到的质量档位是哪一个。
- 历史上有过「在途分析取消重启」的修复（`generation_` 计数器遍布 `AIMatchController` / `AiAdvisor` / `MoveEvaluator`），说明引擎调用存在竞争。

### 7.2 设计

| | 2024 范式（现状） | 2026 Harness |
|---|---|---|
| 延迟 | 提问后完整等待一次往返 | AI 思考期间投机预生成，落子瞬间呈现 |
| 降级 | 静默掉到模板 | 明示档位阶梯，用户知情 |
| 质量保障 | 靠人肉点一遍 | golden 集进 CI + trace 可观测 |

- **投机预生成**：AI `genmove` 期间后台并行预跑 `explainLastMove` 的工具调用与讲解生成，缓存 key = position hash，命中近乎零延迟。
- **模型路由**：轻模型承担意图识别 / 工具选择，强模型做最终讲解；按本地 / 远端可用性与预算自动选路。
- **优雅降级阶梯明示化**：KataGo + 强 LLM → 本地 7B → 模板；UI 常驻「当前讲解档位」指示徽标。
- **KataGo 分析并发池 + 优先级队列**：消除提问 vs 形势分析的互相 cancel（现状已用 `generation_` 打补丁，D5 用队列正本清源）。
- **预算守护**：每局 / 每日 token 上限 + 单次 `max_tokens` + 超时熔断，超限转模板并明确告知。
- **可观测**：trace 记录 `{position hash, route, steps, latency, verification result, cost, model}`，本地面板可查。
- **离线评测**：golden 集纳入现有 4 套 CTest，release gate = 回归通过。

### 7.3 任务计划

| ID | 任务 | 产出 | 状态 |
|---|---|---|---|
| D5.1 | 降级档位徽标（最便宜、立竿见影，消除静默降级） | `MainWindow` / `ChatPanel` | ✅ |
| D5.2 | 投机预生成：genmove 期间并行预跑讲解，position hash 缓存 | `advisor` / `MainWindow` | ✅ |
| D5.3 | 模型路由：轻模型（意图/工具）+ 强模型（讲解） | `conversation_http` / 配置 | ⬜ |
| D5.4 | KataGo 分析并发池 + 优先级队列 | `engine_pool` / `AiAdvisor` / `MainWindow` | ✅ |
| D5.5 | 预算守护：token 上限 / max_tokens / 超时熔断 | `conversation_http` | ✅ |
| D5.6 | 可观测：trace 面板（`{position hash, route, latency, verify, cost, model}`） | `ui/` + 本地存储 | ✅ |
| D5.7 | golden 集纳入 CTest，release gate = 回归通过 | `tests/` + CI | ⬜ |

### 7.4 验收标准

- 讲解首字延迟 p95 < 800ms；档位全程可见；LLM 不可用时不静默。

**UX 提升**：快的感知 + 不骗人的透明，这是「用起来像成品」的最后一块。

---

## 8. 优先级、依赖与分批实施

| 方向 | 一句话 | 依赖 | 批次 | UX 冲击 |
|---|---|---|---|---|
| **D1** Tool-Use Loop | 模型自己调工具 | 无 | P0（第一批） | ★★★★★ |
| **D2** Verify-then-Speak | 讲的话先过真值校验 | D1（产生 structured claims） | P0 | ★★★★★ |
| **D5** 延迟与编排 | 投机预生成 + 明示降级 + 可观测 | 最好在 D1/D2 稳定后做「老 App vs 新版」对比评测 | P0（可先做降级明示 + telemetry 两项） | ★★★★☆ |
| **D3** 学习者画像 | 跨局记得你 | blunder 依赖 `MoveEvaluator`（已有） | P1 | ★★★★☆ |
| **D4** Skills 2.0 | 知识包可检索可度量 | golden 集建议与 D2 共用 | P1 | ★★★☆☆ |

**建议的最小第一步（半天到一天产量）**

1. 在 `src/ai/agent/` 建 `tool.h` + `tool_registry.h`，把 `evaluate_move` 与 `life_death_solve` 两个**已有**能力注册成工具——零新增算法、纯收益。
2. 同时把「当前讲解档位」徽标做进 UI（D5 中最便宜的一项，立竿见影消除静默降级）。
3. 之后跑 D1 的有界环路，接 D2 校验闸门；此时 golden 集自然成型，再回头喂 D4 的评估闭环。

---

## 9. 与既有文档 / 长期任务清单的关系

| D 方向 | 对应 / 覆盖的既有长期任务 |
|---|---|
| D1 Tool-Use Loop | 取代关键词分发；`sgf_lookup_position` 依赖 L5（SGF 读写） |
| D2 Verify-then-Speak | **将 L11（Grounding 校验器）从 P2 升格为 P0 一等门禁** |
| D3 学习者画像 | 新增；blunder 采集复用 `MoveEvaluator`（M3b.2 预警的升级） |
| D4 Skills 2.0 | **将 L10（持续优化 Skills）从「永远完不成」变为有度量、有完成标准** |
| D5 延迟与编排 | 覆盖 L13（真实集成实测）的评测方法（golden 集进 CI）；L9（LLM 半提示）在工具化后自然加强 |

> 详细设计以本文为准；长期任务清单里的 L 系列任务状态与本文同步更新（完成一项后两处一起改）。

---

## 10. 落地约束与「不做的事」

**必须遵守**

1. harness 核心全部在 `src/ai/`，无 Qt、无 I/O；平台层（下载 / 持久化 / 密钥 / 面板）在 `src/ui/` 与 `src/ui/model/`。
2. 工具是既有 `EngineInterface` / `GoBoard` 之上的 C++ 函数；**写注册表，不重写能力**。
3. 端点不支持工具、校验失败、LLM 不可用时，**回退现有 C++ dispatch + 模板解说**，开箱即用不被破坏。
4. 每项落地都带单测（沿用现有自研轻量框架 + CTest）。

**明确不做**

- 不为对齐概念而接 MCP。
- 不引入 Python 侧链。
- 不上重型 Agent 框架。

---

## 11. 风险与对策

| 风险 | 对策 |
|---|---|
| 工具环路发散 / 超时 | `max_steps = 4` + `max_wallclock_ms = 3000` + 复用 `generation_` 取消；超限回退单次 dispatch |
| 模型造坐标 / 编目数 | D2 校验闸门三档处置；不可验证即灰显 + `unsure` |
| 画像隐私 / 数据膨胀 | 本地 jsonl + DPAPI；隐私开关 / 导出 / 清空；滚动窗口限 20 局 |
| skill 注入失控 | 渐进披露 + token 预算裁剪 + 单文件体积上限 + schema 校验 |
| 异步竞态（讲解 vs 形势分析） | KataGo 并发池 + 优先级队列，取代零散的 `generation_` 补丁 |
| 评测成本 | golden 集进 CTest 离线跑；坏样本回收反哺评估闭环 |
