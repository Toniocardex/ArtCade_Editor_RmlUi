#pragma once

#include "../../core/engine-context.h"
#include "../../core/runtime-profiler.h"
#include "../../core/types.h"
#include "../../modules/scene-system/include/scene-invalidation.h"
#include "../../modules/scene-system/include/scene-lifecycle-result.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ArtCade {

namespace Modules { class SplashState; }

/** How the renderer maps world vs viewport for the active scene. */
enum class ViewportPolicy {
    EditorPreview, /**< worldSize window; viewport = world (1:1 edit canvas) */
    NativePlay,    /**< viewportSize window; camera lens = viewport */
};

/**
 * Startup state for the standalone native player.
 *
 * Inactive is the default before a project is loaded. Splash blocks startup
 * for free projects, Activating dispatches On Start, and Complete permits the
 * normal simulation loop.
 */
enum class GameplayStartupPhase {
    Inactive,
    Splash,
    Activating,
    Complete,
};

/**
 * Application is the native-player composition root. It owns runtime module
 * lifetimes, loads one exported project, and drives its simulation/render loop.
 */
class Application {
public:
    Application();
    ~Application();

    int run(int argc, char* argv[]);

    /** Apply target FPS, physics mode, and viewport/window from project settings. */
    void applyRuntimeSettings(const ProjectRuntimeSettings& settings,
                              ViewportPolicy policy);

private:
    struct Modules;
    std::unique_ptr<Modules> mod_;

    EngineContext ctx_;
    RuntimeProfiler profiler_;

    bool initModules(const std::string& projectPath);
    bool initUtilities();
    bool initSubsystems();
    bool loadProject(const std::string& projectPath);

    void shutdownModules();
    void mainLoop();
    void loopIteration();

    float sanitizeFrameDt(float rawFrameDt) const;
    void tickSplash(float frameDt);
    void tickGameplayActivation();
    void tickGameplay(float frameDt);
    void drainGameplayDiagnostics();
    void tickFrameEnd(GameplayStartupPhase framePhase);
    void renderActiveScene();
    void renderSplashFrame();

    /** Coalesces scene-transition invalidations for the next frame boundary. */
    void handleSceneTransition(const ArtCade::Modules::SceneTransitionResult& result);
    void applyPendingSceneInvalidations();

    ArtCade::Modules::SceneInvalidation pendingSceneInvalidations_ =
        ArtCade::Modules::SceneInvalidation::None;

    uint64_t frameNumber_ = 0;

#ifndef NDEBUG
    /** Debug-only: tilemap pointers in SceneFrameSnapshot alias SceneDef while true. */
    bool sceneFrameRenderActive_ = false;
#endif

    float targetDt_ = 1.f / 60.f;
    float accumulator_ = 0.f;
    bool running_ = false;
    PhysicsMode physicsMode_ = PhysicsMode::Auto;
    std::string licenseTier_ = "free";
    GameplayStartupPhase startupPhase_ = GameplayStartupPhase::Inactive;
    std::unique_ptr<::ArtCade::Modules::SplashState> splash_;
    std::unordered_map<int, ::ArtCade::Vec4> tileColors_;
    std::unordered_map<std::string, ::ArtCade::TilesetAsset> tilesets_;
};

} // namespace ArtCade
