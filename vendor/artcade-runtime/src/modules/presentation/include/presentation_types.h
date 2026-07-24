#pragma once

#include "../../../core/types.h"

namespace ArtCade::Presentation {

/** Backbuffer / framebuffer pixel (top-left origin, Y-down). */
struct SurfacePoint {
    double x = 0.;
    double y = 0.;
};

/** Game-view / logical viewport pixel (top-left origin, Y-down). */
struct LogicalPoint {
    double x = 0.;
    double y = 0.;
};

/** World authoring space (top-left origin, Y-down). */
struct WorldPoint {
    double x = 0.;
    double y = 0.;
};

/** CSS canvas size + derived framebuffer size. */
struct SurfaceMetrics {
    double cssWidth = 0.;
    double cssHeight = 0.;
    double framebufferWidth = 0.;
    double framebufferHeight = 0.;
    double devicePixelRatio = 1.;
};

/**
 * Where a logical viewport is placed on the framebuffer after output policy.
 * Maps logical pixels to surface pixels (uniform scale except Stretch).
 */
struct OutputPlacement {
    double destX = 0.;
    double destY = 0.;
    double destW = 0.;
    double destH = 0.;
    double scaleX = 1.;
    double scaleY = 1.;
    double srcX = 0.;
    double srcY = 0.;
    double srcW = 0.;
    double srcH = 0.;
};

/** Raylib-style 2D camera parameters used for logical ↔ world mapping. */
struct ViewCamera2D {
    double targetX = 0.;
    double targetY = 0.;
    double offsetX = 0.;
    double offsetY = 0.;
    double zoom = 1.;
};

/** DOM / CSS canvas coordinates (top-left origin, Y-down). */
struct CssPoint {
    double x = 0.;
    double y = 0.;
};

/** Editor navigation state owned by Presentation (pan / zoom in world space). */
struct EditorViewState {
    double positionX = 0.;
    double positionY = 0.;
    double zoom = 1.;
    double rotation = 0.;

    bool panActive = false;
    double panAnchorSurfaceX = 0.;
    double panAnchorSurfaceY = 0.;
    double panAnchorPositionX = 0.;
    double panAnchorPositionY = 0.;
};

/** Authoring camera used in SceneEdit / editor navigation. */
struct EditorCamera {
    double positionX = 0.;
    double positionY = 0.;
    double zoom = 1.;
    double rotation = 0.;
};

/** Simulation-owned game camera (before modifiers). */
struct GameCameraState {
    double positionX = 0.;
    double positionY = 0.;
    double zoom = 1.;
    double rotation = 0.;
};

/** Shake, recoil, cinematic offsets — not surface transforms. */
struct CameraModifiers {
    double translationOffsetX = 0.;
    double translationOffsetY = 0.;
    double zoomMultiplier = 1.;
    double rotationOffset = 0.;
};

/** Composed game camera used for play / preview rendering and picking. */
struct EffectiveGameCamera {
    double positionX = 0.;
    double positionY = 0.;
    double zoom = 1.;
    double rotation = 0.;
};

} // namespace ArtCade::Presentation
