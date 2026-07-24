// camera-manager-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       camera-manager-test.cpp \
//       ../src/modules/camera-manager/src/camera-manager.cpp \
//       -o camera_manager_test && ./camera_manager_test

#include <cassert>
#include <cstdio>
#include <cmath>

#include "../src/modules/camera-manager/include/camera-manager.h"

using CM = ArtCade::Modules::CameraManager;

static bool approx(float a, float b, float eps = 0.5f) {
    return std::fabs(a - b) < eps;
}

static void test_init() {
    CM cam; cam.init();
    assert(approx(cam.position().x, 0.f) && approx(cam.position().y, 0.f));
    assert(approx(cam.zoom(), 1.f));
    assert(approx(cam.rotation(), 0.f));
    std::puts("  [ok] init defaults");
}

static void test_set_position() {
    CM cam; cam.init();
    cam.setPosition({ 100.f, 200.f });
    assert(approx(cam.position().x, 100.f) && approx(cam.position().y, 200.f));
    std::puts("  [ok] setPosition");
}

static void test_set_zoom() {
    CM cam; cam.init();
    cam.setZoom(2.f);
    assert(approx(cam.zoom(), 2.f));
    std::puts("  [ok] setZoom");
}

static void test_move_to_immediate() {
    CM cam; cam.init();
    cam.moveTo({ 50.f, 50.f }, 0.f);
    assert(approx(cam.position().x, 50.f) && approx(cam.position().y, 50.f));
    std::puts("  [ok] moveTo duration=0 snaps immediately");
}

static void test_move_to_lerp() {
    CM cam; cam.init();
    cam.setScreenSize(1280.f, 720.f);
    cam.setPosition({ 0.f, 0.f });
    cam.moveTo({ 100.f, 0.f }, 1.f);   // 1 s tween
    cam.update(0.5f);
    float x = cam.position().x;
    assert(x > 30.f && x < 70.f);   // ~50 at midpoint
    cam.update(0.6f);
    assert(approx(cam.position().x, 100.f, 1.f));
    std::puts("  [ok] moveTo lerp interpolates position");
}

static void test_zoom_to_lerp() {
    CM cam; cam.init();
    cam.zoomTo(2.f, 1.f);
    cam.update(0.5f);
    float z = cam.zoom();
    assert(z > 1.3f && z < 1.7f);
    cam.update(0.6f);
    assert(approx(cam.zoom(), 2.f, 0.01f));
    std::puts("  [ok] zoomTo lerp interpolates zoom");
}

static void test_follow_target() {
    CM cam; cam.init();
    cam.setPosition({ 0.f, 0.f });
    ArtCade::Vec2 target = { 200.f, 100.f };
    cam.setFollowTarget([&]{ return target; }, 10.f);
    cam.update(0.5f);   // large dt * speed → should move toward target
    float x = cam.position().x;
    assert(x > 10.f);
    std::puts("  [ok] follow target moves camera toward target");
}

static void test_visible_bounds() {
    CM cam; cam.init();
    cam.setScreenSize(1280.f, 720.f);
    cam.setPosition({ 0.f, 0.f });
    cam.setZoom(1.f);
    ArtCade::Vec2 tl, br;
    cam.visibleBounds(tl, br);
    assert(approx(tl.x, -640.f) && approx(br.x, 640.f));
    assert(approx(tl.y, -360.f) && approx(br.y, 360.f));
    std::puts("  [ok] visibleBounds");
}

static void test_shake_adds_offset() {
    CM cam; cam.init();
    cam.addTrauma(1.f);
    cam.refreshShakeOffset(0.016f);
    const auto off = cam.shakeOffset();
    assert(off.x != 0.f || off.y != 0.f);
    std::puts("  [ok] addTrauma produces non-zero shakeOffset");
}

static void test_trauma_decays() {
    CM cam; cam.init();
    cam.addTrauma(0.5f);
    for (int i = 0; i < 100; ++i) {
        cam.refreshShakeOffset(0.1f);
        cam.decayTrauma(0.1f);
    }
    assert(approx(cam.shakeOffset().x, 0.f, 0.01f) &&
           approx(cam.shakeOffset().y, 0.f, 0.01f));
    std::puts("  [ok] trauma decays over time");
}

static void test_shake_duration_sets_decay_rate() {
    CM cam; cam.init();
    cam.addTrauma(1.f, 2.f);
    cam.refreshShakeOffset(0.f);
    cam.decayTrauma(1.f);
    assert(approx(cam.trauma(), 0.5f, 0.02f));
    std::puts("  [ok] addTrauma duration controls decay rate");
}

static void test_shake_refresh_without_sim_step() {
    CM cam; cam.init();
    cam.addTrauma(0.75f);
    // Edit-mode preview: trauma from hot-reload init, no fixed-step dt yet.
    cam.refreshShakeOffset(1.f / 60.f);
    const auto off = cam.shakeOffset();
    assert(off.x != 0.f || off.y != 0.f);
    std::puts("  [ok] refreshShakeOffset with frame dt (no sim step)");
}

static void test_multi_step_lua_before_frame_decay() {
    CM cam; cam.init();
    cam.addTrauma(0.8f);
    const float step = 1.f / 60.f;
    // Old bug: updateShake per fixed step decayed trauma 4x before render.
    for (int i = 0; i < 4; ++i)
        cam.updateShake(step);
    const auto weak = cam.shakeOffset();

    cam.init();
    cam.addTrauma(0.8f);
    cam.refreshShakeOffset(4.f * step);
    cam.decayTrauma(4.f * step);
    const auto strong = cam.shakeOffset();

    const float weakMag = std::fabs(weak.x) + std::fabs(weak.y);
    const float strongMag = std::fabs(strong.x) + std::fabs(strong.y);
    assert(strongMag > weakMag + 0.5f);
    std::puts("  [ok] frame-once refresh keeps stronger shake than per-step decay");
}

int main() {
    std::puts("=== CameraManager check ===");
    test_init();
    test_set_position();
    test_set_zoom();
    test_move_to_immediate();
    test_move_to_lerp();
    test_zoom_to_lerp();
    test_follow_target();
    test_visible_bounds();
    test_shake_adds_offset();
    test_shake_refresh_without_sim_step();
    test_trauma_decays();
    test_shake_duration_sets_decay_rate();
    test_multi_step_lua_before_frame_decay();
    std::puts("=== all 14 tests passed ===");
    return 0;
}
