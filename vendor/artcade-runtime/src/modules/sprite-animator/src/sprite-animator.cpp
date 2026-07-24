#include "../include/sprite-animator.h"

#include "sprite-animation-core.h"

#include <cmath>
#include <unordered_set>

namespace ArtCade::Modules {

bool SpriteAnimator::init() {
    clips_.clear();
    firstFramesByAsset_.clear();
    instances_.clear();
    return true;
}

void SpriteAnimator::shutdown() {
    clips_.clear();
    firstFramesByAsset_.clear();
    instances_.clear();
    finishBuffer_.clear();
    eventBuffer_.clear();
}

std::vector<SpriteAnimator::FinishEvent> SpriteAnimator::pollFinished() {
    std::vector<FinishEvent> out;
    out.swap(finishBuffer_);
    return out;
}

std::vector<SpriteAnimator::AnimEvent> SpriteAnimator::pollEvents() {
    std::vector<AnimEvent> out;
    out.swap(eventBuffer_);
    return out;
}

void SpriteAnimator::setWatchedEventKinds(uint32_t mask) {
    watchedEventKinds_ = mask;
}

void SpriteAnimator::pushEvent(AnimEventKind kind, EntityId entity,
                               const std::string& clipName, int frameIdx) {
    if (watchedEventKinds_ & animEventBit(kind))
        eventBuffer_.push_back({ kind, entity, clipName, frameIdx });
}

// ------------------------------------------------------------------ clip definition

void SpriteAnimator::defineClip(const Clip& clip) {
    clips_[clipKey(clip.animationAssetId, clip.name)] = clip;
}

bool SpriteAnimator::hasClip(const std::string& name) const {
    return clips_.count(name) > 0;
}

std::string SpriteAnimator::clipKey(const AssetId& animationAssetId,
                                    const std::string& clipId) {
    if (animationAssetId.empty()) return clipId;
    return animationAssetId + "\x1f" + clipId;
}

const SpriteAnimator::Clip* SpriteAnimator::findClip(const std::string& key) const {
    const auto it = clips_.find(key);
    return it == clips_.end() ? nullptr : &it->second;
}

bool SpriteAnimator::hasClip(const AssetId& animationAssetId,
                             const std::string& clipId) const {
    return findClip(clipKey(animationAssetId, clipId)) != nullptr;
}

bool SpriteAnimator::isClipPlayable(const AssetId& animationAssetId,
                                    const std::string& clipId) const {
    const Clip* clip = findClip(clipKey(animationAssetId, clipId));
    return clip && !clip->frames.empty() && std::isfinite(clip->fps) && clip->fps > 0.f;
}

void SpriteAnimator::clearClips() {
    clips_.clear();
    firstFramesByAsset_.clear();
}

void SpriteAnimator::removeClipsExcept(const std::unordered_set<std::string>& keep) {
    for (auto it = clips_.begin(); it != clips_.end(); ) {
        // This API belongs to the legacy image-asset hot-sync path. Current
        // animation assets use composite keys and are reconciled separately.
        if (it->second.animationAssetId.empty() && keep.count(it->first) == 0)
            it = clips_.erase(it);
        else
            ++it;
    }
}

// ------------------------------------------------------------------ instance control

void SpriteAnimator::play(EntityId entity, const std::string& clipName, FinishCb onFinish) {
    playByKey(entity, clipName, std::move(onFinish));
}

bool SpriteAnimator::play(EntityId entity, const AssetId& animationAssetId,
                          const std::string& clipId, FinishCb onFinish) {
    const std::string key = clipKey(animationAssetId, clipId);
    if (!isClipPlayable(animationAssetId, clipId)) return false;
    playByKey(entity, key, std::move(onFinish));
    return true;
}

void SpriteAnimator::playByKey(EntityId entity, const std::string& key,
                               FinishCb onFinish) {
    const Clip* nextClip = findClip(key);
    if (!nextClip) return;
    // Capture the previously playing clip (if any) before we overwrite the
    // instance, so we can surface a Change event on a real clip switch.
    std::string prevKey;
    auto prevIt = instances_.find(entity);
    if (prevIt != instances_.end() && prevIt->second.state != PlayState::Stopped) {
        prevKey = prevIt->second.clipKey;
    }

    // Idempotent re-play: a held-key rule fires "play(clip)" every frame. If the
    // entity is already playing that same clip, keep it advancing instead of
    // resetting to frame 0 each frame (which would freeze the animation).
    if (prevIt != instances_.end()
        && prevIt->second.state == PlayState::Playing
        && prevIt->second.clipKey == key) {
        return;
    }

    AnimInstance inst;
    inst.clipKey  = key;
    inst.frameIdx = 0;
    inst.elapsed  = 0.f;
    if (prevIt != instances_.end()) inst.playbackSpeed = prevIt->second.playbackSpeed;
    inst.state    = PlayState::Playing;
    inst.onFinish = std::move(onFinish);
    instances_[entity] = std::move(inst);

    pushEvent(AnimEventKind::Start, entity, nextClip->name, 0);
    if (!prevKey.empty() && prevKey != key)
        pushEvent(AnimEventKind::Change, entity, nextClip->name, 0);
}

void SpriteAnimator::pause(EntityId entity) {
    auto it = instances_.find(entity);
    if (it != instances_.end() && it->second.state == PlayState::Playing)
        it->second.state = PlayState::Paused;
}

void SpriteAnimator::resume(EntityId entity) {
    auto it = instances_.find(entity);
    if (it != instances_.end() && it->second.state == PlayState::Paused)
        it->second.state = PlayState::Playing;
}

void SpriteAnimator::stop(EntityId entity) {
    auto it = instances_.find(entity);
    if (it != instances_.end())
        it->second.state = PlayState::Stopped;
}

bool SpriteAnimator::setPlaybackSpeed(EntityId entity, float speed) {
    if (!std::isfinite(speed) || speed <= 0.f) return false;
    instances_[entity].playbackSpeed = speed;
    return true;
}

void SpriteAnimator::seekFrame(EntityId entity, int frame) {
    auto iit = instances_.find(entity);
    if (iit == instances_.end()) return;

    auto cit = clips_.find(iit->second.clipKey);
    if (cit == clips_.end()) return;

    const Clip& clip = cit->second;
    int count = static_cast<int>(clip.frames.size());
    if (count == 0) return;

    iit->second.frameIdx = frame % count;
    iit->second.elapsed  = 0.f;
}

// ------------------------------------------------------------------ update

void SpriteAnimator::update(float dt) {
    for (auto& [entity, inst] : instances_) {
        if (inst.state != PlayState::Playing) continue;

        auto cit = clips_.find(inst.clipKey);
        if (cit == clips_.end()) continue;

        const Clip& clip  = cit->second;
        const std::size_t count = clip.frames.size();
        if (count == 0) continue;

        Animation::AnimationPlaybackCursor cursor;
        cursor.frameIndex = inst.frameIdx >= 0
            ? static_cast<std::size_t>(inst.frameIdx) : 0;
        cursor.elapsedSeconds = inst.elapsed;
        cursor.playbackSpeed = inst.playbackSpeed;
        cursor.playing = true;
        cursor.completed = false;
        const AnimationPlaybackMode mode = clip.loop
            ? AnimationPlaybackMode::Loop : AnimationPlaybackMode::Once;
        const Animation::AnimationAdvanceResult advanced = Animation::advanceAnimation(
            count, clip.fps, mode, cursor, dt);

        const int previousFrame = inst.frameIdx;
        inst.frameIdx = static_cast<int>(advanced.cursor.frameIndex);
        inst.elapsed = advanced.cursor.elapsedSeconds;
        if (!advanced.cursor.playing) {
            inst.state = PlayState::Stopped;
        }
        for (std::uint32_t i = 0; i < advanced.loopsCompleted; ++i) {
            pushEvent(AnimEventKind::Loop, entity, clip.name, 0);
            pushEvent(AnimEventKind::Frame, entity, clip.name, 0);
        }
        if (advanced.completedThisStep) {
            finishBuffer_.push_back({ entity, clip.name });
            if (inst.onFinish) inst.onFinish(entity, clip.name);
        } else if (advanced.cursor.playing && inst.frameIdx != previousFrame
                   && advanced.loopsCompleted == 0) {
            pushEvent(AnimEventKind::Frame, entity, clip.name, inst.frameIdx);
        }
    }
}

// ------------------------------------------------------------------ query

SpriteAnimator::Frame SpriteAnimator::currentFrame(EntityId entity) const {
    auto iit = instances_.find(entity);
    if (iit == instances_.end()) return {};

    auto cit = clips_.find(iit->second.clipKey);
    if (cit == clips_.end()) return {};

    const auto& frames = cit->second.frames;
    if (frames.empty()) return {};

    int idx = iit->second.frameIdx;
    if (idx < 0 || idx >= static_cast<int>(frames.size())) return {};
    return frames[idx];
}

SpriteAnimator::Frame SpriteAnimator::clipFrame(const std::string& clipName, int frameIdx) const {
    auto cit = clips_.find(clipName);
    if (cit == clips_.end()) return {};

    const auto& frames = cit->second.frames;
    if (frames.empty()) return {};

    if (frameIdx < 0 || frameIdx >= static_cast<int>(frames.size())) return {};
    return frames[static_cast<size_t>(frameIdx)];
}

std::string SpriteAnimator::clipAssetId(const std::string& clipName) const {
    auto cit = clips_.find(clipName);
    return (cit != clips_.end()) ? cit->second.assetId : std::string{};
}

std::string SpriteAnimator::clipAssetId(const AssetId& animationAssetId,
                                        const std::string& clipId) const {
    const Clip* clip = findClip(clipKey(animationAssetId, clipId));
    return clip ? clip->assetId : std::string{};
}

std::string SpriteAnimator::currentClipAssetId(EntityId entity) const {
    // Mirror currentFrame(): a finished non-loop clip still shows its last
    // frame, so its sheet must stay selected regardless of play state.
    auto iit = instances_.find(entity);
    if (iit == instances_.end()) return {};
    const Clip* clip = findClip(iit->second.clipKey);
    return clip ? clip->assetId : std::string{};
}

SpriteAnimator::Frame SpriteAnimator::firstFrameForAsset(const std::string& assetId) const {
    auto it = firstFramesByAsset_.find(assetId);
    if (it == firstFramesByAsset_.end()) return {};
    return it->second;
}

void SpriteAnimator::clearFirstFramesByAsset() {
    firstFramesByAsset_.clear();
}

void SpriteAnimator::setFirstFrameForAsset(const std::string& assetId, const Frame& frame) {
    if (assetId.empty() || frame.w <= 0 || frame.h <= 0) return;
    firstFramesByAsset_[assetId] = frame;
}

std::string SpriteAnimator::currentClip(EntityId entity) const {
    auto it = instances_.find(entity);
    if (it == instances_.end() || it->second.state == PlayState::Stopped)
        return "";
    const Clip* clip = findClip(it->second.clipKey);
    return clip ? clip->name : std::string{};
}

bool SpriteAnimator::isPlaying(EntityId entity) const {
    auto it = instances_.find(entity);
    return (it != instances_.end()) && (it->second.state == PlayState::Playing);
}

int SpriteAnimator::frameIndex(EntityId entity) const {
    auto it = instances_.find(entity);
    return (it != instances_.end()) ? it->second.frameIdx : -1;
}

float SpriteAnimator::playbackSpeed(EntityId entity) const {
    const auto it = instances_.find(entity);
    return it != instances_.end() ? it->second.playbackSpeed : 1.f;
}

void SpriteAnimator::removeEntity(EntityId entity) {
    instances_.erase(entity);
}

void SpriteAnimator::clearInstances() {
    instances_.clear();
    finishBuffer_.clear();
    eventBuffer_.clear();
}

} // namespace ArtCade::Modules
