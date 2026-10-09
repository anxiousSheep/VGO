#include "winhttp_transport.h"

#include <vector>

#include <QString>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winhttp.h>
#endif

namespace vgo {

#ifdef Q_OS_WIN

namespace {

struct UrlParts {
    std::wstring host;
    int port = 0;
    std::wstring path;
    bool secure = false;
    bool valid = false;
};

UrlParts parseUrl(const std::string& url) {
    UrlParts p;
    const QUrl q(QString::fromStdString(url));
    if (!q.isValid()) return p;
    const QString scheme = q.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) return p;
    p.secure = (scheme == QLatin1String("https"));
    p.host = q.host().toStdWString();
    if (p.host.empty()) return p;
    p.port = q.port(p.secure ? 443 : 80);
    QString path = q.path();
    if (path.isEmpty()) path = QStringLiteral("/");
    if (q.hasQuery()) path += QLatin1Char('?') + q.query();
    p.path = path.toStdWString();
    p.valid = true;
    return p;
}

// 把 WinHTTP 的 GetLastError() 转成可读的英文/数字串（WinHTTP 错误优先，回退系统错误）。
std::string lastErrorText() {
    const DWORD err = GetLastError();
    wchar_t buf[512] = {};
    HMODULE h = GetModuleHandleW(L"winhttp.dll");
    DWORD n = 0;
    if (h) {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
                           h, err, 0, buf, 512, nullptr);
    }
    if (n == 0) {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, err, 0, buf, 512, nullptr);
    }
    QString msg = QString::fromWCharArray(buf, n > 0 ? static_cast<int>(n) : 0).trimmed();
    return "error " + std::to_string(err) + (msg.isEmpty() ? "" : ": " + msg.toStdString());
}

std::wstring toWide(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size())).toStdWString();
}

}  // namespace

WinHttpResult winHttpPost(const std::string& url, const std::string& body,
                          const std::string& contentType, const std::string& authHeader,
                          int timeoutMs) {
    // 非流式 = 流式 + 空回调（结果体在流式实现里已组装）。
    return winHttpPostStream(url, body, contentType, authHeader, timeoutMs, ChunkCallback{});
}

WinHttpResult winHttpPostStream(const std::string& url, const std::string& body,
                                const std::string& contentType, const std::string& authHeader,
                                int timeoutMs, const ChunkCallback& onChunk) {
    WinHttpResult result;
    const UrlParts p = parseUrl(url);
    if (!p.valid) {
        result.error = "invalid URL";
        return result;
    }

    // NO_PROXY：直连，不查询系统代理（避免在部分 Windows 网络环境下触发额外重试）。
    // WinHTTP 对本地 / 远程 http/https 都适用。
    HINTERNET hSession = WinHttpOpen(L"VGO/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        result.error = "WinHttpOpen failed: " + lastErrorText();
        return result;
    }
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, p.host.c_str(), static_cast<INTERNET_PORT>(p.port), 0);
    if (!hConnect) {
        result.error = "WinHttpConnect failed: " + lastErrorText();
        WinHttpCloseHandle(hSession);
        return result;
    }

    const DWORD flags = p.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", p.path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        result.error = "WinHttpOpenRequest failed: " + lastErrorText();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // 组装请求头：Content-Type + 可选 Authorization。
    std::wstring headers;
    headers += L"Content-Type: " + toWide(contentType) + L"\r\n";
    if (!authHeader.empty()) headers += toWide(authHeader) + L"\r\n";

    const LPCWSTR hdrPtr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    const DWORD hdrLen = headers.empty() ? 0 : static_cast<DWORD>(headers.size());
    const LPVOID bodyPtr = body.empty() ? nullptr : const_cast<char*>(body.data());
    const DWORD bodyLen = static_cast<DWORD>(body.size());

    if (!WinHttpSendRequest(hRequest, hdrPtr, hdrLen, bodyPtr, bodyLen, bodyLen, 0)) {
        result.error = "WinHttpSendRequest failed: " + lastErrorText();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        result.error = "WinHttpReceiveResponse failed: " + lastErrorText();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = static_cast<int>(status);

    // 循环读取响应体（LLM 回复受 max_tokens 约束，不会无限大）。
    std::string bodyOut;
    for (;;) {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
            result.error = "WinHttpQueryDataAvailable failed: " + lastErrorText();
            break;
        }
        if (bytesAvailable == 0) break;
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead)) {
            result.error = "WinHttpReadData failed: " + lastErrorText();
            break;
        }
        bodyOut.append(buf.data(), bytesRead);
        if (onChunk) onChunk(std::string(buf.data(), bytesRead));  // B3：增量回调
    }
    result.body = std::move(bodyOut);
    result.ok = result.status > 0 && result.error.empty();

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

#else  // !Q_OS_WIN

// 非 Windows：WinHTTP 不可用（本项目面向 Windows）。返回传输错误，调用方降级为模板解说。
WinHttpResult winHttpPost(const std::string&, const std::string&, const std::string&,
                          const std::string&, int) {
    WinHttpResult result;
    result.error = "WinHTTP unavailable on this platform";
    return result;
}

WinHttpResult winHttpPostStream(const std::string&, const std::string&, const std::string&,
                                const std::string&, int, const ChunkCallback&) {
    WinHttpResult result;
    result.error = "WinHTTP unavailable on this platform";
    return result;
}

#endif  // Q_OS_WIN

}  // namespace vgo
