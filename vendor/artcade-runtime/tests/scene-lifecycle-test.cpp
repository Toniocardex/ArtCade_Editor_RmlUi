// scene-lifecycle-test.cpp — load/restart/fade via SceneLifecycleService.

#include "../src/modules/scene-system/include/scene-manager.h"
#include "../src/modules/scene-system/include/scene-mutation-service.h"
#include "../src/modules/scene-system/include/scene-lifecycle-service.h"
#include "../src/modules/scene-system/include/scene-invalidation.h"
#include "../src/modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../src/modules/physics/include/physics.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::SceneId;
using ArtCade::Modules::SceneInvalidation;
using ArtCade::Modules::SceneLifecycleService;
using ArtCade::Modules::SceneManager;
using ArtCade::Modules::SceneMutationService;
using ArtCade::Modules::SceneTransitionResult;
using ArtCade::Modules::RuntimeEntityGateway;

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

    ArtCade::SceneDef sceneA{};
    sceneA.id = "a";
    sceneA.worldSize = { 512.f, 320.f };
    sceneA.viewportSize = { 512.f, 320.f };

    ArtCade::SceneDef sceneB = sceneA;
    sceneB.id = "b";
    sceneB.worldSize = { 800.f, 600.f };
    sceneB.cameraStart = { 100.f, 50.f };

    scenes.registerScenes({ { "a", sceneA }, { "b", sceneB } }, {});
    scenes.loadScene("a");

    SceneMutationService mutations(scenes);
    RuntimeEntityGateway gateway(scenes);
    gateway.init();
    gateway.registerScenes({ { "a", sceneA }, { "b", sceneB } }, {});
    ArtCade::Modules::Physics physics;
    physics.init();
    gateway.setPhysics(&physics);

  SceneTransitionResult lastTransition{};
    SceneLifecycleService lifecycle(
        scenes,
        mutations,
        [&gateway]() { gateway.syncSceneActivation(); });
    lifecycle.set_transition_handler([&lastTransition](const SceneTransitionResult& r) {
        lastTransition = r;
    });
    gateway.set_scene_lifecycle_service(&lifecycle);

    lifecycle.set_restore_handler([&gateway](const SceneId& sceneId) {
        return gateway.restoreSceneFromAuthoring(sceneId);
    });

    const auto immediate = lifecycle.load_immediate("b");
    expect(immediate.changed, "immediate load succeeds");
    expect(immediate.sceneRevision == 1u, "immediate load bumps revision");
    expect(scenes.activeSceneId() == "b", "active scene updated");
    expect(
        ArtCade::Modules::scene_invalidation_has(
            lastTransition.invalidations, SceneInvalidation::SceneActivation),
        "transition handler receives activation invalidation");

    const auto reactivate = lifecycle.request_reactivate(0.f);
    expect(reactivate.changed, "reactivate with zero fade reloads");
    expect(reactivate.sceneRevision == 2u, "reactivate bumps revision");

    const auto restart = lifecycle.request_restart(0.f);
    expect(restart.changed, "restart with zero fade succeeds");
    expect(restart.sceneRevision == 3u, "restart bumps revision");
    expect(restart.sceneRevision != reactivate.sceneRevision,
           "restart and reactivate are distinct commits");

    const auto badFade = lifecycle.request_load("missing", 2.f);
    expect(badFade.error == ArtCade::Modules::SceneTransitionError::SceneNotFound,
           "invalid fade target returns not found");
    expect(!lifecycle.transition_active(), "invalid fade does not start transition");
    expect(scenes.activeSceneId() == "b", "active scene unchanged after bad fade");

    lifecycle.request_load("a", 2.f);
    expect(lifecycle.transition_active(), "fade request starts transition");
    expect(std::abs(lifecycle.scene_fade_alpha()) < 0.001f, "fade starts at zero alpha");

    lifecycle.tick(1.1f);
    expect(scenes.activeSceneId() == "a", "fade midpoint commits load");
    expect(lastTransition.sceneRevision == 4u, "fade commit bumps revision");

    lifecycle.tick(1.1f);
    expect(!lifecycle.transition_active(), "fade completes");
    expect(std::abs(lifecycle.scene_fade_alpha()) < 0.001f, "fade alpha cleared");

    std::puts("scene_lifecycle_test: all passed");
    return 0;
}
