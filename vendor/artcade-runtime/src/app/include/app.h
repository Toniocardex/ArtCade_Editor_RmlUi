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
 * GameplayStartupPhase (ADR-0039) — scoped to standalone executable startup
 * only, not the whole application lifecycle. `Inactive` is also the
 * permanent state for Editor Play and WASM edit-mode preview, which never
 * call loadProject() and so never move off it; only the standalone
 * "game"/WASM-export loadProject() path enters Splash or Activating.
 *
 *   Inactive   - no standalone startup sequence in progress (default; also
 *                Editor Play/WASM preview's permanent state).
 *   Splash     - standalone startup is blocked on the splash.
 *   Activating - On Start dispatch is running; simulation is still blocked.
 *   Complete   - the normal loop is allowed to simulate.
 *
 * Deliberately does not span shutdown (`running_` is already that
 * authority) or a "preparing" step (loadProject() is synchronous and
 * returns before mainLoop() starts, so there is no rendered frame for it)
 * or a "failed" value (structural failures use the project's existing
 * error-return convention; see ADR-0039 §18).
 */
enum class GameplayStartupPhase {
    Inactive,
    Splash,
    Activating,
    Complete,
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

    // ADR-0039 §4: general frame-time sanitization (finite, non-negative).
    // SplashState additionally owns its own max-presentation-step clamp -
    // this only guards against NaN/negative/huge deltas reaching anything.
    float sanitizeFrameDt(float rawFrameDt) const;

    // ADR-0039 §11-13: one tick function per GameplayStartupPhase value that
    // corresponds to a rendered frame (Inactive and Complete share
    // tickGameplay - Inactive only differs by the WASM edit-mode gate
    // tickGameplay already checks internally).
    void tickSplash(float frameDt);
    void tickGameplayActivation();
    void tickGameplay(float frameDt);

    /** Prints buffered Script/Logic diagnostics (ADR-0039 §18: shared by
     *  tickGameplay and tickGameplayActivation so an authored-content error
     *  during On Start is visible immediately, not delayed to first tick). */
    void drainGameplayDiagnostics();

    /** Per-render-frame tail: profiler counts, draw, console flush, input
     *  reset. Takes the frame's phase frozen at the top of loopIteration()
     *  (ADR-0039 §4) so tick and render always agree on which phase produced
     *  them, instead of tickSplash() being able to flip startupPhase_ mid-frame
     *  and this function reading the new value before the frame that earned
     *  it has actually run. */
    void tickFrameEnd(GameplayStartupPhase framePhase);
    void renderActiveScene();
    /** Splash-exclusive render (ADR-0039 §16): no scene pipeline, no
     *  PresentationSnapshot - a raw raylib frame bracket, same as
     *  SplashState::render()'s own direct raylib draw calls. Unreachable
     *  under ARTCADE_WASM: WASM's Application::run() never calls
     *  loadProject(), the only place startupPhase_ becomes Splash. */
    void renderSplashFrame();

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
    // ADR-0039: startup-only state machine; default Inactive is also Editor
    // Play/WASM preview's permanent value (see GameplayStartupPhase above).
    GameplayStartupPhase startupPhase_ = GameplayStartupPhase::Inactive;
    // FREE-tier splash overlay; owned only while startupPhase_ == Splash.
    std::unique_ptr<::ArtCade::Modules::SplashState> splash_;
    std::unordered_map<int, ::ArtCade::Vec4> tileColors_;  // Phase D2: id → render colour
    std::unordered_map<std::string, ::ArtCade::TilesetAsset> tilesets_;  // Phase F3

#ifdef ARTCADE_WASM
    // Emscripten requires a static callback that forwards to the active instance.
    static Application* webInstance_;
    static void         webLoopCallback();
#endif
};

} // namespace ArtCade
