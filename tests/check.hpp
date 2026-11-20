#pragma once
// Shared by the tests: prints one line per check and counts the failures; each test returns them.
#include <cstdio>

inline int failures = 0;

inline void expect(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
