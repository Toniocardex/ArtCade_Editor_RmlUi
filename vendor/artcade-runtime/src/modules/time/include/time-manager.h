#pragma once

#include "../../../core/module.h"
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>

namespace ArtCade::Modules {

/**
 * TimeManager — delta time, time scale, pause stack, layer temporali, timer.
 *
 * Layer predefiniti: "gameplay", "ui", "audio", "physics", "realtime".
 * Ogni layer ha uno scale indipendente e può essere escluso dalla pausa.
 *
 * Pausa: stack-based (ogni sorgente richiede la propria pausa; la ripresa
 * avviene solo quando TUTTE le sorgenti rilasciano).
 */
class TimeManager final : public IModule {
public:
    TimeManager() = default;

    bool init() override;
    void shutdown() override;

    // Chiamato una volta per frame dal loop principale
    void tick(float realDeltaSeconds);

    // ------------------------------------------------------------------ Time
    float now()     const;  // tempo di gioco scalato (secondi)
    float realNow() const;  // tempo reale non scalato

    // delta corrente per layer (0 se pausa attiva e layer affectedByPause)
    float delta(const std::string& layer = "gameplay") const;

    // ------------------------------------------------------------------ Scale
    // duration == 0 → immediato; duration > 0 → transizione lineare
    void setTimeScale(float scale,
                      const std::string& layer   = "gameplay",
                      float              duration = 0.f);

    float timeScale(const std::string& layer = "gameplay") const;

    // ------------------------------------------------------------------ Pause
    // Restituisce un token opaco da passare a resume()
    uint32_t pause(const std::string& source, int priority = 0);
    void     resume(uint32_t token);
    void     resumeSource(const std::string& source);

    bool isPaused()                              const;
    bool isPauseSourceActive(const std::string& source) const;

    // ------------------------------------------------------------------ Timer
    using TimerCallback = std::function<void()>;

    // Esegue callback una volta dopo `seconds` (rispetta layer temporale)
    uint32_t delay(float seconds, TimerCallback cb,
                   const std::string& layer = "gameplay");

    // Esegue callback ogni `interval` secondi; ritorna un id per cancel()
    uint32_t every(float interval, TimerCallback cb,
                   const std::string& layer = "gameplay");

    void cancelTimer(uint32_t timerId);

private:
    struct TimeLayer {
        float scale         = 1.f;
        float targetScale   = 1.f;
        float transitionDur = 0.f;
        float transitionElapsed = 0.f;
        float startScale    = 1.f;
        bool  affectedByPause = true;
        float elapsed       = 0.f;  // tempo accumulato sul layer
    };

    struct PauseRequest {
        uint32_t    token;
        std::string source;
        int         priority = 0;
    };

    struct Timer {
        uint32_t      id;
        float         interval;
        float         remaining;
        bool          repeat;
        TimerCallback cb;
        std::string   layer;
        bool          cancelled = false;
    };

    std::unordered_map<std::string, TimeLayer> layers_;
    std::vector<PauseRequest>                  pauseStack_;
    std::vector<Timer>                         timers_;

    float    realElapsed_   = 0.f;
    float    gameElapsed_   = 0.f;
    float    realDelta_     = 0.f;
    uint32_t nextToken_     = 1;
    uint32_t nextTimerId_   = 1;

    void initDefaultLayers();
    void updateLayer(TimeLayer& layer, float realDelta);
    void updateTimers(float realDelta);
};

} // namespace ArtCade::Modules
