// Dependency-free test helpers shared by the test executables.
#pragma once
#include <cstdio>

inline int g_checks = 0, g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failures++;                                                          \
      std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                        \
  } while (0)

inline int report() {
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
