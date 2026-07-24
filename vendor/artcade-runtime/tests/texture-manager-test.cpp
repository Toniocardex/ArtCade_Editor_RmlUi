// texture-manager-test.cpp
// Uses a Raylib stub so no GPU/window is needed.

#include <cassert>
#include <cstdio>

#include "../src/modules/texture-manager/include/texture-manager.h"

using TM   = ArtCade::Modules::TextureManager;
using Info = ArtCade::Modules::TextureInfo;

static void test_init_shutdown() {
    TM tm; tm.init();
    assert(tm.loadedCount() == 0);
    tm.shutdown();
    std::puts("  [ok] init / shutdown");
}

static void test_load_returns_valid_handle() {
    TM tm; tm.init();
    uint32_t h = tm.load("missing.png");  // stub always returns placeholder
    assert(h != 0);
    assert(tm.loadedCount() == 1);
    Info info;
    assert(tm.getInfo(h, info));          // handle is valid
    assert(info.gpuId != 0);             // placeholder has a gpu id
    tm.shutdown();
    std::puts("  [ok] load returns valid handle (placeholder for missing file)");
}

static void test_load_same_path_cached() {
    TM tm; tm.init();
    uint32_t h1 = tm.load("sprite.png");
    uint32_t h2 = tm.load("sprite.png");
    assert(h1 == h2);               // same handle
    assert(tm.loadedCount() == 1);  // only one GPU texture
    tm.release(h1);
    tm.release(h2);
    assert(tm.loadedCount() == 0);
    tm.shutdown();
    std::puts("  [ok] same path returns cached handle");
}

static void test_load_different_paths() {
    TM tm; tm.init();
    uint32_t h1 = tm.load("a.png");
    uint32_t h2 = tm.load("b.png");
    assert(h1 != h2);
    assert(tm.loadedCount() == 2);
    tm.shutdown();
    std::puts("  [ok] different paths → different handles");
}

static void test_release_by_handle() {
    TM tm; tm.init();
    uint32_t h = tm.load("x.png");
    tm.release(h);
    assert(tm.loadedCount() == 0);
    Info info;
    assert(!tm.getInfo(h, info));   // handle no longer valid
    tm.shutdown();
    std::puts("  [ok] release by handle unloads texture");
}

static void test_release_by_path() {
    TM tm; tm.init();
    tm.load("y.png");
    tm.release("y.png");
    assert(tm.loadedCount() == 0);
    tm.shutdown();
    std::puts("  [ok] release by path unloads texture");
}

static void test_ref_count_multiple_loads() {
    TM tm; tm.init();
    uint32_t h = tm.load("z.png");
    tm.load("z.png");
    tm.load("z.png");
    assert(tm.loadedCount() == 1);

    tm.release(h);   // refcount 2
    assert(tm.loadedCount() == 1);
    tm.release(h);   // refcount 1
    assert(tm.loadedCount() == 1);
    tm.release(h);   // refcount 0 → unloaded
    assert(tm.loadedCount() == 0);
    tm.shutdown();
    std::puts("  [ok] ref count: three loads need three releases");
}

static void test_handle_of() {
    TM tm; tm.init();
    assert(tm.handleOf("nope.png") == 0);
    uint32_t h = tm.load("ok.png");
    assert(tm.handleOf("ok.png") == h);
    tm.shutdown();
    std::puts("  [ok] handleOf");
}

static void test_get_info_unknown_returns_false() {
    TM tm; tm.init();
    Info info;
    assert(!tm.getInfo(9999u, info));
    assert(tm.handleOf("unknown.png") == 0);
    tm.shutdown();
    std::puts("  [ok] getInfo unknown handle returns false");
}

int main() {
    std::puts("=== TextureManager check ===");
    test_init_shutdown();
    test_load_returns_valid_handle();
    test_load_same_path_cached();
    test_load_different_paths();
    test_release_by_handle();
    test_release_by_path();
    test_ref_count_multiple_loads();
    test_handle_of();
    test_get_info_unknown_returns_false();
    std::puts("=== all 9 tests passed ===");
    return 0;
}
