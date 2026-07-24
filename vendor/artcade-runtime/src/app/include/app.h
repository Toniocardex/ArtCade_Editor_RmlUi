#pragma once

#include "../../core/engine-context.h"
#include "../../core/runtime-profiler.h"
#include "../../core/types.h"
#include "../../modules/scene-system/include/scene-invalidation.h"
#include "../../modules/scene-system/include/scene-mutation-result.h"
#include "../../modules/scene-system/include/scene-lifecycle-result.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade {

namespace Modules { class SplashState; }

/** How the renderer maps world vs viewport for the active scene. */
enum class ViewportPolicy {
    EditorPreview, /**< worldSize window; viewport = world (1:1 edit canvas) */
    NativePlay,    /**< viewportSize window; camera lens = viewport */
};


/**
 * Application - top-level orchestrator (Layer 4).
 *
 * Owns all modules, wires the EngineContext, and drives the main loop.
 * main.cpp contains only:  return Application{}.run(argc, argv);
 * Implementations are split by domain across app_bootstrap, app_loop,
 * app_project_lifecycle, and app_scene_render.
 *
 * Responsibilities:
 *   - Module lifetime management (init order, shutdown reverse order)
 *   - Main loop (fixed-timestep accumulator)
 *   - Lua tick -> Physics step -> Render
 */
class Application {
public:
    Application();
    ~Application();

    int run(int argc, char* argv[]);

    /** Apply targetFPS, physicsMode, and viewport/window from project settings. */
    void applyRuntimeSettings(const ProjectRuntimeSettings& settings,
                              ViewportPolicy              policy);

#ifdef ARTCADE_WASM
    /** Shared tile setup after editor project JSON is applied (no viewport).
     *  @p evictAssets unloads the texture/sound caches — needed when the project
     *  content may have changed (HotSync / restore), but NOT on edit↔play mode
     *  transitions where the asset set is identical. Evicting there would wipe the
     *  GPU textures and force an async JS re-upload, so the first play frame would
     *  render the placeholder square until the upload lands. */
    void applyEditorProjectCommon(const std::vector<TilePaletteEntry>& tilePalette,
                                  const std::vector<TilesetAsset>&     tilesets,
                                  bool                                 evictAssets = true);
    /** Single revision bump + invalidation queue after a full project replace. */
    void onProjectReplaced();
    /** Called from editor_load_project after the gateway swap. */
    void applyEditorProjectLoaded(const std::vector<TilePaletteEntry>& tilePalette,
                                  const std::vector<TilesetAsset>&     tilesets,
                                  const std::vector<GameVariableDefinition>& variables,
                                  const ProjectRuntimeSettings&        settings);
    /** Called from editor_restore_from_project: reset runtime, restore design. */
    void applyEditorPreviewRestore(const std::vector<TilePaletteEntry>& tilePalette,
                                   const std::vector<TilesetAsset>&     tilesets,
                                   const std::vector<GameVariableDefinition>& variables,
                                   const ProjectRuntimeSettings&        settings);
    /** Atomic PLAY: world sync + gameplay module reset (Lua set by editor-api). */
    void applyEditorEnterPlay(const std::vector<TilePaletteEntry>& tilePalette,
                              const std::vector<TilesetAsset>&     tilesets,
                              const std::vector<GameVariableDefinition>& variables,
                              const ProjectRuntimeSettings&        settings);
    /** Atomic STOP: design restore + gameplay Lua (no empty stub). */
    void applyEditorExitPlay(const std::vector<TilePaletteEntry>& tilePalette,
                             const std::vector<TilesetAsset>&     tilesets,
                             const std::vector<GameVariableDefinition>& variables,
                             const ProjectRuntimeSettings&        settings,
                             const std::string&                   luaSource);
    /** Shared reset for tween/audio/animator/event/layer/save/time/state/camera. */
    void resetGameplayRuntimeModules();
#endif

private:
    struct Modules;
    std::unique_ptr<Modules> mod_;

    EngineContext ctx_;
    RuntimeProfiler profiler_;

    // Top-level initialization delegates to dependency-ordered domain steps.
    bool initModules(const std::string& projectPath);

    // Layer 0: stateless modules + GameStateManager.
    bool initUtilities();
    // Layer 1-4: renderer, physics, input, audio, world, GameAPI, LuaHost
    bool initSubsystems();
    // Layer 5: load project data, initialize the world, and load Lua bytecode.
    bool loadProject(const std::string& projectPath);
    // D-20 (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo, debt
    // register): installLogicScopesForActiveScene/installLogicScopeForEntity/
    // installScriptScopesForActiveScene moved into GameplaySession - they are
    // simulation state/behavior (which entity owns which Logic/Script scope),
    // not host bookkeeping, and moving them lets World's destroy handler and
    // the Logic spawn installer capture GameplaySession instead of
    // Application. Call sites now read
    // mod_->gameplaySession->installLogicScopesForActiveScene() etc.

    void shutdownModules();
    void mainLoop();
    void loopIteration();      // One frame, shared by native and WASM loops.
    /** Per-render-frame tail: profiler counts, draw, console flush, input reset. */
    void tickFrameEnd();
    void renderActiveScene();

    /**
     * Post-mutation handler registered with EditorAPI (composition root).
     * Coalesces invalidation flags; rebuild runs at the next frame boundary.
     */
    void handleSceneMutation(const ArtCade::Modules::SceneMutationResult& result);

    /**
     * Post-transition handler for SceneLifecycleService commits.
     * Coalesces invalidation flags; rebuild runs at the next frame boundary.
     */
    void handleSceneTransition(const ArtCade::Modules::SceneTransitionResult& result);

    /** Queues invalidation flags (tilemap/entity sync paths). */
    void queueSceneInvalidations(ArtCade::Modules::SceneInvalidation flags);

    void beginAuthoringSyncBatch();
    void endAuthoringSyncBatch();

    /** Consumes coalesced invalidations before presentation commit (PR3). */
    void applyPendingSceneInvalidations();

    ArtCade::Modules::SceneInvalidation pendingSceneInvalidations_ =
        ArtCade::Modules::SceneInvalidation::None;

    ArtCade::Modules::SceneInvalidation pendingAuthoringInvalidations_ =
        ArtCade::Modules::SceneInvalidation::None;

    int authoringSyncBatchDepth_ = 0;

    uint64_t frameNumber_ = 0;

#ifndef NDEBUG
    /** Debug-only: tilemap pointers in SceneFrameSnapshot alias SceneDef while true. */
    bool sceneFrameRenderActive_ = false;
#endif

    float targetDt_        = 1.f / 60.f;
    float accumulator_      = 0.f;          // Persistent across frames for WASM.
    bool  running_          = false;
    PhysicsMode physicsMode_ = PhysicsMode::Auto;
    std::string licenseTier_ = "free";      // from ProjectDoc, used by SplashState
    std::unique_ptr<::ArtCade::Modules::SplashState> splash_;  // FREE-tier watermark overlay
    std::unordered_map<int, ::ArtCade::Vec4> tileColors_;  // Phase D2: id → render colour
    std::unordered_map<std::string, ::ArtCade::TilesetAsset> tilesets_;  // Phase F3

#ifdef ARTCADE_WASM
    // Emscripten requires a static callback that forwards to the active instance.
    static Application* webInstance_;
    static void         webLoopCallback();
#endif
};

} // namespace ArtCade
