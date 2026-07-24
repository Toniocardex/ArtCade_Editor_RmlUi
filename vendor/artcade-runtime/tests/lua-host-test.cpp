// lua-host-test.cpp — LuaHost error-handling and lifecycle tests
//
// Tests:
//   1. init + shutdown without any script — no crash
//   2. load valid Lua source, tick() runs without error
//   3. runtime error in tick() → caught, hasError() = true, no crash
//   4. clearError() resets the error flag
//   5. syntax error in loadBytecodeBuffer() → returns false
//   6. tick() when no "tick" function defined → no crash, no error
//   7. repeated runtime errors over many ticks → stable, no crash

#include "modules/lua-runtime/include/lua-host.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace ArtCade::Modules;

// ---- minimal test harness -----------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) { \
            ++g_passed; \
        } else { \
            std::cerr << "  FAIL: " #cond "  (line " << __LINE__ << ")\n"; \
            ++g_failed; \
        } \
    } while (false)

static void load(LuaHost& host, const char* src) {
    host.loadBytecodeBuffer(
        reinterpret_cast<const uint8_t*>(src),
        std::strlen(src));
}

// ---- tests ---------------------------------------------------------------

static void test_init_shutdown() {
    std::cout << "Test 1: init/shutdown without script\n";
    LuaHost host;
    CHECK(host.init());
    host.tick(0.016f);      // no script loaded — must be a no-op
    CHECK(!host.hasError());
    host.shutdown();
}

static void test_valid_script() {
    std::cout << "Test 2: valid Lua script loads and ticks\n";
    LuaHost host;
    host.init();
    load(host,
        "count = 0\n"
        "function tick(dt)\n"
        "    count = count + 1\n"
        "end");
    CHECK(!host.hasError());
    host.tick(0.016f);
    CHECK(!host.hasError());
    host.tick(0.016f);
    CHECK(!host.hasError());
    host.shutdown();
}

static void test_runtime_error_caught() {
    std::cout << "Test 3: runtime error in tick() is caught, no crash\n";
    LuaHost host;
    host.init();
    load(host, "function tick(dt) error('boom') end");
    host.tick(0.016f);      // must NOT crash or throw
    CHECK(host.hasError());
    CHECK(!host.lastError().empty());
    std::cout << "       lastError: " << host.lastError() << "\n";
    host.shutdown();
}

static void test_clear_error() {
    std::cout << "Test 4: clearError() resets the flag\n";
    LuaHost host;
    host.init();
    load(host, "function tick(dt) error('err') end");
    host.tick(0.016f);
    CHECK(host.hasError());
    host.clearError();
    CHECK(!host.hasError());
    CHECK(host.lastError().empty());
    host.shutdown();
}

static void test_syntax_error() {
    std::cout << "Test 5: syntax error → loadBytecodeBuffer returns false\n";
    LuaHost host;
    host.init();
    const char* bad = "function tick(dt end";   // missing ')' and 'end'
    bool ok = host.loadBytecodeBuffer(
        reinterpret_cast<const uint8_t*>(bad), std::strlen(bad));
    CHECK(!ok);
    host.shutdown();
}

static void test_missing_tick() {
    std::cout << "Test 6: no tick() defined — tick() is a no-op, no error\n";
    LuaHost host;
    host.init();
    load(host, "-- no tick function here");
    host.tick(0.016f);
    CHECK(!host.hasError());
    host.shutdown();
}

static void test_repeated_errors() {
    std::cout << "Test 7: repeated runtime errors across 100 ticks\n";
    LuaHost host;
    host.init();
    load(host, "function tick(dt) error('err ' .. tostring(dt)) end");
    for (int i = 0; i < 100; ++i)
        host.tick(static_cast<float>(i) * 0.016f);
    CHECK(host.hasError());     // last error still recorded
    std::cout << "       survived 100 error ticks OK\n";
    host.shutdown();
}

static void test_tick_can_be_disabled() {
    std::cout << "Test 8: event-only script can disable tick()\n";
    LuaHost host;
    host.init();
    load(host,
        "__artcade_requires_tick = false\n"
        "function tick(dt)\n"
        "    error('tick should be skipped')\n"
        "end");
    CHECK(!host.hasError());
    CHECK(!host.isScriptTickRequired());
    host.tick(0.016f);
    CHECK(!host.hasError());
    host.shutdown();
}

static void test_io_library_not_exposed() {
    std::cout << "Test 9: io library is not exposed to gameplay Lua\n";
    LuaHost host;
    host.init();
    load(host,
        "function tick(dt)\n"
        "    if io ~= nil then error('io library exposed') end\n"
        "end");
    CHECK(!host.hasError());
    host.tick(0.016f);
    CHECK(!host.hasError());
    host.shutdown();
}

// -------------------------------------------------------------------------

int main() {
    std::cout << "=== LuaHost error-handling tests ===\n";

    test_init_shutdown();
    test_valid_script();
    test_runtime_error_caught();
    test_clear_error();
    test_syntax_error();
    test_missing_tick();
    test_repeated_errors();
    test_tick_can_be_disabled();
    test_io_library_not_exposed();

    std::cout << "\nResults: " << g_passed << " passed, "
              << g_failed  << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
