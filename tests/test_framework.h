#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vgo_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
int& failures();

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void report_failure(const char* file, int line, const char* expr) {
    ++failures();
    std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, expr);
}

template <typename A, typename B>
inline void report_eq(const char* file, int line, const char* ea, const char* eb,
                      const A& a, const B& b) {
    std::ostringstream os;
    os << file << ":" << line << ": CHECK_EQ(" << ea << ", " << eb << ") failed: got "
       << a << ", want " << b;
    ++failures();
    std::fprintf(stderr, "%s\n", os.str().c_str());
}

}  // namespace vgo_test

#define VGO_TEST_CONCAT_(a, b) a##b
#define VGO_TEST_CONCAT(a, b) VGO_TEST_CONCAT_(a, b)

#define TEST(name)                                                                  \
    static void VGO_TEST_CONCAT(vgo_test_fn_, __LINE__)();                          \
    static ::vgo_test::Registrar VGO_TEST_CONCAT(vgo_test_reg_, __LINE__)(          \
        #name, VGO_TEST_CONCAT(vgo_test_fn_, __LINE__));                            \
    static void VGO_TEST_CONCAT(vgo_test_fn_, __LINE__)()

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ::vgo_test::report_failure(__FILE__, __LINE__, #cond);                \
        }                                                                         \
    } while (0)

#define CHECK_EQ(a, b)                                                            \
    do {                                                                          \
        const auto vgo_a = (a);                                                   \
        const auto vgo_b = (b);                                                   \
        if (!(vgo_a == vgo_b)) {                                                  \
            ::vgo_test::report_eq(__FILE__, __LINE__, #a, #b, vgo_a, vgo_b);      \
        }                                                                         \
    } while (0)
