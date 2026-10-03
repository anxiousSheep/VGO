#include "test_framework.h"

#include "ai/conversation_http/network_probe.h"

using namespace vgo;

// L16 冒烟测试：在健康机器上，主线程网卡自检应通过（不崩溃、返回 true）。真正的崩溃
// 环境（VPN / 虚拟网卡）无法在 CI 复现，此测试锁定「自检代码本身在正常环境可用」，防止
// 后续改动把自检改成在健康环境也误报或崩溃。
TEST(network_probe_safe_on_healthy_host) {
    CHECK(networkInterfaceSafe());
}
