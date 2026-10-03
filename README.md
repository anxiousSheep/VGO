# VGO — 围棋练习

一个用于练习围棋的桌面应用（C++17 + Qt 6）。支持 **本地双人对弈** 与 **人机对弈（可交流）**：AI 不只是下棋，还会解释自己，并且你可以随时用自然语言向它提问。

## 功能

- **完整规则**：落子、提子、气、禁着（自杀）、打劫、虚着、终局、数子计分 + 贴目。
- **三种模式（标签页）**：摆棋（自由摆子 + AI 问答）、人机对弈（AI 落子 + 主动解说）、复盘（导入棋谱回看 / 试下）。
- **人机对弈（可交流）**：
  - AI 落子后**主动解说**这手棋做了什么（提子 / 打吃 / 连接 / 延展等）。
  - 右侧聊天面板**随时提问**：「为什么下这里」「我该下哪」「谁领先」等。
  - **难度 1~19**（新手 → 业余 1–9 段 → 职业 1–9 段），弱棋更像人、会犯错。
  - **猜先**：开局随机决定执黑方。
  - 内置**离线引擎 + 模板解说**，开箱即用；接入 KataGo 与 LLM 后升级为更强决策 + 自然语言讲解。
- **棋盘规格**：9 / 13 / 19 路（默认 9 路）。
- **形势显示**：接入 KataGo 后，点「形势」或问「谁领先」即可看到领地热力图、死子标记与「黑领先 X 目 · 黑方胜率 Y%」。
- **讲解增强**：能答深层问题——「为什么这步不好」给出胜率 / 目数变化，「为什么做不活」指出哪块棋并诚实说明能否救活（需 KataGo）。
- **棋盘演示**：「演示」进入试下态，摆出前几步或让「权威 AI 后续」续下，逐手确认 / 回退 / 退出，不污染真实对局。
- **流式讲解**：接入 LLM 后，讲解随落子逐字流式呈现（SSE）；未接 LLM 回退模板文本。
- **Skills 知识包**：按问题类型自动注入围棋领域知识（死活 / 落点 / 数子 / 打劫 / 术语），可从 `resources/skills/` 目录热更。
- **教学增强**：半提示（求指点只给方向）、局中预警（明显恶手提醒，可开关）、终局复盘（汇总恶手）、难度 ↔ 语气联动。
- **一键接入**：设置页可自动下载并安装 KataGo（官方 + 镜像双源、自动选 OpenCL / Eigen 变体）、自选模型、密钥 DPAPI 加密落盘、连接自检。
- **操作**：新局、悔棋、虚着、认输、请求数目（协商终局）、计分 / 终局、保存棋谱；状态栏实时显示行棋方、手数、提子数、当前引擎 / LLM。
- **终局协商**：双方（人或 AI）都可「请求数目」，另一方确认后按 KataGo 归属数子；对局达到一定手数后自动呼叫「智能裁判」判定胜负。

## 人机对弈怎么用

1. 顶部切到「人机对弈」标签页，菜单「对局 → 新对局…」。
2. 选定 棋盘大小 / 执子（执黑 / 执白 / 猜先）/ 难度（1~19）。
3. 右侧「与 AI 交流」面板输入问题，或点快捷按钮「为什么下这里 / 我该下哪 / 谁领先」。

**开箱即用**（无需任何外部服务）：使用内置随机 / 启发式引擎 + 模板解说。
**升级到 KataGo + LLM**：见下方「配置」。

## 配置（可选，启用 KataGo / LLM）

把 `vgo_config.example.json` 复制为 `vgo_config.json` 并填写；或直接在设置页（「设置」）完成接入与自检：

```json
{
  "katago": {
    "enabled": true,
    "binary": "C:/path/to/katago.exe",
    "model": "C:/path/to/b18c384nbt-humanv0.bin.gz",
    "config": "C:/path/to/gtp_human5k_example.cfg"
  },
  "llm": {
    "enabled": true,
    "base_url": "http://localhost:11434/v1",
    "api_key": "",
    "model": "qwen2.5:7b"
  }
}
```

- **KataGo**：本地运行，通过 GTP 通信；`humanSLProfile` 在配置文件中标定难度；设置页可一键自动下载安装。
- **LLM**：兼容 OpenAI 协议的任何端点（本地 Ollama / llama.cpp，或 DeepSeek、Qwen、Kimi、智谱等）。`base_url` 不含 `/chat/completions` 后缀。未配置时自动回退到模板解说。

## 构建

依赖：CMake ≥ 3.16、Qt 6（Widgets + Network）、MSVC 2022（或兼容的 Qt 套件）。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

测试（规则引擎 / AI 核心 / 模型层 / KataGo 解析，均无头）：

```bash
ctest --test-dir build -C Release --output-on-failure
```

运行应用（构建时已 `windeployqt` 部署 Qt 运行时，可直接双击 exe）：

```bash
build/Release/vgo_app.exe
```

## 目录结构

```
src/go/                 规则引擎（无 Qt、无 I/O，可独立单元测试）
src/ai/                 AI 核心（无 Qt）：分析/引擎/解说/会话编排/讲解增强/演示/Skills
src/ai/engine_kata/     KataGo GTP 客户端（Qt 适配层）
src/ai/conversation_http/  LLM HTTP 客户端（Qt 适配层，OpenAI 兼容 + SSE 流式）
src/ui/                 桌面界面（Qt Widgets）：棋盘、聊天面板、演示面板、主窗口
src/ui/model/           模型供给（Qt 层）：配置/密钥(DPAPI)/下载/GPU 检测/自安装
tests/                  引擎与 AI 单元测试（自研轻量框架）
tools/                  辅助工具（难度标定 spike）
docs/                   文档（长期任务清单 + archive/ 历史归档）
resources/skills/       Skills 知识包（可热更的 markdown 数据文件）
```

## 文档

- [`docs/长期任务清单.md`](docs/长期任务清单.md) — **长期任务 backlog（持续进行）**

> 历史设计与技术路线文档已归档至 [`docs/archive/`](docs/archive/)：
>
> - 人机对弈（可交流）模式设计、讲解增强与演示设计、技术路线（选型与任务计划）、模型接入（技术路线与任务计划）

## 长期任务（Roadmap）

详见 [`docs/长期任务清单.md`](docs/长期任务清单.md)。要点：

- **P0**：19 级难度真实 KataGo 标定（L1）、真实 KataGo / LLM 集成实测（L13）、**持续优化 LLM 模型的 Skills（L10）**。
- **P1**：眼形 / 要点死活求解（L3）、SGF 标准棋谱读写（L5）、让子对弈（L6）、复盘分支回放（L7）、LLM 半提示完整实现（L9）。
- **P2**：更细难度标定（L2）、死活题（L4）、叙事级复盘（L8）、Grounding 校验器（L11）、CommentaryPolicy（L12）、模型升级检查（L14）、界面打磨（L15）。
