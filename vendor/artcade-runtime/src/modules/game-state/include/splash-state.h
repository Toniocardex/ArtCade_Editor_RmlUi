#pragma once

#include <string>

namespace ArtCade::Modules {

/**
 * SplashState — Standalone branded splash screen with optional watermark.
 *
 * NOT a GameStateManager state: this is a self-contained presentation
 * component driven by Application's GameplayStartupPhase state machine
 * (ADR-0039), not the engine's gameplay FSM. GameStateManager is a
 * string-keyed FSM with no render callback and is ticked from inside
 * gameplay simulation (GameplaySession::tickFixedStep) — exactly what must
 * not run while the splash is showing.
 *
 * Ownership and lifecycle (ADR-0039 §5): Application holds
 * `std::unique_ptr<SplashState> splash_` and drives it only from its
 * `Splash` phase:
 *
 *   if (splash_->update(presentationDt)) {
 *       startupPhase_ = GameplayStartupPhase::Activating;
 *   }
 *   // render() called between beginFrame()/endFrame() while the phase is Splash
 *
 * update() is edge-triggered: it returns true exactly once, on the update
 * that crosses the completion threshold. There is no isDone() — the loop
 * transitions off the return value, not a separate poll, so there is only
 * one place that can decide "the splash is over."
 *
 * Display: 4.5 seconds with fade in/out.
 * FREE tier: shows "MADE WITH ARTCADE" watermark
 * PRO  tier: shows logo only, no watermark
 */
class SplashState {
public:
    explicit SplashState(const std::string& tier = "free");

    // Advance once per rendered frame using unscaled presentation time.
    // Independent from gameplay simulation. Rejects non-finite/non-positive
    // dt (no-op, returns false) and internally clamps a single update to a
    // maximum presentation step, so a suspended process or a paused
    // debugger cannot complete the splash instantly on resume. Returns true
    // exactly once, on the update that reaches completion.
    bool update(float presentationDt);

    // Draw the splash overlay. Call between beginFrame()/endFrame().
    void render(int screenWidth, int screenHeight) const;

private:
    std::string licenseTier_;  // "free" or "pro"
    float       timer_ = 0.0f;
    bool        completionReported_ = false;
};

} // namespace ArtCade::Modules
