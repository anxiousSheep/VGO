---
title: VGO-AI-Harness-2026-扩展路线
---

# VGO · 2026 Harness 概念下的 5 个 AI 扩展方向

> **状态（2026-10）**：本文的五个方向已全部落地，详细任务计划与状态见 [`docs/AI-Harness-2026-扩展-设计.md`](../AI-Harness-2026-扩展-设计.md)。本文保留为「为什么做、做什么」的原始设想（下方勾选框不再逐一回填）。

> 结论先行：VGO 的**围棋内核和 UI 已经相当扎实**，真正停在 2024 年的是「模型周边的运行时」——  
> 关键词分路由 + 静态 facts 灌 prompt + 单次同步调用 + 无记忆 + 无校验。  
> 2026 年的 harness 指的就是这一层：**让模型自己决定调什么工具、让事实可被校验、让用户被记住、让知识可被度量、让延迟被编排**。  
> 五个方向按此展开，每条都给到可勾选的功能代办。

**C++ 落地约束（必须先讲清）**：本项目无关 Python 侧链，harness 必须是 `src/ai/` 下**无 Qt 依赖**的纯逻辑层（与现有 `src/ai` 分层哲学一致），工具即现有 `EngineInterface` / `GoBoard` 之上的 C++ 函数。MCP 过重，本项目应当自建轻量 Tool Registry。

---

## D1 · Tool-Use Agent Loop：把「关键词分发」换成「模型调工具」

**现状证据**

| 位置                                  | 事实                                                                                                |
| ----------------------------------- | ------------------------------------------------------------------------------------------------- |
| `src/ai/conversation.h`             | `classifyQuestion()` 是硬编码中文关键词 `contains()` 顺序判定，源码注释写着「先于 WhyThisMove，避免『为什么下这里不好』被误判」——靠写死顺序打补丁 |
| `src/ai/advisor.cpp`                | `handleMoveQuality()` / `handleLifeDeath()`：**在调用 LLM 之前**，C++ 已决定要算什么、怎么算                        |
| `MoveEvaluator` / `LifeDeathResult` | 确定性能力已存在，但对模型不可见、不可组合                                                                             |

**本质**：系统能力集合 = 关键词命中集合。用户换个说法就掉进 `QuestionClass::Other`；多跳问题（「右上角能活吗，另外中腹谁大」）必然漏一半。

|        | 2024 范式（现状）        | 2026 Harness  |
| ------ | ------------------ | ------------- |
| 谁决定算什么 | C++ 端 `if/else` 预判 | 模型规划，自主发起工具调用 |
| 上下文    | facts 一次性灌满 prompt | 按需取用，用到才注入    |
| 复合问题   | 静默降级为一类            | 单轮内多跳，逐步取证    |

**功能代办**

- [ ] 新建 `src/ai/agent/{tool.h, tool_registry.h, loop.h}`，定义 `Tool { name, description, json_schema, invoke(args) -> ToolResult }`（纯逻辑、可单测）
- [ ] 把**已有确定性能力注册为工具**（写注册表，不重写能力）：
    - [ ] `analyze_position` → `AiAdvisor::analyzePosition`
    - [ ] `evaluate_move(point)` → `MoveEvaluator::evaluate`
    - [ ] `evaluate_last_move` → `MoveEvaluator::evaluateLastMove`
    - [ ] `life_death_solve(group)` → 现有 `LifeDeathAnalyzer`
    - [ ] `ownership_map` → `ownership.h`
    - [ ] `variation_pv(n)` → `variation.h`
    - [ ] `count_territory` → `src/go` 数子
    - [x] `sgf_lookup_position`（已落地：注册表，返回当前局面手顺 SGF + 结构化着法；棋谱库位置索引另行推进）
- [ ] `ConversationService` 增加 tool-capable 分支（OpenAI 兼容 `tools` / `tool_calls`）；**端点不支持工具时回退现有 C++ dispatch 路径**，保住 fallback 引擎 + 模板解说可用性
- [ ] 有界环路：建议 `max_steps = 4`、`max_wallclock_ms = 3000`、取消复用现有 `generation_` 语义
- [ ] 环路 trace 落 `agent_trace.jsonl`（为 D5 评估留证据）
- [ ] 单测：工具 JSON 参数解析、环路终止 / 超时 / 取消、非法坐标拒答

**验收指标**：100 题 golden 集（含改写说法与多跳题）命中率显著高于现状基线；环路 p95 退出耗时 < 3s。  
**UX 提升**：用户第一次可以**用自己的话提问而不会被答非所问**。

---

## D2 · Verify-then-Speak：把 Grounding 校验器从 P2 升格为一等门禁

**现状证据**：`ConversationReply { text, unsure, highlight }` 里有 `unsure` 却没有校验；`GroundedFacts` 各字段（`MoveEvaluation` / `LifeDeathResult`）是 C++ 算出的真值，但**模型输出不受任何约束**。L11「Grounding 校验器」现挂在 P2 backlog。

> 教学产品的特殊性：**讲错比不讲更糟**。用户无法分辨「黑领先 5 目」是引擎真值还是模型编的。

|      | 2024 范式（现状）        | 2026 Harness            |
| ---- | ------------------ | ----------------------- |
| 真值关系 | 事实塞进 prompt，祈祷模型照说 | 结论交还给确定性模块验证后才渲染        |
| 幻觉处置 | 无                  | 分三档：通过 / 自动修正并标注 / 降级标注 |
| 可信度  | 不可见                | 每句可溯源 hover             |

**功能代办**

- [ ] 输出结构化：要求模型同时产出可机读 claims（轻量方案：`<claims>{...}</claims>` JSON；或 strict schema 分支）
- [ ] `ClaimExtractor`：从回复中抽出断言 `{坐标, 目数, 胜率, 所指棋块}`
- [ ] `ClaimVerifier`：对照 `Board` 状态 + KataGo `Analysis` 逐条校验（建议容差：目数 ±1.0、胜率 ±5pp）
- [ ] 三档处置：一致→正常渲染；小偏差→**改写为引擎真值并标「已按引擎数据修正」**；不可验证→该句灰显 + 复用 `unsure`
- [ ] 证据挂载：UI 上每句讲解 hover 显示来源（KataGo 分析 / 规则引擎 / 模型推断），可一键跳对应 PV
- [ ] 拦截失败进 telemetry（D5），形成坏样本集

**验收指标**：grounded claim 精确率 ≥ 98%；幻觉句未拦截率 < 1%。  
**UX 提升**：从「看着像真的」变成「可以信」。这是所有教育类 AI 的分水岭。

---

## D3 · 跨局学习者画像：从两个标量到持久 Player Model

**现状证据**：`PlayerProfile { int level = 5; bool teaching = true; }`——只有两个标量，且 `level` 实际来自**难度滑块的静态设置**，并非对用户棋力的观测。关闭应用再打开，AI **完全不认识你**。

|      | 2024 范式（现状） | 2026 Harness             |
| ---- | ----------- | ------------------------ |
| 用户状态 | 单次会话内、两个标量  | 跨局持久化、可解释的弱项向量           |
| 难度   | 用户自己拖滑块     | 由观测棋力自动标定（保留手动 override） |
| 复习   | 无           | 从**用户自己的恶手**生成的间隔重复队列    |

**功能代办**

- [ ] `LearnerModel` 本地持久化（SQLite 或 jsonl；密钥沿用现有 DPAPI 机制）
- [ ] 每步异步 what-if（`MoveEvaluator`），**仅在 UI 空闲 / 低优先级线程**跑，不阻塞行棋，产出 blunder 序列
- [ ] 弱项向量：按 `FactType` + blunder 上下文打标签（战斗 / 死活 / 官子 / 定式 / 形势判断 / 打劫 / 收官目数），滚动窗口取近 20 局
- [ ] 真实棋力估计（平均目损 / 胜率偏离）→ **动态难度**，取代固定滑块
- [ ] 复习队列：自有恶手 + 题源构成 SM-2（简化）间隔重复，接「今日一题」入口
- [ ] 教练记忆注入 context：「你最近三次在打劫判断上吃亏」这类跨局事实进 prompt
- [ ] 隐私开关 / 导出 / 清空（本地桌面应用，这点必须做）

**验收指标**：连打 5 局后系统给出的 Top-3 弱项与人工复盘标注一致率 ≥ 70%。  
**UX 提升**：**「它记得我」**——这是重开应用时最强烈的差异感，也是最容易被现有版本暴露的短板。

---

## D4 · Skills 2.0：渐进式披露 + 语义检索 + 可度量自进化

**现状证据**：`Skill { id, name, applies_to, prompt }` + `SkillRegistry::forQuestion(QuestionClass)`——按类别白名单**整段注入**。三个后果：注入量不可控、多 skill 同时命中无冲突解决、L10「持续优化 LLM 的 Skills」因为没有度量而成为**永远完不成的持续任务**。

|      | 2024 范式（现状）       | 2026 Harness             |
| ---- | ----------------- | ------------------------ |
| 选择方式 | QuestionClass 白名单 | 语义检索 top-k + 证据          |
| 加载粒度 | 整段进 prompt        | frontmatter 常驻，body 按需加载 |
| 质量   | 人工主观修 md          | golden 集打分，版本化可回滚        |

**功能代办**

- [ ] skill 文件格式升级：YAML frontmatter（`id / name / description / applies_to / version`）+ body + 可选 `references/`
- [ ] 渐进式披露：常驻只加载 frontmatter，命中再读 body（省 token）
- [ ] 检索升级：embedding 语义检索优先，无 embedding 时用 TF-IDF 兜底；结果携带命中理由
- [ ] 组合与冲突解决：多命中时按优先级 + token 预算裁剪，超限按检索分砍
- [ ] 评估闭环：golden Q\&A 集（每题带 rubric + 引擎真值），跑 skill-on / skill-off 对比，产出每 skill 的 `hit-rate / Δquality` 指标表
- [ ] 自进化：低分样本沉淀进 skill 的 pending 区，支持一键采纳 / 模型提议改写——**L10 从此有了完成标准**
- [ ] 版本与回滚：文件带 `version` + 上次评估时间，设置页显示「该知识包上次评分 0.71」
- [ ] 热更加固：schema 校验 + 单文件体积上限 + 注入沙箱（防止一个 md 拖垮 prompt）

**验收指标**：Top-1 命中率 ≥ 90%；平均 prompt token 下降 ≥ 30%；每个 skill 有可复跑评分。  
**UX 提升**：讲解质量**随时间变好**，而不是靠作者手动改 markdown。

---

## D5 · 延迟与可靠性编排：投机预生成 + 明示降级 + 可观测

**现状证据**：单 `base_url` 单 `model`；每问一次同步调用；讲解与 KataGo 分析串行；更关键的是——接入失败时**静默回退到模板解说**，用户不知道自己拿到的质量档位是哪一个。历史上还有过「在途分析取消重启」的修复，说明引擎调用存在竞争。

|      | 2024 范式（现状） | 2026 Harness             |
| ---- | ----------- | ------------------------ |
| 延迟   | 提问后完整等待一次往返 | AI 思考期间投机预生成，落子瞬间呈现      |
| 降级   | 静默掉到模板      | 明示档位阶梯，用户知情              |
| 质量保障 | 靠人肉点一遍      | golden 集进 CI + trace 可观测 |

**功能代办**

- [ ] **投机预生成**：AI `genmove` 期间后台并行预跑 `explainLastMove` 的工具调用与讲解生成，缓存 key = position hash，命中近乎零延迟
- [ ] 模型路由：轻模型承担意图识别 / 工具选择，强模型做最终讲解；按本地 / 远端可用性与预算自动选路
- [ ] 优雅降级阶梯**明示化**：KataGo + 强 LLM → 本地 7B → 模板；UI 常驻「当前讲解档位」指示徽标
- [ ] KataGo 分析并发池 + 优先级队列，消除提问 vs 形势分析的互相 cancel
- [ ] 预算守护：每局 / 每日 token 上限 + 单次 `max_tokens` + 超时熔断，超限转模板并明确告知
- [ ] 可观测：trace 记录 `{position hash, route, steps, latency, verification result, cost, model}`，本地面板可查
- [ ] 离线评测：golden 集纳入现有 4 套 CTest，release gate = 回归通过

**验收指标**：讲解首字延迟 p95 < 800ms；档位全程可见；LLM 不可用时不静默。  
**UX 提升**：快的感知 + 不骗人的透明，这是「用起来像成品」的最后一块。

---

## 优先级与依赖总表

| 方向                       | 一句话                | 依赖                             | 建议批次                      | UX 冲击 |
| ------------------------ | ------------------ | ------------------------------ | ------------------------- | ----- |
| **D1** Tool-Use Loop     | 模型自己调工具            | 无                              | P0（第一批）                   | ★★★★★ |
| **D2** Verify-then-Speak | 讲的话先过真值校验          | D1（产生 structured claims）       | P0                        | ★★★★★ |
| **D5** 延迟与编排             | 投机预生成 + 明示降级 + 可观测 | 最好在 D1/D2 稳定后做“老 Appvs 新版对比评测” | P0（可先做降级明示与 telemetry 两项） | ★★★★☆ |
| **D3** 学习者画像             | 跨局记得你              | blunder 依赖 `MoveEvaluator`（已有） | P1                        | ★★★★☆ |
| **D4** Skills 2.0        | 知识包可检索可度量          | golden 集建议与 D2 共用              | P1                        | ★★★☆☆ |

**建议的最小第一步（半天到一天产量）**

1. 在 `src/ai/agent/` 建 `tool.h` + `tool_registry.h`，把 `evaluate_move` 与 `life_death_solve` 两个**已有**能力注册成工具——这是零新增算法、纯收益的动作。
2. 同时把「当前讲解档位」徽标做进 UI（D5 中最便宜的一项，立竿见影消除静默降级）。
3. 之后跑 D1 的有界环路，接 D2 校验闸门，此时 golden 集自然成型，再回头喂 D4 的评估闭环。

**不建议做的事**：为了对齐概念而去接 MCP / 引入 Python 侧链 / 上重型 Agent 框架。本项目 harness 的正确形态就是 `src/ai/` 里一层无 Qt 的 C++ 编排，这正是现有代码分层已经铺好的路。
