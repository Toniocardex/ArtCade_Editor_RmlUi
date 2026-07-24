#pragma once

#include <string>

namespace ArtCade::Modules {

/**
 * SplashState — Standalone branded splash screen with optional watermark.
 *
 * NOT a GameStateManager state: this is a self-contained overlay driven
 * directly by Application's main loop (update + render).  The engine's
 * GameStateManager is a string-keyed FSM with no render callback, so the
 * splash is rendered by Application::renderActiveScene() instead.
 *
 * Usage (in Application):
 *   if (licenseTier == "free")
 *       splash_ = std::make_unique<SplashState>("free");
 *   ...
 *   if (splash_ && !splash_->isDone()) {
 *       splash_->update(dt);
 *       // render() called after beginFrame() in renderActiveScene()
 *   }
 *
 * Display: 4.5 seconds with fade in/out.
 * FREE tier: shows "MADE WITH ARTCADE" watermark
 * PRO  tier: shows logo only, no watermark
 */
class SplashState {
public:
    explicit SplashState(const std::string& tier = "free");

    // Advance the splash timer.  Call once per fixed step.
    void update(float dt);

    // Draw the splash overlay.  Call between beginFrame()/endFrame().
    void render(int screenWidth, int screenHeight) const;

    // True once the 4.5s splash duration has elapsed.
    bool isDone() const;

private:
    std::string licenseTier_;  // "free" or "pro"
    float       timer_ = 0.0f;
};

} // namespace ArtCade::Modules
