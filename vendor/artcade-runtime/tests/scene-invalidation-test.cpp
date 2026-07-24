// scene-invalidation-test.cpp — invalidation masks and coalescing helpers (PR3).

#include "../src/modules/scene-system/include/scene-invalidation.h"

#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::SceneInvalidation;
using ArtCade::Modules::scene_invalidation_has;
using ArtCade::Modules::scene_invalidation_needs_collision_rebuild;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

int main() {
    SceneInvalidation batch = SceneInvalidation::None;
    batch |= SceneInvalidation::SceneActivation;
    batch |= SceneInvalidation::Collision;

    expect(scene_invalidation_has(batch, SceneInvalidation::SceneActivation),
           "coalesced batch retains activation");
    expect(scene_invalidation_has(batch, SceneInvalidation::Collision),
           "coalesced batch retains collision");
    expect(scene_invalidation_needs_collision_rebuild(batch),
           "collision flag requests rebuild pass");
    expect(!scene_invalidation_needs_collision_rebuild(SceneInvalidation::SceneActivation),
           "activation alone does not request collision rebuild");

    {
        SceneInvalidation consumed = batch;
        batch = SceneInvalidation::None;
        expect(consumed != SceneInvalidation::None, "consume clears pending accumulator");
        expect(batch == SceneInvalidation::None, "pending reset after consume");
    }

    std::puts("scene_invalidation_test: all passed");
    return 0;
}
