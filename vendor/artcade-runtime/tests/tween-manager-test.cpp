// tween-manager-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       tween-manager-test.cpp \
//       ../src/modules/tween-manager/src/tween-manager.cpp \
//       -o tween_manager_test && ./tween_manager_test

#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>

#include "../src/modules/tween-manager/include/tween-manager.h"

using TM = ArtCade::Modules::TweenManager;

static bool approx(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) < eps;
}

static void test_init_shutdown() {
    TM tm; tm.init();
    tm.shutdown();
    std::puts("  [ok] init / shutdown");
}

static void test_linear_tween() {
    TM tm; tm.init();
    std::vector<float> vals;
    tm.tweenTo(0.f, 1.f, 1.f, TM::Ease::Linear, [&](float v){ vals.push_back(v); });
    tm.update(0.5f);
    assert(!vals.empty() && approx(vals.back(), 0.5f));
    tm.update(0.5f);
    assert(approx(vals.back(), 1.f));
    std::puts("  [ok] linear tween interpolates correctly");
}

static void test_on_complete_fires() {
    TM tm; tm.init();
    bool done = false;
    tm.tweenTo(0.f, 1.f, 0.1f, TM::Ease::Linear, {}, [&]{ done = true; });
    tm.update(0.2f);
    assert(done);
    std::puts("  [ok] onComplete fires when tween finishes");
}

static void test_tween_removed_after_complete() {
    TM tm; tm.init();
    auto id = tm.tweenTo(0.f, 1.f, 0.1f, TM::Ease::Linear, {});
    assert(tm.isActive(id));
    tm.update(0.2f);
    assert(!tm.isActive(id));
    std::puts("  [ok] tween removed from manager after completion");
}

static void test_cancel() {
    TM tm; tm.init();
    bool done = false;
    auto id = tm.tweenTo(0.f, 1.f, 1.f, TM::Ease::Linear, {}, [&]{ done = true; });
    tm.cancel(id);
    tm.update(2.f);
    assert(!done && !tm.isActive(id));
    std::puts("  [ok] cancel stops tween without firing onComplete");
}

static void test_pause_resume() {
    TM tm; tm.init();
    float last = -1.f;
    auto id = tm.tweenTo(0.f, 1.f, 1.f, TM::Ease::Linear, [&](float v){ last = v; });
    tm.update(0.3f);
    float mid = last;
    tm.pause(id);
    tm.update(0.5f);   // paused — value must not change
    assert(approx(last, mid, 0.001f));
    tm.resume(id);
    tm.update(0.1f);
    assert(last > mid);
    std::puts("  [ok] pause / resume");
}

static void test_loop() {
    TM tm; tm.init();
    int cycles = 0;
    float last = 0.f;
    TM::Params p;
    p.from = 0.f; p.to = 1.f; p.duration = 0.25f;
    p.ease = TM::Ease::Linear; p.loop = true;
    p.onUpdate = [&](float v){ last = v; };
    auto id = tm.tween(p);
    // After 3 * duration, the tween should still be active and have restarted
    tm.update(0.75f);
    assert(tm.isActive(id));
    std::puts("  [ok] looping tween stays active");
}

static void test_delay() {
    TM tm; tm.init();
    float val = -1.f;
    TM::Params p;
    p.from = 0.f; p.to = 1.f; p.duration = 0.1f;
    p.ease = TM::Ease::Linear; p.delay = 0.5f;
    p.onUpdate = [&](float v){ val = v; };
    tm.tween(p);
    tm.update(0.3f);
    assert(val < 0.f);   // still in delay
    tm.update(0.3f);     // now past delay
    assert(val >= 0.f);
    std::puts("  [ok] delay holds tween start");
}

static void test_ease_quad_out_reaches_one() {
    float out = TM::applyEase(TM::Ease::QuadOut, 1.f);
    assert(approx(out, 1.f, 0.001f));
    float mid = TM::applyEase(TM::Ease::QuadOut, 0.5f);
    // QuadOut at 0.5 = 0.5*(2-0.5) = 0.75
    assert(approx(mid, 0.75f));
    std::puts("  [ok] applyEase(QuadOut) correct values");
}

static void test_cancel_all() {
    TM tm; tm.init();
    auto id1 = tm.tweenTo(0.f, 1.f, 1.f, TM::Ease::Linear, {});
    auto id2 = tm.tweenTo(0.f, 1.f, 1.f, TM::Ease::Linear, {});
    tm.cancelAll();
    tm.update(0.1f);
    assert(!tm.isActive(id1) && !tm.isActive(id2));
    std::puts("  [ok] cancelAll removes all tweens");
}

static void test_multiple_independent_tweens() {
    TM tm; tm.init();
    float v1 = 0.f, v2 = 0.f;
    tm.tweenTo(0.f,  100.f, 1.f, TM::Ease::Linear, [&](float v){ v1 = v; });
    tm.tweenTo(100.f, 0.f, 1.f, TM::Ease::Linear, [&](float v){ v2 = v; });
    tm.update(0.5f);
    assert(approx(v1, 50.f) && approx(v2, 50.f));
    std::puts("  [ok] multiple independent tweens run in parallel");
}

int main() {
    std::puts("=== TweenManager check ===");
    test_init_shutdown();
    test_linear_tween();
    test_on_complete_fires();
    test_tween_removed_after_complete();
    test_cancel();
    test_pause_resume();
    test_loop();
    test_delay();
    test_ease_quad_out_reaches_one();
    test_cancel_all();
    test_multiple_independent_tweens();
    std::puts("=== all 11 tests passed ===");
    return 0;
}
