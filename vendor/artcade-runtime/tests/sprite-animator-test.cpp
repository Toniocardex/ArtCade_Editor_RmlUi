// sprite-animator-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       sprite-animator-test.cpp \
//       ../src/modules/sprite-animator/src/sprite-animator.cpp \
//       -o sprite_animator_test && ./sprite_animator_test

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/modules/sprite-animator/include/sprite-animator.h"

using SA = ArtCade::Modules::SpriteAnimator;

// ------------------------------------------------------------------ helpers

static SA::Clip makeClip(const std::string& name, int frameCount,
                          float fps = 10.f, bool loop = true) {
    SA::Clip c;
    c.name  = name;
    c.fps   = fps;
    c.loop  = loop;
    for (int i = 0; i < frameCount; ++i)
        c.frames.push_back({ i * 32, 0, 32, 32 });
    return c;
}

// ------------------------------------------------------------------ tests

static void test_init_shutdown() {
    SA sa; sa.init();
    assert(!sa.hasClip("run"));
    sa.shutdown();
    std::puts("  [ok] init / shutdown");
}

static void test_define_and_has_clip() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    assert(sa.hasClip("run"));
    assert(!sa.hasClip("jump"));
    std::puts("  [ok] defineClip / hasClip");
}

static void test_play_sets_frame_zero() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    assert(sa.isPlaying(1u));
    assert(sa.frameIndex(1u) == 0);
    assert(sa.currentClip(1u) == "run");
    std::puts("  [ok] play starts at frame 0");
}

static void test_update_advances_frames() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f));   // 10 fps → 0.1 s/frame
    sa.play(1u, "run");
    sa.update(0.15f);   // should advance to frame 1
    assert(sa.frameIndex(1u) == 1);
    std::puts("  [ok] update advances frames");
}

static void test_loop_wraps_back_to_zero() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f, true));
    sa.play(1u, "run");
    sa.update(0.45f);   // 4.5 frames → wraps once → frame 0 or 1 depending on partial
    int idx = sa.frameIndex(1u);
    assert(idx >= 0 && idx < 4);
    assert(sa.isPlaying(1u));  // still playing because loop=true
    std::puts("  [ok] looping clip wraps back");
}

static void test_non_loop_stops_at_last_frame() {
    SA sa; sa.init();
    sa.defineClip(makeClip("die", 3, 10.f, false));
    sa.play(1u, "die");
    sa.update(1.0f);   // way past end
    assert(!sa.isPlaying(1u));
    assert(sa.frameIndex(1u) == 2);   // last frame
    std::puts("  [ok] non-looping clip stops at last frame");
}

static void test_on_finish_callback() {
    SA sa; sa.init();
    sa.defineClip(makeClip("explode", 2, 10.f, false));
    bool fired = false;
    sa.play(1u, "explode", [&](uint32_t, const std::string& clip){
        fired = (clip == "explode");
    });
    sa.update(0.3f);   // past 2 frames
    assert(fired);
    std::puts("  [ok] onFinish callback fires");
}

static void test_pause_resume() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f));
    sa.play(1u, "run");
    sa.update(0.15f);   // advance to frame 1
    sa.pause(1u);
    int idx = sa.frameIndex(1u);
    sa.update(0.5f);    // should NOT advance while paused
    assert(sa.frameIndex(1u) == idx);

    sa.resume(1u);
    sa.update(0.15f);
    assert(sa.frameIndex(1u) > idx);
    std::puts("  [ok] pause / resume");
}

static void test_stop() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    sa.stop(1u);
    assert(!sa.isPlaying(1u));
    std::puts("  [ok] stop");
}

static void test_seek_frame() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    sa.seekFrame(1u, 3);
    assert(sa.frameIndex(1u) == 3);
    std::puts("  [ok] seekFrame");
}

static void test_current_frame_rect() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    sa.seekFrame(1u, 2);
    auto f = sa.currentFrame(1u);
    assert(f.x == 64 && f.w == 32);   // 3rd frame at x=2*32=64
    std::puts("  [ok] currentFrame returns correct subrect");
}

static void test_clip_frame_rect_without_instance() {
    SA sa; sa.init();
    sa.defineClip(makeClip("idle", 3));
    auto f = sa.clipFrame("idle", 1);
    assert(f.x == 32 && f.w == 32);
    auto missing = sa.clipFrame("idle", 9);
    assert(missing.w == 0 && missing.h == 0);
    std::puts("  [ok] clipFrame returns authored subrect without playback");
}

static void test_first_frame_for_asset() {
    SA sa; sa.init();
    sa.setFirstFrameForAsset("assets/images/walk.png", { 16, 0, 16, 16 });
    auto f = sa.firstFrameForAsset("assets/images/walk.png");
    assert(f.x == 16 && f.w == 16);
    auto missing = sa.firstFrameForAsset("assets/images/missing.png");
    assert(missing.w == 0 && missing.h == 0);
    sa.clearFirstFramesByAsset();
    auto cleared = sa.firstFrameForAsset("assets/images/walk.png");
    assert(cleared.w == 0 && cleared.h == 0);
    std::puts("  [ok] firstFrameForAsset stores static sheet fallback frames");
}

static void test_remove_entity() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    sa.removeEntity(1u);
    assert(!sa.isPlaying(1u));
    assert(sa.frameIndex(1u) == -1);
    std::puts("  [ok] removeEntity clears instance");
}

static void test_independent_entities() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f));
    sa.play(1u, "run");
    sa.play(2u, "run");
    sa.update(0.15f);
    sa.pause(1u);
    sa.update(0.15f);
    // entity 2 should be ahead of entity 1
    assert(sa.frameIndex(2u) > sa.frameIndex(1u));
    std::puts("  [ok] multiple entities are independent");
}

static int countKind(const std::vector<SA::AnimEvent>& evs, SA::AnimEventKind k) {
    int n = 0;
    for (const auto& e : evs) if (e.kind == k) ++n;
    return n;
}

static void test_play_emits_start_event() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "run");
    auto evs = sa.pollEvents();
    assert(countKind(evs, SA::AnimEventKind::Start) == 1);
    assert(countKind(evs, SA::AnimEventKind::Change) == 0);  // no prior clip
    // pollEvents drains the buffer.
    assert(sa.pollEvents().empty());
    std::puts("  [ok] play emits a Start event (no Change on first clip)");
}

static void test_switch_clip_emits_change_event() {
    SA sa; sa.init();
    sa.defineClip(makeClip("idle", 2));
    sa.defineClip(makeClip("run", 4));
    sa.play(1u, "idle");
    sa.pollEvents();                 // drain the idle Start
    sa.play(1u, "run");
    auto evs = sa.pollEvents();
    assert(countKind(evs, SA::AnimEventKind::Start) == 1);
    assert(countKind(evs, SA::AnimEventKind::Change) == 1);
    std::puts("  [ok] switching to a different clip emits a Change event");
}

static void test_update_emits_frame_events() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f));   // 0.1 s/frame
    sa.play(1u, "run");
    sa.pollEvents();                 // drain Start
    sa.update(0.25f);                // advance ~2 frames
    auto evs = sa.pollEvents();
    assert(countKind(evs, SA::AnimEventKind::Frame) == 2);
    assert(evs.back().frameIdx == sa.frameIndex(1u));
    std::puts("  [ok] update emits a Frame event per advanced frame");
}

static void test_loop_emits_loop_event() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 3, 10.f, true));   // 3 frames, loops
    sa.play(1u, "run");
    sa.pollEvents();                 // drain Start
    sa.update(0.35f);                // 3.5 frames → one wrap
    auto evs = sa.pollEvents();
    assert(countKind(evs, SA::AnimEventKind::Loop) == 1);
    std::puts("  [ok] looping clip emits a Loop event on wrap");
}

static void test_watched_kinds_gate_emission() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f, true));

    // Watch only Start: play emits Start, update emits no Frame/Loop.
    sa.setWatchedEventKinds(SA::animEventBit(SA::AnimEventKind::Start));
    sa.play(1u, "run");
    sa.update(0.25f);
    auto evs = sa.pollEvents();
    assert(countKind(evs, SA::AnimEventKind::Start) == 1);
    assert(countKind(evs, SA::AnimEventKind::Frame) == 0);
    assert(countKind(evs, SA::AnimEventKind::Loop) == 0);

    // Watch nothing: zero events recorded at all.
    sa.setWatchedEventKinds(0u);
    sa.play(2u, "run");
    sa.update(0.25f);
    assert(sa.pollEvents().empty());

    // Frame still advances even when its event is unwatched (state is intact).
    assert(sa.frameIndex(2u) >= 1);
    std::puts("  [ok] watched-kinds mask gates which events are recorded");
}

static void test_clip_carries_sheet_asset_id() {
    SA sa; sa.init();
    SA::Clip idle = makeClip("idle", 2);
    idle.assetId = "idle.png";
    SA::Clip walk = makeClip("walking", 3);
    walk.assetId = "walking.png";
    sa.defineClip(idle);
    sa.defineClip(walk);

    // Per-clip sheet lookup is independent of any playing instance.
    assert(sa.clipAssetId("idle") == "idle.png");
    assert(sa.clipAssetId("walking") == "walking.png");
    assert(sa.clipAssetId("missing").empty());

    // The active clip's sheet follows what the entity is playing — this is what
    // lets one object animate across sheets without slicing the wrong texture.
    assert(sa.currentClipAssetId(7u).empty());
    sa.play(7u, "walking");
    assert(sa.currentClipAssetId(7u) == "walking.png");
    sa.play(7u, "idle");
    assert(sa.currentClipAssetId(7u) == "idle.png");

    sa.shutdown();
    std::puts("  [ok] clip carries its sheet assetId (cross-sheet animation)");
}

static void test_replay_same_clip_does_not_restart() {
    SA sa; sa.init();
    sa.defineClip(makeClip("run", 4, 10.f, true)); // 0.1 s/frame, loops
    sa.defineClip(makeClip("idle", 2, 10.f, true));

    sa.play(1u, "run");
    sa.update(0.25f);                 // advance to frame 2
    assert(sa.frameIndex(1u) == 2);

    // Re-playing the SAME clip while it's running (held-key rule firing every
    // frame) must NOT reset it to frame 0.
    sa.play(1u, "run");
    assert(sa.frameIndex(1u) == 2);

    // Switching to a DIFFERENT clip still restarts from frame 0.
    sa.play(1u, "idle");
    assert(sa.frameIndex(1u) == 0);

    sa.shutdown();
    std::puts("  [ok] re-playing the same clip keeps it advancing (no freeze)");
}

static void test_asset_scoped_clip_and_playback_speed() {
    SA sa; sa.init();
    SA::Clip clip = makeClip("run", 4, 10.f, true);
    clip.animationAssetId = "hero-animation";
    clip.assetId = "hero-sheet";
    sa.defineClip(clip);

    assert(sa.hasClip("hero-animation", "run"));
    assert(!sa.hasClip("other-animation", "run"));
    assert(sa.isClipPlayable("hero-animation", "run"));
    assert(sa.play(7u, "hero-animation", "run"));
    assert(sa.setPlaybackSpeed(7u, 2.f));
    sa.update(0.15f); // 0.30 animation seconds -> three frames.
    assert(sa.frameIndex(7u) == 3);
    assert(sa.playbackSpeed(7u) == 2.f);
    assert(sa.currentClipAssetId(7u) == "hero-sheet");
    assert(!sa.setPlaybackSpeed(7u, 0.f));
    std::puts("  [ok] asset-scoped clips preserve per-entity playback speed");
}

int main() {
    std::puts("=== SpriteAnimator check ===");
    test_init_shutdown();
    test_define_and_has_clip();
    test_play_sets_frame_zero();
    test_update_advances_frames();
    test_loop_wraps_back_to_zero();
    test_non_loop_stops_at_last_frame();
    test_on_finish_callback();
    test_pause_resume();
    test_stop();
    test_seek_frame();
    test_current_frame_rect();
    test_clip_frame_rect_without_instance();
    test_first_frame_for_asset();
    test_remove_entity();
    test_independent_entities();
    test_play_emits_start_event();
    test_switch_clip_emits_change_event();
    test_update_emits_frame_events();
    test_loop_emits_loop_event();
    test_watched_kinds_gate_emission();
    test_clip_carries_sheet_asset_id();
    test_replay_same_clip_does_not_restart();
    test_asset_scoped_clip_and_playback_speed();
    std::puts("=== all 23 tests passed ===");
    return 0;
}
