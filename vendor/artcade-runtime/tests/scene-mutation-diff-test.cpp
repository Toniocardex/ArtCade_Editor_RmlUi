// scene-mutation-diff-test.cpp — invalidations follow applied field diffs only.

#include "../src/modules/scene-system/include/scene-manager.h"
#include "../src/modules/scene-system/include/scene-mutation-service.h"
#include "../src/modules/scene-system/include/scene-invalidation.h"

#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::SceneInvalidation;
using ArtCade::Modules::SceneManager;
using ArtCade::Modules::SceneMutationService;
using ArtCade::Modules::ScenePatch;
using ArtCade::Modules::scene_invalidation_has;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

static void setup_scene(SceneManager& scenes) {
    scenes.init();
    ArtCade::SceneDef scene{};
    scene.id = "s";
    scene.worldSize = { 512.f, 320.f };
    scene.viewportSize = { 800.f, 600.f };
    scene.backgroundColor = { 0.f, 0.f, 0.f, 1.f };
    scene.layerSettings["bg"].visible = true;
    scenes.registerScenes({ { "s", scene } }, {});
    scenes.loadScene("s");
}

int main() {
    {
        SceneManager scenes;
        setup_scene(scenes);
        SceneMutationService mutation(scenes);
        const uint64_t rev0 = mutation.revision();

        ScenePatch patch = ScenePatch::from_projection(*scenes.activeScene());
        patch.backgroundColor = { 0.1f, 0.2f, 0.3f, 1.f };
        patch.hasBackground = true;

        const auto result = mutation.apply("s", patch);
        expect(result.changed, "background-only patch changes scene");
        expect(result.sceneRevision == rev0 + 1u, "revision bumps once");
        expect(result.invalidations == SceneInvalidation::None,
               "background-only patch emits no invalidation flags");
        expect(!scene_invalidation_has(result.invalidations, SceneInvalidation::Collision),
               "background does not emit Collision");
    }

    {
        SceneManager scenes;
        setup_scene(scenes);
        SceneMutationService mutation(scenes);
        const uint64_t rev0 = mutation.revision();

        ScenePatch patch{};
        patch.layerSettings = scenes.activeScene()->layerSettings;
        patch.hasLayerSettings = true;

        const auto result = mutation.apply("s", patch);
        expect(!result.changed, "identical layerSettings is no-op");
        expect(result.sceneRevision == rev0, "revision unchanged on no-op layerSettings");
        expect(result.invalidations == SceneInvalidation::None,
               "no invalidations on identical layerSettings");
    }

    {
        SceneManager scenes;
        setup_scene(scenes);
        SceneMutationService mutation(scenes);

        ScenePatch patch{};
        patch.viewportSize = { 1024.f, 768.f };
        patch.hasViewportSize = true;

        const auto result = mutation.apply("s", patch);
        expect(result.changed, "viewport-only patch changes scene");
        expect(result.invalidations == SceneInvalidation::None,
               "viewport-only emits no invalidation flags");
        expect(!scene_invalidation_has(result.invalidations, SceneInvalidation::Collision),
               "viewport-only does not emit Collision");
    }

    {
        SceneManager scenes;
        setup_scene(scenes);
        SceneMutationService mutation(scenes);
        const uint64_t rev0 = mutation.revision();

        ScenePatch patch = ScenePatch::from_projection(*scenes.activeScene());
        const auto result = mutation.apply("s", patch);
        expect(!result.changed, "identical full patch is no-op");
        expect(result.sceneRevision == rev0, "identical patch keeps revision");
        expect(result.invalidations == SceneInvalidation::None,
               "identical patch emits no invalidations");
    }

    {
        SceneManager scenes;
        setup_scene(scenes);
        SceneMutationService mutation(scenes);

        ScenePatch patch{};
        patch.worldSize = { 1024.f, 768.f };
        patch.hasWorldSize = true;

        const auto result = mutation.apply("s", patch);
        expect(result.changed, "world-size patch changes scene");
        expect(scene_invalidation_has(result.invalidations, SceneInvalidation::Collision),
               "world-size patch emits Collision");
    }

    std::puts("scene_mutation_diff_test: all passed");
    return 0;
}
