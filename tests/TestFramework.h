// Minimal dependency-free test framework.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ttest {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace ttest

#define TT_CAT2(a, b) a##b
#define TT_CAT(a, b) TT_CAT2(a, b)
#define TEST(name)                                                                     \
    static void name();                                                                \
    static ::ttest::Registrar TT_CAT(tt_reg_, name)(#name, &name);                     \
    static void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++::ttest::failures();                                                     \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                              \
    } while (0)

#define CHECK_MSG(cond, msg)                                                           \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++::ttest::failures();                                                     \
            std::printf("    FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond,        \
                        std::string(msg).c_str());                                     \
        }                                                                              \
    } while (0)
