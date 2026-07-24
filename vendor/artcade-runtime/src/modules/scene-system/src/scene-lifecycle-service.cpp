#include "../include/scene-lifecycle-service.h"

#include "../include/scene-manager.h"
#include "../include/scene-mutation-service.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

namespace {

constexpr SceneInvalidation kActivationInvalidations =
    SceneInvalidation::SceneActivation
    | SceneInvalidation::Collision;

} // namespace

SceneLifecycleService::SceneLifecycleService(
    SceneManager& scenes,
    SceneMutationService& mutations,
    SceneActivationSyncFn activationSync)
    : scenes_(scenes),
      mutations_(mutations),
      activationSync_(std::move(activationSync)) {}

void SceneLifecycleService::set_transition_handler(
    SceneTransitionHandlerFn handler) {
    transitionHandler_ = std::move(handler);
}

void SceneLifecycleService::set_gameplay_reset_handler(
    SceneGameplayResetFn handler) {
    gameplayReset_ = std::move(handler);
}

void SceneLifecycleService::set_restore_handler(SceneRestoreFn handler) {
    restoreHandler_ = std::move(handler);
}

SceneTransitionResult SceneLifecycleService::make_success(
    const SceneId& sceneId) {
    SceneTransitionResult result{};
    result.changed = true;
    result.error = SceneTransitionError::None;
    result.sceneId = sceneId;
    result.sceneRevision = mutations_.bump_revision();
    result.invalidations = kActivationInvalidations;
    return result;
}

void SceneLifecycleService::notify_transition(
    const SceneTransitionResult& result) {
    if (transitionHandler_) transitionHandler_(result);
}

bool SceneLifecycleService::commit_scene_refresh(
    const SceneId& sceneId,
    bool restoreFromAuthoring,
    SceneTransitionResult& out)
{
    if (!scenes_.getScene(sceneId)) {
        out.error = SceneTransitionError::SceneNotFound;
        out.sceneId = sceneId;
        return false;
    }

    if (restoreFromAuthoring) {
        if (!restoreHandler_ || !restoreHandler_(sceneId)) {
            out.error = SceneTransitionError::RestoreFailed;
            out.sceneId = sceneId;
            return false;
        }
    }

    if (activationSync_) activationSync_();
    if (gameplayReset_) gameplayReset_();
    out = make_success(sceneId);
    notify_transition(out);
    return true;
}

bool SceneLifecycleService::commit_load(
    const SceneId& sceneId, SceneTransitionResult& out) {
    if (!scenes_.loadScene(sceneId)) {
        out.error = SceneTransitionError::SceneNotFound;
        out.sceneId = sceneId;
        return false;
    }
    return commit_scene_refresh(sceneId, false, out);
}

SceneTransitionResult SceneLifecycleService::reactivate_immediate() {
    cancel_transition();
    const SceneId id = scenes_.activeSceneId();
    SceneTransitionResult result{};
    if (id.empty()) {
        result.error = SceneTransitionError::SceneNotFound;
        return result;
    }
    if (!commit_scene_refresh(id, false, result))
        return result;
    return result;
}

SceneTransitionResult SceneLifecycleService::restart_immediate() {
    cancel_transition();
    const SceneId id = scenes_.activeSceneId();
    SceneTransitionResult result{};
    if (id.empty()) {
        result.error = SceneTransitionError::SceneNotFound;
        return result;
    }
    if (!commit_scene_refresh(id, true, result))
        return result;
    return result;
}

SceneTransitionResult SceneLifecycleService::load_immediate(
    const SceneId& sceneId) {
    cancel_transition();
    SceneTransitionResult result{};
    if (!commit_load(sceneId, result)) return result;
    return result;
}

SceneTransitionResult SceneLifecycleService::request_load(
    const SceneId& sceneId, float fadeSeconds) {
    if (fadeSeconds <= 0.f) return load_immediate(sceneId);

    if (!scenes_.getScene(sceneId)) {
        SceneTransitionResult notFound{};
        notFound.error = SceneTransitionError::SceneNotFound;
        notFound.sceneId = sceneId;
        return notFound;
    }

    pendingSceneId_ = sceneId;
    pendingRestore_ = false;
    fadeDuration_ = fadeSeconds;
    fadeElapsed_ = 0.f;
    fadePhase_ = FadePhase::Out;

    SceneTransitionResult pending{};
    pending.sceneId = sceneId;
    return pending;
}

SceneTransitionResult SceneLifecycleService::request_reactivate(
    float fadeSeconds) {
    if (fadeSeconds <= 0.f) return reactivate_immediate();
    return request_load(scenes_.activeSceneId(), fadeSeconds);
}

SceneTransitionResult SceneLifecycleService::request_restart(
    float fadeSeconds) {
    if (fadeSeconds <= 0.f) return restart_immediate();

    const SceneId id = scenes_.activeSceneId();
    if (id.empty() || !scenes_.getScene(id)) {
        SceneTransitionResult notFound{};
        notFound.error = SceneTransitionError::SceneNotFound;
        notFound.sceneId = id;
        return notFound;
    }

    pendingSceneId_ = id;
    pendingRestore_ = true;
    fadeDuration_ = fadeSeconds;
    fadeElapsed_ = 0.f;
    fadePhase_ = FadePhase::Out;

    SceneTransitionResult pending{};
    pending.sceneId = id;
    return pending;
}

void SceneLifecycleService::tick(float dt) {
    if (fadePhase_ == FadePhase::None) return;

    fadeElapsed_ += dt;
    const float half = fadeDuration_ * 0.5f;

    if (fadePhase_ == FadePhase::Out) {
        if (fadeElapsed_ >= half) {
            SceneTransitionResult result{};
            if (pendingRestore_) {
                commit_scene_refresh(pendingSceneId_, true, result);
                pendingRestore_ = false;
            } else {
                commit_load(pendingSceneId_, result);
            }
            fadePhase_ = FadePhase::In;
            fadeElapsed_ = 0.f;
        }
    } else if (fadePhase_ == FadePhase::In) {
        if (fadeElapsed_ >= half) fadePhase_ = FadePhase::None;
    }
}

float SceneLifecycleService::scene_fade_alpha() const {
    if (fadePhase_ == FadePhase::None || fadeDuration_ <= 0.f) return 0.f;

    const float half = fadeDuration_ * 0.5f;
    if (half <= 0.f) return 0.f;
    if (fadePhase_ == FadePhase::Out)
        return std::min(1.f, fadeElapsed_ / half);
    return std::max(0.f, 1.f - fadeElapsed_ / half);
}

bool SceneLifecycleService::transition_active() const {
    return fadePhase_ != FadePhase::None;
}

void SceneLifecycleService::cancel_transition() {
    fadePhase_ = FadePhase::None;
    fadeDuration_ = 0.f;
    fadeElapsed_ = 0.f;
    pendingSceneId_.clear();
    pendingRestore_ = false;
}

} // namespace ArtCade::Modules
