// Tiny test framework (no external dependencies).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tt {

struct TestCase { const char* name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };

}  // namespace tt

#define TEST(name) \
    static void name(); \
    static tt::Registrar reg_##name(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("    CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); ++tt::failures(); } } while (0)

#define CHECK_MSG(cond, ...) \
    do { if (!(cond)) { std::printf("    CHECK failed: %s (%s:%d): ", #cond, __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++tt::failures(); } } while (0)
