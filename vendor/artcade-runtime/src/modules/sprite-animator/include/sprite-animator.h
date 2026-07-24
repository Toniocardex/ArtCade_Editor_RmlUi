#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * SpriteAnimator — clip-based sprite sheet animation system.
 *
 * A "clip" is a named sequence of frame rectangles (all from one texture)
 * with a playback FPS and loop flag.
 *
 * Each entity has an independent AnimInstance that tracks:
 *   - which clip is playing
 *   - elapsed time within that clip
 *   - current frame index
 *   - playback state (playing / paused / stopped)
 *
 * update(dt) advances all active instances.
 *
 * The system is pure CPU math — it does not call Raylib.  The renderer
 * reads currentFrame() and draws the appropriate subrect of the texture.
 */
class SpriteAnimator final : public IModule {
public:
    SpriteAnimator() = default;

    bool init()     override;
    void shutdown() override;

    // ------------------------------------------------------------------ clip definition

    struct Frame {
        int x, y, w, h;      // pixel subrect on the sprite sheet
    };

    struct Clip {
        std::string        name;
        std::vector<Frame> frames;
        float              fps    = 12.f;
        bool               loop   = true;
        /** Owning authoring animation asset. Empty for legacy clips. */
        std::string        animationAssetId;
        /** Sprite sheet this clip's frame rects belong to. The renderer draws
         *  THIS sheet while the clip plays, so an entity can animate across
         *  sheets without its frames being sliced from the wrong texture. */
        std::string        assetId;
    };

    void defineClip(const Clip& clip);
    bool hasClip(const std::string& name) const;
    bool hasClip(const AssetId& animationAssetId, const std::string& clipId) const;
    bool isClipPlayable(const AssetId& animationAssetId,
                        const std::string& clipId) const;
    /** Remove clip definitions whose names are not in keep (instances may still reference them). */
    void removeClipsExcept(const std::unordered_set<std::string>& keep);
    void clearClips();

    // ------------------------------------------------------------------ instance control

    using EntityId = uint32_t;
    using FinishCb = std::function<void(EntityId, const std::string& clipName)>;

    // Start playing a clip on an entity; resets the instance if already playing
    void play(EntityId entity, const std::string& clipName,
              FinishCb onFinish = {});
    bool play(EntityId entity, const AssetId& animationAssetId,
              const std::string& clipId, FinishCb onFinish = {});

    void pause (EntityId entity);
    void resume(EntityId entity);
    void stop  (EntityId entity);
    bool setPlaybackSpeed(EntityId entity, float speed);

    // Jump to a specific frame (0-based)
    void seekFrame(EntityId entity, int frame);

    // Advance all active instances
    void update(float dt);

    // ------------------------------------------------------------------ query

    // Current source subrect to draw (zeroed if no active instance)
    Frame currentFrame(EntityId entity) const;

    /** Source subrect for an authored clip frame (zeroed if missing/out of range). */
    Frame clipFrame(const std::string& clipName, int frameIdx) const;

    /** Sprite sheet a clip's frames belong to ("" if the clip is unknown). */
    std::string clipAssetId(const std::string& clipName) const;
    std::string clipAssetId(const AssetId& animationAssetId,
                            const std::string& clipId) const;

    /** Sheet of the entity's currently playing clip ("" if no active instance). */
    std::string currentClipAssetId(EntityId entity) const;

    /** First authored frame for a sprite sheet asset (zeroed if the asset has no clips). */
    Frame firstFrameForAsset(const std::string& assetId) const;

    /** Clear asset fallback frames; clip definitions and playback instances are kept. */
    void clearFirstFramesByAsset();

    /** Store the static preview frame used when an entity has a sheet but no active clip. */
    void setFirstFrameForAsset(const std::string& assetId, const Frame& frame);

    // Name of the playing clip ("" if stopped)
    std::string currentClip(EntityId entity) const;

    bool isPlaying(EntityId entity) const;
    int  frameIndex(EntityId entity) const;
    float playbackSpeed(EntityId entity) const;

    struct FinishEvent {
        EntityId    entityId = 0;
        std::string clipName;
    };

    /** Drain animation-finished events since last poll (non-loop clips only). */
    std::vector<FinishEvent> pollFinished();

    // ------------------------------------------------------------------ generic anim events

    /**
     * Non-finish playback events surfaced to the Logic Board:
     *   Start  — a clip began playing (play() called)
     *   Frame  — the frame index advanced (carries the new frameIdx)
     *   Loop   — a looping clip wrapped past its last frame
     *   Change — play() switched to a clip different from the one playing
     * Finish keeps its own buffer (pollFinished) for renderer/back-compat.
     */
    enum class AnimEventKind { Start, Frame, Loop, Change };

    struct AnimEvent {
        AnimEventKind kind;
        EntityId      entityId = 0;
        std::string   clipName;
        int           frameIdx = 0;
    };

    /** Bit for an AnimEventKind within a watched-kinds mask. */
    static constexpr uint32_t animEventBit(AnimEventKind kind) {
        return 1u << static_cast<uint32_t>(kind);
    }

    /**
     * Restrict which AnimEvent kinds update()/play() record. A kind whose bit
     * is clear is never pushed, so unwatched kinds cost nothing on the
     * per-frame path. The host sets this each frame from the set of registered
     * Logic Board handlers; defaults to every kind enabled.
     * @param mask  OR of animEventBit(kind) for each kind to record.
     */
    void setWatchedEventKinds(uint32_t mask);

    /** Drain Start/Frame/Loop/Change events accumulated since the last poll. */
    std::vector<AnimEvent> pollEvents();

    // Remove instance data for an entity (e.g. on entity destroy)
    void removeEntity(EntityId entity);

    /** Drop all per-entity playback state; clip definitions are kept. */
    void clearInstances();

private:
    enum class PlayState { Stopped, Playing, Paused };

    struct AnimInstance {
        std::string clipKey;
        int         frameIdx  = 0;
        float       elapsed   = 0.f;
        float       playbackSpeed = 1.f;
        PlayState   state     = PlayState::Stopped;
        FinishCb    onFinish;
    };

    static std::string clipKey(const AssetId& animationAssetId,
                               const std::string& clipId);
    const Clip* findClip(const std::string& key) const;
    void playByKey(EntityId entity, const std::string& key, FinishCb onFinish);

    /** Record an AnimEvent only if its kind is currently watched. */
    void pushEvent(AnimEventKind kind, EntityId entity,
                   const std::string& clipName, int frameIdx);

    std::unordered_map<std::string, Clip>        clips_;
    std::unordered_map<std::string, Frame>       firstFramesByAsset_;
    std::unordered_map<EntityId, AnimInstance>   instances_;
    std::vector<FinishEvent>                   finishBuffer_;
    std::vector<AnimEvent>                      eventBuffer_;
    uint32_t                                   watchedEventKinds_ = ~0u;
};

} // namespace ArtCade::Modules
