#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/editor-api/include/editor-api.h"
#include "../../modules/game-state/include/splash-state.h"
// D-20: needed directly now for Logic::supportedLogicKeys()/logicInputCode()
// - this file used to get logic-core.h transitively via app_modules.h's
// logic-runtime.h include, which D-20 removed (LogicRuntime is
// GameplaySession-owned now, no longer aliased on Application::Modules).
#include "../../modules/logic-core/include/logic-core.h"

#include <raylib.h>

#ifdef ARTCADE_WASM
#include <emscripten/emscripten.h>
#endif

#include <chrono>
#include <cmath>
#include <iostream>

namespace ArtCade {

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

float Application::sanitizeFrameDt(float rawFrameDt) const {
    return std::isfinite(rawFrameDt) && rawFrameDt > 0.f ? rawFrameDt : 0.f;
}

void Application::drainGameplayDiagnostics() {
    // RU-03 (D-21) + ADR-0028: GameplaySession buffers Script/Logic
    // diagnostics; this host prints them once per frame after the
    // catch-up loop (order preserved; timing shifts within the frame).
    for (const auto& diagnostic : mod_->gameplaySession->drainScriptDiagnostics()) {
        std::cerr << "[Script] " << diagnostic.sourcePath;
        if (diagnostic.line > 0) std::cerr << ":" << diagnostic.line;
        std::cerr << " [" << diagnostic.callback << "] entity "
                  << diagnostic.owner << ": " << diagnostic.message << "\n";
    }
    for (const auto& diagnostic : mod_->gameplaySession->drainLogicDiagnostics()) {
        std::cerr << "[Logic] " << diagnostic << "\n";
    }
}

// ADR-0039 §12: presentation-only - no gameplay input, no fixed step, no
// physics/Logic/Script update. The accumulator is zeroed every frame so no
// catch-up backlog builds up to be replayed once the splash ends.
void Application::tickSplash(float frameDt) {
    accumulator_ = 0.f;

    if (!splash_) {
        startupPhase_ = GameplayStartupPhase::Activating;
        return;
    }

    if (splash_->update(frameDt)) {
        startupPhase_ = GameplayStartupPhase::Activating;
    }
}

// ADR-0039 §13: dispatches Logic/Script On Start exactly once and resolves
// to Complete within the same frame it starts - not a multi-frame waiting
// state. No fixed step runs in this frame (§13/§14): the render that follows
// (still gated by the frame's frozen phase, see tickFrameEnd()) shows the
// post-On-Start state because buildFrameSnapshot() resolves entity transform/
// sprite/animator state live from the World on every render call rather than
// from data tickFixedStep() populates - On Start's own GameAPI/Logic calls
// (setPosition, etc.) already mutate the World synchronously, so no separate
// presentation-only refresh or dt=0 fixed step is needed here.
void Application::tickGameplayActivation() {
    accumulator_ = 0.f;

    // ADR-0039 §19: discards edge input (pressed/released) accumulated while
    // the splash was visible. Held state is unaffected by this call and
    // reads normally starting the first Complete frame.
    mod_->input->resetFrameState();

    if (!mod_->gameplaySession->startPreparedActiveSceneGameplay()) {
        // Structural failure (§18): startPreparedActiveSceneGameplay() only
        // rejects a call made from a non-Prepared state, which means
        // prepareActiveSceneGameplay() never ran or failed - a programming-
        // contract violation, not an authored-content error. That category
        // (a Lua exception inside an author's own On Start) stays a
        // non-fatal diagnostic drained below and never reaches this branch.
        std::cerr << "[App] Gameplay activation failed to start from a "
                     "non-Prepared state; aborting standalone startup\n";
        running_ = false;
        return;
    }

    // Author-side startup errors are visible the moment they happen, not
    // delayed behind the activation frame boundary.
    drainGameplayDiagnostics();

    splash_.reset();
    startupPhase_ = GameplayStartupPhase::Complete;
}

// ADR-0039 §15: only phase that runs simulation. Structure mirrors the
// pre-ADR-0039 loop exactly (accumulator/dispatch/fixed-step gated by
// canSimulate; camera shake unconditional after it) rather than the ADR's
// simplified pseudocode, so WASM edit-mode's existing behavior - camera
// shake keeps decaying off raw frameDt even while s_mode gates simulation
// off - is preserved unchanged. Only reachable for
// GameplayStartupPhase::Inactive/Complete (see loopIteration()'s switch), so
// the startup gate itself needs no re-checking here - only the WASM
// edit/play toggle does.
void Application::tickGameplay(float frameDt) {
#ifdef ARTCADE_WASM
    const bool canSimulate = EditorAPI::s_mode == 1;
#else
    const bool canSimulate = true;
#endif

    float simulatedDt = 0.f;
    if (canSimulate) {
        accumulator_ += frameDt;
        if (accumulator_ > targetDt_ * 4.f) accumulator_ = targetDt_ * 4.f;

        // Host-side frame prep (RU-02b): cleared once per loopIteration, not
        // once per fixed step, so a frame with a catch-up backlog of several
        // tickFixedStep calls still clears exactly once - identical to the
        // old per-fixed-step clear, since nothing draws between fixed steps.
        mod_->renderer->clearDrawQueue();
        // RU-02d: Application only polls Raylib/Input and resolves the
        // supported key catalog into an immutable GameplayInputFrame; the
        // dispatch to Logic/Script/GameAPI (beginFrame, per-kind dispatch,
        // ScriptInputSnapshot, entity-queue flush, dialog-gated GameAPI
        // input events) lives entirely in GameplaySession::dispatchInput
        // now (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo).
        GameplayInputFrame inputFrame;
        for (LogicKey key : Logic::supportedLogicKeys()) {
            const std::string code = Logic::logicInputCode(key);
            if (mod_->input->wasKeyPressed(code)) inputFrame.pressed.push_back(key);
            if (mod_->input->wasKeyReleased(code)) inputFrame.released.push_back(key);
            if (mod_->input->isKeyDown(code)) inputFrame.held.push_back(key);
        }
        mod_->gameplaySession->dispatchInput(inputFrame);
        // RU-02h: Application::tickFixedStep (T-04 in the debt register) is
        // gone - it was a one-line wrapper around this call, kept only until
        // every call site could drive GameplaySession directly. This is now
        // the only call site.
        while (accumulator_ >= targetDt_) {
            mod_->gameplaySession->tickFixedStep(targetDt_);
            accumulator_ -= targetDt_;
            simulatedDt += targetDt_;
        }
        drainGameplayDiagnostics();
    } else {
        accumulator_ = 0.f;
    }

    // RU02 host-cadence: once per frame (not inside tickFixedStep). Prefer the
    // session API so Editor Play and standalone share the same entry point.
    // Deliberately unconditional (not gated by canSimulate): WASM edit-mode
    // camera shake keeps decaying off raw frameDt even while simulation is
    // paused - unrelated to, and unchanged by, ADR-0039.
    if (mod_->gameplaySession) {
        const float shakeDt = simulatedDt > 0.f ? simulatedDt : frameDt;
        mod_->gameplaySession->updateCameraShake(shakeDt);
    } else if (mod_->cameraManager && mod_->cameraManager->trauma() > 0.f) {
        const float shakeDt = simulatedDt > 0.f ? simulatedDt : frameDt;
        mod_->cameraManager->refreshShakeOffset(shakeDt);
        mod_->cameraManager->decayTrauma(shakeDt);
    }
}

void Application::renderSplashFrame() {
    BeginDrawing();
    ClearBackground(BLACK);
    if (splash_) {
        splash_->render(
            static_cast<int>(mod_->renderer->windowWidth()),
            static_cast<int>(mod_->renderer->windowHeight()));
    }
    EndDrawing();
}

void Application::tickFrameEnd(GameplayStartupPhase framePhase) {
    profiler_.setCounts(
        static_cast<uint32_t>(mod_->entityGateway->activeSceneEntityCount()),
        static_cast<uint32_t>(mod_->entityGateway->activePhysicsBodyCount()));
    {
        const auto start = Clock::now();
        // ADR-0039 §16: the splash is exclusive of scene rendering - never
        // drawn on top of renderActiveScene() anymore. Uses the phase frozen
        // at the top of loopIteration(), not startupPhase_ directly, so a
        // frame where tickSplash() just flipped the phase to Activating
        // still renders as Splash (the transition itself renders Activating
        // on the *next* frame, after On Start has actually run - see §4).
        if (framePhase == GameplayStartupPhase::Splash) {
            renderSplashFrame();
        } else {
            renderActiveScene();
        }
        profiler_.setRenderMs(elapsedMs(start));
    }
    EditorAPI::flushConsoleLines();
    EditorAPI::processSpritesheetPreviewQueue();
    mod_->input->resetFrameState();
    profiler_.endFrame();

    if (!mod_->renderer) return;
    const float dt = mod_->renderer->deltaTime();
    const float fps = (dt > 1e-6f) ? (1.f / dt) : 0.f;
    const auto snapshot = profiler_.snapshot();
    EditorAPI::publishRuntimeProfile(
        fps, static_cast<float>(snapshot.luaMs),
        static_cast<float>(snapshot.physicsMs),
        static_cast<float>(snapshot.renderMs),
        snapshot.entityCount, snapshot.activePhysicsBodies);
    EditorAPI::notifyRuntimeProfile(
        fps, static_cast<float>(snapshot.luaMs),
        static_cast<float>(snapshot.physicsMs),
        static_cast<float>(snapshot.renderMs),
        snapshot.entityCount, snapshot.activePhysicsBodies);
}

void Application::loopIteration() {
    profiler_.beginFrame();
#ifndef ARTCADE_WASM
    if (!running_ || mod_->renderer->shouldClose()) {
        running_ = false;
        return;
    }
#endif

    // ADR-0039 §4: captured once, before anything this frame can mutate it,
    // so ticking and rendering always agree on which phase produced them.
    const GameplayStartupPhase framePhase = startupPhase_;

    const float frameDt = sanitizeFrameDt(mod_->renderer->deltaTime());

    mod_->input->poll();
#ifndef ARTCADE_WASM
    // Host input allowed in every phase, including Splash (ADR-0039 §12).
    if (mod_->input->wasKeyPressed("F11")) {
        const auto mode = mod_->renderer->toggleBorderlessFullscreen();
        if (mod_->editorViewport)
            mod_->editorViewport->set_presentation_mode(mode);
    }
#endif

    switch (framePhase) {
    case GameplayStartupPhase::Splash:
        tickSplash(frameDt);
        break;
    case GameplayStartupPhase::Activating:
        tickGameplayActivation();
        break;
    case GameplayStartupPhase::Inactive:
    case GameplayStartupPhase::Complete:
        tickGameplay(frameDt);
        break;
    }

    tickFrameEnd(framePhase);
}

void Application::mainLoop() {
#ifdef ARTCADE_WASM
    webInstance_ = this;
    emscripten_set_main_loop(webLoopCallback, 0, 1);
#else
    while (running_ && !mod_->renderer->shouldClose()) loopIteration();
#endif
}

} // namespace ArtCade
