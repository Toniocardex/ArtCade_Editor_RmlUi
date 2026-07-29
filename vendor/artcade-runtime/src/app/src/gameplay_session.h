#pragma once

// RU-02c/RU-02e-1/RU-02e-2/RU-02e-3/RU-02f/RU-02h-D20 (docs/RU02_GAMEPLAY_
// SESSION_REFACTOR.md, editor repo): the gameplay tick algorithm moved
// verbatim out of Application::tickFixedStep / Application::
// dispatchGameplayCollisionTransitions (RU-02c), then the simulation graph
// (Physics/SceneManager/SceneMutationService/RuntimeEntityGateway/
// SceneLifecycleService/World) moved into this class's own composition
// root, `initialize()` (RU-02e-1), then RuntimeLogicHostAdapter/
// LogicRuntime/GameAPI/LuaHost moved in too via `initializeGameplayModules()`
// (RU-02e-2), then ScriptRuntime - reconstructed per-scene, unlike every
// other module here which is boot-time-stable - moved in via
// `resetScriptRuntime()`/`clearScriptRuntime()` (RU-02e-3), then the utility
// modules (EventBus/TimeManager/VariableManager/TweenManager/SpriteAnimator/
// CameraManager/SaveLoadManager/GameStateManager) moved in via
// `initializeUtilities()` (RU-02f). Application now owns only host modules
// (Renderer/Input/Audio/TextureManager/AssetLoader/DialogManager/
// EditorViewportService); every gameplay module is session-owned.
//
// D-20 ("Accesso mutabile agli interni della sessione", debt register P0,
// eliminazione RU-02h): world()/physics()/sceneMutation()/logicHost()/
// logicRuntime()/gameAPI()/luaHost() used to hand out live mutable
// references to internal modules, letting Application call arbitrary
// methods on them directly. Removed in favor of narrow, intent-named
// methods (setGravity/loadLogicPrograms/loadLuaSource/etc.) and a
// const-only debugWorldView() for the one legitimate read-only consumer
// (physics_debug_renderer.cpp's debug-shape overlay). Logic/Script scope
// bookkeeping (logicScopes_/logicObjectTypes_/scriptPrograms_/
// scriptAttachments_) and installLogicScopesForActiveScene()/
// installLogicScopeForEntity()/installScriptScopesForActiveScene() moved in
// from Application too, since they are simulation state/behavior that lived
// across ticks (D-09's general rule), not host bookkeeping - this also lets
// World's entity-destroyed handler and the Logic spawn installer capture
// `this` (GameplaySession) instead of Application, closing the last D-08
// callback-capture gap.
//
// sceneManager()/entityGateway()/spriteAnimator() stay as live accessors:
// The native host reads scene and entity state at the presentation boundary;
// that data is stable for the rendered frame and not simulation control.

#include "gameplay_host_ports.h"
#include "../render/tilemap_component_resolve.h"

#include "../../core/gameplay-runtime-host.h"
#include "../../core/types.h"
#include "../../modules/scene-system/include/scene-lifecycle-result.h"
// Needed as a complete type: scriptPrograms_ below stores Scripts::
// ScriptProgram by value in an unordered_map, unlike LogicProgram (only ever
// named via a std::vector<T>& parameter, where a forward declaration is
// enough).
#include "../../modules/script-core/include/script-core.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ArtCade::Modules {
class Physics;
class SceneManager;
class SceneMutationService;
class RuntimeEntityGateway;
class SceneLifecycleService;
class TimeManager;
class TweenManager;
class SpriteAnimator;
class CameraManager;
class GameStateManager;
class EventBus;
class GameAPI;
class LuaHost;
class VariableManager;
class Input;
class Audio;
class Renderer;
class SaveLoadManager;
struct ScenePatch;
struct SceneMutationResult;
} // namespace ArtCade::Modules

namespace ArtCade::Logic {
class LogicRuntime;
struct LogicProgram;
// Matches the real declaration in logic-runtime.h exactly (redeclaring a
// type alias with an identical underlying type is well-formed in C++) - the
// full header isn't included here, but logicScopes_ below needs the
// complete (trivial) type to declare its map value.
using ScopeToken = std::uint64_t;
} // namespace ArtCade::Logic

namespace ArtCade::Scripts {
class ScriptRuntime;
struct ScriptRuntimeDiagnostic;
} // namespace ArtCade::Scripts

namespace ArtCade {

class World;
struct EngineContext;
struct SceneFrameSnapshot;

// RU-02d (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md 5, editor repo): the host's
// input polling result for one frame, immutable once built. Deliberately
// narrower than the plan's own sketch (no pointer fields) - GameplaySession
// consumes no pointer/mouse input today; widen only when a real caller needs
// it, matching how the RU-02c host ports were scoped to actual usage.
struct GameplayInputFrame {
    std::vector<LogicKey> pressed;
    std::vector<LogicKey> released;
    std::vector<LogicKey> held;
};

// ADR-0039 §7/§8: separates "scopes installed" from "On Start dispatched"
// so a caller (Application's startup phase machine, a scene transition) can
// prepare a scene's Logic/Script scopes without gameplay callbacks firing,
// then activate them atomically once. Full transition table:
//
//   Empty     + prepare -> Prepared
//   Prepared  + prepare -> reject
//   Started   + prepare -> reject
//   Prepared  + start   -> Started
//   Empty     + start   -> reject
//   Started   + start   -> reject
//   any       + reset   -> Empty  (new World/scene, project replace, editor
//                                  enter/exit Play, scene transition/restart,
//                                  shutdown, or a failed prepare)
enum class GameplayActivationState {
    Empty,
    Prepared,
    Started,
};

// RU-02e-2: minimal stand-in kept in its own translation unit-visible spot
// (moved verbatim from app_modules.h, previously private to the `game`
// executable target) so GameplaySession can own an instance directly.
// Forwards transform/platformer/animation/variable/destroy calls to
// World/RuntimeEntityGateway/VariableManager/Physics - method bodies
// live in gameplay_session.cpp, where those concrete headers are already
// fully included; only reference/pointer members are declared here, so this
// header itself does not need their complete definitions.
class RuntimeLogicHostAdapter final : public IGameplayRuntimeHost {
public:
    using SpawnInstaller = std::function<bool(EntityId)>;

    // Deferred Logic scene request (scene.restart / scene.go_to). Queued here
    // and flushed by GameplaySession::tickFixedStep at the same safe point
    // that ticks SceneLifecycleService - never committed synchronously from a
    // Lua callback, where cancelling the caller's own scope mid-dispatch
    // would fail the rest of the rule and drop the new scene's On Start
    // (LogicRuntime::dispatch refuses nested dispatch). Last request wins,
    // matching SceneLifecycleService's own pending-transition semantics.
    struct PendingSceneRequest {
        enum class Kind { Restart, Load };
        Kind kind = Kind::Restart;
        SceneId sceneId;
    };

    RuntimeLogicHostAdapter(Modules::RuntimeEntityGateway& gateway, Modules::Audio& audio,
                            Modules::CameraManager& cameraManager);

    /** World is constructed after this adapter; wired in once available. */
    void setWorld(World* world) { world_ = world; }
    void setVariableManager(Modules::VariableManager* variables) { variables_ = variables; }
    void setPhysics(Modules::Physics* physics) { physics_ = physics; }
    void setSpawnInstaller(SpawnInstaller installer) { spawnInstaller_ = std::move(installer); }
    void setHeldLogicKeys(const std::vector<LogicKey>& keys) { heldLogicKeys_ = keys; }

    bool setVisible(EntityId owner, bool value) override;
    bool isVisible(EntityId owner) override;
    bool setSpriteFlipX(EntityId owner, bool flipX) override;
    bool setPosition(EntityId owner, Vec2 value) override;
    std::optional<Vec2> getPosition(EntityId owner) const override;
    std::optional<Vec2> getSceneWorldSize() const override;
    bool isOutsideSceneBounds(EntityId owner, float margin) const override;
    bool translate(EntityId owner, Vec2 delta) override;
    bool setRotation(EntityId owner, float radians) override;
    bool rotateBy(EntityId owner, float deltaRadians) override;
    bool setScale(EntityId owner, Vec2 scale) override;
    bool isGrounded(EntityId owner) override;
    bool isFalling(EntityId owner) override;
    PlatformerState platformerState(EntityId owner) override;
    bool isPlatformerMoving(EntityId owner) override;
    bool requestPlatformerMove(EntityId owner, float axis) override;
    bool requestTopDownMove(EntityId owner, Vec2 direction) override;
    bool requestPlatformerJump(EntityId owner) override;
    bool isObjectType(EntityId entity, const ObjectTypeId& expected) override;
    bool requestDestroy(EntityId owner) override;
    bool playAnimationClip(EntityId owner, const AssetId& animationAssetId,
                           const std::string& clipId) override;
    bool stopAnimation(EntityId owner) override;
    bool setAnimationPlaybackSpeed(EntityId owner, float speed) override;
    bool playSound(EntityId owner, const AssetId& audioAssetId, float volume) override;
    bool setStateNumber(const GameVariableId& id, double value) override;
    bool addStateNumber(const GameVariableId& id, double delta) override;
    bool toggleStateBoolean(const GameVariableId& id) override;
    std::optional<double> getStateNumber(const GameVariableId& id) const override;
    std::optional<bool> getStateBoolean(const GameVariableId& id) const override;
    std::optional<std::string> getStateString(const GameVariableId& id) const override;
    std::optional<double> getLocalNumber(EntityId owner,
                                         const GameVariableId& id) const override;
    bool setVelocity(EntityId owner, Vec2 velocity) override;
    bool isKeyDown(LogicKey key) override;
    EntityId spawnObjectType(EntityId owner, const ObjectTypeId& objectTypeId,
                             float x, float y) override;
    bool requestSceneRestart() override;
    bool requestSceneGoTo(const SceneId& sceneId) override;
    bool cameraShake(float intensity, float durationSeconds) override;
    /** Returns and clears the queued scene request (flush point only). */
    std::optional<PendingSceneRequest> takePendingSceneRequest();

private:
    Modules::RuntimeEntityGateway& gateway_;
    Modules::Audio& audio_;
    Modules::CameraManager& cameraManager_;
    World* world_ = nullptr;
    Modules::VariableManager* variables_ = nullptr;
    Modules::Physics* physics_ = nullptr;
    std::vector<LogicKey> heldLogicKeys_;
    SpawnInstaller spawnInstaller_;
    std::optional<PendingSceneRequest> pendingSceneRequest_;
};

class GameplaySession {
public:
    using BootStepFn = std::function<bool(const char*, bool)>;
    using SceneTransitionHandlerFn =
        std::function<void(const Modules::SceneTransitionResult&)>;

    GameplaySession();
    ~GameplaySession();

    // RU-02f: builds EventBus, TimeManager, VariableManager, TweenManager,
    // SpriteAnimator, CameraManager, SaveLoadManager and GameStateManager, in
    // the same order and with the same boot-step names and internal wiring
    // (GameStateManager::setEventBus) Application::initUtilities() used to
    // perform directly. Must run before initialize(), which needs
    // variableManager() already built (World's constructor takes it).
    bool initializeUtilities(const BootStepFn& bootStep);

    Modules::EventBus& eventBus() { return *eventBus_; }
    Modules::TimeManager& timeManager() { return *timeManager_; }
    Modules::VariableManager& variableManager() { return *variableManager_; }
    Modules::TweenManager& tweenManager() { return *tweenManager_; }
    Modules::SpriteAnimator& spriteAnimator() { return *spriteAnimator_; }
    Modules::CameraManager& cameraManager() { return *cameraManager_; }
    Modules::SaveLoadManager& saveLoadManager() { return *saveLoadManager_; }
    Modules::GameStateManager& gameStateManager() { return *gameStateManager_; }

    // RU-02e-1: builds the simulation graph (Physics, SceneManager,
    // SceneMutationService, RuntimeEntityGateway, SceneLifecycleService,
    // World) in the same order and with the same internal wiring
    // Application::initSubsystems() used to perform directly. `bootStep`
    // preserves the existing boot-failure telemetry (same step names as
    // before: "physics", "scene_manager", "entity_gateway"). `onSceneTransition`
    // is the one callback that still needs to reach the host, since it
    // mutates Application::pendingSceneInvalidations_. `ctx` is Application's
    // EngineContext, populated here with physics/sceneManager/entityGateway/
    // world (D-20: Application no longer fetches these via accessors just to
    // copy them into ctx). `presentationRenderer` is the one remaining T-02
    // debt (concrete Renderer* passed straight through to World::setRenderer);
    // may be null (World tolerates it, e.g. in headless tests).
    bool initialize(PhysicsMode physicsMode,
                     EngineContext& ctx,
                     Modules::Renderer* presentationRenderer,
                     const BootStepFn& bootStep,
                     SceneTransitionHandlerFn onSceneTransition);

    // RU-02e-2/D-20: builds RuntimeLogicHostAdapter, LogicRuntime, GameAPI and
    // LuaHost, in the same order and with the same internal wiring
    // Application::initSubsystems() used to perform directly. `ctx` is
    // Application's own EngineContext (still host-owned - GameAPI needs it
    // fully populated with renderer/physics/input/audio/etc. by this point);
    // this method only sets `ctx.gameAPI`/`ctx.luaHost`, mirroring what
    // Application used to do right after constructing each. `audio` is the
    // Application-owned module RuntimeLogicHostAdapter needs.
    // The Logic spawn installer now wires to installLogicScopeForEntity()
    // internally (D-20/D-08) - no external spawnInstaller callback needed
    // anymore, since that method lives here too.
    bool initializeGameplayModules(EngineContext& ctx,
                                    Modules::Audio& audio,
                                    const BootStepFn& bootStep);

    // RU-02f: replaces the transitional GameplayRuntimeRefs struct (T-01,
    // eliminated as scheduled) - wires the three host-port adapters
    // Application still owns outright (Application::initSubsystems(), after
    // initializeGameplayModules()) directly, no struct needed since nothing
    // else here is externally-owned anymore.
    void wireHostPorts(IGameplayAudioService* audio,
                        IGameplayDialogGate* dialog,
                        IRuntimeProfilerSink* profiler) {
        audioPort_ = audio;
        dialogPort_ = dialog;
        profilerPort_ = profiler;
    }

    // SceneManager/RuntimeEntityGateway stay exposed for native presentation
    // data, not mutable simulation control.
    Modules::SceneManager& sceneManager() { return *sceneManager_; }
    Modules::RuntimeEntityGateway& entityGateway() { return *entityGateway_; }

    // D-20: read-only debug view for physics_debug_renderer.cpp's collision-
    // shape overlay (RU-02g's other approved live-read exception) - a const
    // reference cannot mutate simulation state, so this does not reopen the
    // "mutable access to internals" gate the mutable world() accessor used
    // to leave open.
    const World& debugWorldView() const { return *world_; }
    /** Read-only runtime camera state for non-runtime presentation hosts. */
    Vec2 cameraCenter() const;
    /**
     * Host-cadence camera shake (RU02): once per real frame, never inside
     * tickFixedStep. Refreshes render-only offset then decays trauma.
     */
    void updateCameraShake(float frameDt);
    /**
     * Authoritative cameraCenter plus current shake offset for presentation
     * (Editor Play Scene View / equivalent). Does not mutate simulation state.
     */
    Vec2 presentationCameraCenter() const;

    // D-20: replaces the removed `Modules::Physics& physics()` accessor -
    // Application only ever used it for one thing (applyRuntimeSettings).
    void setGravity(Vec2 gravity);

    // D-20: replaces the removed `Modules::SceneMutationService&
    // sceneMutation()` accessor with the exact operations Application (scene
    // mutation bridge, invalidation queueing, render revision read) performs -
    // thin forwards to SceneMutationService, no new behavior.
    uint64_t bumpSceneRevision();
    uint64_t sceneRevision() const;
    void beginSceneMutationBatch();
    Modules::SceneMutationResult commitSceneMutationBatch();
    Modules::SceneMutationResult applySceneMutation(const SceneId& sceneId,
                                                     const Modules::ScenePatch& patch);
    bool sceneMutationBatchOpen() const;

    // D-20: replaces the removed `World& world()` accessor's mutating call
    // sites (project load/editor sync/restore/collision rebuild) - thin
    // forwards to World, no new behavior.
    void loadWorldProject(const ProjectDoc& doc);
    void syncWorldAfterEditorProject(const std::vector<TilePaletteEntry>& tilePalette);
    void restoreWorldDesignState(const std::vector<TilePaletteEntry>& tilePalette);
    void rebuildWorldCollision();

    // D-20: replaces the removed `Logic::LogicRuntime& logicRuntime()`
    // accessor - the only direct call Application made beyond scope
    // install/cancel (now internalized below) was loading compiled programs.
    bool loadLogicPrograms(const std::vector<Logic::LogicProgram>& programs,
                            std::string* error);

    // D-20: replaces the removed `Modules::LuaHost& luaHost()` accessor's
    // mutating call sites (project load).
    void loadLuaSource(const std::string& source);
    bool loadLuaBytecode(const uint8_t* data, size_t size, std::string* error);
    // Native bootstrap may inspect the Lua host without taking ownership.
    Modules::LuaHost* luaHostHandle() { return luaHost_.get(); }

    // D-20/D-08/D-09: Logic/Script scope bookkeeping and install/cancel moved
    // in from Application (installLogicScopesForActiveScene/
    // installLogicScopeForEntity/installScriptScopesForActiveScene) - it is
    // simulation state that lives across ticks (which entity owns which
    // scope token, which object types have a compiled program), not host
    // bookkeeping, and moving it lets World's destroy handler and the Logic
    // spawn installer capture `this` (GameplaySession) instead of Application
    // (closing the D-08 gap those two callbacks were still leaving open).
    // `setScriptCatalog` receives the resolved/validated script programs and
    // attachments Application builds from AssetLoader + ProjectDoc (legitimate
    // host/asset-loading work) and stores them for installScriptScopesForActiveScene.
    void setScriptCatalog(std::unordered_map<AssetId, Scripts::ScriptProgram> programs,
                          std::unordered_map<ObjectTypeId, std::vector<ScriptAttachmentDef>> attachments);
    // ADR-0039 §7: install-only now - neither dispatches Logic nor Script
    // On Start. installLogicScopeForEntity() is unaffected (runtime spawns,
    // always mid-simulation, dispatches only that entity's own Start).
    bool installLogicScopesForActiveScene();
    bool installLogicScopeForEntity(EntityId entityId);
    bool installScriptScopesForActiveScene();

    // ADR-0039 §7/§8: the prepare/start split. prepareActiveSceneGameplay()
    // installs the active scene's Logic and Script scopes and requires
    // Empty; on partial failure it tears down whatever it installed and
    // leaves activationState_ at Empty (§9). startPreparedActiveSceneGameplay()
    // requires Prepared, dispatches Logic then Script On Start exactly once,
    // and moves to Started. Both reject (log + return false) outside their
    // required state rather than silently no-op-ing - see
    // reportActivationContractViolation().
    bool prepareActiveSceneGameplay();
    bool startPreparedActiveSceneGameplay();

    // Resets the activation state machine to Empty without touching scope
    // data directly - prepareActiveSceneGameplay() itself discards/reinstalls
    // scopes as its own first step (§7 "discard any previous scopes"), so
    // this only needs to clear the FSM guard. Call at every reset boundary
    // in §8 that does not already go through loadProject() (which starts
    // from a freshly-constructed, already-Empty session) or shutdown (which
    // resets via shutdownLogicModules()): scene transitions/restarts and any
    // future editor project-replace path that re-activates a scene outside
    // loadProject().
    void resetGameplayActivationState() {
        activationState_ = GameplayActivationState::Empty;
    }

    // RU-02d: dispatches one host-built input frame to Logic and Script
    // through the same immutable snapshot, then flushes queued destroys and
    // (if not dialog-blocked) GameAPI's own input handlers - exactly the
    // responsibilities the plan assigns to this method (RU02_GAMEPLAY_
    // SESSION_REFACTOR.md, RU-02d "Sessione"). No Input::poll() call and no
    // Raylib key lookup happen in here - the host already resolved
    // pressed/released/held into `input` before calling this.
    void dispatchInput(const GameplayInputFrame& input);

    void tickFixedStep(float dt);

    // RU-02g (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo): resolves
    // gameplay-visible entity render data (transform/sprite/visibility/
    // animator frame/text/gauge) into `snapshot.renderables`, already sorted
    // in final draw order, and stamps `snapshot.elapsedTime` - using
    // session-owned entityGateway_/spriteAnimator_/variableManager_/
    // sceneManager_/timeManager_ so the render pass stops querying those live
    // during draw. `snapshot` must already carry the scene/presentation
    // fields from frame_coordinator_build_frame() (Renderer/
    // EditorViewportService stay host-owned per T-02 in the debt register) -
    // this only adds to it, it does not rebuild the scene-level truth.
    // const: reads sceneManager_/entityGateway_/spriteAnimator_/variableManager_/
    // timeManager_ but mutates none of them - lets a const wrapper (editor
    // Play facade, RU-03) call this from a const accessor without needing a
    // mutable member.
    SceneFrameSnapshot buildFrameSnapshot(SceneFrameSnapshot snapshot) const;

    // RU-02e-3: ScriptRuntime is reconstructed at project-load and
    // scene-lifecycle time (installScriptScopesForActiveScene, above),
    // unlike every other module here, which is boot-time-stable - the
    // session now owns it directly instead of borrowing an Application-built
    // instance. Discards any previous instance, builds a fresh one bound to
    // the session's own logicHost_, and returns it so the caller can install
    // scopes against it. Kept public (unlike the removed logicHost()/
    // logicRuntime()/gameAPI()/luaHost() accessors): it returns a plain
    // reference the caller installs *onto*, not a handle for arbitrary
    // control-plane calls, and installScriptScopesForActiveScene() (above)
    // already uses it internally for the one production call site - this
    // stays public only for the characterization test's finer-grained setup.
    Scripts::ScriptRuntime& resetScriptRuntime();
    // Install failure path: destroys the just-built instance without
    // replacing it (mirrors the old mod_->scriptRuntime.reset() +
    // setScriptRuntime(nullptr) pair on that path). Declared here but defined
    // in gameplay_session.cpp (not inline): scriptRuntime_.reset() needs
    // Scripts::ScriptRuntime complete, and script-runtime.h is no longer
    // pulled in transitively by every includer of this header now that D-20
    // removed ScriptRuntime's Application::Modules alias (app_modules.h no
    // longer needs to include it either).
    void clearScriptRuntime();

    // RU-03 (editor repo): tickFixedStep() only ever prints Script diagnostics
    // to std::cerr (D-21 in the debt register, still open) - the editor's
    // Play facade needs to surface them as console messages the same way
    // Application::updateRuntime used to for the old PlaySession, so this
    // exposes the drain PlaySession's replacement needs without waiting on
    // D-21's full scope (a host-agnostic diagnostics channel for every
    // subsystem). Thin forward only, same style as resetScriptRuntime().
    std::vector<Scripts::ScriptRuntimeDiagnostic> drainScriptDiagnostics();
    /**
     * ADR-0028: Logic Board runtime diagnostics (including rate-limited
     * non-finite expression failures). Same RU-03 host-drain contract as
     * drainScriptDiagnostics().
     */
    std::vector<std::string> drainLogicDiagnostics();

    // Application::physicsMode_ can change after construction too
    // (applyRuntimeSettings, project-load time) - kept in sync the same way.
    void setPhysicsMode(PhysicsMode mode) { physicsMode_ = mode; }

    // Collision-pair tracking resets whenever Script scopes are reinstalled
    // for a scene (old pairs no longer meaningful against the new scopes) -
    // mirrors the pre-RU-02c mod_->activeGameplayCollisionPairs.clear() call
    // in installScriptScopesForActiveScene.
    void resetCollisionTracking() { activeGameplayCollisionPairs_.clear(); }

    // RU-02e-1: tears down the simulation graph in the exact relative order
    // Application::shutdownModules() used (world -> gateway -> lifecycle ->
    // mutation -> scene manager). Physics shuts down separately via
    // shutdownPhysics() since the original order interleaves it after
    // Application-owned audio/input, not contiguously with the rest of the
    // graph.
    void shutdownGraph();
    void shutdownPhysics();

    // RU-02e-2: mirrors the same "don't collapse into one call" lesson -
    // Application-owned dialogManager shutdown sits in between the original
    // logicRuntime/logicHost and luaHost/gameAPI shutdown lines, so two
    // granular methods preserve that exact interleaving. shutdownScriptRuntime()
    // (RU-02e-3) is its own call too, since it used to sit between them.
    void shutdownLogicModules();
    void shutdownScriptRuntime();
    void shutdownScriptingModules();

    // RU-02f: matches the original contiguous tail of Application::
    // shutdownModules() (gameStateManager -> saveLoadManager -> cameraManager
    // -> spriteAnimator -> tweenManager -> variableManager -> timeManager ->
    // eventBus) - no host module was ever interleaved between these, unlike
    // physics/scriptRuntime/dialogManager elsewhere, so one method suffices.
    void shutdownUtilities();

private:
    void rebuildActiveSceneTilemapDraws();
    void clearResolvedTilemapDraws();
    void dispatchGameplayCollisionTransitions();

    // ADR-0039 §9: failure-path teardown for prepareActiveSceneGameplay() -
    // cancels any Logic scopes already installed and discards the Script
    // runtime, mirroring installLogicScopesForActiveScene()'s own reset step
    // so a partial failure never leaves scopes bound to a scene that never
    // activates. Leaves activationState_ untouched; callers set it.
    void clearPreparedGameplayScopes();
    // ADR-0039 §8: logs a prepare()/start() call made from the wrong
    // activation state, with enough context (state, operation) to diagnose
    // the misuse without a debugger. This is a programming-contract
    // violation on the host/caller side, not a runtime/user-content error -
    // see ADR-0039 §18 for that distinction (Lua/Logic errors inside an
    // author's own On Start stay non-fatal diagnostics, unrelated to this).
    void reportActivationContractViolation(const char* reason) const;

    std::unique_ptr<Modules::EventBus> eventBus_;
    std::unique_ptr<Modules::TimeManager> timeManager_;
    std::unique_ptr<Modules::VariableManager> variableManager_;
    std::unique_ptr<Modules::TweenManager> tweenManager_;
    std::unique_ptr<Modules::SpriteAnimator> spriteAnimator_;
    std::unique_ptr<Modules::CameraManager> cameraManager_;
    std::unique_ptr<Modules::SaveLoadManager> saveLoadManager_;
    std::unique_ptr<Modules::GameStateManager> gameStateManager_;

    std::unique_ptr<Modules::Physics> physics_;
    std::unique_ptr<Modules::SceneManager> sceneManager_;
    std::unique_ptr<Modules::SceneMutationService> sceneMutation_;
    std::unique_ptr<Modules::RuntimeEntityGateway> entityGateway_;
    std::unique_ptr<Modules::SceneLifecycleService> sceneLifecycle_;
    std::unique_ptr<World> world_;

    std::unique_ptr<RuntimeLogicHostAdapter> logicHost_;
    std::unique_ptr<Logic::LogicRuntime> logicRuntime_;
    std::unique_ptr<Modules::GameAPI> gameAPI_;
    std::unique_ptr<Modules::LuaHost> luaHost_;
    std::unique_ptr<Scripts::ScriptRuntime> scriptRuntime_;

    // D-20/D-09: Logic/Script scope bookkeeping, moved in from
    // Application::Modules - simulation state that lives across ticks, not
    // host bookkeeping (same rule that already moved
    // activeGameplayCollisionPairs_ here in RU-02c).
    std::unordered_map<EntityId, Logic::ScopeToken> logicScopes_;
    std::unordered_set<ObjectTypeId> logicObjectTypes_;
    std::unordered_map<AssetId, Scripts::ScriptProgram> scriptPrograms_;
    std::unordered_map<ObjectTypeId, std::vector<ScriptAttachmentDef>> scriptAttachments_;
    // Derived active-scene presentation only. It is rebuilt at scene/world
    // boundaries and is never persistent or mutable through the public API.
    std::unordered_map<EntityId, AppRender::ResolvedTilemapDraw> resolvedTilemapDraws_;

    IGameplayAudioService* audioPort_ = nullptr;
    IGameplayDialogGate* dialogPort_ = nullptr;
    IRuntimeProfilerSink* profilerPort_ = nullptr;

    PhysicsMode physicsMode_ = PhysicsMode::Auto;
    // ADR-0039 §7/§8: Empty at construction, reset to Empty by
    // shutdownLogicModules() - matches the reset-boundary list needing no
    // other constructor/destructor-adjacent wiring.
    GameplayActivationState activationState_ = GameplayActivationState::Empty;
    std::set<std::pair<EntityId, EntityId>> activeGameplayCollisionPairs_;
    // RU-03: accumulated by tickFixedStep(), drained by drainScriptDiagnostics().
    std::vector<Scripts::ScriptRuntimeDiagnostic> pendingScriptDiagnostics_;
    // ADR-0028: Logic Runtime diagnostics (expression_once, dispatch limits…).
    std::vector<std::string> pendingLogicDiagnostics_;
};

static_assert(!std::is_abstract_v<RuntimeLogicHostAdapter>,
              "RuntimeLogicHostAdapter must implement every IGameplayRuntimeHost method");

} // namespace ArtCade
