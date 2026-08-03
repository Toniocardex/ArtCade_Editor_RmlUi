#pragma once

#include "../../../core/module.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade::Modules {

/**
 * TimeManager — delta time, time scale, pause stack, temporal layers, timers.
 *
 * The implementation in time-manager.cpp is authoritative for the public API
 * and storage layout. This header deliberately declares every member used by
 * that implementation instead of relying on transitive includes or stale
 * compatibility fields.
 */
class TimeManager final : public IModule {
public:
    TimeManager() = default;

    bool init() override;
    void shutdown() override;

    void tick(float realDeltaSeconds);

    float now() const;
    float realNow() const;
    float delta(const std::string& layer = "gameplay") const;

    void setTimeScale(float scale,
                      const std::string& layer = "gameplay",
                      float duration = 0.f);
    float timeScale(const std::string& layer = "gameplay") const;

    uint32_t pause(const std::string& source, int priority = 0);
    void resume(uint32_t token);
    void resumeSource(const std::string& source);
    bool isPaused() const;
    bool isPauseSourceActive(const std::string& source) const;

    using TimerCallback = std::function<void()>;

    uint32_t delay(float seconds,
                   TimerCallback callback,
                   const std::string& layer = "gameplay");
    uint32_t every(float interval,
                   TimerCallback callback,
                   const std::string& layer = "gameplay");
    void cancelTimer(uint32_t timerId);

private:
    struct TimeLayer {
        float scale = 1.f;
        float targetScale = 1.f;
        float transitionDur = 0.f;
        float transitionElapsed = 0.f;
        float startScale = 1.f;
        bool affectedByPause = true;
        float elapsed = 0.f;
    };

    struct PauseRequest {
        uint32_t token = 0;
        std::string source;
        int priority = 0;
    };

    struct Timer {
        uint32_t id = 0;
        float remaining = 0.f;
        float interval = 0.f;
        bool repeat = false;
        TimerCallback cb;
        std::string layer;
        bool cancelled = false;
    };

    void initDefaultLayers();
    void updateLayer(TimeLayer& layer, float realDelta);
    void updateTimers(float realDelta);

    float realElapsed_ = 0.f;
    float gameElapsed_ = 0.f;
    float realDelta_ = 0.f;

    std::unordered_map<std::string, TimeLayer> layers_;
    std::vector<PauseRequest> pauseStack_;
    std::vector<Timer> timers_;

    uint32_t nextToken_ = 1;
    uint32_t nextTimerId_ = 1;
};

} // namespace ArtCade::Modules
