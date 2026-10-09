#pragma once

#include <string>
#include <utility>
#include <vector>

#include "analysis.h"
#include "go/go_types.h"

namespace vgo {

enum class GamePhase { Fuseki, MiddleGame, Endgame };

// B2：按手数 + 领地固化度（ownership）启发式判段，纯逻辑、无 Qt。
//   - Fuseki：move_number < 2 * size。
//   - Endgame：ownership 非空且固化度 fraction(|ownership| > 0.8) > 0.55（主信号），
//     兜底 move_number > size * size / 2。
//   - 其余 MiddleGame。ownership 为空（fallback 引擎）时只用 move_number 阈值。
GamePhase classifyPhase(int move_number, int size, const OwnershipMap& ownership);

const char* phaseLabel(GamePhase p);  // "布局" / "中盘" / "官子"

// B2：手顺串——取最近 max_moves 手（不足则全部），渲染成「黑 H6、白 G5、…」。
// 虚着渲染为「虚着」。纯逻辑，供 deliverAnswer 与单测复用。
std::string buildMoveSequence(const std::vector<std::pair<Color, Move>>& moves, int size,
                              int max_moves = 10);

}  // namespace vgo
