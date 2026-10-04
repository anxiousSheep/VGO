#pragma once

#include <string>

namespace vgo {

// 单次 HTTP POST 的结果（WinHTTP 传输层，仅 Windows）。`ok` 为 true 表示请求完成
// （含任意 HTTP 状态码）；`status` 为 HTTP 状态码；`error` 在传输层失败（DNS / 连接 /
// 超时 / 取消）时非空。
struct WinHttpResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

// 同步 POST `body`（JSON）到 `url`（http/https），阻塞至响应完成或 `timeoutMs` 超时。
// 必须在工作线程调用（会阻塞）。用 WinHTTP 直连（不走系统代理），从根本上绕开
// Qt6Network 的 QNetworkConnectionMonitor 网卡枚举崩溃（L16）。
WinHttpResult winHttpPost(const std::string& url, const std::string& body,
                          const std::string& contentType, const std::string& authHeader,
                          int timeoutMs);

}  // namespace vgo
