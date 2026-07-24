#include "../include/dialog-renderer.h"
#include "../../renderer/include/renderer.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

namespace {
constexpr Vec4 kNameColor    {1.f, 215.f / 255.f, 0.f, 1.f};
constexpr Vec4 kBodyColor    {1.f, 1.f, 1.f, 1.f};
constexpr Vec4 kChoiceActive {1.f, 1.f, 0.f, 1.f};
constexpr Vec4 kChoiceIdle   {204.f / 255.f, 204.f / 255.f, 204.f / 255.f, 1.f};
constexpr Vec4 kBoxColor     {0.f, 0.f, 0.f, 0.82f};
constexpr float kPortraitSize = 96.f;
constexpr float kPortraitGap  = 14.f;
} // namespace

void updateTypewriter(TypewriterState& tw, float dt, float charsPerSecond) {
    if (tw.complete) return;
    tw.timer += dt;
    const int charsToShow =
        static_cast<int>(std::floor(tw.timer * charsPerSecond));
    const int maxChars = static_cast<int>(tw.fullText.size());
    const int n = std::min(charsToShow, maxChars);
    tw.visibleText = tw.fullText.substr(0, static_cast<std::size_t>(n));
    if (n >= maxChars)
        tw.complete = true;
}

void DialogRenderer::draw(Renderer& renderer,
                          const std::string& characterName,
                          const TypewriterState& tw,
                          const std::vector<DialogChoiceOption>* choices,
                          int selectedChoice,
                          const std::string& portraitPath) const
{
    const float x = config_.boxX;
    float       y = config_.boxY;
    const bool hasPortrait = !portraitPath.empty();
    const float contentX = x + config_.padding
        + (hasPortrait ? kPortraitSize + kPortraitGap : 0.f);

    renderer.drawRect(x, config_.boxY, config_.boxWidth, config_.boxHeight,
                      kBoxColor, true);
    if (hasPortrait) {
        renderer.drawImage(portraitPath,
                           x + config_.padding,
                           config_.boxY + config_.padding,
                           kPortraitSize,
                           kPortraitSize,
                           1.f,
                           true);
    }

    if (!characterName.empty()) {
        renderer.drawText(characterName, contentX, y + config_.padding,
                        config_.nameSize, kNameColor, "", 0, true);
        y += config_.nameSize + 8.f;
    }

    if (!tw.visibleText.empty()) {
        renderer.drawText(tw.visibleText,
                          contentX,
                          y + config_.padding + config_.nameSize,
                          config_.fontSize,
                          kBodyColor,
                          "",
                          0,
                          true);
    }

    if (choices && !choices->empty()) {
        float cy = config_.boxY + config_.boxHeight - config_.padding
                   - static_cast<float>(choices->size()) * (config_.choiceSize + 6.f);
        for (int i = 0; i < static_cast<int>(choices->size()); ++i) {
            const std::string prefix =
                (i == selectedChoice) ? "> " : "  ";
            const std::string line = prefix + (*choices)[static_cast<std::size_t>(i)].text;
            const Vec4& color =
                (i == selectedChoice) ? kChoiceActive : kChoiceIdle;
            renderer.drawText(line,
                              contentX,
                              cy,
                              config_.choiceSize,
                              color,
                              "",
                              0,
                              true);
            cy += config_.choiceSize + 6.f;
        }
    }
}

} // namespace ArtCade::Modules
