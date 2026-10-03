#include <QApplication>

#include "MainWindow.h"

#ifdef Q_OS_WIN
#include <cstdio>
#include <cstring>
#include <windows.h>

// 崩溃兜底：捕获未处理的 Windows 异常，把异常码 + 地址写入 exe 旁的 vgo_crash.log，
// 并弹框提示，用于定位「未响应后闪退」这类非确定崩溃。
namespace {
LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionAddress;

    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) > 0) {
        char* slash = strrchr(path, '\\');
        if (slash) snprintf(slash + 1, MAX_PATH - (slash + 1 - path), "vgo_crash.log");
    }
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "code=0x%08lX addr=%p\n", static_cast<unsigned long>(code), addr);
        fclose(f);
    }
    char msg[256];
    snprintf(msg, sizeof(msg),
             "VGO 崩溃：\n异常码 0x%08lX\n地址 %p\n\n已写入日志：\n%s",
             static_cast<unsigned long>(code), addr, path);
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
