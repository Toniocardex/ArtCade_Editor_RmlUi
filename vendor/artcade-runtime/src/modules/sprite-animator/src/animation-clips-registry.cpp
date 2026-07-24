#include "../include/animation-clips-registry.h"
#include "../include/sprite-animator.h"

#include <cmath>
#include <unordered_set>

namespace ArtCade {

namespace {

void defineClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<ImageAssetDef>& imageAssets)
{
    std::unordered_set<std::string> assetsWithFirstFrame;
    for (const ImageAssetDef& asset : imageAssets) {
        for (const AnimationClipDef& def : asset.clips) {
            if (def.name.empty() || def.frames.empty()) continue;
            Modules::SpriteAnimator::Clip clip;
            clip.name = def.name;
            clip.fps  = def.fps > 0.f ? def.fps : 12.f;
            clip.loop = def.loop;
            clip.assetId = asset.assetId;
            clip.frames.reserve(def.frames.size());
            for (const AnimationFrameRect& r : def.frames) {
                if (r.w <= 0.f || r.h <= 0.f) continue;
                clip.frames.push_back({
                    static_cast<int>(r.x),
                    static_cast<int>(r.y),
                    static_cast<int>(r.w),
                    static_cast<int>(r.h),
                });
            }
            if (!clip.frames.empty()) {
                if (!asset.assetId.empty() && assetsWithFirstFrame.count(asset.assetId) == 0) {
                    animator.setFirstFrameForAsset(asset.assetId, clip.frames.front());
                    assetsWithFirstFrame.insert(asset.assetId);
                }
                animator.defineClip(clip);
            }
        }
    }
}

} // namespace

void registerAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<ImageAssetDef>& imageAssets)
{
    animator.clearClips();
    defineClipsFromAssets(animator, imageAssets);
}

void appendAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<SpriteAnimationAssetDef>& animationAssets)
{
    for (const SpriteAnimationAssetDef& asset : animationAssets) {
        if (asset.id.empty() || asset.sourceImageAssetId.empty()) continue;
        for (const SpriteAnimationClipDef& def : asset.clips) {
            if (def.id.empty() || def.frameIds.empty()
                || !std::isfinite(def.framesPerSecond) || def.framesPerSecond <= 0.f) {
                continue;
            }
            Modules::SpriteAnimator::Clip clip;
            clip.name = def.id;
            clip.animationAssetId = asset.id;
            clip.assetId = asset.sourceImageAssetId;
            clip.fps = def.framesPerSecond;
            clip.loop = def.playbackMode == AnimationPlaybackMode::Loop;
            clip.frames.reserve(def.frameIds.size());
            for (const SpriteFrameId& frameId : def.frameIds) {
                const SpriteFrameDef* frame = nullptr;
                for (const SpriteFrameDef& candidate : asset.frames) {
                    if (candidate.id == frameId) { frame = &candidate; break; }
                }
                if (!frame || frame->width <= 0 || frame->height <= 0) continue;
                clip.frames.push_back({frame->x, frame->y, frame->width, frame->height});
            }
            if (clip.frames.empty()) continue;
            if (animator.firstFrameForAsset(asset.sourceImageAssetId).w <= 0)
                animator.setFirstFrameForAsset(asset.sourceImageAssetId, clip.frames.front());
            animator.defineClip(clip);
        }
    }
}

void replaceAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<ImageAssetDef>& imageAssets)
{
    std::unordered_set<std::string> keep;
    for (const ImageAssetDef& asset : imageAssets) {
        for (const AnimationClipDef& def : asset.clips) {
            if (!def.name.empty() && !def.frames.empty())
                keep.insert(def.name);
        }
    }
    animator.removeClipsExcept(keep);
    animator.clearFirstFramesByAsset();
    defineClipsFromAssets(animator, imageAssets);
}

} // namespace ArtCade
