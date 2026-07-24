#include "modules/physics/include/physics.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/sprite-animator/include/sprite-animator.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "world.h"

#include <iostream>

using namespace ArtCade;
using namespace ArtCade::Modules;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

int main() {
    SceneManager scenes;
    RuntimeEntityGateway gateway(scenes);
    Physics physics;
    VariableManager variables;
    SpriteAnimator animator;
    CHECK(scenes.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());
    CHECK(animator.init());

    SpriteAnimator::Clip clip;
    clip.name = "jump";
    clip.animationAssetId = "hero-animation";
    clip.assetId = "hero-sheet";
    clip.fps = 10.f;
    clip.frames = {{0, 0, 16, 16}, {16, 0, 16, 16}};
    animator.defineClip(clip);

    EntityDef hero;
    hero.id = 1;
    hero.className = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    hero.spriteAnimator = SpriteAnimatorComponent{"hero-animation", "jump", false, 1.f};

    EntityDef plain;
    plain.id = 2;
    plain.className = "Plain";

    SceneDef scene;
    scene.id = "main";
    scene.entityIds = {hero.id, plain.id};
    ProjectDoc project;
    project.activeSceneId = scene.id;
    project.entities = {{hero.id, hero}, {plain.id, plain}};
    project.scenes = {{scene.id, scene}};

    World world(gateway, physics, variables);
    world.setSpriteAnimator(&animator);
    int destroyed = 0;
    world.setEntityDestroyedHandler([&](EntityId id) {
        if (id == hero.id) ++destroyed;
    });
    world.init(project);

    CHECK(world.isObjectType(hero.id, "Hero"));
    CHECK(!world.isObjectType(hero.id, "Plain"));
    CHECK(!world.isObjectType(999, "Hero"));
    CHECK(world.playAnimationClip(hero.id, "hero-animation", "jump"));
    CHECK(animator.isPlaying(hero.id));
    CHECK(animator.frameIndex(hero.id) == 0);
    CHECK(!world.playAnimationClip(hero.id, "wrong-animation", "jump"));
    CHECK(!world.playAnimationClip(plain.id, "hero-animation", "jump"));
    CHECK(world.setAnimationPlaybackSpeed(hero.id, 2.f));
    CHECK(animator.playbackSpeed(hero.id) == 2.f);
    CHECK(!world.setAnimationPlaybackSpeed(hero.id, 0.f));
    CHECK(world.stopAnimation(hero.id));
    CHECK(!animator.isPlaying(hero.id));

    CHECK(world.requestDestroy(hero.id));
    CHECK(world.requestDestroy(hero.id));
    CHECK(world.isActiveEntity(hero.id));
    CHECK(destroyed == 0);
    world.flushEntityQueues();
    CHECK(destroyed == 1);
    CHECK(!world.isActiveEntity(hero.id));
    CHECK(!world.requestDestroy(hero.id));
    CHECK(!animator.isPlaying(hero.id));

    world.shutdown();
    animator.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    scenes.shutdown();

    if (failed == 0) {
        std::cout << "runtime-logic-host-parity-test: " << passed << " passed\n";
        return 0;
    }
    std::cerr << "runtime-logic-host-parity-test: " << passed << " passed, "
              << failed << " failed\n";
    return 1;
}
