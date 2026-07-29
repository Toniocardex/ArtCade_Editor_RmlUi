#pragma once

#include "../../core/types.h"

namespace ArtCade {

namespace Modules { class Renderer; }

/** Presentation values shared by the native player's scene render passes. */
struct EditorOverlayState {
    bool inEditMode = false;
    bool guidesEnabled = false;
    float gridSize = 32.f;
    EntityId selectedId = 0u;
};

namespace EditorOverlayRenderer {

void drawBackdrop(Modules::Renderer& renderer,
                  const Vec4& backgroundColor,
                  const EditorOverlayState& state);

void drawGrid(Modules::Renderer& renderer,
              const Vec2& worldSize,
              const EditorOverlayState& state);

void drawBackdrop(Modules::Renderer& renderer,
                  const SceneDef& scene,
                  const EditorOverlayState& state);

void drawGrid(Modules::Renderer& renderer,
              const SceneDef& scene,
              const EditorOverlayState& state);

} // namespace EditorOverlayRenderer
} // namespace ArtCade
