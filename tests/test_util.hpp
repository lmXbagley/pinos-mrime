#pragma once
// Минимальный тестовый фреймворк (без внешних зависимостей).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mpt {

struct TestCase { std::string name; void (*fn)(); };
inline std::vector<TestCase>& all() { static std::vector<TestCase> v; return v; }
inline int& failures() { static int f = 0; return f; }

inline void check(bool cond, const std::string& msg, const char* file, int line) {
  if (!cond) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, msg.c_str());
  }
}
inline bool near(double a, double b, double eps = 1e-6) {
  return std::fabs(a - b) <= eps;
}
inline void runAll() {
  for (const auto& t : all()) {
    int before = failures();
    std::fprintf(stderr, "RUN %s\n", t.name.c_str());
    t.fn();
    std::fprintf(stderr, "%-4s %s\n", failures() > before ? "FAIL" : "ok", t.name.c_str());
  }
}

} // namespace mpt

#define MPT_TEST(name)                                        \
  static void name();                                         \
  static mpt::Registrar reg_##name(#name, name);              \
  static void name()

namespace mpt {
struct Registrar { Registrar(const char* n, void (*f)()) { all().push_back({n, f}); } };
}
#define CHECK(cond, msg) mpt::check((cond), (msg), __FILE__, __LINE__)
