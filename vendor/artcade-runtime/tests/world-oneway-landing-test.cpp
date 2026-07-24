// world-oneway-landing-test.cpp — ADR-0022: One Way resting parity with Solid.

#include "modules/physics/include/physics.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "world.h"

#include <cmath>
#include <iostream>
#include <optional>

using namespace ArtCade;
using namespace ArtCade::Modules;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        const float _a = (a); \
        const float _b = (b); \
        if (std::fabs(_a - _b) <= (eps)) ++g_passed; \
        else { \
            std::cerr << "FAIL: CHECK_NEAR(" #a ", " #b ") got " << _a \
                      << " vs " << _b << " (line " << __LINE__ << ")\n"; \
            ++g_failed; \
        } \
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

static EntityDef makeHero(EntityId id, Vec2 pos) {
    EntityDef hero = makeEntity(id, "Hero", pos);
    PlatformerControllerComponent pc;
    pc.maxSpeed = 180.f;
    pc.jumpForce = 600.f;
    pc.customGravity = 1200.f;
    hero.platformerController = pc;
    hero.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 24.f, 32.f }, true, BoxColliderMode::Solid };
    return hero;
}

static EntityDef makePlatform(EntityId id, Vec2 pos, BoxColliderMode mode) {
    EntityDef platform = makeEntity(id, "Platform", pos);
    platform.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 128.f, 16.f }, true, mode };
    return platform;
}

struct WorldHarness {
    SceneManager sceneManager;
    RuntimeEntityGateway gateway{sceneManager};
    Physics physics;
    VariableManager variables;
    std::optional<World> world;

    bool init(ProjectDoc doc) {
        if (!sceneManager.init() || !gateway.init() || !physics.init()
            || !variables.init())
            return false;
        world.emplace(gateway, physics, variables);
        world->init(std::move(doc));
        return true;
    }

    void shutdown() {
        if (world) {
            world->shutdown();
            world.reset();
        }
        variables.shutdown();
        physics.shutdown();
        gateway.shutdown();
        sceneManager.shutdown();
    }
};

static PhysicsMath::Aabb entityBodyAabb(
    const RuntimeEntityGateway& gateway,
    const World& world,
    EntityId id)
{
    Transform transform{};
    gateway.getTransform(id, transform);
    for (const CollisionWorld::ShapeRef& shape : world.collisionShapes()) {
        if (shape.id != id) continue;
        if (shape.shape.role != CollisionShapeRole::Body
            && shape.shape.role != CollisionShapeRole::Feet)
            continue;
        return PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(transform, shape.shape));
    }
    return {};
}

static ProjectDoc makeDoc(EntityDef hero, EntityDef platform) {
    SceneDef scene;
    scene.id = "s";
    scene.entityIds = { hero.id, platform.id };
    ProjectDoc doc;
    doc.activeSceneId = "s";
    doc.scenes = { { scene.id, scene } };
    doc.entities = { { hero.id, hero }, { platform.id, platform } };
    return doc;
}

static void fallUntilSettled(World& world, int maxFrames = 180) {
    constexpr float dt = 1.f / 60.f;
    for (int i = 0; i < maxFrames; ++i) {
        world.clearFrameMovementIntents();
        world.tickPlatformerControllers(dt);
    }
}

static void test_landing_parity_solid_vs_oneway() {
    constexpr Vec2 kPlatformPos{ 0.f, 200.f };
    constexpr Vec2 kHeroStart{ 0.f, 40.f };

    float solidFeetMaxY = 0.f;
    float oneWayFeetMaxY = 0.f;
    float solidPlatformMinY = 0.f;
    float oneWayPlatformMinY = 0.f;

    {
        WorldHarness h;
        EntityDef hero = makeHero(1, kHeroStart);
        EntityDef platform = makePlatform(2, kPlatformPos, BoxColliderMode::Solid);
        CHECK(h.init(makeDoc(hero, platform)));
        fallUntilSettled(*h.world);
        h.world->rebuildCollisionWorld();
        const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
        const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
        solidFeetMaxY = feet.maxY;
        solidPlatformMinY = plat.minY;
        CHECK(h.world->isPlatformerGrounded(1));
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        CHECK_NEAR(t.velocity.y, 0.f, 0.001f);
        CHECK_NEAR(solidFeetMaxY, solidPlatformMinY, 0.001f);
        h.shutdown();
    }

    {
        WorldHarness h;
        EntityDef hero = makeHero(1, kHeroStart);
        EntityDef platform =
            makePlatform(2, kPlatformPos, BoxColliderMode::OneWayPlatform);
        CHECK(h.init(makeDoc(hero, platform)));
        fallUntilSettled(*h.world);
        h.world->rebuildCollisionWorld();
        const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
        const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
        oneWayFeetMaxY = feet.maxY;
        oneWayPlatformMinY = plat.minY;
        CHECK(h.world->isPlatformerGrounded(1));
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        CHECK_NEAR(t.velocity.y, 0.f, 0.001f);
        CHECK_NEAR(oneWayFeetMaxY, oneWayPlatformMinY, 0.001f);
        h.shutdown();
    }

    CHECK_NEAR(solidFeetMaxY, oneWayFeetMaxY, 0.001f);
    CHECK_NEAR(solidPlatformMinY, oneWayPlatformMinY, 0.001f);
}

static void test_stable_rest_on_oneway() {
    WorldHarness h;
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef platform =
        makePlatform(2, { 0.f, 200.f }, BoxColliderMode::OneWayPlatform);
    CHECK(h.init(makeDoc(hero, platform)));
    fallUntilSettled(*h.world);

    constexpr float dt = 1.f / 60.f;
    for (int i = 0; i < 60; ++i) {
        h.world->clearFrameMovementIntents();
        h.world->tickPlatformerControllers(dt);
        h.world->rebuildCollisionWorld();
        const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
        const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
        CHECK_NEAR(feet.maxY, plat.minY, 0.001f);
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        CHECK_NEAR(t.velocity.y, 0.f, 0.001f);
    }
    h.shutdown();
}

static void test_pass_through_from_below() {
    constexpr Vec2 kPlatformPos{ 0.f, 100.f };

    {
        WorldHarness h;
        EntityDef hero = makeHero(1, { 0.f, 140.f }); // below platform (Y-down)
        EntityDef platform =
            makePlatform(2, kPlatformPos, BoxColliderMode::OneWayPlatform);
        CHECK(h.init(makeDoc(hero, platform)));
        h.world->rebuildCollisionWorld();

        Transform before{};
        CHECK(h.gateway.getTransform(1, before));
        Transform after = before;
        after.position.y = 60.f; // move upward through platform
        float vx = 0.f;
        float vy = -400.f;
        (void)h.world->resolveKinematicCollisionBody(1, after, before, vx, vy);

        CollisionBodyComponent body{};
        CHECK(h.gateway.getResolvedCollisionBody(1, body));
        const auto feetAabb = PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(after, body.shapes[0]));
        const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
        CHECK(feetAabb.maxY < plat.minY); // crossed above top
        CHECK(after.position.y < kPlatformPos.y);
        h.shutdown();
    }

    {
        WorldHarness h;
        EntityDef hero = makeHero(1, { 0.f, 140.f });
        EntityDef platform = makePlatform(2, kPlatformPos, BoxColliderMode::Solid);
        CHECK(h.init(makeDoc(hero, platform)));
        h.world->rebuildCollisionWorld();

        Transform before{};
        CHECK(h.gateway.getTransform(1, before));
        const float startY = before.position.y;
        Transform after = before;
        after.position.y = 60.f;
        float vx = 0.f;
        float vy = -400.f;
        (void)h.world->resolveKinematicCollisionBody(1, after, before, vx, vy);

        CollisionBodyComponent body{};
        CHECK(h.gateway.getResolvedCollisionBody(1, body));
        const auto feetAabb = PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(after, body.shapes[0]));
        const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
        // Solid underside blocks upward motion: must not fully clear the platform.
        CHECK(after.position.y > 60.f);
        CHECK(feetAabb.maxY >= plat.minY - 0.5f || after.position.y >= startY - 1.f);
        h.shutdown();
    }
}

static void test_fast_fall_onto_oneway() {
    WorldHarness h;
    // Far above so one frame delta exceeds platform thickness (16).
    EntityDef hero = makeHero(1, { 0.f, -200.f });
    PlatformerControllerComponent pc = *hero.platformerController;
    pc.customGravity = 20000.f;
    hero.platformerController = pc;
    EntityDef platform =
        makePlatform(2, { 0.f, 200.f }, BoxColliderMode::OneWayPlatform);
    CHECK(h.init(makeDoc(hero, platform)));

    constexpr float dt = 1.f / 60.f;
    for (int i = 0; i < 30; ++i) {
        h.world->clearFrameMovementIntents();
        h.world->tickPlatformerControllers(dt);
    }
    h.world->rebuildCollisionWorld();
    const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
    const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
    CHECK_NEAR(feet.maxY, plat.minY, 0.001f);
    CHECK(h.world->isPlatformerGrounded(1));
    h.shutdown();
}

static void test_ascending_near_oneway_not_grounded() {
    WorldHarness h;
    // Platform top at y=192; place feet 1 px above (maxY=191).
    EntityDef hero = makeHero(1, { 0.f, 175.f });
    EntityDef platform =
        makePlatform(2, { 0.f, 200.f }, BoxColliderMode::OneWayPlatform);
    CHECK(h.init(makeDoc(hero, platform)));
    h.world->rebuildCollisionWorld();

    const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
    const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
    CHECK(std::fabs(plat.minY - feet.maxY) <= kGroundContactSkin + 0.01f);

    // Ascending: must not report grounded on One Way.
    CHECK(!h.world->collisionGrounded(1, -100.f));
    h.shutdown();
}

int main() {
    test_landing_parity_solid_vs_oneway();
    test_stable_rest_on_oneway();
    test_pass_through_from_below();
    test_fast_fall_onto_oneway();
    test_ascending_near_oneway_not_grounded();

    if (g_failed == 0) {
        std::cout << "world-oneway-landing-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "world-oneway-landing-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
