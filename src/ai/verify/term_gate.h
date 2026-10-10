#pragma once

#include <string>

namespace vgo {

// G1-3 术语断言闸门（确定性，纯逻辑，进 CTest）。程序已对推荐点判定出 expectedTerm
// （如「点」= 点入三三）后，若回答正文使用了与 expectedTerm 不同的着法术语（扳/断/尖/
// 飞/跳/虎/点/挂/夹/贴/长/爬/挡/顶/觑/拆），且未使用 expectedTerm，则判违规——模型把
// 程序给的术语「改写」成了另一个术语（治现象 ①「R17 说成扳」）。
bool termGateViolation(const std::string& text, const std::string& expectedTerm);

}  // namespace vgo
