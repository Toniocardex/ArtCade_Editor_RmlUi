#include "gameplay_session.h"
#include "gameplay_session_seed.h"

#include "../../core/engine-context.h"
#include "../../modules/audio/include/audio.h"
#include "../../modules/camera-manager/include/camera-manager.h"
#include "../../modules/event-bus/include/event-bus.h"
#include "../../modules/game-api/include/game-api.h"
#include "../../modules/game-state/include/game-state-manager.h"
#include "../../modules/input/include/input.h"
#include "../../modules/logic-core/include/logic-core.h"
#include "../../modules/logic-runtime/include/logic-runtime.h"
#include "../../modules/lua-runtime/include/lua-host.h"
#include "../../modules/physics/include/physics.h"
#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/save-load/include/save-load-manager.h"
#include "../../modules/scene-system/include/scene-lifecycle-service.h"
#include "../../modules/scene-system/include/scene-manager.h"
#include "../../modules/scene-system/include/scene-mutation-service.h"
#include "../../modules/script-runtime/include/script-runtime.h"
#include "../../modules/sprite-animator/include/sprite-animator.h"
#include "../../modules/time/include/time-manager.h"
#include "../../modules/tween-manager/include/tween-manager.h"
#include "../../modules/variable-manager/include/variable-manager.h"
#include "../../world/include/world.h"
#include "../render/scene_frame_snapshot.h"
#include "../render/text_value_formatter.h"
#include "../../core/text-component-format.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <unordered_map>

namespace ArtCade {

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

// RU-02e-2: moved verbatim from app_modules.h (previously private to the
// `game` executable target's Application::Modules) - method bodies unchanged,
// only the declaring header moved so GameplaySession can own an instance.
RuntimeLogicHostAdapter::RuntimeLogicHostAdapter(
    Modules::RuntimeEntityGateway& gateway, Modules::Audio& audio,
    Modules::CameraManager& cameraManager)
    : gateway_(gateway), audio_(audio), cameraManager_(cameraManager) {}

bool RuntimeLogicHostAdapter::setVisible(EntityId owner, bool value) {
    return gateway_.setRuntimeVisible(owner, value);
}
bool RuntimeLogicHostAdapter::isVisible(EntityId owner) {
    return gateway_.visibleInGame(owner);
}
bool RuntimeLogicHostAdapter::setSpriteFlipX(EntityId owner, bool flipX) {
    SpriteComponent sprite{};
    if (!gateway_.getSprite(owner, sprite)) return false;
    sprite.flipX = flipX;
    return gateway_.setSprite(owner, sprite);
}
bool RuntimeLogicHostAdapter::setPosition(EntityId owner, Vec2 value) {
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.position = value;
    return gateway_.setTransform(owner, transform);
}
std::optional<Vec2> RuntimeLogicHostAdapter::getPosition(EntityId owner) const {
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return std::nullopt;
    return transform.position;
}
std::optional<Vec2> RuntimeLogicHostAdapter::getSceneWorldSize() const {
    const SceneDef* scene = gateway_.activeScene();
    if (!scene) return std::nullopt;
    return scene->worldSize;
}
bool RuntimeLogicHostAdapter::translate(EntityId owner, Vec2 delta) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return false;
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.position.x += delta.x;
    transform.position.y += delta.y;
    return gateway_.setTransform(owner, transform);
}
bool RuntimeLogicHostAdapter::setRotation(EntityId owner, float radians) {
    if (!std::isfinite(radians)) return false;
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.rotation = radians;
    return gateway_.setTransform(owner, transform);
}
bool RuntimeLogicHostAdapter::rotateBy(EntityId owner, float deltaRadians) {
    if (!std::isfinite(deltaRadians)) return false;
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.rotation += deltaRadians;
    return gateway_.setTransform(owner, transform);
}
bool RuntimeLogicHostAdapter::setScale(EntityId owner, Vec2 scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y)
        || scale.x <= 0.f || scale.y <= 0.f) {
        return false;
    }
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.scale = scale;
    return gateway_.setTransform(owner, transform);
}
bool RuntimeLogicHostAdapter::isGrounded(EntityId owner) {
    return world_ && world_->isPlatformerGrounded(owner);
}
bool RuntimeLogicHostAdapter::isFalling(EntityId owner) {
    return platformerState(owner) == PlatformerState::Falling;
}
PlatformerState RuntimeLogicHostAdapter::platformerState(EntityId owner) {
    return world_ ? world_->platformerState(owner) : PlatformerState::Stopped;
}
bool RuntimeLogicHostAdapter::isPlatformerMoving(EntityId owner) {
    return platformerState(owner) == PlatformerState::Moving;
}
bool RuntimeLogicHostAdapter::requestPlatformerMove(EntityId owner, float axis) {
    PlatformerControllerComponent platformer{};
    if (!world_ || !std::isfinite(axis)
        || !gateway_.getPlatformerController(owner, platformer)) return false;
    world_->setMovementIntent(owner, axis, 0.f);
    return true;
}
bool RuntimeLogicHostAdapter::requestTopDownMove(EntityId owner, Vec2 direction) {
    TopDownControllerComponent topDown{};
    if (!world_ || !std::isfinite(direction.x) || !std::isfinite(direction.y)
        || direction.x < -1.f || direction.x > 1.f
        || direction.y < -1.f || direction.y > 1.f
        || !gateway_.getTopDownController(owner, topDown)) return false;
    world_->addTopDownMovementContribution(owner, direction);
    return true;
}
bool RuntimeLogicHostAdapter::requestPlatformerJump(EntityId owner) {
    PlatformerControllerComponent platformer{};
    if (!world_ || !gateway_.getPlatformerController(owner, platformer)) return false;
    world_->requestJump(owner);
    return true;
}
bool RuntimeLogicHostAdapter::isObjectType(EntityId entity, const ObjectTypeId& expected) {
    return world_ && world_->isObjectType(entity, expected);
}
bool RuntimeLogicHostAdapter::requestDestroy(EntityId owner) {
    return world_ && world_->requestDestroy(owner);
}
bool RuntimeLogicHostAdapter::playAnimationClip(
    EntityId owner, const AssetId& animationAssetId, const std::string& clipId) {
    return world_ && world_->playAnimationClip(owner, animationAssetId, clipId);
}
bool RuntimeLogicHostAdapter::stopAnimation(EntityId owner) {
    return world_ && world_->stopAnimation(owner);
}
bool RuntimeLogicHostAdapter::setAnimationPlaybackSpeed(EntityId owner, float speed) {
    return world_ && world_->setAnimationPlaybackSpeed(owner, speed);
}
bool RuntimeLogicHostAdapter::playSound(
    EntityId owner, const AssetId& audioAssetId, float volume) {
    return world_ && world_->isActiveEntity(owner)
        && audio_.playResolvedAsset(audioAssetId, volume);
}
bool RuntimeLogicHostAdapter::setStateNumber(const GameVariableId& id, double value) {
    if (!variables_) return false;
    return variables_->setGlobal(id, value).accepted();
}
bool RuntimeLogicHostAdapter::addStateNumber(const GameVariableId& id, double delta) {
    if (!variables_) return false;
    return variables_->addNumber(id, delta).accepted();
}
bool RuntimeLogicHostAdapter::toggleStateBoolean(const GameVariableId& id) {
    if (!variables_) return false;
    return variables_->toggleBoolean(id).accepted();
}
std::optional<double> RuntimeLogicHostAdapter::getStateNumber(const GameVariableId& id) const {
    if (!variables_) return std::nullopt;
    return variables_->tryGetNumber(id);
}
std::optional<bool> RuntimeLogicHostAdapter::getStateBoolean(const GameVariableId& id) const {
    if (!variables_) return std::nullopt;
    return variables_->tryGetBool(id);
}
std::optional<std::string> RuntimeLogicHostAdapter::getStateString(const GameVariableId& id) const {
    if (!variables_) return std::nullopt;
    return variables_->tryGetString(id);
}
std::optional<double> RuntimeLogicHostAdapter::getLocalNumber(
    EntityId owner, const GameVariableId& id) const {
    if (!variables_ || !variables_->entityExists(owner, id)) return std::nullopt;
    const Modules::VariableManager::Value value = variables_->getEntity(owner, id);
    if (!std::holds_alternative<double>(value)) return std::nullopt;
    return std::get<double>(value);
}
bool RuntimeLogicHostAdapter::setVelocity(EntityId owner, Vec2 velocity) {
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return false;
    Transform transform{};
    if (!gateway_.getTransform(owner, transform)) return false;
    transform.velocity = velocity;
    if (!gateway_.setTransform(owner, transform)) return false;
    const uint32_t handle = gateway_.physicsHandle(owner);
    if (handle != 0 && physics_) physics_->setLinearVelocity(handle, velocity);
    return true;
}
bool RuntimeLogicHostAdapter::isKeyDown(LogicKey key) {
    return input_ && input_->isKeyDown(Logic::logicInputCode(key));
}
EntityId RuntimeLogicHostAdapter::spawnObjectType(
    EntityId owner, const ObjectTypeId& objectTypeId, float x, float y) {
    if (!world_ || !world_->isActiveEntity(owner) || objectTypeId.empty())
        return INVALID_ENTITY;
    if (!std::isfinite(x) || !std::isfinite(y)) return INVALID_ENTITY;
    const EntityId spawned = gateway_.spawnFromClass(objectTypeId, x, y);
    if (spawned == INVALID_ENTITY) return INVALID_ENTITY;
    // Installer must succeed when present; otherwise destroy the orphan and fail.
    if (spawnInstaller_ && !spawnInstaller_(spawned)) {
        gateway_.destroy(spawned);
        return INVALID_ENTITY;
    }
    return spawned;
}

bool RuntimeLogicHostAdapter::requestSceneRestart() {
    pendingSceneRequest_ =
        PendingSceneRequest{PendingSceneRequest::Kind::Restart, {}};
    return true;
}

bool RuntimeLogicHostAdapter::requestSceneGoTo(const SceneId& sceneId) {
    if (sceneId.empty()) return false;
    pendingSceneRequest_ =
        PendingSceneRequest{PendingSceneRequest::Kind::Load, sceneId};
    return true;
}

bool RuntimeLogicHostAdapter::cameraShake(float intensity, float durationSeconds) {
    if (!std::isfinite(intensity) || !std::isfinite(durationSeconds)
        || intensity < 0.f || intensity > 1.f || durationSeconds <= 0.f) {
        return false;
    }
    if (intensity == 0.f) {
        return true;
    }
    cameraManager_.addTrauma(intensity, durationSeconds);
    return true;
}

std::optional<RuntimeLogicHostAdapter::PendingSceneRequest>
RuntimeLogicHostAdapter::takePendingSceneRequest() {
    std::optional<PendingSceneRequest> request = std::move(pendingSceneRequest_);
    pendingSceneRequest_.reset();
    return request;
}

GameplaySession::GameplaySession() = default;

// unique_ptr members need complete types at destruction; every module they
// point to is fully defined above by the time this runs.
GameplaySession::~GameplaySession() = default;

// RU-02f: lines moved from Application::initUtilities() (app_bootstrap.cpp)
// in the same order, same boot-step names, same internal wiring
// (GameStateManager::setEventBus). Must run before initialize(), which needs
// variableManager_ already built (World's constructor takes it).
bool GameplaySession::initializeUtilities(const BootStepFn& bootStep) {
    eventBus_ = std::make_unique<Modules::EventBus>();
    timeManager_ = std::make_unique<Modules::TimeManager>();
    variableManager_ = std::make_unique<Modules::VariableManager>();
    tweenManager_ = std::make_unique<Modules::TweenManager>();
    spriteAnimator_ = std::make_unique<Modules::SpriteAnimator>();
    cameraManager_ = std::make_unique<Modules::CameraManager>();
    saveLoadManager_ = std::make_unique<Modules::SaveLoadManager>();

    if (!bootStep("event_bus", eventBus_->init())) return false;
    if (!bootStep("time_manager", timeManager_->init())) return false;
    if (!bootStep("variable_manager", variableManager_->init())) return false;
    if (!bootStep("tween_manager", tweenManager_->init())) return false;
    if (!bootStep("sprite_animator", spriteAnimator_->init())) return false;
    if (!bootStep("camera_manager", cameraManager_->init())) return false;
    if (!bootStep("save_load_manager", saveLoadManager_->init())) return false;

    gameStateManager_ = std::make_unique<Modules::GameStateManager>();
    gameStateManager_->setEventBus(eventBus_.get());
    if (!bootStep("game_state_manager", gameStateManager_->init())) return false;

    return true;
}

// RU-02e-1/D-20: lines moved from Application::initSubsystems()
// (app_bootstrap.cpp) in the same relative order, same boot-step names, same
// wiring - the two SceneLifecycleService handlers that used to capture
// Application state (`gw = mod_->entityGateway.get()`) capture `this`
// (GameplaySession), per the plan's callback rule (RU02_GAMEPLAY_SESSION_
// REFACTOR.md RU-02e: "Callback: non devono più catturare Application per
// modificare oggetti posseduti dalla sessione"). D-20 moves World's
// entity-destroyed handler and setSpriteAnimator/setRenderer wiring in here
// too (previously done by Application right after fetching world() - now
// removed), and populates ctx.physics/sceneManager/entityGateway/world
// directly (previously Application copied these from the now-removed
// physics()/sceneManager()/entityGateway()/world() accessors just to do
// this). `onSceneTransition` still reaches the host, since it mutates
// Application::pendingSceneInvalidations_; `presentationRenderer` is the one
// remaining T-02 debt (concrete Renderer* handed to World::setRenderer) and
// may be null (World tolerates it - e.g. headless tests).
bool GameplaySession::initialize(PhysicsMode physicsMode,
                                  EngineContext& ctx,
                                  Modules::Renderer* presentationRenderer,
                                  const BootStepFn& bootStep,
                                  SceneTransitionHandlerFn onSceneTransition) {
    physicsMode_ = physicsMode;

    physics_ = std::make_unique<Modules::Physics>();
    if (!bootStep("physics", physics_->init())) return false;

    sceneManager_ = std::make_unique<Modules::SceneManager>();
    if (!bootStep("scene_manager", sceneManager_->init())) return false;

    sceneMutation_ = std::make_unique<Modules::SceneMutationService>(*sceneManager_);

    entityGateway_ = std::make_unique<Modules::RuntimeEntityGateway>(*sceneManager_);
    if (!bootStep("entity_gateway", entityGateway_->init())) return false;

    sceneLifecycle_ = std::make_unique<Modules::SceneLifecycleService>(
        *sceneManager_,
        *sceneMutation_,
        [this]() { entityGateway_->syncSceneActivation(); });
    sceneLifecycle_->set_transition_handler(std::move(onSceneTransition));
    entityGateway_->set_scene_lifecycle_service(sceneLifecycle_.get());

    world_ = std::make_unique<World>(*entityGateway_, *physics_, *variableManager_);
    entityGateway_->setPhysics(physics_.get());

    sceneLifecycle_->set_gameplay_reset_handler([this]() {
        world_->onSceneActivated();
    });
    sceneLifecycle_->set_restore_handler([this](const SceneId& sceneId) {
        return entityGateway_->restoreSceneFromAuthoring(sceneId);
    });
    world_->setSceneLifecycleService(sceneLifecycle_.get());

    // D-20/D-08: moved from Application::initSubsystems() - the destroy
    // handler used to capture Application (`this`) to reach
    // mod_->logicScopes/logicRuntime/scriptRuntime; now it reads the
    // session's own logicScopes_/logicRuntime_/scriptRuntime_ directly.
    // logicRuntime_/scriptRuntime_ don't exist yet at this point in boot -
    // safe, since destroy only ever fires well after boot completes (same
    // reasoning already established for RU-02e-2's spawn installer).
    world_->setSpriteAnimator(spriteAnimator_.get());
    // Same wiring app_bootstrap applies for game.exe/WASM: spawn-clip Auto Play
    // and sheet binding run on the gateway, which keeps its own animator
    // pointer (World's copy is only for playAnimationClip / stop helpers).
    entityGateway_->setSpriteAnimator(spriteAnimator_.get());
    world_->setEntityDestroyedHandler([this](EntityId id) {
        const auto it = logicScopes_.find(id);
        if (it != logicScopes_.end()) {
            if (logicRuntime_) logicRuntime_->cancelScope(it->second);
            logicScopes_.erase(it);
        }
        if (scriptRuntime_) scriptRuntime_->cancelOwner(id);
    });
    world_->setRenderer(presentationRenderer);

    ctx.physics = physics_.get();
    ctx.sceneManager = sceneManager_.get();
    ctx.entityGateway = entityGateway_.get();
    ctx.world = world_.get();

    return true;
}

Vec2 GameplaySession::cameraCenter() const {
    return world_ ? world_->cameraCenter() : Vec2{};
}

void GameplaySession::updateCameraShake(float frameDt) {
    if (!cameraManager_ || !std::isfinite(frameDt) || frameDt <= 0.f) return;
    if (cameraManager_->trauma() <= 0.f) return;
    cameraManager_->refreshShakeOffset(frameDt);
    cameraManager_->decayTrauma(frameDt);
}

Vec2 GameplaySession::presentationCameraCenter() const {
    Vec2 center = cameraCenter();
    if (!cameraManager_) return center;
    const Vec2 shake = cameraManager_->shakeOffset();
    return {center.x + shake.x, center.y + shake.y};
}

// RU-02e-2/D-20: lines moved from Application::initSubsystems()
// (app_bootstrap.cpp) in the same relative order, same boot-step names, same
// wiring. `ctx` is Application's own EngineContext, already populated with
// everything the simulation graph above wires (renderer/physics/input/audio/
// sceneManager/entityGateway/assetLoader/world/dialogManager/...) by the
// time this runs - this method only ever writes ctx.gameAPI/ctx.luaHost,
// exactly like Application used to right after constructing each. D-20: the
// spawn installer now wires to installLogicScopeForEntity() (this class,
// below) directly - Application no longer needs to pass one in, since that
// method moved in from Application too.
bool GameplaySession::initializeGameplayModules(
    EngineContext& ctx,
    Modules::Audio& audio,
    Modules::Input& input,
    const BootStepFn& bootStep) {
    logicHost_ = std::make_unique<RuntimeLogicHostAdapter>(
        *entityGateway_, audio, *cameraManager_);
    logicRuntime_ = std::make_unique<Logic::LogicRuntime>(
        *logicHost_, GameplaySessionSeed::make());

    logicHost_->setWorld(world_.get());
    logicHost_->setVariableManager(variableManager_.get());
    logicHost_->setInput(&input);
    logicHost_->setPhysics(physics_.get());
    logicHost_->setSpawnInstaller([this](EntityId id) { return installLogicScopeForEntity(id); });

    gameAPI_ = std::make_unique<Modules::GameAPI>(ctx);
    if (!bootStep("game_api", gameAPI_->init())) return false;
    ctx.gameAPI = gameAPI_.get();

    luaHost_ = std::make_unique<Modules::LuaHost>();
    luaHost_->registerBindings([this](sol::state& lua) {
        gameAPI_->registerAll(lua);
    });
    if (!bootStep("lua_host", luaHost_->init())) return false;
    ctx.luaHost = luaHost_.get();

    return true;
}

// RU-02e-3: replaces the old Application-side
// `mod_->scriptRuntime = std::make_unique<Scripts::ScriptRuntime>(*mod_->logicHost)`
// - same construction, now against the session's own logicHost_ instead of a
// borrowed pointer, and no separate setScriptRuntime() sync call needed since
// this class already owns the pointer it dispatches through.
Scripts::ScriptRuntime& GameplaySession::resetScriptRuntime() {
    scriptRuntime_ = std::make_unique<Scripts::ScriptRuntime>(*logicHost_);
    return *scriptRuntime_;
}

void GameplaySession::clearScriptRuntime() { scriptRuntime_.reset(); }

std::vector<Scripts::ScriptRuntimeDiagnostic> GameplaySession::drainScriptDiagnostics() {
    std::vector<Scripts::ScriptRuntimeDiagnostic> result = std::move(pendingScriptDiagnostics_);
    pendingScriptDiagnostics_.clear();
    return result;
}

std::vector<std::string> GameplaySession::drainLogicDiagnostics() {
    std::vector<std::string> result = std::move(pendingLogicDiagnostics_);
    pendingLogicDiagnostics_.clear();
    return result;
}

// D-20: replaces the removed `Modules::Physics& physics()` accessor.
void GameplaySession::setGravity(Vec2 gravity) { physics_->setGravity(gravity); }

// D-20: replace the removed `Modules::SceneMutationService& sceneMutation()`
// accessor - thin forwards, no new behavior.
uint64_t GameplaySession::bumpSceneRevision() { return sceneMutation_->bump_revision(); }
uint64_t GameplaySession::sceneRevision() const { return sceneMutation_->revision(); }
void GameplaySession::beginSceneMutationBatch() { sceneMutation_->begin_batch(); }
Modules::SceneMutationResult GameplaySession::commitSceneMutationBatch() {
    return sceneMutation_->commit_batch();
}
Modules::SceneMutationResult GameplaySession::applySceneMutation(
    const SceneId& sceneId, const Modules::ScenePatch& patch) {
    return sceneMutation_->apply(sceneId, patch);
}
bool GameplaySession::sceneMutationBatchOpen() const { return sceneMutation_->batch_open(); }

// D-20: replace the removed `World& world()` accessor's mutating call sites -
// thin forwards, no new behavior.
void GameplaySession::loadWorldProject(const ProjectDoc& doc) { world_->init(doc); }
void GameplaySession::syncWorldAfterEditorProject(
    const std::vector<TilePaletteEntry>& tilePalette) {
    world_->syncAfterEditorProject(tilePalette);
}
void GameplaySession::restoreWorldDesignState(
    const std::vector<TilePaletteEntry>& tilePalette) {
    world_->restoreDesignState(tilePalette);
}
void GameplaySession::rebuildWorldCollision() { world_->rebuildCollisionWorld(); }

// D-20: replaces the removed `Logic::LogicRuntime& logicRuntime()` accessor -
// also rebuilds logicObjectTypes_ from the compiled programs, mirroring what
// Application::loadProject used to do right after loadPrograms() succeeded.
bool GameplaySession::loadLogicPrograms(
    const std::vector<Logic::LogicProgram>& programs, std::string* error) {
    if (!logicRuntime_ || !logicRuntime_->loadPrograms(programs, error)) return false;
    logicObjectTypes_.clear();
    for (const Logic::LogicProgram& program : programs) {
        logicObjectTypes_.insert(program.objectTypeId);
    }
    return true;
}

// D-20: replace the removed `Modules::LuaHost& luaHost()` accessor.
void GameplaySession::loadLuaSource(const std::string& source) {
    if (luaHost_) luaHost_->loadLuaSource(source);
}
bool GameplaySession::loadLuaBytecode(const uint8_t* data, size_t size, std::string* error) {
    if (!luaHost_ || !luaHost_->loadBytecodeBuffer(data, size)) {
        if (error) *error = luaHost_ ? luaHost_->lastError() : std::string();
        return false;
    }
    return true;
}

// D-20/D-08/D-09: moved verbatim from Application::installLogicScopesForActiveScene/
// installLogicScopeForEntity/installScriptScopesForActiveScene (app_project_
// lifecycle.cpp) - only mod_->X became X_ for every member this class now
// owns (logicRuntime_/entityGateway_/scriptRuntime_/logicScopes_/
// logicObjectTypes_/scriptPrograms_/scriptAttachments_), same as every other
// method moved in during RU-02e/f.
void GameplaySession::setScriptCatalog(
    std::unordered_map<AssetId, Scripts::ScriptProgram> programs,
    std::unordered_map<ObjectTypeId, std::vector<ScriptAttachmentDef>> attachments) {
    scriptPrograms_ = std::move(programs);
    scriptAttachments_ = std::move(attachments);
}

bool GameplaySession::installLogicScopesForActiveScene() {
    if (!logicRuntime_ || !entityGateway_) return false;
    for (const auto& [entityId, token] : logicScopes_) {
        (void)entityId;
        logicRuntime_->cancelScope(token);
    }
    logicScopes_.clear();
    std::string error;
    for (EntityId id : entityGateway_->activeSceneIds()) {
        const ObjectTypeId typeId = entityGateway_->className(id);
        if (logicObjectTypes_.find(typeId) == logicObjectTypes_.end()) continue;
        const auto token = logicRuntime_->install(typeId, id, &error);
        if (!token) {
            std::cerr << "[App] Could not install Logic Board scope: " << error << "\n";
            return false;
        }
        logicScopes_.emplace(id, *token);
    }
    // ADR-0039 §7: On Start dispatch moved to startPreparedActiveSceneGameplay() -
    // this installs scopes only, so a caller can prepare a scene without any
    // gameplay callback firing yet.
    return true;
}

bool GameplaySession::installLogicScopeForEntity(EntityId entityId) {
    if (!logicRuntime_ || !entityGateway_ || entityId == INVALID_ENTITY) return false;
    if (logicScopes_.count(entityId) != 0) return true;
    const ObjectTypeId typeId = entityGateway_->className(entityId);
    if (logicObjectTypes_.find(typeId) == logicObjectTypes_.end()) return true;
    std::string error;
    const auto token = logicRuntime_->install(typeId, entityId, &error);
    if (!token) {
        std::cerr << "[App] Could not install Logic Board scope for spawn: " << error << "\n";
        return false;
    }
    logicScopes_.emplace(entityId, *token);
    // Owner-scoped Start only - never re-fire On Start for the whole scene.
    logicRuntime_->dispatchStartForOwner(entityId);
    return true;
}

bool GameplaySession::installScriptScopesForActiveScene() {
    if (!entityGateway_ || !logicHost_) return false;
    resetCollisionTracking();
    resetScriptRuntime();
    std::string error;
    for (EntityId id : entityGateway_->activeSceneIds()) {
        const ObjectTypeId typeId = entityGateway_->className(id);
        const auto attachments = scriptAttachments_.find(typeId);
        if (attachments == scriptAttachments_.end()) continue;
        for (const ScriptAttachmentDef& attachment : attachments->second) {
            if (!attachment.enabled) continue;
            const auto program = scriptPrograms_.find(attachment.scriptAssetId);
            if (program == scriptPrograms_.end()
                || !scriptRuntime_->install(program->second, id, attachment.id, &error)) {
                std::cerr << "[App] Could not install Script scope: " << error << "\n";
                clearScriptRuntime();
                return false;
            }
        }
    }
    // ADR-0039 §7: On Start dispatch moved to startPreparedActiveSceneGameplay().
    return true;
}

// ADR-0039 §7/§8/§9.
bool GameplaySession::prepareActiveSceneGameplay() {
    if (activationState_ != GameplayActivationState::Empty) {
        reportActivationContractViolation("prepare requires Empty state");
        return false;
    }

    if (!installLogicScopesForActiveScene()) {
        clearPreparedGameplayScopes();
        return false;
    }

    if (!installScriptScopesForActiveScene()) {
        clearPreparedGameplayScopes();
        return false;
    }

    activationState_ = GameplayActivationState::Prepared;
    return true;
}

bool GameplaySession::startPreparedActiveSceneGameplay() {
    if (activationState_ != GameplayActivationState::Prepared) {
        reportActivationContractViolation("start requires Prepared state");
        return false;
    }

    if (logicRuntime_) {
        logicRuntime_->beginFrame();
        logicRuntime_->dispatchStart();
    }
    if (scriptRuntime_) {
        scriptRuntime_->dispatchStart();
    }

    activationState_ = GameplayActivationState::Started;
    return true;
}

void GameplaySession::clearPreparedGameplayScopes() {
    if (logicRuntime_) {
        for (const auto& [entityId, token] : logicScopes_) {
            (void)entityId;
            logicRuntime_->cancelScope(token);
        }
    }
    logicScopes_.clear();
    clearScriptRuntime();
}

void GameplaySession::reportActivationContractViolation(const char* reason) const {
    std::cerr << "[GameplaySession] activation contract violation: " << reason
              << " (current state="
              << static_cast<int>(activationState_) << ")\n";
}

void GameplaySession::shutdownGraph() {
    if (world_) { world_->shutdown(); world_.reset(); }
    if (entityGateway_) {
        entityGateway_->set_scene_lifecycle_service(nullptr);
        entityGateway_->shutdown();
        entityGateway_.reset();
    }
    if (sceneLifecycle_) {
        sceneLifecycle_->cancel_transition();
        sceneLifecycle_.reset();
    }
    sceneMutation_.reset();
    if (sceneManager_) { sceneManager_->shutdown(); sceneManager_.reset(); }
}

void GameplaySession::shutdownPhysics() {
    if (physics_) { physics_->shutdown(); physics_.reset(); }
}

// RU-02e-2: matches Application::shutdownModules()'s original logicRuntime
// shutdown/reset immediately followed by logicHost.reset() (no explicit
// shutdown() call on logicHost - it never held resources beyond references).
void GameplaySession::shutdownLogicModules() {
    if (logicRuntime_) { logicRuntime_->shutdown(); logicRuntime_.reset(); }
    logicHost_.reset();
    // D-20: these used to be mod_->logicScopes.clear()/logicObjectTypes.clear()
    // right after Application::shutdownModules()'s shutdownLogicModules() call
    // - same relative position, now internal since the maps moved in too.
    logicScopes_.clear();
    logicObjectTypes_.clear();
    // ADR-0039 §8: application shutdown is one of the binding reset
    // boundaries for the activation state machine.
    activationState_ = GameplayActivationState::Empty;
}

// RU-02e-3: matches the original Application-side
// `if (mod_->scriptRuntime) { mod_->scriptRuntime->shutdown(); mod_->scriptRuntime.reset(); }`,
// which used to sit between logicHost.reset() and luaHost's shutdown.
void GameplaySession::shutdownScriptRuntime() {
    if (scriptRuntime_) { scriptRuntime_->shutdown(); scriptRuntime_.reset(); }
    // D-20: these used to be mod_->scriptPrograms.clear()/scriptAttachments.
    // clear() right after Application::shutdownModules()'s shutdownScriptRuntime()
    // call - same relative position, now internal since the maps moved in too.
    scriptPrograms_.clear();
    scriptAttachments_.clear();
}

// RU-02e-2: matches the original luaHost shutdown/reset immediately followed
// by gameAPI shutdown/reset.
void GameplaySession::shutdownScriptingModules() {
    if (luaHost_) { luaHost_->shutdown(); luaHost_.reset(); }
    if (gameAPI_) { gameAPI_->shutdown(); gameAPI_.reset(); }
}

// RU-02f: matches the original contiguous tail of Application::
// shutdownModules() - gameStateManager, saveLoadManager, cameraManager,
// spriteAnimator, tweenManager, variableManager, timeManager, eventBus, in
// that exact order, with no host module ever interleaved between them.
void GameplaySession::shutdownUtilities() {
    if (gameStateManager_) { gameStateManager_->shutdown(); gameStateManager_.reset(); }
    if (saveLoadManager_) { saveLoadManager_->shutdown(); saveLoadManager_.reset(); }
    if (cameraManager_) { cameraManager_->shutdown(); cameraManager_.reset(); }
    if (spriteAnimator_) { spriteAnimator_->shutdown(); spriteAnimator_.reset(); }
    if (tweenManager_) { tweenManager_->shutdown(); tweenManager_.reset(); }
    if (variableManager_) { variableManager_->shutdown(); variableManager_.reset(); }
    if (timeManager_) { timeManager_->shutdown(); timeManager_.reset(); }
    if (eventBus_) { eventBus_->shutdown(); eventBus_.reset(); }
}

// Moved from Application::loopIteration's input block (app_loop.cpp,
// pre-RU-02d). One structural change from the original, both sanctioned by
// the frame itself being category-shaped rather than per-key (RU-02d's own
// GameplayInputFrame): the original iterated Logic::supportedLogicKeys()
// once and handled pressed/released/held for each key together, in that
// interleaved order; here every pressed key dispatches before any released
// key, which dispatches before any held key. LogicRuntime::dispatch() and
// ScriptInputSnapshot both filter/normalize per (kind, key) independently of
// any other key, so this does not change what fires for a given key - only
// the relative order across *different* keys within the same frame, which
// nothing in this codebase currently depends on.
void GameplaySession::dispatchInput(const GameplayInputFrame& input) {
    // Frame-local movement: Logic/Script must re-assert Move Horizontal /
    // Top Down Move each input frame (typically While Key Held). Sticky
    // intents made platformer keep walking after key release.
    world_->clearFrameMovementIntents();
    if (logicRuntime_) logicRuntime_->beginFrame();
    Scripts::ScriptInputSnapshot scriptInput;
    for (LogicKey key : input.pressed) {
        if (logicRuntime_) logicRuntime_->dispatchKeyPressed(key);
        scriptInput.pressed.push_back(key);
    }
    for (LogicKey key : input.released) {
        if (logicRuntime_) logicRuntime_->dispatchKeyReleased(key);
        scriptInput.released.push_back(key);
    }
    for (LogicKey key : input.held) {
        if (logicRuntime_) logicRuntime_->dispatchKeyHeld(key);
        scriptInput.held.push_back(key);
    }
    if (scriptRuntime_) scriptRuntime_->dispatchInput(scriptInput);
    // Both languages consumed the same immutable input frame; queued
    // destroys may now commit before any fixed-step update.
    world_->flushEntityQueues();

    if (!dialogPort_ || !dialogPort_->blocksGameplay()) {
        const auto start = Clock::now();
        const std::uint32_t events = gameAPI_->dispatchInputEvents();
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->addLuaEvents(events);
        }
    }
}

// Moved verbatim from Application::dispatchGameplayCollisionTransitions
// (app_loop.cpp) - only mod_->X became world_->X/entityGateway_->X and the
// collision-pair state moved from Application::Modules into this class.
void GameplaySession::dispatchGameplayCollisionTransitions() {
    std::set<std::pair<EntityId, EntityId>> current;
    for (const CollisionWorld::ContactEvent& event : world_->collisionEvents()) {
        if (event.kind == CollisionWorld::ContactEvent::Kind::Exit
            || event.self == INVALID_ENTITY || event.other == INVALID_ENTITY
            || event.self == event.other
            || !world_->isActiveEntity(event.self)
            || !world_->isActiveEntity(event.other)) continue;
        current.emplace(std::min(event.self, event.other),
                        std::max(event.self, event.other));
    }

    std::set<std::pair<EntityId, EntityId>> entered;
    std::set<std::pair<EntityId, EntityId>> exited;
    for (const auto& pair : current)
        if (activeGameplayCollisionPairs_.count(pair) == 0) entered.insert(pair);
    for (const auto& pair : activeGameplayCollisionPairs_)
        if (current.count(pair) == 0) exited.insert(pair);

    const std::vector<EntityId> structuralOrder = entityGateway_->activeSceneIds();
    const auto dispatch = [&](const auto& edges, bool enter, auto invoke) {
        for (EntityId owner : structuralOrder) {
            for (const auto& pair : edges) {
                EntityId other = INVALID_ENTITY;
                if (pair.first == owner) other = pair.second;
                else if (pair.second == owner) other = pair.first;
                if (other != INVALID_ENTITY) invoke(owner, other, enter);
            }
        }
    };

    // One immutable entity-pair snapshot: every generated board runs before
    // any manual attachment, both in scene structural order.
    if (logicRuntime_) {
        dispatch(entered, true, [&](EntityId owner, EntityId other, bool) {
            logicRuntime_->dispatchCollisionEnter(owner, other);
        });
        dispatch(exited, false, [&](EntityId owner, EntityId other, bool) {
            logicRuntime_->dispatchCollisionExit(owner, other);
        });
    }
    if (scriptRuntime_) {
        dispatch(entered, true, [&](EntityId owner, EntityId other, bool) {
            scriptRuntime_->dispatchCollisionEnter(owner, other);
        });
        dispatch(exited, false, [&](EntityId owner, EntityId other, bool) {
            scriptRuntime_->dispatchCollisionExit(owner, other);
        });
    }
    activeGameplayCollisionPairs_ = std::move(current);
}

// Moved verbatim from Application::tickFixedStep (app_loop.cpp), post RU-02b
// (clearDrawQueue/splash already extracted to the host). mod_->X became
// X_->X for every member this class now owns directly (RU-02e-1/2/RU-02f);
// only the 3 remaining externally-owned host ports (audioPort_/dialogPort_/
// profilerPort_, wired via wireHostPorts()) are still null-checked pointers.
void GameplaySession::tickFixedStep(float dt) {
    {
        const auto start = Clock::now();
        timeManager_->tick(dt);
        tweenManager_->update(dt);
        spriteAnimator_->update(dt);
        cameraManager_->updateMotion(dt);
        gameStateManager_->update(dt);
        eventBus_->flushDeferred();
        if (!dialogPort_ || !dialogPort_->blocksGameplay()) {
            world_->tickGameplaySystems(dt);
            // Logic scene actions commit here - dispatch depth is zero, so the
            // transition handler's scope reinstall can re-fire On Start for
            // the incoming scene (LogicRuntime refuses nested dispatch).
            if (logicHost_) {
                if (const auto request = logicHost_->takePendingSceneRequest()) {
                    using Kind = RuntimeLogicHostAdapter::PendingSceneRequest::Kind;
                    if (request->kind == Kind::Restart) {
                        entityGateway_->requestRestartScene(0.f);
                    } else if (sceneManager_->getScene(request->sceneId)) {
                        entityGateway_->requestLoadScene(request->sceneId, 0.f);
                    } else {
                        std::cerr << "[GameplaySession] Logic Go To Scene ignored: "
                                     "unknown scene " << request->sceneId << "\n";
                    }
                }
            }
            entityGateway_->tickSceneTransition(dt);
        }
        if (profilerPort_) profilerPort_->addGameplayMs(elapsedMs(start));
    }
    // Drain animator events once; feed Logic Runtime then GameAPI Lua handlers.
    {
        const auto finished = spriteAnimator_->pollFinished();
        const auto events = spriteAnimator_->pollEvents();
        if (logicRuntime_) {
            for (const auto& ev : events) {
                if (ev.kind == Modules::SpriteAnimator::AnimEventKind::Start)
                    logicRuntime_->dispatchAnimationStarted(ev.entityId);
            }
            for (const auto& ev : finished)
                logicRuntime_->dispatchAnimationFinished(ev.entityId);
            logicRuntime_->dispatchTick(dt);
        }
        const auto start = Clock::now();
        const std::uint32_t luaEvents = gameAPI_->dispatchAnimationEvents(finished, events);
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->addLuaEvents(luaEvents);
        }
    }
    {
        const auto start = Clock::now();
        luaHost_->tick(dt);
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->setLuaTickEnabled(luaHost_->isScriptTickRequired());
        }
    }
    // Manual on_update runs after generated input rules and before platformer
    // integration, so its movement intent can deliberately override the board.
    if (scriptRuntime_) {
        scriptRuntime_->update(dt);
        world_->flushEntityQueues();
    }
    if (dialogPort_) dialogPort_->tick(dt);

    if (!dialogPort_ || !dialogPort_->blocksGameplay()) {
        world_->tickPlatformerControllers(dt);
        world_->tickSimpleMovementIntents(dt);
    }
    const bool runPhysics = physicsMode_ == PhysicsMode::On
        || (physicsMode_ == PhysicsMode::Auto && physics_->hasActiveBodies());
    if (runPhysics) {
        const auto start = Clock::now();
        physics_->step(dt);
        if (profilerPort_) profilerPort_->addPhysicsMs(elapsedMs(start));
    }

    world_->flushEntityQueues();
    if (runPhysics) world_->syncPhysicsToEntities();
    world_->tickCameraTargets(dt);

    world_->refreshCollisionEvents();

    {
        const auto start = Clock::now();
        const std::uint32_t events = gameAPI_->dispatchLifecycleEvents();
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->addLuaEvents(events);
        }
    }

    world_->tickAutoDestroy(dt);
    {
        const auto start = Clock::now();
        world_->flushEntityQueues();
        if (profilerPort_) profilerPort_->addGameplayMs(elapsedMs(start));
    }
    {
        const auto start = Clock::now();
        const std::uint32_t events = gameAPI_->dispatchLifecycleEvents();
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->addLuaEvents(events);
        }
    }
    dispatchGameplayCollisionTransitions();
    // Collision Destroy Self/Other queue during dispatch (never mid-callback).
    // Flush here so removal is visible this frame — same post-dispatch contract
    // as input (dispatchInput) and ADR-0026; no extra fixed-step of lag.
    {
        const auto start = Clock::now();
        world_->flushEntityQueues();
        if (profilerPort_) profilerPort_->addGameplayMs(elapsedMs(start));
    }
    {
        const auto start = Clock::now();
        const std::uint32_t events = gameAPI_->dispatchLifecycleEvents();
        if (profilerPort_) {
            profilerPort_->addLuaMs(elapsedMs(start));
            profilerPort_->addLuaEvents(events);
        }
    }

    // Drain errors from input/update/collision callbacks once the fixed-step
    // lifecycle has reached a stable post-dispatch boundary. RU-03: buffered
    // here instead of printed directly - the host decides where diagnostics
    // go (stderr for game.exe/app_loop.cpp, ConsoleMessage for the editor's
    // Play facade), this class no longer picks a policy for it (D-21).
    if (scriptRuntime_) {
        auto diagnostics = scriptRuntime_->drainDiagnostics();
        pendingScriptDiagnostics_.insert(
            pendingScriptDiagnostics_.end(),
            std::make_move_iterator(diagnostics.begin()),
            std::make_move_iterator(diagnostics.end()));
    }
    if (logicRuntime_) {
        auto diagnostics = logicRuntime_->drainDiagnostics();
        pendingLogicDiagnostics_.insert(
            pendingLogicDiagnostics_.end(),
            std::make_move_iterator(diagnostics.begin()),
            std::make_move_iterator(diagnostics.end()));
    }

    eventBus_->flushDeferred();
    if (audioPort_) audioPort_->update();
}

// RU-02g: moved verbatim from Application::renderActiveScene's per-entity draw
// loop (app_scene_render.cpp / scene_entities_pass.cpp) - the layer-rank
// lookup, the forEachActiveRenderable discovery + stable_sort by (layer
// priority, sprite.renderOrder, insertion order), and the per-entity
// visibility/animator-frame/text-binding/gauge-binding resolution are exactly
// what the render pass used to compute inline, during draw, against live
// entityGateway_/spriteAnimator_/variableManager_. Only the entity id ->
// (transform, sprite) source and the drawing itself moved out; the
// resolution logic and its order are unchanged.
SceneFrameSnapshot GameplaySession::buildFrameSnapshot(SceneFrameSnapshot snapshot) const {
    const bool inEditMode = snapshot.overlay.inEditMode;

    std::unordered_map<std::string, int> layerRankById;
    const auto& layers = sceneManager_->sceneLayers();
    for (size_t i = 0; i < layers.size(); ++i)
        layerRankById.emplace(layers[i].id, static_cast<int>(i));

    struct DiscoveredRenderable {
        EntityId id = 0;
        Transform transform{};
        SpriteComponent sprite{};
        int layerPriority = 0;
        size_t insertionIndex = 0u;
    };
    std::vector<DiscoveredRenderable> discovered;
    size_t renderableIndex = 0u;
    entityGateway_->forEachActiveRenderable(
        [&discovered, &layerRankById, &renderableIndex]
        (EntityId id, const Transform& transform, const SpriteComponent& sprite) {
            const auto rankIt = layerRankById.find(sprite.layerId);
            discovered.push_back(DiscoveredRenderable{
                id, transform, sprite,
                rankIt != layerRankById.end() ? rankIt->second : 0,
                renderableIndex++,
            });
        });
    std::stable_sort(
        discovered.begin(), discovered.end(),
        [](const DiscoveredRenderable& a, const DiscoveredRenderable& b) {
            if (a.layerPriority != b.layerPriority)
                return a.layerPriority < b.layerPriority;
            if (a.sprite.renderOrder != b.sprite.renderOrder)
                return a.sprite.renderOrder < b.sprite.renderOrder;
            return a.insertionIndex < b.insertionIndex;
        });

    snapshot.renderables.clear();
    snapshot.renderables.reserve(discovered.size());
    for (const auto& item : discovered) {
        RenderableEntitySnapshot entry;
        entry.id = item.id;
        entry.transform = item.transform;
        entry.sprite = item.sprite;
        entry.visibleInGame = entityGateway_->visibleInGame(item.id);
        entry.spriteFrame = AppRender::sprite_frame_resolve(
            spriteAnimator_.get(), item.id, item.sprite, inEditMode);

        TextComponent text{};
        if (entityGateway_->getText(item.id, text)
            && (!text.text.empty() || !text.bindKey.empty())) {
            std::optional<GameVariableValue> boundValue;
            if (!text.bindKey.empty()) {
                const bool hasBoundValue = text.bindScope == "local"
                    ? variableManager_->entityExists(item.id, text.bindKey)
                    : variableManager_->exists(text.bindKey);
                if (hasBoundValue) {
                    boundValue = text.bindScope == "local"
                        ? variableManager_->getEntity(item.id, text.bindKey)
                        : variableManager_->get(text.bindKey);
                }
            }
            text.text = resolveTextDisplay(text, boundValue);
            entry.text = std::move(text);
        }

        GaugeComponent gauge{};
        if (entityGateway_->getGauge(item.id, gauge)
            && gauge.width > 0.f && gauge.height > 0.f) {
            float value = gauge.maxValue;
            const bool hasBoundValue = !gauge.bindKey.empty()
                && (gauge.bindScope == "local"
                    ? variableManager_->entityExists(item.id, gauge.bindKey)
                    : variableManager_->exists(gauge.bindKey));
            if (hasBoundValue) {
                const auto boundValue = gauge.bindScope == "local"
                    ? variableManager_->getEntity(item.id, gauge.bindKey)
                    : variableManager_->get(gauge.bindKey);
                value = static_cast<float>(AppRender::variableToNumber(boundValue));
            }
            float ratio = gauge.maxValue > 0.f ? value / gauge.maxValue : 0.f;
            entry.gaugeRatio = std::clamp(ratio, 0.f, 1.f);
            entry.gauge = std::move(gauge);
        }

        snapshot.renderables.push_back(std::move(entry));
    }

    snapshot.elapsedTime = timeManager_->now();

    return snapshot;
}

} // namespace ArtCade
