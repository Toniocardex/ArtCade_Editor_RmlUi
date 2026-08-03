#pragma once

#include "../../../core/module.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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

    // scala globale (0 = pausa implicita, 1 = normale, 2 = doppia velocità)
    void  setTimeScale(float scale);
    float timeScale() const { return globalScale_; }

    // ----------------------------------------------------------------- Pausa
    // Ogni chiamante conserva il token e lo passa a resume().
    uint32_t pause(const std::string& source, int priority = 0);
    void     resume(uint32_t token);
    bool     isPaused() const { return !pauseStack_.empty(); }

    // --------------------------------------------------------- Layer temporali
    void  createLayer(const std::string& name, float scale = 1.f,
                      bool affectedByPause = true);
    void  setLayerScale(const std::string& name, float scale);
    float layerScale(const std::string& name) const;

    // ------------------------------------------------------------------ Timer
    using TimerCallback = std::function<void()>;

    uint32_t delay(float seconds, TimerCallback cb,
                   const std::string& layer = "gameplay");

    // Ripete ogni interval secondi. Restituisce id cancellabile.
    uint32_t every(float interval, TimerCallback cb,
                   const std::string& layer = "gameplay");

    void cancelTimer(uint32_t timerId);

private:
    struct TimeLayer {
        float scale = 1.f;
        bool  affectedByPause = true;
        float currentDelta = 0.f;
    };

    struct PauseRequest {
        uint32_t    token;
        std::string source;
        int         priority;
    };

    struct Timer {
        uint32_t      id;
        float         remaining;
        float         interval;
        bool          repeating;
        std::string   layer;
        TimerCallback callback;
        bool          cancelled = false;
    };

    float realNow_ = 0.f;
    float gameNow_ = 0.f;
    float globalScale_ = 1.f;

    std::unordered_map<std::string, TimeLayer> layers_;
    std::vector<PauseRequest> pauseStack_;
    std::vector<Timer> timers_;

    uint32_t nextToken_ = 1;
    uint32_t nextTimerId_ = 1;

    bool pausedByStack() const { return !pauseStack_.empty(); }
    void updateTimers();
};

} // namespace ArtCade::Modules
