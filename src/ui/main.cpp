#include <QApplication>

#include "MainWindow.h"

#ifdef Q_OS_WIN
#include <cstdio>
#include <cstring>
#include <windows.h>

// 崩溃兜底：捕获未处理的 Windows 异常，把异常码 + 地址 + 所属模块 + 调用栈写入
// exe 旁的 vgo_crash.log，并弹框提示，用于定位「对局中闪退」这类访问违例。
namespace {
LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionAddress;

    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) > 0) {
        char* slash = strrchr(path, '\\');
        if (slash) snprintf(slash + 1, MAX_PATH - (slash + 1 - path), "vgo_crash.log");
    }

    // 崩溃地址所属的模块（能区分是 vgo_app.exe、Qt DLL 还是系统/显卡 DLL）。
    char module[MAX_PATH] = {0};
    HMODULE hmod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &hmod) &&
        hmod) {
        GetModuleFileNameA(hmod, module, MAX_PATH);
    }

    // 调用栈返回地址（无符号也能看出调用链轮廓）。
    void* frames[32];
    const USHORT nframes = CaptureStackBackTrace(0, 32, frames, nullptr);

    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "code=0x%08lX addr=%p module=%s\n",
                static_cast<unsigned long>(code), addr,
                module[0] ? module : "(unknown)");
        for (USHORT i = 0; i < nframes; ++i) fprintf(f, "  [%u] %p\n", i, frames[i]);
        fclose(f);
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
             "VGO 崩溃：\n异常码 0x%08lX\n地址 %p\n模块：%s\n\n已写入日志：\n%s",
             static_cast<unsigned long>(code), addr,
             module[0] ? module : "(unknown)", path);
    MessageBoxA(nullptr, msg, "VGO 崩溃", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashFilter);
#endif
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VGO"));

    MainWindow window;
    window.show();
    return app.exec();
}
