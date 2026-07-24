// world-tilemap-collision-test.cpp - tilemap collision aggregation.

#include "modules/physics/include/physics.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "world.h"

#include <iostream>
#include <utility>

using namespace ArtCade;
using namespace ArtCade::Modules;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static CollisionBodyComponent tile_rect_body(
    CollisionResponse response,
    CollisionShapeRole role,
    std::string layerId,
    std::vector<std::string> maskLayerIds)
{
    CollisionShape shape;
    shape.type = CollisionShapeType::Rectangle;
    shape.response = response;
    shape.role = role;
    shape.layerId = std::move(layerId);
    shape.maskLayerIds = std::move(maskLayerIds);
    shape.offset = {};
    shape.size = { 32.f, 32.f };
    shape.enabled = true;

    CollisionBodyComponent body;
    body.bodyType = BodyType::Static;
    body.enabled = true;
    body.shapes.push_back(std::move(shape));
    return body;
}

static CollisionBodyComponent partial_polygon_body() {
    CollisionShape shape;
    shape.type = CollisionShapeType::Polygon;
    shape.response = CollisionResponse::Solid;
    shape.role = CollisionShapeRole::Body;
    shape.layerId = "ground";
    shape.maskLayerIds = { "player" };
    shape.points = {
        { -16.f, -16.f },
        { 16.f, -16.f },
        { -16.f, 16.f },
    };
    shape.enabled = true;

    CollisionBodyComponent body;
    body.bodyType = BodyType::Static;
    body.enabled = true;
    body.shapes.push_back(std::move(shape));
    return body;
}

static void test_tilemap_collision_uses_maximal_rectangles() {
    SceneManager sceneManager;
    RuntimeEntityGateway gateway(sceneManager);
    Physics physics;
    VariableManager variables;

    CHECK(sceneManager.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    ProjectDoc doc;
    doc.activeSceneId = "s";
    doc.physicsLayers = {
        { "player", "Player", 0, {} },
        { "ground", "Ground", 1, {} },
        { "hazard", "Hazard", 2, {} },
    };

    TilePaletteEntry groundA;
    groundA.id = 1;
    groundA.collisionBody = tile_rect_body(
        CollisionResponse::Solid,
        CollisionShapeRole::Body,
        "ground",
        { "player", "hazard" });

    TilePaletteEntry groundB = groundA;
    groundB.id = 2;
    groundB.name = "Equivalent ground";
    groundB.collisionBody->shapes[0].maskLayerIds = { "hazard", "player" };

    TilePaletteEntry hazard;
    hazard.id = 3;
    hazard.collisionBody = tile_rect_body(
        CollisionResponse::Sensor,
        CollisionShapeRole::Hitbox,
        "hazard",
        { "player" });

    TilePaletteEntry slope;
    slope.id = 4;
    slope.collisionBody = partial_polygon_body();

    TilePaletteEntry disabled;
    disabled.id = 5;
    disabled.collisionBody = tile_rect_body(
        CollisionResponse::Solid,
        CollisionShapeRole::Body,
        "ground",
        { "player" });
    disabled.collisionBody->enabled = false;

    doc.tilePalette = { groundA, groundB, hazard, slope, disabled };

    SceneDef scene;
    scene.id = "s";
    scene.tilemap.tileSize = 32.f;
    scene.tilemap.cols = 4;
    scene.tilemap.rows = 3;
    scene.tilemap.data = {
        1, 2, 0, 3,
        2, 1, 0, 3,
        5, 0, 4, 4,
    };
    doc.scenes = { { scene.id, scene } };

    World world(gateway, physics, variables);
    world.init(doc);

    // 2x2 equivalent ground -> 1 shape; hazard column -> 1 shape;
    // two non-full polygon tiles stay unmerged -> 2 shapes.
    CHECK(world.collisionShapeCount() == 4);

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    sceneManager.shutdown();
}

int main() {
    test_tilemap_collision_uses_maximal_rectangles();

    if (g_failed == 0) {
        std::cout << "world-tilemap-collision-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "world-tilemap-collision-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
