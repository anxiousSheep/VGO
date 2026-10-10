#pragma once

#include <string>

namespace vgo {

// G4-3 泄露闸门（确定性正则，纯逻辑，进 CTest）：回答正文若出现定式判定书 / 仲裁的
// 内部字段名，说明模型把内部参数原文泄露给了用户（把「解释判定结果」执行成了「逐字段
// 朗读」）。这些 token 是英文内部键名，合法中文围棋讲解里绝不出现，故按子串匹配即安全。
// 这是模型「说不听」也拦得住的硬保险（与 D2 校验闸门并列，见诊断文档现象 ④）。
bool leakGateViolation(const std::string& text);

}  // namespace vgo
