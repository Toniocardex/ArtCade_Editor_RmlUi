// =============================================================================
// gameplay-tick-order-characterization-test — RU-02a (see docs/
// RU02_GAMEPLAY_SESSION_REFACTOR.md in the editor repo).
//
// Zero ownership changes: this test does not modify Application. Two kinds
// of assertion live here:
//
//   VERIFIED  — exercises real production modules (LogicRuntime, ScriptRuntime,
//               World, RuntimeEntityGateway, SpriteAnimator) directly, so a
//               regression in their own behavior fails this test today.
//   CONTRACT  — encodes the call order read out of app_loop.cpp/app_bootstrap.cpp
//               (Application::tickFixedStep/loopIteration are private and not
//               reachable from a standalone test). These assertions currently
//               only prove this test's own driver follows the documented order;
//               they become true regression guards once RU-02c exposes
//               GameplaySession::tickFixedStep and this driver is pointed at it
//               instead of replaying the sequence by hand.
// =============================================================================

#include "modules/logic-core/include/logic-core.h"
#include "modules/logic-runtime/include/logic-runtime.h"
#include "modules/script-runtime/include/script-runtime.h"
#include "modules/scene-system/include/scene-manager.h"
#include "modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "modules/physics/include/physics.h"
#include "modules/variable-manager/include/variable-manager.h"
#include "modules/sprite-animator/include/sprite-animator.h"
#include "modules/audio/include/audio.h"
#include "modules/input/include/input.h"
#include "world.h"

// RU-02c/d/e-1/e-2/e-3/RU-02f made GameplaySession real and gave it its own
// owned utility + simulation + Logic/GameAPI/LuaHost/ScriptRuntime graph
// (EventBus/TimeManager/VariableManager/TweenManager/SpriteAnimator/
// CameraManager/SaveLoadManager/GameStateManager/Physics/SceneManager/
// SceneMutationService/RuntimeEntityGateway/SceneLifecycleService/World/
// RuntimeLogicHostAdapter/LogicRuntime/GameAPI/LuaHost/ScriptRuntime) -
// gameplay_session.h now only forward-declares the modules it touches
// through pointers, so this test includes each concrete header it still
// needs directly (VariableManager/SpriteAnimator for the other test
// functions below, which don't touch GameplaySession) instead of relying on
// GameplaySession to pull them in transitively.
#include "app/src/gameplay_session.h"
#include "core/engine-context.h"

#include <iostream>
#include <string>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::Logic;
using namespace ArtCade::Scripts;
using namespace ArtCade::Modules;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

namespace {

// Minimal stand-in for RuntimeLogicHostAdapter (app_modules.h:43-191), which
// cannot be linked into a standalone test (private header of the `game`
// executable target, and would need a real Modules::Audio device never
// tested headless in this repo). Forwards transform/platformer/animation/
// variable/destroy calls to real World/RuntimeEntityGateway/VariableManager
// instances so those parts stay production-faithful; audio/input/physics-
// velocity are log-only stubs (not exercised by this test).
struct Host final : IGameplayRuntimeHost {
    RuntimeEntityGateway& gateway;
    World& world;
    VariableManager& variables;
    std::string actor;                    // "logic" or "script", set by the driver
    std::vector<std::string> callLog;      // "<actor>:<method>:<args>"

    Host(RuntimeEntityGateway& g, World& w, VariableManager& v)
        : gateway(g), world(w), variables(v) {}

    bool setVisible(EntityId owner, bool value) override {
        return gateway.setRuntimeVisible(owner, value);
    }
    bool isVisible(EntityId owner) override { return gateway.visibleInGame(owner); }
    bool setSpriteFlipX(EntityId owner, bool flipX) override {
        SpriteComponent sprite{};
        if (!gateway.getSprite(owner, sprite)) return false;
        sprite.flipX = flipX;
        callLog.push_back(actor + ":flip_x:" + (flipX ? "1" : "0"));
        return gateway.setSprite(owner, sprite);
    }
    bool setPosition(EntityId owner, Vec2 value) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.position = value;
        return gateway.setTransform(owner, t);
    }
    std::optional<Vec2> getPosition(EntityId owner) const override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return std::nullopt;
        return t.position;
    }
    std::optional<Vec2> getSceneWorldSize() const override {
        if (const SceneDef* scene = gateway.activeScene()) return scene->worldSize;
        return std::nullopt;
    }
    bool translate(EntityId owner, Vec2 delta) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.position.x += delta.x;
        t.position.y += delta.y;
        return gateway.setTransform(owner, t);
    }
    bool setRotation(EntityId owner, float radians) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.rotation = radians;
        return gateway.setTransform(owner, t);
    }
    bool rotateBy(EntityId owner, float deltaRadians) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.rotation += deltaRadians;
        return gateway.setTransform(owner, t);
    }
    bool setScale(EntityId owner, Vec2 scale) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.scale = scale;
        return gateway.setTransform(owner, t);
    }
    bool isGrounded(EntityId owner) override { return world.isPlatformerGrounded(owner); }
    bool isFalling(EntityId owner) override { return world.isPlatformerFalling(owner); }
    PlatformerState platformerState(EntityId owner) override {
        return world.platformerState(owner);
    }
    bool isPlatformerMoving(EntityId owner) override {
        return world.isPlatformerMovingHorizontally(owner);
    }
    bool requestPlatformerMove(EntityId owner, float axis) override {
        PlatformerControllerComponent pc{};
        if (!gateway.getPlatformerController(owner, pc)) return false;
        callLog.push_back(actor + ":platformer_move:" + std::to_string(axis));
        world.setMovementIntent(owner, axis, 0.f);
        return true;
    }
    bool requestTopDownMove(EntityId owner, Vec2 direction) override {
        TopDownControllerComponent tc{};
        if (!gateway.getTopDownController(owner, tc)) return false;
        world.addTopDownMovementContribution(owner, direction);
        return true;
    }
    bool requestPlatformerJump(EntityId owner) override {
        PlatformerControllerComponent pc{};
        if (!gateway.getPlatformerController(owner, pc)) return false;
        world.requestJump(owner);
        return true;
    }
    bool isObjectType(EntityId entity, const ObjectTypeId& expected) override {
        return world.isObjectType(entity, expected);
    }
    bool requestDestroy(EntityId owner) override { return world.requestDestroy(owner); }
    bool playAnimationClip(EntityId owner, const AssetId& animationAssetId,
                           const std::string& clipId) override {
        return world.playAnimationClip(owner, animationAssetId, clipId);
    }
    bool stopAnimation(EntityId owner) override { return world.stopAnimation(owner); }
    bool setAnimationPlaybackSpeed(EntityId owner, float speed) override {
        return world.setAnimationPlaybackSpeed(owner, speed);
    }
    bool playSound(EntityId owner, const AssetId&, float) override {
        return world.isActiveEntity(owner); // audio device out of scope for this test
    }
    bool setStateNumber(const GameVariableId& id, double value) override {
        return variables.setGlobal(id, value).accepted();
    }
    bool addStateNumber(const GameVariableId& id, double delta) override {
        return variables.addNumber(id, delta).accepted();
    }
    bool toggleStateBoolean(const GameVariableId& id) override {
        return variables.toggleBoolean(id).accepted();
    }
    std::optional<double> getStateNumber(const GameVariableId& id) const override {
        return variables.tryGetNumber(id);
    }
    bool setVelocity(EntityId owner, Vec2 velocity) override {
        Transform t{};
        if (!gateway.getTransform(owner, t)) return false;
        t.velocity = velocity;
        return gateway.setTransform(owner, t);
    }
    bool isKeyDown(LogicKey) override { return false; } // no real Input in this test
    EntityId spawnObjectType(EntityId owner, const ObjectTypeId& objectTypeId,
                             float x, float y) override {
        if (!world.isActiveEntity(owner) || objectTypeId.empty()) return INVALID_ENTITY;
        return gateway.spawnFromClass(objectTypeId, x, y);
    }
    bool requestSceneRestart() override { return false; }
    bool requestSceneGoTo(const SceneId&) override { return false; }
};

LogicProgram makeLogicMoveProgram(const std::string& typeId, float axis) {
    LogicProgram program;
    program.objectTypeId = typeId;
    program.boardId = "logic:" + typeId;
    program.source =
        "logic.require_api_version(2)\n"
        "logic.define_board('logic:" + typeId + "', '" + typeId + "', function(context)\n"
        "  context:on_update('move', function(dt)\n"
        "    context.self:platformer_move(" + std::to_string(axis) + ")\n"
        "  end)\n"
        "end)\n";
    return program;
}

ScriptProgram makeScriptMoveProgram(float axis) {
    ScriptProgram program;
    program.assetId = "script-move";
    program.sourcePath = "characterization-move.lua";
    program.source =
        "artcade.require_api_version(1)\n"
        "return {\n"
        "  on_update = function(ctx, dt)\n"
        "    ctx.platformer:move(" + std::to_string(axis) + ")\n"
        "  end\n"
        "}\n";
    return program;
}

// CONTRACT: app_loop.cpp:248-280 dispatches the same immutable input frame to
// Logic and then Script within one loopIteration, before any fixed step runs;
// app_loop.cpp:81-187 (Application::tickFixedStep) runs the manual Script
// update (line 124-127) strictly after the generated Logic tick (line 109)
// and before platformer/movement integration (line 130-133), so a Script's
// on_update can deliberately override a movement intent the Logic Board set
// earlier in the same fixed step.
void testLogicRunsBeforeScriptWithinOneStep() {
    SceneManager scenes;
    RuntimeEntityGateway gateway(scenes);
    Physics physics;
    VariableManager variables;
    CHECK(scenes.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero;
    hero.id = 1;
    hero.className = "Hero";
    hero.platformerController = PlatformerControllerComponent{};

    SceneDef scene;
    scene.id = "main";
    scene.entityIds = {hero.id};
    ProjectDoc project;
    project.activeSceneId = scene.id;
    project.entities = {{hero.id, hero}};
    project.scenes = {{scene.id, scene}};

    World world(gateway, physics, variables);
    world.init(project);

    Host host(gateway, world, variables);
    LogicRuntime logicRuntime(host);
    ScriptRuntime scriptRuntime(host);

    // Same shared host instance, exactly as RuntimeLogicHostAdapter is shared
    // between LogicRuntime and ScriptRuntime in app_bootstrap.cpp:93-94.
    CHECK(&logicRuntime != nullptr && &scriptRuntime != nullptr);

    std::string error;
    CHECK(logicRuntime.loadPrograms({makeLogicMoveProgram("Hero", 1.f)}, &error));
    CHECK(logicRuntime.install("Hero", hero.id, &error).has_value());
    CHECK(scriptRuntime.install(makeScriptMoveProgram(-1.f), hero.id, "move-script", &error));

    logicRuntime.beginFrame();
    logicRuntime.dispatchStart();
    scriptRuntime.dispatchStart();

    // CONTRACT: replicate tickFixedStep's Logic-then-Script ordering by hand;
    // becomes a real regression guard once RU-02c exposes the extracted tick.
    host.actor = "logic";
    logicRuntime.dispatchTick(1.f / 60.f);
    host.actor = "script";
    scriptRuntime.update(1.f / 60.f);

    CHECK(host.callLog.size() == 2);
    CHECK(host.callLog[0] == "logic:platformer_move:1.000000");
    CHECK(host.callLog[1] == "script:platformer_move:-1.000000");

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    scenes.shutdown();
}

// VERIFIED: real LogicRuntime scope install/cancel semantics.
void testSpawnInstallsScopeAndDestroyCancelsIt() {
    SceneManager scenes;
    RuntimeEntityGateway gateway(scenes);
    Physics physics;
    VariableManager variables;
    CHECK(scenes.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero;
    hero.id = 1;
    hero.className = "Hero";
    SceneDef scene;
    scene.id = "main";
    scene.entityIds = {hero.id};
    ProjectDoc project;
    project.activeSceneId = scene.id;
    project.entities = {{hero.id, hero}};
    project.scenes = {{scene.id, scene}};

    World world(gateway, physics, variables);
    Host host(gateway, world, variables);

    LogicProgram program;
    program.objectTypeId = "Hero";
    program.boardId = "logic:Hero";
    program.source =
        "logic.require_api_version(2)\n"
        "logic.define_board('logic:Hero', 'Hero', function(context)\n"
        "  context:on_start('r', function()\n"
        "    context.self:set_velocity(9, 0)\n"
        "  end)\n"
        "end)\n";

    LogicRuntime logicRuntime(host);
    std::string error;
    CHECK(logicRuntime.loadPrograms({program}, &error));

    int destroyed = 0;
    std::optional<ScopeToken> scope;
    world.setEntityDestroyedHandler([&](EntityId id) {
        if (id == hero.id && scope.has_value()) {
            // Mirrors app_bootstrap.cpp:119-126: destroy cancels the Logic scope.
            CHECK(logicRuntime.cancelScope(*scope));
            ++destroyed;
        }
    });

    world.init(project);
    scope = logicRuntime.install("Hero", hero.id, &error);
    CHECK(scope.has_value());

    logicRuntime.beginFrame();
    logicRuntime.dispatchStartForOwner(hero.id);
    Transform afterStart{};
    CHECK(gateway.getTransform(hero.id, afterStart));
    CHECK(afterStart.velocity.x == 9.f);

    CHECK(world.requestDestroy(hero.id));
    world.flushEntityQueues();
    CHECK(destroyed == 1);

    // VERIFIED: cancelScope actually disables the scope, not just bookkeeping —
    // a second cancellation attempt on the same (now-cancelled) token fails,
    // proving the scope was truly deactivated rather than merely counted once.
    CHECK(!logicRuntime.cancelScope(*scope));

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    scenes.shutdown();
}

// VERIFIED: ScriptRuntime::cancelOwner semantics (app_bootstrap.cpp:125).
void testScriptCancelOwnerStopsDispatch() {
    SceneManager scenes;
    RuntimeEntityGateway gateway(scenes);
    Physics physics;
    VariableManager variables;
    CHECK(scenes.init());
    CHECK(gateway.init());
    CHECK(physics.init());
    CHECK(variables.init());

    EntityDef hero;
    hero.id = 1;
    hero.className = "Hero";
    SceneDef scene;
    scene.id = "main";
    scene.entityIds = {hero.id};
    ProjectDoc project;
    project.activeSceneId = scene.id;
    project.entities = {{hero.id, hero}};
    project.scenes = {{scene.id, scene}};

    World world(gateway, physics, variables);
    world.init(project);
    Host host(gateway, world, variables);
    ScriptRuntime scriptRuntime(host);

    ScriptProgram program;
    program.assetId = "script-position";
    program.sourcePath = "characterization-position.lua";
    program.source =
        "artcade.require_api_version(1)\n"
        "return {\n"
        "  on_start = function(ctx) ctx.self:set_position(5, 0) end\n"
        "}\n";
    std::string error;
    CHECK(scriptRuntime.install(program, hero.id, "pos-script", &error));
    CHECK(scriptRuntime.activeScopeCount() == 1);

    scriptRuntime.dispatchStart();
    Transform t{};
    CHECK(gateway.getTransform(hero.id, t));
    CHECK(t.position.x == 5.f);

    scriptRuntime.cancelOwner(hero.id);
    CHECK(scriptRuntime.activeScopeCount() == 0);

    world.shutdown();
    variables.shutdown();
    physics.shutdown();
    gateway.shutdown();
    scenes.shutdown();
}

// VERIFIED: SpriteAnimator's finished/event buffers are drained exactly once
// per poll — tickFixedStep (app_loop.cpp:99-101) polls exactly once per fixed
// step and hands the same drained batch to both LogicRuntime and GameAPI; a
// second poll before the next update must return nothing new.
void testAnimationEventsDrainedOnce() {
    SpriteAnimator animator;
    CHECK(animator.init());

    SpriteAnimator::Clip clip;
    clip.name = "jump";
    clip.animationAssetId = "hero-animation";
    clip.assetId = "hero-sheet";
    clip.fps = 240.f; // kMaxAnimationFps (sprite-animation-core.h) - highest valid rate
    clip.frames = {{0, 0, 16, 16}, {16, 0, 16, 16}};
    clip.loop = false;
    animator.defineClip(clip);

    const EntityId hero = 1;
    CHECK(animator.play(hero, "hero-animation", "jump"));
    animator.update(0.01f);

    const auto finishedFirst = animator.pollFinished();
    CHECK(!finishedFirst.empty());
    const auto finishedSecond = animator.pollFinished();
    CHECK(finishedSecond.empty());

    animator.shutdown();
}

// VERIFIED (RU-02c/RU-02d/RU-02e-1/RU-02e-2/RU-02e-3/D-20, upgraded from the
// CONTRACT note above): drives the real GameplaySession::dispatchInput then
// GameplaySession::tickFixedStep - not a hand-replayed mirror of the
// algorithm. RU-02e-2/3 moved RuntimeLogicHostAdapter/LogicRuntime/GameAPI/
// LuaHost/ScriptRuntime ownership into GameplaySession itself, so this test
// can no longer substitute its own instrumented `Host` stub for Logic/Script.
// D-20 then removed the world()/logicRuntime() accessors this test used to
// reach the session's internals directly - it now drives the same
// production entry points Application does (loadWorldProject/
// loadLogicPrograms/installLogicScopeForEntity), which is a strictly better
// characterization: it exercises the real install path instead of hand-
// calling LogicRuntime::install(). The Logic-before-Script ordering claim is
// checked through a real production side effect instead of a callLog:
// World::setMovementIntent (driven by requestPlatformerMove) overwrites
// rather than accumulates (world_movement.cpp), so the final velocity after
// tickFixedStep reflects whichever on_update ran last - Script's -1.0 axis,
// proving it really did run after Logic's 1.0. Audio is constructed but
// never init()'d (skips InitAudioDevice(), never exercised by playSound in
// this test); Input::init() is a trivial no-op (input.cpp:36) so it's safe
// to call for real.
void testRealGameplaySessionDispatchInputThenTick() {
    Modules::Audio audio; // never init()'d - playSound is not exercised here.
    Modules::Input input;
    CHECK(input.init());

    // RU-02e-1/2/RU-02f/D-20: GameplaySession now owns the whole utility +
    // simulation + Logic/GameAPI/LuaHost graph itself - build it via
    // initializeUtilities() then initialize() then initializeGameplayModules()
    // instead of constructing everything standalone. ctx.input stays null -
    // GameAPI never queries it here; no renderer in this headless test
    // (World tolerates a null one, per RU-02a).
    GameplaySession session;
    CHECK(session.initializeUtilities([](const char*, bool ok) { return ok; }));
    EngineContext ctx;
    CHECK(session.initialize(
        PhysicsMode::Auto,
        ctx,
        nullptr,
        [](const char*, bool ok) { return ok; },
        [](const Modules::SceneTransitionResult&) {}));

    CHECK(session.initializeGameplayModules(
        ctx, audio, input,
        [](const char*, bool ok) { return ok; }));

    EntityDef hero;
    hero.id = 1;
    hero.className = "Hero";
    hero.platformerController = PlatformerControllerComponent{};
    SceneDef scene;
    scene.id = "main";
    scene.entityIds = {hero.id};
    ProjectDoc project;
    project.activeSceneId = scene.id;
    project.entities = {{hero.id, hero}};
    project.scenes = {{scene.id, scene}};

    session.loadWorldProject(project);

    std::string error;
    CHECK(session.loadLogicPrograms({makeLogicMoveProgram("Hero", 1.f)}, &error));
    // Same production entry point installLogicScopesForActiveScene's spawn
    // installer uses (installLogicScopeForEntity) - installs the scope and
    // dispatches owner-scoped Start immediately, exactly like a real spawn.
    CHECK(session.installLogicScopeForEntity(hero.id));

    // RU-02e-3: ScriptRuntime is session-owned now too - resetScriptRuntime()
    // builds it against the session's own logicHost(), same as production's
    // installScriptScopesForActiveScene.
    Scripts::ScriptRuntime& scriptRuntime = session.resetScriptRuntime();
    CHECK(scriptRuntime.install(makeScriptMoveProgram(-1.f), hero.id, "move-script", &error));

    session.wireHostPorts(nullptr, nullptr, nullptr); // audio/dialog/profiler: not exercised

    // dispatchInput below is the first and only beginFrame() call in this
    // test, exactly where it belongs: at the start of the one frame being
    // simulated. Logic's Start already fired above, at install time (matching
    // production, where installLogicScopeForEntity's dispatchStartForOwner
    // never waits for a frame boundary either).
    scriptRuntime.dispatchStart();

    // Synthetic frame: no LogicKey here has a registered on_key_* handler (the
    // installed programs use on_update), so this proves dispatchInput's own
    // plumbing runs cleanly end to end, not any particular key's side effect.
    GameplayInputFrame frame;
    frame.pressed.push_back(LogicKey::Space);
    frame.held.push_back(LogicKey::Space);
    session.dispatchInput(frame);
    CHECK(scriptRuntime.drainDiagnostics().empty());

    session.tickFixedStep(1.f / 60.f);

    // Real GameplaySession, real tickFixedStep, real World - Script's
    // on_update (axis -1.0) runs after Logic's (axis 1.0) within the same
    // step, and World::setMovementIntent overwrites rather than accumulates,
    // so the final velocity carries Script's sign only if it truly ran last.
    Transform afterTick{};
    CHECK(session.entityGateway().getTransform(hero.id, afterTick));
    CHECK(afterTick.velocity.x < 0.f);

    session.shutdownLogicModules();
    session.shutdownScriptRuntime();
    session.shutdownScriptingModules();
    session.shutdownGraph();
    session.shutdownPhysics();
    session.shutdownUtilities();
}

} // namespace

int main() {
    std::cout << "=== Gameplay Tick Order Characterization (RU-02a) ===\n";
    testLogicRunsBeforeScriptWithinOneStep();
    testSpawnInstallsScopeAndDestroyCancelsIt();
    testScriptCancelOwnerStopsDispatch();
    testAnimationEventsDrainedOnce();
    testRealGameplaySessionDispatchInputThenTick();

    std::cout << "\ngameplay-tick-order-characterization-test: " << passed
              << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}
