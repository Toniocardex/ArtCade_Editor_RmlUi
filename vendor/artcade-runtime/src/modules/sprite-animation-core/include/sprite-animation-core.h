#pragma once

#include "types.h"

#include <cstddef>
#include <cstdint>

namespace ArtCade::Animation {

inline constexpr float kMaxAnimationFps = 240.f;

struct AnimationPlaybackCursor {
    std::size_t frameIndex = 0;
    float elapsedSeconds = 0.f;
    float playbackSpeed = 1.f;
    bool playing = false;
    bool completed = false;
};

struct AnimationAdvanceResult {
    AnimationPlaybackCursor cursor;
    bool started = false;
    bool completedThisStep = false;
    std::uint32_t loopsCompleted = 0;
};

// Pure temporal advance. Knows nothing about documents, assets, rects, or I/O.
// effectiveFps = framesPerSecond * cursor.playbackSpeed (caller may pre-scale).
AnimationAdvanceResult advanceAnimation(
    std::size_t frameCount,
    float framesPerSecond,
    AnimationPlaybackMode mode,
    const AnimationPlaybackCursor& cursor,
    float deltaSeconds);

bool isValidAnimationFps(float framesPerSecond);

} // namespace ArtCade::Animation
