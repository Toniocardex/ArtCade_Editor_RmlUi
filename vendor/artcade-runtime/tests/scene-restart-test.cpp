// scene-restart-test.cpp — restoreSceneFromAuthoring and restart vs reactivate semantics.

#include "../src/modules/scene-system/include/scene-manager.h"
#include "../src/modules/scene-system/include/scene-mutation-service.h"
#include "../src/modules/scene-system/include/scene-lifecycle-service.h"
#include "../src/modules/scene-system/include/scene-invalidation.h"
#include "../src/modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../src/modules/physics/include/physics.h"
#include "../src/modules/variable-manager/include/variable-manager.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <variant>

using ArtCade::BodyType;
using ArtCade::EntityDef;
using ArtCade::EntityId;
using ArtCade::GameVariableDefinition;
using ArtCade::SceneId;
using ArtCade::Transform;
using ArtCade::Modules::SceneInvalidation;
using ArtCade::Modules::SceneLifecycleService;
using ArtCade::Modules::SceneManager;
using ArtCade::Modules::SceneMutationService;
using ArtCade::Modules::SceneTransitionResult;
using ArtCade::Modules::RuntimeEntityGateway;
using ArtCade::Modules::VariableManager;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

static EntityDef make_player_def(EntityId id, float x, float y) {
    EntityDef def{};
    def.id = id;
    def.className = "Player";
    def.transform.position = { x, y };
    def.transform.rotation = 0.f;
    def.transform.scale = { 1.f, 1.f };
    def.physics.bodyType = BodyType::Dynamic;
    def.physics.collider.size = { 32.f, 32.f };
    GameVariableDefinition hp{};
    hp.key = "hp";
    hp.type = GameVariableDefinition::Type::Number;
    hp.initialValue = 100.0;
    def.localVariables.push_back(hp);
    return def;
}

static void wire_variables(RuntimeEntityGateway& gateway, VariableManager& variables) {
    gateway.setEntityDestroyHandler([&](EntityId id) {
        variables.destroyEntity(id);
    });
    gateway.setEntityCreatedHandler([&](EntityId id, const EntityDef& def) {
        variables.createEntity(id, def.localVariables, def.localVariableOverrides);
    });
}

struct TestHarness {
    SceneManager scenes;
    SceneMutationService mutations;
    RuntimeEntityGateway gateway;
    VariableManager variables;
    ArtCade::Modules::Physics physics;
    SceneLifecycleService lifecycle;
    SceneTransitionResult lastTransition{};

    explicit TestHarness()
        : mutations(scenes),
          gateway(scenes),
          lifecycle(scenes, mutations, [this]() { gateway.syncSceneActivation(); })
    {
        scenes.init();
        gateway.init();
        variables.init();
        physics.init();
        gateway.setPhysics(&physics);
        wire_variables(gateway, variables);
        gateway.set_scene_lifecycle_service(&lifecycle);
        lifecycle.set_restore_handler(
            [this](const SceneId& sceneId) {
                return gateway.restoreSceneFromAuthoring(sceneId);
            });
        lifecycle.set_transition_handler([this](const SceneTransitionResult& r) {
            lastTransition = r;
        });
    }

    bool load_project() {
        ArtCade::SceneDef sceneA{};
        sceneA.id = "a";
        sceneA.entityIds = { 1u };
        sceneA.worldSize = { 512.f, 320.f };
        sceneA.cameraStart = { 0.f, 0.f };

        ArtCade::SceneDef sceneB{};
        sceneB.id = "b";
        sceneB.entityIds = { 2u };
        sceneB.worldSize = { 800.f, 600.f };
        sceneB.cameraStart = { 100.f, 50.f };

        const std::unordered_map<SceneId, ArtCade::SceneDef> sceneMap{
            { "a", sceneA },
            { "b", sceneB },
        };
        const std::unordered_map<EntityId, EntityDef> entityDefs{
            { 1u, make_player_def(1u, 10.f, 20.f) },
            { 2u, make_player_def(2u, 50.f, 60.f) },
        };
        return gateway.replaceProject(sceneMap, entityDefs, "a", nullptr);
    }
};

int main() {
    TestHarness h{};

    expect(h.load_project(), "project loads");
    expect(h.gateway.exists(1u), "authored entity on scene a exists");
    expect(h.gateway.exists(2u), "authored entity on scene b exists");

    {
        h.gateway.destroy(1u);
        expect(!h.gateway.exists(1u), "destroyed authored entity is gone");

        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.changed, "restart succeeds after destroy");
        expect(h.gateway.exists(1u), "restart recreates destroyed authored entity");

        Transform transform{};
        expect(h.gateway.getTransform(1u, transform), "restored entity has transform");
        expect(std::fabs(transform.position.x - 10.f) < 0.01f
               && std::fabs(transform.position.y - 20.f) < 0.01f,
               "restart restores authored transform");
    }

    {
        const EntityId spawnId = h.gateway.spawnFromClass("Player", 200.f, 300.f);
        expect(spawnId != 0u, "runtime spawn succeeds");
        expect(h.gateway.exists(spawnId), "runtime spawn exists");

        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.changed, "restart after spawn succeeds");
        expect(!h.gateway.exists(spawnId), "restart removes runtime spawn");
        expect(h.gateway.exists(1u), "authored entity remains after spawn purge");
    }

    {
        expect(h.gateway.setTransform(1u, { 99.f, 88.f }, 0.f, { 1.f, 1.f }),
               "transform mutation applies");
        const auto reactivate = h.lifecycle.request_reactivate(0.f);
        expect(reactivate.changed, "reactivate succeeds");

        Transform transform{};
        h.gateway.getTransform(1u, transform);
        expect(std::fabs(transform.position.x - 99.f) < 0.01f,
               "reactivate does not restore authored transform");

        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.changed, "restart after transform drift succeeds");
        h.gateway.getTransform(1u, transform);
        expect(std::fabs(transform.position.x - 10.f) < 0.01f
               && std::fabs(transform.position.y - 20.f) < 0.01f,
               "restart restores authored transform");
    }

    {
        expect(h.variables.setEntity(1u, "hp", 42.0), "local variable mutation applies");
        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.changed, "restart after variable drift succeeds");
        const auto hp = h.variables.getEntity(1u, "hp");
        expect(std::holds_alternative<double>(hp) && std::get<double>(hp) == 100.0,
               "restart restores authored local variable default");
    }

    {
        expect(h.gateway.hasPhysicsBody(1u), "active entity has physics body");
        expect(h.gateway.setTransform(1u, { 40.f, 40.f }, 0.f, { 1.f, 1.f }),
               "move entity with physics body");
        const uint32_t handleBefore = h.gateway.physicsHandle(1u);
        expect(handleBefore != 0u, "physics handle exists before restart");

        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.changed, "restart with physics succeeds");
        expect(h.gateway.hasPhysicsBody(1u), "physics body recreated after restart");
        expect(h.gateway.physicsHandle(1u) != 0u,
               "physics handle exists after restart");

        Transform transform{};
        h.gateway.getTransform(1u, transform);
        expect(std::fabs(transform.position.x - 10.f) < 0.01f,
               "restart restores authored position for physics entity");
    }

    {
        h.gateway.setTransform(1u, { 77.f, 77.f }, 0.f, { 1.f, 1.f });
        h.lifecycle.load_immediate("b");
        expect(h.scenes.activeSceneId() == "b", "scene b is active");

        Transform sceneAEntity{};
        h.gateway.getTransform(1u, sceneAEntity);
        expect(std::fabs(sceneAEntity.position.x - 77.f) < 0.01f,
               "scene a entity transform before restart b");

        h.gateway.setTransform(2u, { 111.f, 222.f }, 0.f, { 1.f, 1.f });
        const auto restartB = h.lifecycle.request_restart(0.f);
        expect(restartB.changed, "restart scene b succeeds");

        Transform sceneBEntity{};
        h.gateway.getTransform(2u, sceneBEntity);
        expect(std::fabs(sceneBEntity.position.x - 50.f) < 0.01f
               && std::fabs(sceneBEntity.position.y - 60.f) < 0.01f,
               "restart scene b restores its authored entity");

        h.gateway.getTransform(1u, sceneAEntity);
        expect(std::fabs(sceneAEntity.position.x - 77.f) < 0.01f,
               "restart scene b does not modify scene a runtime state");
    }

    {
        h.lifecycle.load_immediate("a");
        h.gateway.destroy(1u);
        const auto reactivate = h.lifecycle.request_reactivate(0.f);
        expect(reactivate.changed, "reactivate after destroy succeeds");
        expect(!h.gateway.exists(1u), "reactivate does not recreate destroyed entity");
    }

    {
        const EntityId spawnId = h.gateway.spawnFromClass("Player", 5.f, 5.f);
        const auto reactivate = h.lifecycle.request_reactivate(0.f);
        expect(reactivate.changed, "reactivate with runtime spawn succeeds");
        expect(h.gateway.exists(spawnId), "reactivate does not remove runtime spawn");
        h.gateway.destroy(spawnId);
    }

    {
        const uint64_t revBefore = h.mutations.revision();
        const auto restart = h.lifecycle.request_restart(0.f);
        expect(restart.sceneRevision == revBefore + 1u,
               "restart bumps scene revision once");
        expect(
            ArtCade::Modules::scene_invalidation_has(
                restart.invalidations, SceneInvalidation::Collision),
            "restart queues collision rebuild");
        expect(
            ArtCade::Modules::scene_invalidation_has(
                restart.invalidations, SceneInvalidation::SceneActivation),
            "restart queues scene activation");
    }

    std::puts("scene_restart_test: all passed");
    return 0;
}
