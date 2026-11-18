#include <cstdio>
#include <cstring>

#include "TestFramework.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : ttest::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = ttest::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        std::printf("[ %s ] %s\n", ttest::failures() == before ? " OK " : "FAIL", c.name);
        ++ran;
    }
    std::printf("\n%d test(s), %d failure(s)\n", ran, ttest::failures());
    return ttest::failures() == 0 ? 0 : 1;
}
