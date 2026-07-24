// collision-world-mask-test.cpp - CollisionWorld layer masks + profile resolve.

#include "collision-json.h"
#include "modules/collision/include/collision_world.h"
#include "modules/runtime-entity-gateway/src/collision-profile-resolve.h"

#include <iostream>
#include <unordered_map>

using namespace ArtCade;
using namespace ArtCade::CollisionWorld;
using namespace ArtCade::Modules::CollisionProfileResolve;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static CollisionShape makeRectShape(
    const std::string& layerId,
    const std::vector<std::string>& maskIds,
    float size = 32.f)
{
    CollisionShape shape;
    shape.type = CollisionShapeType::Rectangle;
    shape.layerId = layerId;
    shape.maskLayerIds = maskIds;
    shape.size = { size, size };
    shape.enabled = true;
    return shape;
}

static CollisionShape makeCircleShape(
    const std::string& layerId,
    const std::vector<std::string>& maskIds,
    float radius)
{
    CollisionShape shape;
    shape.type = CollisionShapeType::Circle;
    shape.layerId = layerId;
    shape.maskLayerIds = maskIds;
    shape.radius = radius;
    shape.enabled = true;
    return shape;
}

static std::vector<PhysicsLayerDef> defaultLayers() {
    return {
        { "player", "Player", 1, {} },
        { "ground", "Ground", 2, {} },
        { "enemy", "Enemy", 3, {} },
    };
}

static void test_shape_instance_scales_with_transform() {
    World world;
    world.setLayers(defaultLayers());

    Transform tf{};
    tf.position = { 0.f, 0.f };
    tf.scale = { 2.f, 0.5f };
    CollisionBodyComponent body;
    body.enabled = true;
    body.bodyType = BodyType::Static;
    CollisionShape shape = makeRectShape("player", { "ground" }, 32.f);
    shape.offset = { 4.f, 8.f };
    body.shapes.push_back(shape);

    world.addEntity(1, tf, body);
    CHECK(world.shapes().size() == 1);
    CHECK(world.shapes()[0].instance.size.x == 64.f);
    CHECK(world.shapes()[0].instance.size.y == 16.f);
    CHECK(world.shapes()[0].instance.offset.x == 8.f);
    CHECK(world.shapes()[0].instance.offset.y == 4.f);
}
static void test_overlap_respects_layer_masks() {
    World world;
    world.setLayers(defaultLayers());

    Transform playerTf{};
    playerTf.position = { 100.f, 100.f };
    CollisionBodyComponent playerBody;
    playerBody.bodyType = BodyType::Kinematic;
    playerBody.shapes.push_back(makeRectShape("player", { "ground" }));

    Transform groundTf{};
    groundTf.position = { 100.f, 100.f };
    CollisionBodyComponent groundBody;
    groundBody.bodyType = BodyType::Static;
    groundBody.shapes.push_back(makeRectShape("ground", { "player" }));

    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, groundTf, groundBody);
    CHECK(world.overlapEntities(1, 2));

    playerBody.shapes[0].maskLayerIds = { "enemy" };
    world.clear();
    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, groundTf, groundBody);
    CHECK(!world.overlapEntities(1, 2));
}

static void test_resolve_profile_shapes_from_sprite_path() {
    CollisionProfileDef profile;
    profile.id = "img_a";
    profile.coordinateSpace = CollisionProfileCoordinateSpace::FrameNormalized;
    CollisionShape norm{};
    norm.type = CollisionShapeType::Rectangle;
    norm.layerId = "player";
    norm.maskLayerIds = { "ground" };
    norm.offset = { 0.1f, 0.1f };
    norm.size = { 0.8f, 0.8f };
    norm.enabled = true;
    profile.shapes.push_back(norm);

    std::unordered_map<std::string, CollisionProfileDef> profiles{
        { "img_a", profile },
    };
    std::unordered_map<std::string, std::string> pathToId{
        { "assets/hero.png", "img_a" },
    };

    SpriteComponent sprite{};
    sprite.spriteAssetId = "assets/hero.png";
    Transform transform{};
    transform.scale = { 1.f, 1.f };
    CollisionBodyComponent authored{};
    authored.enabled = true;
    authored.bodyType = BodyType::Kinematic;

    CollisionBodyComponent resolved{};
    CHECK(resolve_collision_body(
        1, sprite, transform, authored, profiles, pathToId, nullptr, resolved));
    CHECK(!resolved.shapes.empty());
    CHECK(resolved.shapes[0].layerId == "player");
    CHECK(resolved.shapes[0].size.x > 1.f);
}

static void test_read_collision_profiles_json() {
    const auto doc = nlohmann::json::parse(R"({
      "collisionProfiles": {
        "img_a": {
          "id": "img_a",
          "name": "Hero",
          "coordinateSpace": "frame-normalized",
          "shapes": [{
            "type": "rectangle",
            "role": "body",
            "layerId": "player",
            "maskLayerIds": ["ground"],
            "offsetX": 0.1,
            "offsetY": 0.1,
            "width": 0.8,
            "height": 0.8,
            "enabled": true
          }]
        }
      }
    })");

    std::unordered_map<std::string, CollisionProfileDef> profiles;
    ProjectJson::read_collision_profiles(doc, profiles);
    CHECK(profiles.size() == 1);
    CHECK(profiles["img_a"].coordinateSpace == CollisionProfileCoordinateSpace::FrameNormalized);
    CHECK(profiles["img_a"].shapes[0].layerId == "player");
    CHECK(profiles["img_a"].shapes[0].maskLayerIds[0] == "ground");
}

static void test_contact_events_survive_world_rebuilds() {
    World world;
    world.setLayers(defaultLayers());

    Transform playerTf{};
    playerTf.position = { 100.f, 100.f };
    CollisionBodyComponent playerBody;
    playerBody.bodyType = BodyType::Kinematic;
    playerBody.shapes.push_back(makeRectShape("player", { "ground" }));

    Transform groundTf{};
    groundTf.position = { 100.f, 100.f };
    CollisionBodyComponent groundBody;
    groundBody.bodyType = BodyType::Static;
    groundBody.shapes.push_back(makeRectShape("ground", { "player" }));

    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, groundTf, groundBody);
    auto events = world.refreshEvents();
    CHECK(events.size() == 1);
    CHECK(events[0].kind == ContactEvent::Kind::Enter);
    CHECK(events[0].self == 1);
    CHECK(events[0].other == 2);

    world.clear();
    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, groundTf, groundBody);
    events = world.refreshEvents();
    CHECK(events.size() == 1);
    CHECK(events[0].kind == ContactEvent::Kind::Stay);

    groundTf.position = { 300.f, 100.f };
    world.clear();
    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, groundTf, groundBody);
    events = world.refreshEvents();
    CHECK(events.size() == 1);
    CHECK(events[0].kind == ContactEvent::Kind::Exit);
    CHECK(events[0].self == 1);
    CHECK(events[0].other == 2);
}

static void test_spatial_broadphase_keeps_queries_deterministic() {
    World world;
    world.setLayers(defaultLayers());

    CollisionBodyComponent playerBody;
    playerBody.bodyType = BodyType::Kinematic;
    playerBody.shapes.push_back(makeRectShape("player", { "ground" }, 64.f));

    CollisionBodyComponent groundBody;
    groundBody.bodyType = BodyType::Static;
    groundBody.shapes.push_back(makeRectShape("ground", { "player" }, 64.f));

    Transform playerTf{};
    playerTf.position = { -256.f, -256.f };
    Transform nearGroundTf{};
    nearGroundTf.position = { -250.f, -256.f };
    Transform farGroundTf{};
    farGroundTf.position = { 2048.f, 2048.f };

    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, farGroundTf, groundBody);
    world.addEntity(3, nearGroundTf, groundBody);

    CHECK(world.firstTouching(1) == 3);
    CHECK(world.overlapEntities(1, 3));
    CHECK(!world.overlapEntities(1, 2));
}

static void test_spatial_broadphase_does_not_duplicate_large_shape_events() {
    World world;
    world.setLayers(defaultLayers());

    Transform playerTf{};
    playerTf.position = { 0.f, 0.f };
    CollisionBodyComponent playerBody;
    playerBody.bodyType = BodyType::Kinematic;
    playerBody.shapes.push_back(makeRectShape("player", { "ground" }, 32.f));

    Transform platformTf{};
    platformTf.position = { 0.f, 0.f };
    CollisionBodyComponent platformBody;
    platformBody.bodyType = BodyType::Static;
    platformBody.shapes.push_back(makeRectShape("ground", { "player" }, 512.f));

    world.addEntity(1, playerTf, playerBody);
    world.addEntity(2, platformTf, platformBody);

    const auto events = world.refreshEvents();
    CHECK(events.size() == 1);
    if (events.size() == 1) {
        CHECK(events[0].self == 1);
        CHECK(events[0].other == 2);
    }
}

static void test_polygon_narrowphase_rejects_aabb_false_positive() {
    World world;
    world.setLayers(defaultLayers());

    CollisionBodyComponent triangleBody;
    triangleBody.bodyType = BodyType::Static;
    CollisionShape triangle = makeRectShape("ground", { "player" });
    triangle.type = CollisionShapeType::Polygon;
    triangle.offset = {};
    triangle.points = {
        { 0.f, 0.f },
        { 64.f, 0.f },
        { 0.f, 64.f },
    };
    triangleBody.shapes.push_back(triangle);

    CollisionBodyComponent playerBody;
    playerBody.bodyType = BodyType::Kinematic;
    playerBody.shapes.push_back(makeRectShape("player", { "ground" }, 8.f));

    Transform triangleTf{};
    triangleTf.position = { 0.f, 0.f };
    Transform outsideTf{};
    outsideTf.position = { 56.f, 56.f };
    Transform insideTf{};
    insideTf.position = { 20.f, 20.f };

    world.addEntity(1, triangleTf, triangleBody);
    world.addEntity(2, outsideTf, playerBody);
    CHECK(!world.overlapEntities(1, 2));

    world.clear();
    world.addEntity(1, triangleTf, triangleBody);
    world.addEntity(3, insideTf, playerBody);
    CHECK(world.overlapEntities(1, 3));
}

static void test_polygon_raycast_hits_shape_edge() {
    World world;
    world.setLayers(defaultLayers());

    CollisionBodyComponent triangleBody;
    triangleBody.bodyType = BodyType::Static;
    CollisionShape triangle = makeRectShape("ground", { "player" });
    triangle.type = CollisionShapeType::Polygon;
    triangle.points = {
        { 0.f, 0.f },
        { 64.f, 0.f },
        { 0.f, 64.f },
    };
    triangleBody.shapes.push_back(triangle);

    Transform triangleTf{};
    triangleTf.position = { 0.f, 0.f };
    world.addEntity(1, triangleTf, triangleBody);

    const auto hit = world.raycast({ -16.f, 16.f }, { 16.f, 16.f });
    CHECK(hit.hit);
    CHECK(hit.entityId == 1);
    CHECK(std::abs(hit.point.x) < 0.01f);
    CHECK(std::abs(hit.point.y - 16.f) < 0.01f);
}

static void test_capsule_narrowphase_uses_rounded_caps() {
    World world;
    world.setLayers(defaultLayers());

    CollisionBodyComponent capsuleBody;
    capsuleBody.bodyType = BodyType::Static;
    CollisionShape capsule = makeRectShape("ground", { "player" });
    capsule.type = CollisionShapeType::Capsule;
    capsule.size = { 20.f, 80.f };
    capsuleBody.shapes.push_back(capsule);

    CollisionBodyComponent circleBody;
    circleBody.bodyType = BodyType::Kinematic;
    circleBody.shapes.push_back(makeCircleShape("player", { "ground" }, 1.f));

    Transform capsuleTf{};
    capsuleTf.position = { 0.f, 0.f };
    Transform cornerTf{};
    cornerTf.position = { 9.f, 39.f };
    Transform bottomTf{};
    bottomTf.position = { 0.f, 39.f };

    world.addEntity(1, capsuleTf, capsuleBody);
    world.addEntity(2, cornerTf, circleBody);
    CHECK(!world.overlapEntities(1, 2));

    world.clear();
    world.addEntity(1, capsuleTf, capsuleBody);
    world.addEntity(3, bottomTf, circleBody);
    CHECK(world.overlapEntities(1, 3));
}

static void test_capsule_polygon_collinear_edges_can_be_separate() {
    const float distSq = PhysicsMath::distanceSegmentSegmentSq(
        { 20.f, -16.f },
        { 60.f, -16.f },
        { -16.f, -16.f },
        { 16.f, -16.f });
    CHECK(distSq > 15.9f);
    CHECK(distSq < 16.1f);
}

static void test_sweep_aabb_ignores_tangent_ground_motion() {
    const PhysicsMath::Aabb player{
        -8.f, -16.f,
        8.f, 0.f,
    };
    const PhysicsMath::Aabb ground{
        -64.f, 0.f,
        64.f, 16.f,
    };
    const auto hit = PhysicsMath::sweepAabb(player, { 24.f, 0.f }, ground);
    CHECK(!hit.hit);
}

static void test_sweep_aabb_hits_thin_wall() {
    const PhysicsMath::Aabb projectile{
        -1.f, -1.f,
        1.f, 1.f,
    };
    const PhysicsMath::Aabb wall{
        49.5f, -32.f,
        50.5f, 32.f,
    };
    const auto hit = PhysicsMath::sweepAabb(projectile, { 100.f, 0.f }, wall);
    CHECK(hit.hit);
    CHECK(hit.fraction > 0.48f && hit.fraction < 0.49f);
    CHECK(hit.normal.x < -0.5f);
}

int main() {
    test_shape_instance_scales_with_transform();
    test_overlap_respects_layer_masks();
    test_resolve_profile_shapes_from_sprite_path();
    test_read_collision_profiles_json();
    test_contact_events_survive_world_rebuilds();
    test_spatial_broadphase_keeps_queries_deterministic();
    test_spatial_broadphase_does_not_duplicate_large_shape_events();
    test_polygon_narrowphase_rejects_aabb_false_positive();
    test_polygon_raycast_hits_shape_edge();
    test_capsule_narrowphase_uses_rounded_caps();
    test_capsule_polygon_collinear_edges_can_be_separate();
    test_sweep_aabb_ignores_tangent_ground_motion();
    test_sweep_aabb_hits_thin_wall();

    if (g_failed == 0) {
        std::cout << "collision-world-mask-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "collision-world-mask-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
