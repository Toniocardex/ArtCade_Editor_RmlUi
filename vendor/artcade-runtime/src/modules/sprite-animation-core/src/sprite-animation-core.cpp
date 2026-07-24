#include "sprite-animation-core.h"

#include <cmath>

namespace ArtCade::Animation {

bool isValidAnimationFps(float framesPerSecond) {
    return std::isfinite(framesPerSecond)
        && framesPerSecond > 0.f
        && framesPerSecond <= kMaxAnimationFps;
}

AnimationAdvanceResult advanceAnimation(
    std::size_t frameCount,
    float framesPerSecond,
    AnimationPlaybackMode mode,
    const AnimationPlaybackCursor& cursor,
    float deltaSeconds) {
    AnimationAdvanceResult result;
    result.cursor = cursor;

    if (frameCount == 0 || !isValidAnimationFps(framesPerSecond)
        || !std::isfinite(cursor.playbackSpeed) || cursor.playbackSpeed <= 0.f
        || !std::isfinite(deltaSeconds) || deltaSeconds < 0.f) {
        result.cursor.playing = false;
        return result;
    }

    if (result.cursor.frameIndex >= frameCount) {
        result.cursor.frameIndex = frameCount - 1;
    }

    if (!result.cursor.playing || result.cursor.completed) {
        return result;
    }

    const float effectiveFps = framesPerSecond * result.cursor.playbackSpeed;
    const float frameDuration = 1.f / effectiveFps;
    result.cursor.elapsedSeconds += deltaSeconds;

    while (result.cursor.elapsedSeconds >= frameDuration) {
        result.cursor.elapsedSeconds -= frameDuration;
        const std::size_t next = result.cursor.frameIndex + 1;
        if (next < frameCount) {
            result.cursor.frameIndex = next;
            continue;
        }
        if (mode == AnimationPlaybackMode::Loop) {
            result.cursor.frameIndex = 0;
            ++result.loopsCompleted;
            continue;
        }
        // Once: hold final frame and pulse completed once.
        result.cursor.frameIndex = frameCount - 1;
        result.cursor.elapsedSeconds = 0.f;
        result.cursor.playing = false;
        if (!result.cursor.completed) {
            result.cursor.completed = true;
            result.completedThisStep = true;
        }
        break;
    }
    return result;
}

} // namespace ArtCade::Animation
