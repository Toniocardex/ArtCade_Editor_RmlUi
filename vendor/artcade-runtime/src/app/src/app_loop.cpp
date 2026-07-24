#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/editor-api/include/editor-api.h"
#include "../../modules/game-state/include/splash-state.h"
// D-20: needed directly now for Logic::supportedLogicKeys()/logicInputCode()
// - this file used to get logic-core.h transitively via app_modules.h's
// logic-runtime.h include, which D-20 removed (LogicRuntime is
// GameplaySession-owned now, no longer aliased on Application::Modules).
#include "../../modules/logic-core/include/logic-core.h"

#ifdef ARTCADE_WASM
#include <emscripten/emscripten.h>
#endif

#include <chrono>
#include <iostream>

namespace ArtCade {

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

void Application::tickFrameEnd() {
    profiler_.setCounts(
        static_cast<uint32_t>(mod_->entityGateway->activeSceneEntityCount()),
        static_cast<uint32_t>(mod_->entityGateway->activePhysicsBodyCount()));
    {
        const auto start = Clock::now();
        renderActiveScene();
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

    const float frameTime = mod_->renderer->deltaTime();
    accumulator_ += frameTime;
    if (accumulator_ > targetDt_ * 4.f) accumulator_ = targetDt_ * 4.f;

    mod_->input->poll();
#ifndef ARTCADE_WASM
    if (mod_->input->wasKeyPressed("F11")) {
        const auto mode = mod_->renderer->toggleBorderlessFullscreen();
        if (mod_->editorViewport)
            mod_->editorViewport->set_presentation_mode(mode);
    }
#endif

#ifdef ARTCADE_WASM
    const bool simulating = EditorAPI::s_mode == 1;
#else
    const bool simulating = true;
#endif

    float simulatedDt = 0.f;
    if (simulating) {
        // Host-side frame prep (RU-02b): cleared once per loopIteration, not
        // once per fixed step, so a frame with a catch-up backlog of several
        // tickFixedStep calls still clears exactly once - identical to the
        // old per-fixed-step clear, since nothing draws between fixed steps.
        // Guarded by `simulating` to match the pre-RU-02b behavior exactly:
        // while paused (WASM edit mode), tickFixedStep never ran and this
        // never cleared either.
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
        // RU-03 (D-21): GameplaySession only buffers Script diagnostics now,
        // it doesn't decide where they go - this host prints them exactly as
        // tickFixedStep used to internally, just once per frame after the
        // catch-up loop instead of once per fixed step (harmless: order is
        // preserved, only the timing shifts within the same real frame).
        for (const auto& diagnostic : mod_->gameplaySession->drainScriptDiagnostics()) {
            std::cerr << "[Script] " << diagnostic.sourcePath;
            if (diagnostic.line > 0) std::cerr << ":" << diagnostic.line;
            std::cerr << " [" << diagnostic.callback << "] entity "
                      << diagnostic.owner << ": " << diagnostic.message << "\n";
        }
        // Host-side product policy (RU-02b), not simulation: ticked with the
        // total simulated dt for this frame, matching the sum of what N
        // per-fixed-step calls would have added; skipped when no fixed step
        // ran (simulatedDt == 0), exactly like the old per-fixed-step call.
        if (splash_ && simulatedDt > 0.f) {
            splash_->update(simulatedDt);
            if (splash_->isDone()) splash_.reset();
        }
    } else {
        accumulator_ = 0.f;
    }

    if (mod_->cameraManager->trauma() > 0.f) {
        const float shakeDt = simulatedDt > 0.f ? simulatedDt : frameTime;
        mod_->cameraManager->refreshShakeOffset(shakeDt);
        mod_->cameraManager->decayTrauma(shakeDt);
    }

    tickFrameEnd();
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
