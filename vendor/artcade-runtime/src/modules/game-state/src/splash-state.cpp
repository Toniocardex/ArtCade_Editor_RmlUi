#include "../include/splash-state.h"

#include <raylib.h>
#include <algorithm>

namespace ArtCade::Modules {

namespace {
constexpr float kSplashDuration = 4.5f;
constexpr float kFadeDuration   = 0.5f;
}

SplashState::SplashState(const std::string& tier)
    : licenseTier_(tier) {}

void SplashState::update(float dt) {
    timer_ += dt;
}

bool SplashState::isDone() const {
    return timer_ >= kSplashDuration;
}

void SplashState::render(int screenWidth, int screenHeight) const {
    // Alpha fade in/out for smooth transition
    float alpha = 1.0f;
    if (timer_ < kFadeDuration) {
        alpha = timer_ / kFadeDuration;                       // fade in
    } else if (timer_ > kSplashDuration - kFadeDuration) {
        alpha = (kSplashDuration - timer_) / kFadeDuration;   // fade out
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    const auto a8 = [alpha](int v) -> unsigned char {
        return static_cast<unsigned char>(v * alpha);
    };

    // Opaque black backdrop covers the game frame
    DrawRectangle(0, 0, screenWidth, screenHeight,
                  Color{0, 0, 0, a8(255)});

    const float cx = screenWidth  / 2.0f;
    const float cy = screenHeight / 2.0f - 60.0f;

    // Logo text
    const Color logoColor{a8(255), a8(255), a8(255), a8(255)};
    DrawText("ARTCADE", static_cast<int>(cx - 120),
             static_cast<int>(cy - 40), 80, logoColor);

    // Watermark — FREE tier only
    if (licenseTier_ == "free") {
        const Color wmColor{a8(200), a8(200), a8(200), a8(200)};
        DrawText("MADE WITH ARTCADE", static_cast<int>(cx - 150),
                 static_cast<int>(cy + 80), 24, wmColor);
    }

    // Edition label
    const Color infoColor{a8(150), a8(150), a8(150), a8(200)};
    const char* tierLabel =
        (licenseTier_ == "free") ? "Free Edition" : "Pro Edition";
    DrawText(tierLabel, static_cast<int>(cx - 60),
             static_cast<int>(cy + 130), 16, infoColor);
}

} // namespace ArtCade::Modules
