#pragma once

namespace vgo {

// L16：Windows 上 QNetworkAccessManager 首次发 HTTP 请求会枚举本机网卡，在特定网络
// 环境（VPN / Hyper-V / VMware 虚拟网卡 / 特定适配器）下，Qt6Network 的
// QNetworkInterface::interfaceNameFromIndex 会空指针解引用崩溃（0xC0000005），且该
// 崩溃发生在 QNAM 的工作线程，无法用 C++ try/catch 捕获，只能预防。
//
// 本函数在**主线程**同步强制 Qt 完成一次网卡枚举 + 每个索引的名称映射解析：
//   - 枚举正常 → true：网卡数据已缓存，后续 HTTP 请求复用，规避崩溃；
//   - 枚举触发原生访问违例 → 在 SEH 中捕获（仅 Windows），返回 false，调用方降级到
//     本地模板解说（TemplateConversation），保证 LLM 不可用时仍可用。
// 非 Windows 恒返回 true（崩溃点仅存在于 Windows 的 Qt6Network 实现）。
bool networkInterfaceSafe();

}  // namespace vgo
