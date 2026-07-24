// world-topdown-boxcollider-test.cpp — ADR-0021: Top Down blocks on BoxCollider2D.

#include "modules/physics/include/physics.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "world.h"

#include <cmath>
#include <iostream>

using namespace ArtCade;
using namespace ArtCade::Modules;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static EntityDef makeEntity(EntityId id, const std::string& cls, Vec2 pos) {
    EntityDef def;
    def.id = id;
    def.name = cls;
    def.className = cls;
    def.transform.position = pos;
    def.transform.scale = { 1.f, 1.f };
    def.sprite.alpha = 1.f;
    def.runtime.sceneActive = true;
    return def;
}

static EntityDef makeTopDownHero(EntityId id, Vec2 pos) {
    EntityDef hero = makeEntity(id, "Hero", pos);
    TopDownControllerComponent topDown;
    topDown.maxSpeed = 400.f;
    topDown.acceleration = 10000.f;
    topDown.friction = 10000.f;
    topDown.fourDirections = true;
    hero.topDownController = topDown;
    hero.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 32.f, 32.f }, true, BoxColliderMode::Solid };
    return hero;
}

static void runRight(World& world, EntityId id, int frames) {
    constexpr float dt = 1.f / 60.f;
    for (int i = 0; i < frames; ++i) {
        world.clearFrameMovementIntents();
        world.addTopDownMovementContribution(id, { 1.f, 0.f });
        world.tickGameplaySystems(dt);
    }
}

static void test_topdown_blocked_by_solid_boxcollider_without_physics_bodies() {
    SceneManager sceneManager;
    RuntimeEntityGateway gateway(sceneManager);
    Physics physics;
    VariableManager variables;

    CHECK(sceneManager.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero = makeTopDownHero(1, { 0.f, 0.f });

    // Wall: BoxCollider2D only — no explicit Physics collider.
    EntityDef wall = makeEntity(2, "Wall", { 64.f, 0.f });
    wall.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 32.f, 32.f }, true, BoxColliderMode::Solid };

    SceneDef scene;
    scene.id = "s";
    scene.entityIds = { 1, 2 };

    ProjectDoc doc;
    doc.activeSceneId = "s";
    doc.scenes = { { scene.id, scene } };
    doc.entities = { { hero.id, hero }, { wall.id, wall } };

    World world(gateway, physics, variables);
    world.init(doc);

    // Slice 2: neither Top Down hero nor Solid wall invents a Physics body.
    CHECK(gateway.physicsHandle(1) == 0);
    CHECK(gateway.physicsHandle(2) == 0);
    CHECK(world.collisionShapeCount() >= 2);

    runRight(world, 1, 90);

    Transform heroTransform{};
    CHECK(gateway.getTransform(1, heroTransform));
    // Centers: hero half-extent 16, wall left edge at 64-16=48 → max center 32.
    CHECK(heroTransform.position.x <= 32.5f);
    CHECK(heroTransform.position.x > 0.f);
    CHECK(std::abs(heroTransform.position.y) < 0.5f);

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    sceneManager.shutdown();
}

static void test_topdown_passes_trigger_boxcollider() {
    SceneManager sceneManager;
    RuntimeEntityGateway gateway(sceneManager);
    Physics physics;
    VariableManager variables;

    CHECK(sceneManager.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero = makeTopDownHero(1, { 0.f, 0.f });

    EntityDef trigger = makeEntity(2, "Trigger", { 64.f, 0.f });
    trigger.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 32.f, 32.f }, true, BoxColliderMode::Trigger };

    SceneDef scene;
    scene.id = "s";
    scene.entityIds = { 1, 2 };

    ProjectDoc doc;
    doc.activeSceneId = "s";
    doc.scenes = { { scene.id, scene } };
    doc.entities = { { hero.id, hero }, { trigger.id, trigger } };

    World world(gateway, physics, variables);
    world.init(doc);

    CHECK(gateway.physicsHandle(1) == 0);
    CHECK(gateway.physicsHandle(2) == 0);

    runRight(world, 1, 90);

    Transform heroTransform{};
    CHECK(gateway.getTransform(1, heroTransform));
    CHECK(heroTransform.position.x > 48.f);

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    sceneManager.shutdown();
}

static void test_topdown_ignores_disabled_boxcollider() {
    SceneManager sceneManager;
    RuntimeEntityGateway gateway(sceneManager);
    Physics physics;
    VariableManager variables;

    CHECK(sceneManager.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero = makeTopDownHero(1, { 0.f, 0.f });

    EntityDef wall = makeEntity(2, "Wall", { 64.f, 0.f });
    wall.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 32.f, 32.f }, false, BoxColliderMode::Solid };

    SceneDef scene;
    scene.id = "s";
    scene.entityIds = { 1, 2 };

    ProjectDoc doc;
    doc.activeSceneId = "s";
    doc.scenes = { { scene.id, scene } };
    doc.entities = { { hero.id, hero }, { wall.id, wall } };

    World world(gateway, physics, variables);
    world.init(doc);

    runRight(world, 1, 90);

    Transform heroTransform{};
    CHECK(gateway.getTransform(1, heroTransform));
    CHECK(heroTransform.position.x > 48.f);

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    sceneManager.shutdown();
}

int main() {
    test_topdown_blocked_by_solid_boxcollider_without_physics_bodies();
    test_topdown_passes_trigger_boxcollider();
    test_topdown_ignores_disabled_boxcollider();

    if (g_failed == 0) {
        std::cout << "world-topdown-boxcollider-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "world-topdown-boxcollider-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
