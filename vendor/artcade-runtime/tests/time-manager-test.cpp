// time-manager-test.cpp
// Standalone structural check — no external test framework required.
//
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       time-manager-test.cpp \
//       ../src/modules/time/src/time-manager.cpp \
//       -o time_manager_test && ./time_manager_test
//
// All assertions use plain assert(); exit code 0 = all green.

#include <cassert>
#include <cstdio>

#include "../src/modules/time/include/time-manager.h"

using TM = ArtCade::Modules::TimeManager;

// ------------------------------------------------------------------ helpers

static void tick(TM& tm, float dt, int steps = 1) {
    for (int i = 0; i < steps; ++i) tm.tick(dt);
}

// ------------------------------------------------------------------ tests

static void test_init_shutdown() {
    TM tm;
    assert(tm.init());
    assert(!tm.isPaused());
    assert(tm.now()     == 0.f);
    assert(tm.realNow() == 0.f);
    tm.shutdown();
    assert(tm.now()     == 0.f);
    assert(tm.realNow() == 0.f);
    std::puts("  [ok] init / shutdown");
}

static void test_tick_advances_time() {
    TM tm; tm.init();
    tick(tm, 0.016f, 10);
    assert(tm.realNow() > 0.f);
    assert(tm.now()     > 0.f);
    std::puts("  [ok] tick advances real + game time");
}

static void test_delta_gameplay() {
    TM tm; tm.init();
    tick(tm, 0.016f);
    assert(tm.delta("gameplay") > 0.f);
    std::puts("  [ok] delta(gameplay) > 0 after one tick");
}

static void test_pause_stack_basic() {
    TM tm; tm.init();
    assert(!tm.isPaused());

    uint32_t tok1 = tm.pause("menu");
    assert(tm.isPaused());
    assert(tm.isPauseSourceActive("menu"));

    uint32_t tok2 = tm.pause("cutscene");
    assert(tm.isPaused());

    tm.resume(tok1);
    assert(tm.isPaused());      // cutscene still active

    tm.resume(tok2);
    assert(!tm.isPaused());
    std::puts("  [ok] pause stack: two sources, two resumes");
}

static void test_pause_stops_gameplay_delta() {
    TM tm; tm.init();
    uint32_t tok = tm.pause("test");
    tick(tm, 0.016f);
    assert(tm.delta("gameplay") == 0.f);
    tm.resume(tok);
    std::puts("  [ok] gameplay delta == 0 while paused");
}

static void test_ui_layer_unaffected_by_pause() {
    TM tm; tm.init();
    uint32_t tok = tm.pause("test");
    tick(tm, 0.016f);
    // "ui" layer has affectedByPause = false → delta is NOT suppressed
    assert(tm.delta("ui") > 0.f);
    tm.resume(tok);
    std::puts("  [ok] ui layer not affected by pause");
}

static void test_time_scale() {
    TM tm; tm.init();
    tm.setTimeScale(2.f, "gameplay");
    assert(tm.timeScale("gameplay") == 2.f);
    tick(tm, 0.1f);
    assert(tm.delta("gameplay") > 0.15f);  // should be ~0.2
    std::puts("  [ok] setTimeScale doubles gameplay delta");
}

static void test_time_scale_transition() {
    TM tm; tm.init();
    // Transition from 1 → 2 over 1 second
    tm.setTimeScale(2.f, "gameplay", 1.f);
    tick(tm, 0.5f);
    float s = tm.timeScale("gameplay");
    assert(s > 1.f && s < 2.f);     // midpoint ~1.5
    tick(tm, 0.6f);                  // total 1.1 s → fully at 2
    assert(tm.timeScale("gameplay") == 2.f);
    std::puts("  [ok] time scale linear transition");
}

static void test_delay_timer() {
    TM tm; tm.init();
    bool fired = false;
    tm.delay(0.5f, [&]{ fired = true; }, "gameplay");

    tick(tm, 0.3f);
    assert(!fired);

    tick(tm, 0.3f);
    assert(fired);
    std::puts("  [ok] delay timer fires after correct time");
}

static void test_every_timer() {
    TM tm; tm.init();
    int count = 0;
    tm.every(0.25f, [&]{ ++count; }, "gameplay");

    tick(tm, 0.1f, 10);   // 10 × 0.1 = 1.0 s → 4 fires at 0.25, 0.5, 0.75, 1.0
    assert(count == 4);
    std::puts("  [ok] every timer fires repeatedly");
}

static void test_cancel_timer() {
    TM tm; tm.init();
    int count = 0;
    uint32_t id = tm.every(0.25f, [&]{ ++count; }, "gameplay");

    tick(tm, 0.3f);           // 1 fire
    tm.cancelTimer(id);
    tick(tm, 1.0f);           // no more fires
    assert(count == 1);
    std::puts("  [ok] cancelTimer stops repetition");
}

static void test_resume_source() {
    TM tm; tm.init();
    tm.pause("ui");
    tm.pause("ui");           // two entries for same source
    tm.resumeSource("ui");    // clears both
    assert(!tm.isPaused());
    std::puts("  [ok] resumeSource clears all entries for that source");
}

// Regression: now() must equal real time at scale=1, not 5× real time
// (one accumulation per default layer). Lua's time.now() depends on this.
static void test_now_tracks_real_time() {
    TM tm; tm.init();
    tick(tm, 0.1f, 10);                // 1.0s of real time
    const float n = tm.now();
    assert(n > 0.95f && n < 1.05f);    // gameplay layer @ scale 1
    std::puts("  [ok] now() tracks real time at scale=1 (not multiplied by layer count)");
}

// ------------------------------------------------------------------ main

int main() {
    std::puts("=== TimeManager check ===");
    test_init_shutdown();
    test_tick_advances_time();
    test_delta_gameplay();
    test_pause_stack_basic();
    test_pause_stops_gameplay_delta();
    test_ui_layer_unaffected_by_pause();
    test_time_scale();
    test_time_scale_transition();
    test_delay_timer();
    test_every_timer();
    test_cancel_timer();
    test_resume_source();
    test_now_tracks_real_time();
    std::puts("=== all 13 tests passed ===");
    return 0;
}
