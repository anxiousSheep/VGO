#include "network_probe.h"

#include <QNetworkInterface>

#ifdef Q_OS_WIN
#include <excpt.h>
#endif

namespace vgo {

#ifdef Q_OS_WIN
namespace {
// 同步枚举本机网卡并解析每个索引的名称映射——覆盖 Qt6Network 在 Windows 上崩溃（L16）
// 的两条内部路径：allInterfaces()（GetAdaptersAddresses）与
// interfaceNameFromIndex()（GetIfTable2 / 名称映射）。独立成函数以便放进 __try/__except
// （SEH 帧内不得有需栈展开的 C++ 对象）。
int enumerateInterfaces() {
    const QList<QNetworkInterface> ifs = QNetworkInterface::allInterfaces();
    int n = 0;
    for (const QNetworkInterface& iface : ifs) {
        (void)QNetworkInterface::interfaceNameFromIndex(iface.index());
        ++n;
    }
    return n;
}
}  // namespace
#endif

bool networkInterfaceSafe() {
#ifdef Q_OS_WIN
    __try {
        (void)enumerateInterfaces();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // 访问违例等原生异常被捕获：该网络环境会触发 Qt 网卡枚举崩溃，降级模板解说。
        return false;
    }
#else
    return true;
#endif
}

}  // namespace vgo
