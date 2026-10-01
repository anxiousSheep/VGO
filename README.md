# VGO — 围棋练习

一个用于练习围棋的桌面应用。支持 **本地双人对弈** 与 **人机对弈（可交流）**：AI 不只是下棋，还会解释自己，并且你可以随时用自然语言向它提问。

## 功能

- **完整规则**：落子、提子、气、禁着（自杀）、打劫、虚着、终局、数子计分 + 贴目。
- **本地双人对弈**：黑白双方在同一窗口轮流落子；也可当作单人摆棋工具。
- **人机对弈（可交流）**：
  - AI 落子后**主动解说**这手棋做了什么（提子 / 打吃 / 连接 / 延展等）。
  - 右侧聊天面板**随时提问**：「为什么下这里」「我该下哪」「谁领先」等。
  - **难度 1~10** 可调（弱棋更像人、会犯错）。
  - 内置**离线引擎 + 模板解说**，开箱即用；接入 KataGo 与 LLM 后升级为更强决策 + 自然语言讲解。
- **棋盘规格**：9 / 13 / 19 路（默认 9 路）。
- **形势显示**：接入 KataGo 后，点「形势」或问「谁领先」即可看到领地热力图、死子标记与「黑领先 X 目 · 黑方胜率 Y%」。
- **操作**：新局、悔棋、虚着、认输、请求数目（协商终局）、计分/终局；状态栏实时显示行棋方、手数、提子数。
- **终局协商**：双方（人或 AI）都可「请求数目」，另一方确认后按 KataGo 归属数子；对局达到一定手数（随棋盘大小，19 路约 350 手）后自动呼叫「智能裁判」判定胜负。

## 人机对弈怎么用

1. 菜单「对局 → 新局 → 人机对弈 · 执黑 / 执白」。
2. 「难度」菜单选择 1~10（入门 → 顶尖）。
3. 右侧「与 AI 交流」面板输入问题，或点快捷按钮「为什么下这里 / 我该下哪 / 谁领先」。

**开箱即用**（无需任何外部服务）：使用内置随机/启发式引擎 + 模板解说。
**升级到 KataGo + LLM**：见下方「配置」。

## 配置（可选，启用 KataGo / LLM）

把 `vgo_config.example.json` 复制为程序目录下的 `vgo_config.json` 并填写：

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

- **KataGo**：本地运行，通过 GTP 通信；`humanSLProfile` 在配置文件中标定难度。
- **LLM**：兼容 OpenAI 协议的任何端点（本地 Ollama / llama.cpp，或 DeepSeek、Qwen 等）。`base_url` 不含 `/chat/completions` 后缀。未配置时自动回退到模板解说。

## 目录结构

```
src/go/                 规则引擎（无 Qt、无 I/O，可独立单元测试）
src/ai/                 AI 核心（无 Qt）：分析/引擎/解说/会话编排
src/ai/engine_kata/     KataGo GTP 客户端（Qt 适配层）
src/ai/conversation_http/  LLM HTTP 客户端（Qt 适配层，OpenAI 兼容）
src/ui/                 桌面界面（Qt Widgets）：棋盘、聊天面板、主窗口
tests/                  引擎与 AI 单元测试（自研轻量框架）
docs/                   设计文档与技术路线
```

## 构建

依赖：CMake ≥ 3.16、Qt 6（Widgets + Network）、MSVC 2022（或兼容的 Qt 套件）。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

测试（引擎 + AI 无头测试）：

```bash
ctest --test-dir build -C Release --output-on-failure
# 或直接运行 build/Release/vgo_tests.exe 与 build/Release/vgo_ai_tests.exe
```

运行应用（需确保 Qt 的 `bin` 目录在 PATH 中，或对可执行文件执行 `windeployqt`）：

```bash
build/Release/vgo_app.exe
```

## Roadmap

- **下一步**：教学增强（`docs/人机对弈-可交流模式-设计.md` §12 M3b）——求指点半提示、局中预警、终局复盘、难度语气联动；随后打磨（M4：LLM 流式、延迟优化、全量打包验证）。
- **P1**：SGF 棋谱读写、死活题、让子对弈、复盘（回放/分支）。
- **P2**：更强的 AI（KataGo 全量分析 + LLM 流式讲解）、界面打磨。
