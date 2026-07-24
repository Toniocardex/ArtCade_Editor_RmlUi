// entity-collision-query-test.cpp — geometric entity overlap (no physics bodies).

#include "modules/collision/include/entity_collision_query.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <iostream>
#include <unordered_map>

using namespace ArtCade;
using namespace ArtCade::Modules;
using namespace ArtCade::CollisionQuery;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static EntityDef makeDef(EntityId id, const std::string& cls)
{
    EntityDef def;
    def.id = id;
    def.name = cls;
    def.className = cls;
    def.transform.position = { 0.f, 0.f };
    def.transform.scale = { 1.f, 1.f };
    def.sprite.alpha = 1.f;
    def.runtime.sceneActive = true;
    return def;
}

int main() {
    SceneManager sm;
    RuntimeEntityGateway gw(sm);
    sm.init();
    gw.init();

    SceneDef scene;
    scene.id = "s";
    scene.entityIds = { 1, 2 };
    std::unordered_map<SceneId, SceneDef> scenes{{ scene.id, scene }};

    EntityDef hero = makeDef(1, "Player");
    hero.transform.position = { 100.f, 100.f };
    EntityDef coin = makeDef(2, "Coin");
    coin.transform.position = { 100.f, 100.f };

    std::unordered_map<EntityId, EntityDef> defs{
        { 1, hero },
        { 2, coin },
    };

    CHECK(gw.replaceProject(scenes, defs, "s"));
    CHECK(gw.physicsHandle(1) == 0);
    CHECK(gw.physicsHandle(2) == 0);

    CHECK(entitiesOverlap(gw, 1, 2));

    Transform far = coin.transform;
    far.position = { 400.f, 400.f };
    CHECK(gw.setTransform(2, far));
    CHECK(!entitiesOverlap(gw, 1, 2));

    gw.shutdown();
    sm.shutdown();

    if (g_failed == 0) {
        std::cout << "entity-collision-query-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "entity-collision-query-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
