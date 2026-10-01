#include "test_framework.h"

#include <cstdio>

namespace vgo_test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int& failures() {
    static int f = 0;
    return f;
}

}  // namespace vgo_test

int main() {
    int ran = 0;
    for (const auto& tc : vgo_test::registry()) {
        const int before = vgo_test::failures();
        tc.fn();
        ++ran;
        const int after = vgo_test::failures();
        std::printf("%s %s\n", (after == before) ? "[PASS]" : "[FAIL]", tc.name);
    }
    const int total = vgo_test::failures();
    std::printf("\n%d test(s) run, %d failure(s)\n", ran, total);
    return total == 0 ? 0 : 1;
}
