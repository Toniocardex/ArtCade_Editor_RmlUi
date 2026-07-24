#pragma once

#include "../../../core/module.h"
#include <functional>
#include <vector>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * TweenManager — numeric property tweening with easing and sequencing.
 *
 * A tween animates a float value from `from` to `to` over `duration` seconds
 * using a named easing function.  An onUpdate callback receives the current
 * value each frame; an optional onComplete fires when the tween finishes.
 *
 * Tweens can be:
 *   - looped (restarts from the beginning)
 *   - ping-ponged (alternates forward/backward)
 *   - chained via onComplete to build sequences
 *
 * All tweens run on the "gameplay" time scale (no Raylib, no engine context).
 * The caller is responsible for passing the correct dt from the time layer it wants.
 */
class TweenManager final : public IModule {
public:
    TweenManager() = default;

    bool init()     override;
    void shutdown() override;

    // ------------------------------------------------------------------ easing

    enum class Ease {
        Linear,
        QuadIn,  QuadOut,  QuadInOut,
        CubicIn, CubicOut, CubicInOut,
        SineIn,  SineOut,  SineInOut,
        ElasticOut,
        BounceOut,
        BackOut,
    };

    // ------------------------------------------------------------------ tween handle

    using TweenId    = uint32_t;
    using UpdateCb   = std::function<void(float value)>;
    using CompleteCb = std::function<void()>;

    // ------------------------------------------------------------------ create

    struct Params {
        float      from       = 0.f;
        float      to         = 1.f;
        float      duration   = 1.f;
        Ease       ease       = Ease::Linear;
        float      delay      = 0.f;
        bool       loop       = false;
        bool       pingPong   = false;
        UpdateCb   onUpdate;
        CompleteCb onComplete;
    };

    TweenId tween(Params params);

    // Convenience wrappers
    TweenId tweenTo(float from, float to, float duration, Ease ease,
                    UpdateCb onUpdate, CompleteCb onComplete = {});

    // ------------------------------------------------------------------ control

    void pause (TweenId id);
    void resume(TweenId id);
    void cancel(TweenId id);

    // Cancel all tweens; onComplete is NOT fired
    void cancelAll();

    bool isActive(TweenId id) const;

    // ------------------------------------------------------------------ update

    void update(float dt);

    // ------------------------------------------------------------------ static easing utility

    static float applyEase(Ease ease, float t);  // t in [0,1] → value in [0,1]

private:
    enum class State { Active, Paused, Done };

    struct TweenEntry {
        TweenId    id;
        float      from, to;
        float      duration;
        Ease       ease;
        float      delay;
        bool       loop;
        bool       pingPong;
        bool       reverse = false;   // ping-pong direction
        float      elapsed = 0.f;
        float      delayRemaining;
        State      state   = State::Active;
        UpdateCb   onUpdate;
        CompleteCb onComplete;
    };

    std::vector<TweenEntry> tweens_;
    TweenId nextId_ = 1;
};

} // namespace ArtCade::Modules
