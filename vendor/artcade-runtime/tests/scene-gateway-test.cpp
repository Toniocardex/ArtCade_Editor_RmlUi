// scene-gateway-test.cpp — RuntimeEntityGateway scene activation + global state

#include "modules/scene-system/include/scene-manager.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/physics/include/physics.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "modules/sprite-animator/include/sprite-animator.h"
#include <algorithm>
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

int main() {
    SceneManager  sm;
    RuntimeEntityGateway gw(sm);
    VariableManager vm;
    Physics physics;

    sm.init();
    gw.init();
    vm.init();
    vm.configureGlobals({
        {"score", GameVariableDefinition::Type::Number, 0.0, {}},
        {"lives", GameVariableDefinition::Type::Number, 0.0, {}},
    });
    physics.init();
    gw.setPhysics(&physics);

    EntityDef player;
    player.id = 1;
    player.className = "Player";
    player.name = "Player";
    PlatformerControllerComponent playerController;
    player.platformerController = playerController;
    TopDownControllerComponent topDownController;
    topDownController.maxSpeed = 180.f;
    topDownController.fourDirections = true;
    player.topDownController = topDownController;
    LinearMoverComponent linearMover;
    linearMover.directionX = 0.f;
    linearMover.directionY = -1.f;
    linearMover.speed = 120.f;
    player.linearMover = linearMover;
    CameraTargetComponent cameraTarget;
    cameraTarget.offsetX = 10.f;
    cameraTarget.offsetY = -20.f;
    cameraTarget.followSpeed = 12.f;
    player.cameraTarget = cameraTarget;
    MagneticItemComponent magneticItem;
    magneticItem.attractTag = "pickup";
    magneticItem.radius = 180.f;
    magneticItem.pullSpeed = 350.f;
    player.magneticItem = magneticItem;
    HordeMemberComponent hordeMember;
    hordeMember.targetClass = "Player";
    hordeMember.maxSpeed = 90.f;
    hordeMember.separationRadius = 40.f;
    player.hordeMember = hordeMember;

    EntityDef coin;
    coin.id = 2;
    coin.className = "Coin";
    coin.name = "Coin";
    coin.sprite.spriteAssetId = "sprites/coin.png";
    coin.sprite.alpha = 1.f;
    coin.tags = { "pickup" };
    coin.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {24.f, 24.f}, true, BoxColliderMode::Trigger};
    AutoDestroyComponent coinAutoDestroy;
    coinAutoDestroy.lifespan = 2.f;
    coin.autoDestroy = coinAutoDestroy;

    // A legacy authored negative scale should migrate into a flip flag so scale
    // stays pure magnitude (flip is decoupled from scale).
    EntityDef flipped;
    flipped.id = 3;
    flipped.className = "Flipped";
    flipped.name = "Flipped";
    flipped.sprite.spriteAssetId = "sprites/hero.png";
    flipped.transform.scale = { -1.f, 1.f };

    SceneDef sceneA;
    sceneA.id = "scene_a";
    sceneA.name = "A";
    sceneA.entityIds = { 1, 2, 3 };

    SceneDef sceneB;
    sceneB.id = "scene_b";
    sceneB.name = "B";
    sceneB.entityIds = { 2 };

    std::unordered_map<SceneId, SceneDef> scenes{
        { sceneA.id, sceneA },
        { sceneB.id, sceneB },
    };
    std::unordered_map<EntityId, EntityDef> entities{
        { player.id, player },
        { coin.id, coin },
        { flipped.id, flipped },
    };

    CHECK(gw.replaceProject(scenes, entities, "scene_a"));

    // Negative authored scale.x migrated to a flip flag; scale becomes magnitude.
    SpriteComponent flippedSprite{};
    CHECK(gw.getSprite(3, flippedSprite));
    CHECK(flippedSprite.flipX);
    CHECK(!flippedSprite.flipY);
    Transform flippedTransform{};
    CHECK(gw.getTransform(3, flippedTransform));
    CHECK(flippedTransform.scale.x > 0.f);
    CHECK(flippedTransform.scale.y > 0.f);
    CHECK(gw.poolCount("Player") == 1);
    CHECK(gw.poolCount("Coin") == 1);
    PlatformerControllerComponent loadedController{};
    CHECK(gw.getPlatformerController(1, loadedController));
    TopDownControllerComponent loadedTopDown{};
    CHECK(gw.getTopDownController(1, loadedTopDown));
    CHECK(loadedTopDown.maxSpeed == 180.f);
    CHECK(loadedTopDown.fourDirections);
    LinearMoverComponent loadedMover{};
    CHECK(gw.getLinearMover(1, loadedMover));
    CHECK(loadedMover.directionY == -1.f);
    CHECK(loadedMover.speed == 120.f);
    CameraTargetComponent loadedCamera{};
    CHECK(gw.getCameraTarget(1, loadedCamera));
    CHECK(std::abs(loadedCamera.offsetX - 10.f) < 0.01f);
    CHECK(std::abs(loadedCamera.offsetY + 20.f) < 0.01f);
    CHECK(std::abs(loadedCamera.followSpeed - 12.f) < 0.01f);
    MagneticItemComponent loadedMagnet{};
    CHECK(gw.getMagneticItem(1, loadedMagnet));
    CHECK(loadedMagnet.attractTag == "pickup");
    CHECK(std::abs(loadedMagnet.radius - 180.f) < 0.01f);
    CHECK(std::abs(loadedMagnet.pullSpeed - 350.f) < 0.01f);
    HordeMemberComponent loadedHorde{};
    CHECK(gw.getHordeMember(1, loadedHorde));
    CHECK(loadedHorde.targetClass == "Player");
    CHECK(std::abs(loadedHorde.maxSpeed - 90.f) < 0.01f);
    CHECK(std::abs(loadedHorde.separationRadius - 40.f) < 0.01f);

    const EntityId spawned = gw.spawnFromClass("Coin", 50.f, 60.f);
    CHECK(spawned != 0);
    CHECK(gw.poolCount("Coin") == 2);
    CHECK(gw.exists(spawned) && gw.isEntityActiveInScene(spawned));
    SpriteComponent spawnedSprite{};
    CHECK(gw.getSprite(spawned, spawnedSprite));
    CHECK(spawnedSprite.spriteAssetId == "sprites/coin.png");
    spawnedSprite.alpha = 0.5f;
    CHECK(gw.setSprite(spawned, spawnedSprite));
    SpriteComponent updatedSprite{};
    CHECK(gw.getSprite(spawned, updatedSprite));
    CHECK(updatedSprite.alpha == 0.5f);
    Transform spawnedTransform{};
    CHECK(gw.getTransform(spawned, spawnedTransform));
    CHECK(spawnedTransform.position.x == 50.f);
    CHECK(spawnedTransform.position.y == 60.f);
    spawnedTransform.position = { 70.f, 80.f };
    CHECK(gw.setTransform(spawned, spawnedTransform));
    Transform movedTransform{};
    CHECK(gw.getTransform(spawned, movedTransform));
    CHECK(movedTransform.position.x == 70.f);
    CHECK(movedTransform.position.y == 80.f);
    PhysicsComponent spawnedPhysics{};
    spawnedPhysics.collider.size = { 24.f, 28.f };
    CHECK(gw.setPhysicsComponent(spawned, spawnedPhysics));
    PhysicsComponent updatedPhysics{};
    CHECK(gw.getPhysicsComponent(spawned, updatedPhysics));
    CHECK(updatedPhysics.collider.size.x == 24.f);
    CHECK(updatedPhysics.collider.size.y == 28.f);
    CollisionBodyComponent spawnedCollisionBody{};
    CHECK(gw.getResolvedCollisionBody(spawned, spawnedCollisionBody));
    CHECK(spawnedCollisionBody.shapes.size() == 1);
    CHECK(spawnedCollisionBody.shapes[0].response == CollisionResponse::Sensor);
    AutoDestroyComponent spawnedAutoDestroy{};
    CHECK(gw.getAutoDestroy(spawned, spawnedAutoDestroy));
    CHECK(spawnedAutoDestroy.lifespan == 2.f);
    spawnedAutoDestroy._timeAlive = 1.f;
    CHECK(gw.setAutoDestroy(spawned, spawnedAutoDestroy));
    AutoDestroyComponent updatedAutoDestroy{};
    CHECK(gw.getAutoDestroy(spawned, updatedAutoDestroy));
    CHECK(updatedAutoDestroy._timeAlive == 1.f);
    const SceneDef* sceneAfter = gw.activeScene();
    CHECK(sceneAfter && std::find(sceneAfter->entityIds.begin(),
                                sceneAfter->entityIds.end(), spawned)
                           != sceneAfter->entityIds.end());

    vm.setGlobal("score", 42.0);
    vm.setGlobal("lives", 3.0);

    CHECK(gw.loadScene("scene_b"));
    CHECK(gw.poolCount("Player") == 0);
    CHECK(gw.poolCount("Coin") == 1);
    CHECK(vm.getInt("score") == 42);
    CHECK(vm.getInt("lives") == 3);

    CHECK(gw.exists(2) && gw.isEntityActiveInScene(2));
    CHECK(gw.exists(1) && !gw.isEntityActiveInScene(1));

    // Kill queue: destroy deferred until flush
    CHECK(gw.exists(2));
    gw.queueDestroy(2);
    CHECK(gw.exists(2));
    gw.flushPendingOperations();
    CHECK(!gw.exists(2));

    CHECK(gw.loadScene("scene_a"));
    CHECK(gw.poolCount("Player") == 1);
    CHECK(gw.poolCount("Coin") == 1);
    SpriteComponent reactivatedSpawnedSprite{};
    CHECK(gw.getSprite(spawned, reactivatedSpawnedSprite));
    CHECK(reactivatedSpawnedSprite.alpha == 0.5f);
    const SceneDef* sceneAfterDestroy = gw.activeScene();
    CHECK(sceneAfterDestroy &&
          std::find(sceneAfterDestroy->entityIds.begin(),
                    sceneAfterDestroy->entityIds.end(), 2)
              == sceneAfterDestroy->entityIds.end());

    // P1 editor canvas drag: position-only updates must keep rotation/scale.
    EntityDef scaledPlayer = player;
    scaledPlayer.transform.rotation = 0.75f;
    scaledPlayer.transform.scale    = { 3.f, 1.f };
    scaledPlayer.transform.position = { 10.f, 20.f };
    CHECK(gw.updateEntity(1, scaledPlayer));
    Transform dragStart{};
    CHECK(gw.getAuthoringTransform(1, dragStart));
    CHECK(std::abs(dragStart.rotation - 0.75f) < 1e-4f);
    CHECK(std::abs(dragStart.scale.x - 3.f) < 1e-4f);
    dragStart.position = { 99.f, 88.f };
    CHECK(gw.setTransform(1, dragStart));
    Transform dragEnd{};
    CHECK(gw.getAuthoringTransform(1, dragEnd));
    CHECK(dragEnd.position.x == 99.f);
    CHECK(dragEnd.position.y == 88.f);
    CHECK(std::abs(dragEnd.rotation - 0.75f) < 1e-4f);
    CHECK(std::abs(dragEnd.scale.x - 3.f) < 1e-4f);
    CHECK(std::abs(dragEnd.scale.y - 1.f) < 1e-4f);

    // ── playClipOnSpawn re-arm after the enter-play module reset ───────────
    // Repro: replaceProject activates the entity and plays its spawn clip, then
    // the editor's gameplay-module reset clears animator instances. The fix is
    // gw.replayActiveSpawnClips(), invoked after the reset on enter-play.
    SpriteAnimator anim;
    anim.init();
    SpriteAnimator::Clip idleClip;
    idleClip.name  = "idle";
    idleClip.fps   = 8.f;
    idleClip.loop  = true;
    idleClip.frames = { {0, 0, 16, 16}, {16, 0, 16, 16} };
    anim.defineClip(idleClip);
    gw.setSpriteAnimator(&anim);

    EntityDef spawnAnimated = player;           // entity id 1, active in scene_a
    spawnAnimated.sprite.spriteAssetId  = "idle.png";
    spawnAnimated.sprite.defaultClip    = "idle";
    spawnAnimated.sprite.playClipOnSpawn = true;
    CHECK(gw.updateEntity(1, spawnAnimated));

    // Simulate the reset wiping animator instances created at activation time.
    anim.clearInstances();
    CHECK(!anim.isPlaying(1));

    // The fix: replay re-arms playClipOnSpawn for every active entity.
    gw.replayActiveSpawnClips();
    CHECK(anim.isPlaying(1));
    CHECK(anim.currentClip(1) == "idle");

    // Entities without playClipOnSpawn must stay idle after replay.
    EntityDef noSpawnClip = spawnAnimated;
    noSpawnClip.sprite.playClipOnSpawn = false;
    CHECK(gw.updateEntity(1, noSpawnClip));
    anim.clearInstances();
    gw.replayActiveSpawnClips();
    CHECK(!anim.isPlaying(1));

    anim.shutdown();

    gw.shutdown();
    sm.shutdown();
    vm.shutdown();
    physics.shutdown();

    std::cout << "scene-gateway-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
