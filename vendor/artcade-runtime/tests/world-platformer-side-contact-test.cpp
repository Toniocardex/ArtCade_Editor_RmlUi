// world-platformer-side-contact-test.cpp — ADR-0052 side-contact / support.

#include "modules/physics/include/physics.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "world.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

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
    pc.coyoteTime = 0.15f;
    pc.jumpBuffer = 0.1f;
    hero.platformerController = pc;
    hero.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, { 24.f, 32.f }, true, BoxColliderMode::Solid };
    return hero;
}

static EntityDef makeSolidBox(
    EntityId id, Vec2 pos, Vec2 size, const std::string& name = "Solid")
{
    EntityDef box = makeEntity(id, name, pos);
    box.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, size, true, BoxColliderMode::Solid };
    return box;
}

static EntityDef makeOneWay(
    EntityId id, Vec2 pos, Vec2 size = { 128.f, 16.f })
{
    EntityDef box = makeEntity(id, "OneWay", pos);
    box.boxCollider2D = BoxCollider2DComponent{
        { 0.f, 0.f }, size, true, BoxColliderMode::OneWayPlatform };
    return box;
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

static ProjectDoc makeDoc(const std::vector<EntityDef>& entities) {
    SceneDef scene;
    scene.id = "s";
    ProjectDoc doc;
    doc.activeSceneId = "s";
    for (const EntityDef& e : entities) {
        scene.entityIds.push_back(e.id);
        doc.entities[e.id] = e;
    }
    doc.scenes = { { scene.id, scene } };
    return doc;
}

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

static void tickPlatformer(World& world, EntityId id, float dt,
                           float moveX = 0.f, bool jump = false)
{
    world.clearFrameMovementIntents();
    if (moveX != 0.f)
        world.setMovementIntent(id, moveX, 0.f);
    if (jump)
        world.requestJump(id);
    world.tickPlatformerControllers(dt);
}

static void fallUntilSettled(World& world, EntityId id, int maxFrames = 180) {
    constexpr float dt = 1.f / 60.f;
    for (int i = 0; i < maxFrames; ++i)
        tickPlatformer(world, id, dt);
}

// ADR-0052 Phase 0 / matrix B: airborne side contact must not hang.
static void test_side_contact_does_not_hang() {
    constexpr float dt = 1.f / 60.f;
    // Floor at y=200 (top ~192 for 16px box centered... BoxCollider at pos is center?).
    // Match oneway test: platform pos {0,200} size {128,16}.
    // Floating wall beside the air path: tall box whose side the hero can press into.
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 256.f, 16.f }, "Floor");
    // Wall left edge near hero right side while airborne; top well above feet so
    // only a lateral/corner relationship is possible mid-jump.
    EntityDef wall = makeSolidBox(3, { 40.f, 80.f }, { 24.f, 48.f }, "Wall");

    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor, wall })));

    fallUntilSettled(*h.world, 1);
    CHECK(h.world->isPlatformerGrounded(1));

    // Jump and hold Right into the floating wall.
    tickPlatformer(*h.world, 1, dt, /*moveX=*/0.f, /*jump=*/true);

    float maxY = -1e9f;
    float minY = 1e9f;
    int groundedWhileAirIntent = 0;
    bool sawFallingProgress = false;
    Transform prev{};
    CHECK(h.gateway.getTransform(1, prev));

    for (int i = 0; i < 90; ++i) {
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f, /*jump=*/false);
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        maxY = std::max(maxY, t.position.y);
        minY = std::min(minY, t.position.y);

        // While clearly above the floor and colliding with wall intent, must not
        // stabilize as grounded with near-zero vertical velocity.
        h.world->rebuildCollisionWorld();
        const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
        const auto floorAabb = entityBodyAabb(h.gateway, *h.world, 2);
        const bool aboveFloor = feet.maxY + 2.f < floorAabb.minY;
        if (aboveFloor) {
            if (h.world->isPlatformerGrounded(1) && std::fabs(t.velocity.y) < 0.01f)
                ++groundedWhileAirIntent;
            if (t.position.y > prev.position.y + 0.01f)
                sawFallingProgress = true;
        }
        prev = t;
    }

    CHECK(groundedWhileAirIntent == 0);
    CHECK(sawFallingProgress);
    // Eventually lands or is still falling — never permanently suspended mid-air.
    CHECK(h.world->isPlatformerGrounded(1) || prev.velocity.y > 0.f);
    h.shutdown();
}

static void test_wall_slide_zeros_vx_keeps_gravity() {
    constexpr float dt = 1.f / 60.f;
    // Tall wall beside the spawn; no floor — airborne press into the wall.
    EntityDef hero = makeHero(1, { 0.f, 80.f });
    EntityDef wall = makeSolidBox(2, { 28.f, 100.f }, { 24.f, 160.f }, "Wall");

    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, wall })));

    Transform start{};
    CHECK(h.gateway.getTransform(1, start));
    bool blocked = false;
    for (int i = 0; i < 45; ++i) {
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f);
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        if (std::fabs(t.velocity.x) < 0.01f) {
            blocked = true;
            break;
        }
    }
    CHECK(blocked);

    // After horizontal block, continue pressing into the wall: gravity must run.
    Transform atBlock{};
    CHECK(h.gateway.getTransform(1, atBlock));
    for (int i = 0; i < 20; ++i)
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f);
    Transform end{};
    CHECK(h.gateway.getTransform(1, end));
    CHECK(!h.world->isPlatformerGrounded(1));
    CHECK_NEAR(end.velocity.x, 0.f, 0.01f);
    CHECK(end.position.y > atBlock.position.y + 1.f);
    CHECK(end.velocity.y > 0.f);
    h.shutdown();
}

static void test_valid_floor_landing() {
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 128.f, 16.f }, "Floor");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor })));
    fallUntilSettled(*h.world, 1);
    CHECK(h.world->isPlatformerGrounded(1));
    Transform t{};
    CHECK(h.gateway.getTransform(1, t));
    CHECK_NEAR(t.velocity.y, 0.f, 0.001f);
    h.world->rebuildCollisionWorld();
    const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
    const auto plat = entityBodyAabb(h.gateway, *h.world, 2);
    CHECK_NEAR(feet.maxY, plat.minY, 0.001f);
    CHECK(h.world->platformerState(1) == PlatformerState::Stopped);
    h.shutdown();
}

static void test_walk_off_edge_applies_gravity_same_tick() {
    constexpr float dt = 1.f / 60.f;
    // Narrow floor; hero walks right off the ledge.
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 48.f, 16.f }, "Floor");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor })));
    fallUntilSettled(*h.world, 1);
    CHECK(h.world->isPlatformerGrounded(1));

    bool leftSupport = false;
    for (int i = 0; i < 120; ++i) {
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f);
        if (!h.world->isPlatformerGrounded(1)) {
            leftSupport = true;
            Transform t{};
            CHECK(h.gateway.getTransform(1, t));
            // Same tick / immediately after: gravity must be active (vy > 0).
            CHECK(t.velocity.y > 0.f);
            break;
        }
    }
    CHECK(leftSupport);
    h.shutdown();
}

static void test_ceiling_zeros_vy_not_grounded() {
    constexpr float dt = 1.f / 60.f;
    EntityDef hero = makeHero(1, { 0.f, 120.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 128.f, 16.f }, "Floor");
    EntityDef ceiling = makeSolidBox(3, { 0.f, 60.f }, { 128.f, 16.f }, "Ceiling");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor, ceiling })));
    fallUntilSettled(*h.world, 1);
    CHECK(h.world->isPlatformerGrounded(1));

    tickPlatformer(*h.world, 1, dt, 0.f, /*jump=*/true);
    bool hitCeilingPhase = false;
    for (int i = 0; i < 60; ++i) {
        tickPlatformer(*h.world, 1, dt);
        Transform t{};
        CHECK(h.gateway.getTransform(1, t));
        h.world->rebuildCollisionWorld();
        const auto head = entityBodyAabb(h.gateway, *h.world, 1);
        const auto ceil = entityBodyAabb(h.gateway, *h.world, 3);
        if (head.minY <= ceil.maxY + 1.f && t.velocity.y >= -0.01f
            && !h.world->isPlatformerGrounded(1) && t.position.y < 180.f) {
            hitCeilingPhase = true;
            // After ceiling hit while still airborne, gravity resumes.
            for (int j = 0; j < 10; ++j)
                tickPlatformer(*h.world, 1, dt);
            CHECK(h.gateway.getTransform(1, t));
            CHECK(t.velocity.y > 0.f || h.world->isPlatformerGrounded(1));
            break;
        }
    }
    CHECK(hitCeilingPhase);
    h.shutdown();
}

static void test_coyote_jump_after_leaving_support() {
    constexpr float dt = 1.f / 60.f;
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 48.f, 16.f }, "Floor");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor })));
    fallUntilSettled(*h.world, 1);

    // Walk off, then jump within coyote window without being grounded.
    bool jumpedAirborne = false;
    for (int i = 0; i < 120; ++i) {
        const bool airborne = !h.world->isPlatformerGrounded(1);
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f, /*jump=*/airborne);
        if (airborne) {
            Transform t{};
            CHECK(h.gateway.getTransform(1, t));
            if (t.velocity.y < -1.f) {
                jumpedAirborne = true;
                break;
            }
        }
    }
    CHECK(jumpedAirborne);
    h.shutdown();
}

static void test_oneway_from_above_and_side() {
    constexpr float dt = 1.f / 60.f;
    {
        EntityDef hero = makeHero(1, { 0.f, 40.f });
        EntityDef plat = makeOneWay(2, { 0.f, 200.f });
        WorldHarness h;
        CHECK(h.init(makeDoc({ hero, plat })));
        fallUntilSettled(*h.world, 1);
        CHECK(h.world->isPlatformerGrounded(1));
        h.shutdown();
    }
    {
        // Horizontal approach into one-way side while airborne: no support, no X block.
        EntityDef hero = makeHero(1, { -40.f, 100.f });
        EntityDef plat = makeOneWay(2, { 20.f, 100.f }, { 64.f, 16.f });
        WorldHarness h;
        CHECK(h.init(makeDoc({ hero, plat })));
        Transform start{};
        CHECK(h.gateway.getTransform(1, start));
        for (int i = 0; i < 40; ++i)
            tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f);
        Transform end{};
        CHECK(h.gateway.getTransform(1, end));
        CHECK(!h.world->isPlatformerGrounded(1));
        CHECK(end.position.x > start.position.x); // not hard-blocked as a wall
        CHECK(end.position.y > start.position.y); // still falling
        h.shutdown();
    }
}

static void test_collision_grounded_rejects_side_overlap() {
    // Place hero so body overlaps wall side near top corner; support must be false.
    EntityDef hero = makeHero(1, { 10.f, 80.f });
    EntityDef wall = makeSolidBox(2, { 40.f, 80.f }, { 24.f, 48.f }, "Wall");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, wall })));
    h.world->rebuildCollisionWorld();
    CHECK(!h.world->collisionGrounded(1, 0.f));
    h.shutdown();
}

// ADR-0053: tall wall excludes top/bottom; assert every fixed step on side flush.
static void test_tall_wall_side_contact_every_tick() {
    constexpr float dt = 1.f / 60.f;
    constexpr float kTestContactTolerance = 0.5f;

    EntityDef hero = makeHero(1, { 0.f, 0.f });
    // Extremely tall wall: any contact during fall can only be lateral.
    EntityDef wall = makeSolidBox(2, { 28.f, 0.f }, { 24.f, 4000.f }, "TallWall");

    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, wall })));

    float previousVy = 0.f;
    bool sawSideContact = false;
    for (int tick = 0; tick < 120; ++tick) {
        tickPlatformer(*h.world, 1, dt, /*moveX=*/1.f);

        Transform transform{};
        CHECK(h.gateway.getTransform(1, transform));
        h.world->rebuildCollisionWorld();
        const auto playerAabb = entityBodyAabb(h.gateway, *h.world, 1);
        const auto wallAabb = entityBodyAabb(h.gateway, *h.world, 2);

        const bool sideFlush =
            std::fabs(playerAabb.maxX - wallAabb.minX) <= kTestContactTolerance;
        const bool verticallyInsideWall =
            playerAabb.minY > wallAabb.minY + 32.f
            && playerAabb.maxY < wallAabb.maxY - 32.f;

        if (sideFlush && verticallyInsideWall) {
            sawSideContact = true;
            CHECK(!h.world->isPlatformerGrounded(1));
            const PlatformerState st = h.world->platformerState(1);
            CHECK(st != PlatformerState::Stopped);
            CHECK(st != PlatformerState::Moving);

            const auto contacts = h.world->lastPlatformerStepContacts(1);
            CHECK(!contacts.hitGround);
            CHECK(contacts.supportEntityId != 2);

            if (previousVy >= 0.f) {
                CHECK(transform.velocity.y >= previousVy - 0.001f);
            }
        }
        previousVy = transform.velocity.y;
    }
    CHECK(sawSideContact);
    h.shutdown();
}

static void test_stress_repeated_jumps_into_wall() {
    constexpr float dt = 1.f / 60.f;
    EntityDef hero = makeHero(1, { 0.f, 40.f });
    EntityDef floor = makeSolidBox(2, { 0.f, 200.f }, { 256.f, 16.f }, "Floor");
    EntityDef wall = makeSolidBox(3, { 36.f, 90.f }, { 24.f, 64.f }, "Wall");
    WorldHarness h;
    CHECK(h.init(makeDoc({ hero, floor, wall })));
    fallUntilSettled(*h.world, 1);

    int hangFrames = 0;
    for (int jump = 0; jump < 40; ++jump) {
        const float dir = (jump % 2 == 0) ? 1.f : -1.f;
        tickPlatformer(*h.world, 1, dt, 0.f, /*jump=*/true);
        for (int i = 0; i < 45; ++i) {
            tickPlatformer(*h.world, 1, dt, dir);
            Transform t{};
            CHECK(h.gateway.getTransform(1, t));
            h.world->rebuildCollisionWorld();
            const auto feet = entityBodyAabb(h.gateway, *h.world, 1);
            const auto floorAabb = entityBodyAabb(h.gateway, *h.world, 2);
            if (feet.maxY + 4.f < floorAabb.minY
                && h.world->isPlatformerGrounded(1)
                && std::fabs(t.velocity.y) < 0.01f) {
                ++hangFrames;
            }
        }
        // Settle briefly on floor between bursts.
        for (int i = 0; i < 30; ++i)
            tickPlatformer(*h.world, 1, dt);
    }
    CHECK(hangFrames == 0);
    h.shutdown();
}

int main() {
    test_side_contact_does_not_hang();
    test_wall_slide_zeros_vx_keeps_gravity();
    test_valid_floor_landing();
    test_walk_off_edge_applies_gravity_same_tick();
    test_ceiling_zeros_vy_not_grounded();
    test_coyote_jump_after_leaving_support();
    test_oneway_from_above_and_side();
    test_collision_grounded_rejects_side_overlap();
    test_tall_wall_side_contact_every_tick();
    test_stress_repeated_jumps_into_wall();

    if (g_failed == 0) {
        std::cout << "world-platformer-side-contact-test: " << g_passed
                  << " passed\n";
        return 0;
    }
    std::cerr << "world-platformer-side-contact-test: " << g_passed
              << " passed, " << g_failed << " failed\n";
    return 1;
}
