#pragma once

#include "dialog-types.h"
#include <string>
#include <vector>

struct Color;
struct Rectangle;

namespace ArtCade::Modules {

class Renderer;

struct DialogRendererConfig {
    float boxX       = 40.f;
    float boxY       = 40.f;
    float boxWidth   = 720.f;
    float boxHeight  = 160.f;
    float padding    = 16.f;
    int   fontSize   = 22;
    int   nameSize   = 18;
    int   choiceSize = 20;
};

class DialogRenderer {
public:
    void setConfig(const DialogRendererConfig& cfg) { config_ = cfg; }

    void draw(Renderer& renderer,
              const std::string& characterName,
              const TypewriterState& tw,
              const std::vector<DialogChoiceOption>* choices,
              int selectedChoice,
              const std::string& portraitPath) const;

private:
    DialogRendererConfig config_;
};

void updateTypewriter(TypewriterState& tw, float dt, float charsPerSecond);

} // namespace ArtCade::Modules
