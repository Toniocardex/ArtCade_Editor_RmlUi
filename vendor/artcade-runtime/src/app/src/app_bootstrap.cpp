#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/editor-api/include/editor-api.h"
#include "../../modules/scene-system/include/scene-mutation-result.h"
#include "../../modules/scene-system/include/scene-patch.h"

#include <memory>
#include <string>
#include <vector>

namespace ArtCade {

namespace {

bool boot_step(const char* step, bool ok) {
    if (ok) return true;
    EditorAPI::recordBootFailure(step);
    return false;
}

} // namespace

bool Application::initModules(const std::string& projectPath) {
    return initUtilities() && initSubsystems() && loadProject(projectPath);
}

// RU-02f (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo): EventBus,
// TimeManager, VariableManager, TweenManager, SpriteAnimator, CameraManager,
// SaveLoadManager and GameStateManager are now GameplaySession's own utility
// modules - this function constructs the session (needs nothing to exist
// yet) and delegates to initializeUtilities(), which builds them with the
// same boot-step names and internal wiring (GameStateManager::setEventBus)
// this function used to perform directly.
bool Application::initUtilities() {
    mod_->gameplaySession = std::make_unique<GameplaySession>();
    if (!mod_->gameplaySession->initializeUtilities(boot_step)) return false;

    mod_->eventBus = &mod_->gameplaySession->eventBus();
    mod_->timeManager = &mod_->gameplaySession->timeManager();
    mod_->variableManager = &mod_->gameplaySession->variableManager();
    mod_->tweenManager = &mod_->gameplaySession->tweenManager();
    mod_->spriteAnimator = &mod_->gameplaySession->spriteAnimator();
    mod_->cameraManager = &mod_->gameplaySession->cameraManager();
    mod_->saveLoadManager = &mod_->gameplaySession->saveLoadManager();
    mod_->gameStateManager = &mod_->gameplaySession->gameStateManager();

    ctx_.eventBus = mod_->eventBus;
    ctx_.timeManager = mod_->timeManager;
    ctx_.variableManager = mod_->variableManager;
    ctx_.tweenManager = mod_->tweenManager;
    ctx_.spriteAnimator = mod_->spriteAnimator;
    ctx_.cameraManager = mod_->cameraManager;
    ctx_.saveLoadManager = mod_->saveLoadManager;
    ctx_.profiler = &profiler_;
    ctx_.gameStateManager = mod_->gameStateManager;

    return true;
}

bool Application::initSubsystems() {
    mod_->editorViewport =
        std::make_unique<ArtCade::Presentation::EditorViewportService>();
    mod_->renderer = std::make_unique<ArtCade::Modules::Renderer>();
    mod_->input = std::make_unique<ArtCade::Modules::Input>();
    mod_->audio = std::make_unique<ArtCade::Modules::Audio>();
    mod_->assetLoader = std::make_unique<ArtCade::Modules::AssetLoader>();

    mod_->renderer->setWindowSize(1280, 720, "ArtCade V2");

    if (!boot_step("renderer", mod_->renderer->init())) return false;
    if (!boot_step("input", mod_->input->init())) return false;
    if (!boot_step("audio", mod_->audio->init())) return false;
    if (!boot_step("asset_loader", mod_->assetLoader->init())) return false;

    mod_->textureManager = std::make_unique<ArtCade::Modules::TextureManager>();
    if (!boot_step("texture_manager", mod_->textureManager->init())) return false;
    ctx_.textureManager = mod_->textureManager.get();

    // RU-02e-1/D-20 (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo):
    // Physics, SceneManager, SceneMutationService, RuntimeEntityGateway,
    // SceneLifecycleService and World are now GameplaySession's own
    // composition root (the session itself was already constructed in
    // initUtilities()). initialize() builds the graph with the same
    // boot-step names and internal wiring this function used to perform
    // directly, wires World's destroy handler/spriteAnimator/renderer
    // internally now (D-20 - Application no longer holds a mutable World*
    // to do this itself), and populates ctx_.physics/sceneManager/
    // entityGateway/world directly (D-20 - no more physics()/world()
    // accessors to copy from).
    const bool graphOk = mod_->gameplaySession->initialize(
        physicsMode_,
        ctx_,
        mod_->renderer.get(),
        boot_step,
        [this](const ArtCade::Modules::SceneTransitionResult& result) {
            handleSceneTransition(result);
        });
    if (!graphOk) return false;

    // D-20: SceneManager/RuntimeEntityGateway keep Application-level aliases
    // (RU-02g render-pass exception - see app_modules.h); Physics/World no
    // longer do, since ctx_ above and the new GameplaySession methods cover
    // every remaining Application need for them.
    mod_->sceneManager = &mod_->gameplaySession->sceneManager();
    mod_->entityGateway = &mod_->gameplaySession->entityGateway();

    ctx_.renderer = mod_->renderer.get();
    ctx_.input = mod_->input.get();
    ctx_.audio = mod_->audio.get();
    ctx_.assetLoader = mod_->assetLoader.get();

    mod_->renderer->setTextureKeyResolver(
        [loader = mod_->assetLoader.get()](const std::string& ref) {
            return loader ? loader->resolveImagePath(ref) : ref;
        });
    mod_->renderer->setFontKeyResolver(
        [loader = mod_->assetLoader.get()](const std::string& ref) {
            return loader ? loader->resolveFontPath(ref) : ref;
        });
    mod_->audio->setAssetPathResolver(
        [loader = mod_->assetLoader.get()](const std::string& ref) {
            return loader ? loader->resolveAudioPath(ref) : ref;
        });

    mod_->dialogManager = std::make_unique<ArtCade::Modules::DialogManager>();
    if (!boot_step("dialog_manager", mod_->dialogManager->init())) return false;
    mod_->dialogManager->setContext(&ctx_);
    ctx_.dialogManager = mod_->dialogManager.get();

    // RU-02e-2/D-20 (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo):
    // RuntimeLogicHostAdapter/LogicRuntime/GameAPI/LuaHost are now built by
    // the session itself, in the same relative order and with the same
    // boot-step names this function used to construct them with directly.
    // logicHost/logicRuntime used to be built earlier - right after World was
    // constructed inside gameplaySession->initialize(), above - but nothing
    // between that point and here ever calls into them: World's destroy
    // handler (now internal to GameplaySession::initialize()) only reads
    // logicRuntime_/scriptRuntime_ lazily, at destroy time, long after boot
    // completes, so building both pairs together here is behaviorally
    // identical. ctx_ already carries
    // renderer/physics/input/audio/sceneManager/entityGateway/assetLoader/
    // world/dialogManager by this point, exactly what GameAPI(ctx_) used to
    // read. D-20: no spawnInstaller param anymore - the Logic spawn installer
    // wires to GameplaySession::installLogicScopeForEntity() internally now,
    // since that method (and the scope bookkeeping it needs) moved into the
    // session too.
    const bool gameplayModulesOk = mod_->gameplaySession->initializeGameplayModules(
        ctx_, *mod_->audio, *mod_->input, boot_step);
    if (!gameplayModulesOk) return false;

    // D-20: logicHost/logicRuntime/gameAPI no longer have Application-level
    // aliases at all (zero remaining call sites once install*/loadLogicPrograms
    // moved into GameplaySession); luaHostHandle() stays only for the
    // EditorAPI::wireLua() call below (D-18 debt, out of scope here).
    EditorAPI::wireEngine(mod_->entityGateway);
    EditorAPI::wireLua(mod_->gameplaySession->luaHostHandle());
    EditorAPI::wireRenderer(mod_->renderer.get());
    EditorAPI::wireEditorViewport(mod_->editorViewport.get());
    EditorAPI::wireDialog(mod_->dialogManager.get());
    EditorAPI::wireSpriteAnimator(mod_->spriteAnimator);
    mod_->entityGateway->setSpriteAnimator(mod_->spriteAnimator);
    EditorAPI::wireAudio(mod_->audio.get());
    EditorAPI::wireVariables(mod_->variableManager);
    EditorAPI::init("#artcade-canvas");

#ifdef ARTCADE_WASM
    EditorAPI::setSceneMutationBridge(
        [this](const SceneId& sceneId,
               const ArtCade::Modules::ScenePatch& patch) {
            return mod_->gameplaySession->applySceneMutation(sceneId, patch);
        },
        [this](const ArtCade::Modules::SceneMutationResult& result) {
            handleSceneMutation(result);
        });
    EditorAPI::setSceneInvalidationQueueHandler(
        [this](const ArtCade::Modules::SceneInvalidation flags) {
            queueSceneInvalidations(flags);
        });
    EditorAPI::setAuthoringSyncBatchHandlers(
        [this]() { beginAuthoringSyncBatch(); },
        [this]() { endAuthoringSyncBatch(); });
    EditorAPI::setSceneMutationBatchOpenPredicate(
        [this]() {
            return mod_->gameplaySession && mod_->gameplaySession->sceneMutationBatchOpen();
        });
    EditorAPI::setProjectLoadedHandler(
        [this](const std::vector<TilePaletteEntry>& palette,
               const std::vector<TilesetAsset>& tilesets,
               const std::vector<GameVariableDefinition>& variables,
               const ProjectRuntimeSettings& settings) {
            applyEditorProjectLoaded(palette, tilesets, variables, settings);
        });
    EditorAPI::setPreviewRestoreHandler(
        [this](const std::vector<TilePaletteEntry>& palette,
               const std::vector<TilesetAsset>& tilesets,
               const std::vector<GameVariableDefinition>& variables,
               const ProjectRuntimeSettings& settings) {
            applyEditorPreviewRestore(palette, tilesets, variables, settings);
        });
    EditorAPI::setEnterPlayHandler(
        [this](const std::vector<TilePaletteEntry>& palette,
               const std::vector<TilesetAsset>& tilesets,
               const std::vector<GameVariableDefinition>& variables,
               const ProjectRuntimeSettings& settings) {
            applyEditorEnterPlay(palette, tilesets, variables, settings);
        });
    EditorAPI::setExitPlayHandler(
        [this](const std::vector<TilePaletteEntry>& palette,
               const std::vector<TilesetAsset>& tilesets,
               const std::vector<GameVariableDefinition>& variables,
               const ProjectRuntimeSettings& settings,
               const std::string& luaSource) {
            applyEditorExitPlay(palette, tilesets, variables, settings, luaSource);
        });
#endif

    // RU-02c/RU-02e-1/2/3/RU-02f: every gameplay module GameplaySession needs
    // is session-owned now; wireHostPorts() wires only the three host-port
    // adapters Application still owns outright (GameplayRuntimeRefs, T-01, is
    // eliminated - no struct needed anymore).
    mod_->audioAdapter = std::make_unique<AudioServiceAdapter>(*mod_->audio);
    mod_->dialogAdapter = std::make_unique<DialogGateAdapter>(*mod_->dialogManager);
    mod_->profilerAdapter = std::make_unique<ProfilerSinkAdapter>(profiler_);
    mod_->gameplaySession->wireHostPorts(
        mod_->audioAdapter.get(), mod_->dialogAdapter.get(), mod_->profilerAdapter.get());

    return true;
}

void Application::shutdownModules() {
    if (!mod_) return;

#ifdef ARTCADE_WASM
    EditorAPI::clearEngineWiring();
#endif

    // GameplaySession's host-port adapters are dropped first so nothing
    // holds a dangling reference once the modules they forward to reset
    // below (unchanged relative order from RU-02c).
    mod_->audioAdapter.reset();
    mod_->dialogAdapter.reset();
    mod_->profilerAdapter.reset();

    // RU-02e-2/3/D-20: logicRuntime/logicHost/scriptRuntime (and the scope
    // bookkeeping that used to live in Application::Modules) are all owned by
    // gameplaySession now; the three granular shutdown methods tear them down
    // in the exact relative order this function used to (logicRuntime ->
    // logicHost -> scriptRuntime -> luaHost -> gameAPI), clearing the scope
    // bookkeeping internally at the same relative points, with Application-
    // owned dialogManager shutdown still interleaved exactly where it was
    // before.
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownLogicModules();
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownScriptRuntime();
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownScriptingModules();
    if (mod_->dialogManager) { mod_->dialogManager->shutdown(); mod_->dialogManager.reset(); }

    // RU-02e-1: World/gateway/lifecycle/mutation/scene-manager are owned by
    // gameplaySession now; shutdownGraph() tears them down in the exact
    // relative order this function used to (world -> gateway -> lifecycle ->
    // mutation -> scene manager). D-20: sceneManager/entityGateway are the
    // only Modules aliases left for this graph (RU-02g render-pass
    // exception); world/sceneMutation never had aliases to null anymore.
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownGraph();
    mod_->entityGateway = nullptr;
    mod_->sceneManager = nullptr;

    if (mod_->assetLoader) { mod_->assetLoader->shutdown(); mod_->assetLoader.reset(); }
    if (mod_->audio) { mod_->audio->shutdown(); mod_->audio.reset(); }
    if (mod_->input) { mod_->input->shutdown(); mod_->input.reset(); }
    // Physics shuts down here, in the same relative position as before
    // (after audio/input, before textureManager/renderer).
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownPhysics();
    if (mod_->textureManager) { mod_->textureManager->shutdown(); mod_->textureManager.reset(); }
    if (mod_->renderer) { mod_->renderer->shutdown(); mod_->renderer.reset(); }

    // RU-02f: gameStateManager/saveLoadManager/cameraManager/spriteAnimator/
    // tweenManager/variableManager/timeManager/eventBus are owned by
    // gameplaySession now; shutdownUtilities() tears them down in the exact
    // same contiguous order this function used to (no host module was ever
    // interleaved between them).
    if (mod_->gameplaySession) mod_->gameplaySession->shutdownUtilities();
    mod_->gameStateManager = nullptr;
    mod_->saveLoadManager = nullptr;
    mod_->cameraManager = nullptr;
    mod_->spriteAnimator = nullptr;
    mod_->tweenManager = nullptr;
    mod_->variableManager = nullptr;
    mod_->timeManager = nullptr;
    mod_->eventBus = nullptr;

    // Every module above is already torn down; this only releases the
    // (now-empty) GameplaySession instance itself.
    mod_->gameplaySession.reset();
}

} // namespace ArtCade
