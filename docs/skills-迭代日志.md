# Skills 迭代日志

> L10「持续优化 LLM 模型的 Skills」的迭代记录。每次改动知识包 / 评估集 / few-shot，在此追加一条，作为后续多模型适配与质量回溯的依据。

| 日期 | 变更 | 评估（`test_skill_eval`） |
|---|---|---|
| 2026-10-08 | A1 扩容：5 存量包（counting/ko/life-death/move-quality/terms）加厚并补 `【示例】` few-shot；新增 6 包（fuseki-joseki / middle-game / tesuji-shape / endgame / ko-fight / handicap），双源（`resources/skills/*.md` 与 `SkillRegistry::builtin()`）同步 11 包。golden 集 12→24 条。 | 分类命中率 100%（24/24）、Top-1 检索命中率 100%（22/22）、`avg_injected≈5.2` |

## 设计约定（承 `docs/VGO-P0技术路线设计-A1-A4.md` §1）

- 文件格式：frontmatter 无前导 `---`，字段 `id/name/description/applies/version` 在前，随后一行 `---` 分隔正文；`fromText` 以首个 `---` 为 header/body 分隔符。
- few-shot 是正文的一部分，用 `【示例】` 标记分隔，格式与 A3 三档结构契约（新手/进阶/高手）措辞一致，避免 system prompt 里「契约说 A、示例说 B」打架。
- 双源同步：改包必须同时改 `resources/skills/*.md` 与 `SkillRegistry::builtin()`，否则 CTest（用 `builtin()`）测不到新包。
- `applies` 取值 = `questionClassToString` 的 6 个串，空格分隔，空 = 全类。
- 体积预算：`select()` 受 4096 字预算裁剪，`where-to-play` 类会白名单注入 `terms` + `fuseki-joseki` + `middle-game` + `endgame` 4 包，正文宜 ≤600 字、few-shot 简短，避免语义检索被预算裁掉。
