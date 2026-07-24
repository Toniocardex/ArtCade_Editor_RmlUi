// scene-mutation-batch-test.cpp — authoring batch defers revision until commit.

#include "../src/modules/scene-system/include/scene-manager.h"
#include "../src/modules/scene-system/include/scene-mutation-service.h"
#include "../src/modules/scene-system/include/scene-invalidation.h"

#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::SceneManager;
using ArtCade::Modules::SceneMutationService;
using ArtCade::Modules::ScenePatch;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

int main() {
    SceneManager scenes;
    scenes.init();

    ArtCade::SceneDef scene{};
    scene.id = "s";
    scene.worldSize = { 512.f, 320.f };
    scene.viewportSize = { 512.f, 320.f };
    scenes.registerScenes({ { "s", scene } }, {});
    scenes.loadScene("s");

    SceneMutationService mutation(scenes);
    expect(mutation.revision() == 0u, "initial revision is zero");

    mutation.begin_batch();

    ScenePatch geometry{};
    geometry.worldSize = { 1024.f, 320.f };
    geometry.hasWorldSize = true;
    const auto first = mutation.apply("s", geometry);
    expect(first.changed, "first batch apply mutates scene");
    expect(mutation.revision() == 0u, "revision not bumped mid-batch");

    ScenePatch metadata{};
    metadata.backgroundColor = { 0.1f, 0.2f, 0.3f, 1.f };
    metadata.hasBackground = true;
    const auto second = mutation.apply("s", metadata);
    expect(second.changed, "second batch apply mutates scene");
    expect(mutation.revision() == 0u, "revision still deferred");

    const auto committed = mutation.commit_batch();
    expect(committed.changed, "batch commit reports change");
    expect(committed.sceneRevision == 1u, "batch commits a single revision");
    expect(
        ArtCade::Modules::scene_invalidation_has(
            committed.invalidations, ArtCade::Modules::SceneInvalidation::Collision),
        "batch merges collision invalidation");
    expect(
        !ArtCade::Modules::scene_invalidation_has(
            committed.invalidations, ArtCade::Modules::SceneInvalidation::SceneActivation),
        "background patch does not emit activation");

    std::puts("scene_mutation_batch_test: all passed");
    return 0;
}
