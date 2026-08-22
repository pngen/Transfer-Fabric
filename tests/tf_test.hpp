#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#  include <windows.h>
#endif

// -- A dependency-free, header-only test framework for Transfer Fabric.
namespace tf_test {

struct TestCase {
    std::string name;
    void (*fn)();
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

inline std::atomic<int>& failures() { static std::atomic<int> f{0}; return f; }
inline std::atomic<int>& checks() { static std::atomic<int> c{0}; return c; }
inline const char*& current() { static const char* c = ""; return c; }

inline void report_fail(const char* expr, int line) {
    ++failures();
    std::printf("  FAILED [%s] line %d: %s\n", current(), line, expr);
}

struct TestAbort {};

} // namespace tf_test

#define TF_TEST(name) \
    static void tf_test_##name(); \
    static ::tf_test::Registrar tf_reg_##name(#name, &tf_test_##name); \
    static void tf_test_##name()

#define TF_CHECK(expr) \
    do { ::tf_test::checks()++; if (!(expr)) ::tf_test::report_fail(#expr, __LINE__); } while (0)

#define TF_REQUIRE(expr) \
    do { ::tf_test::checks()++; if (!(expr)) { ::tf_test::report_fail(#expr, __LINE__); throw ::tf_test::TestAbort(); } } while (0)

#define TF_EQ(a, b) \
    do { ::tf_test::checks()++; auto _a_ = (a); auto _b_ = (b); \
         if (!(_a_ == _b_)) { ::tf_test::report_fail(#a " == " #b, __LINE__); } } while (0)

#define TF_NEAR(a, b, eps) \
    do { ::tf_test::checks()++; double _na_ = (a); double _nb_ = (b); \
         if ((_na_ > _nb_ ? _na_ - _nb_ : _nb_ - _na_) > (eps)) { ::tf_test::report_fail(#a " ~= " #b, __LINE__); } } while (0)

#define TF_THROWS(expr, category) \
    do { ::tf_test::checks()++; bool caught = false; transfer_fabric::ErrorCategory c; \
         try { (void)(expr); } catch (const transfer_fabric::TransferException& e) { caught = true; c = e.category(); } \
         if (!caught || c != (category)) { ::tf_test::report_fail(#expr " throws " #category, __LINE__); } } while (0)

#if defined(_WIN32)
inline LONG __stdcall tf_seh_handler(EXCEPTION_POINTERS* ep) {
    const char* name = "unknown";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case 0xC0000005: name = "access violation"; break;
        case 0xC00000FD: name = "stack overflow"; break;
        case 0x80000003: name = "breakpoint"; break;
        case 0xC000001D: name = "illegal instruction"; break;
        case 0xE06D7363: name = "c++ exception"; break;
    }
    std::printf("\nSEH exception 0x%08X (%s) at 0x%p\n",
                ep->ExceptionRecord->ExceptionCode, name,
                ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

inline void install_seh() {
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(&tf_seh_handler);
#endif
}

inline int tf_test_main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (auto& t : ::tf_test::registry()) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        ::tf_test::current() = t.name.c_str();
        int before = ::tf_test::failures().load();
        std::printf("[ RUN  ] %s\n", t.name.c_str());
        try { t.fn(); }
        catch (const ::tf_test::TestAbort&) {}
        catch (const std::exception& e) { ::tf_test::report_fail(e.what(), 0); }
        catch (...) { ::tf_test::report_fail("unknown exception", 0); }
        int after = ::tf_test::failures().load();
        std::printf("[ %s ] %s\n", after > before ? "FAIL" : "OK", t.name.c_str());
        ++ran;
    }
    std::printf("\n%d test(s) ran, %d check(s), %d failure(s)\n",
                ran, ::tf_test::checks().load(), ::tf_test::failures().load());
    return ::tf_test::failures().load() == 0 ? 0 : 1;
}

#define TF_TEST_MAIN() \
    int main(int argc, char** argv) { \
      ::install_seh(); \
      return ::tf_test_main(argc, argv); }
