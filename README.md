# VGO — 围棋练习

一个用于练习围棋的桌面应用。当前为 **MVP 桌面版**：完整围棋规则 + 悔棋 + 本地双人对弈（单人也可摆棋），暂不含 AI。

## 功能（当前 MVP）

- **完整规则**：落子、提子、气、禁着（自杀）、打劫、虚着、终局、数子计分 + 贴目。
- **悔棋**：可逐手回退。
- **本地双人对弈**：黑白双方在同一窗口轮流落子；也可当作单人摆棋工具。
- **棋盘规格**：9 / 13 / 19 路（默认 9 路）。
- **操作**：新局、悔棋、虚着（停一手）、认输、计分/终局；状态栏实时显示行棋方、手数、提子数。

## 规则说明

- **打劫**：采用**位置超劫**（positional superko）——禁止重现此前出现过的任一局面（比简单劫更严格，属于合法规则变体，能保证对局必定终止）。
- **计分**：采用中国规则**数子法**（子 + 目），白方贴 7.5 目。

## 目录结构

```
src/go/       规则引擎（无 Qt、无 I/O，可独立单元测试）
src/ui/       桌面界面（Qt Widgets）
tests/        引擎单元测试（自研轻量框架，无第三方依赖）
```

## 构建

依赖：CMake ≥ 3.16、Qt 6（Widgets）、MSVC 2022（或兼容的 Qt 套件）。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

测试（引擎无头测试，无需 Qt 运行环境）：

```bash
ctest --test-dir build -C Release --output-on-failure
# 或直接运行 build/Release/vgo_tests.exe
```

运行应用（需确保 Qt 的 `bin` 目录在 PATH 中，或对可执行文件执行 `windeployqt`）：

```bash
build/Release/vgo_app.exe
```

## Roadmap

- **P1**：SGF 棋谱读写、死活题、让子对弈、自由摆棋模式、复盘（回放/分支）。
- **P2**：AI 引擎（蒙特卡洛 / UCT，或对接 KataGo）、更强界面打磨。
