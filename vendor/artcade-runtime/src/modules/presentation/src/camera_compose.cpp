#include "../include/camera_compose.h"

namespace ArtCade::Presentation {

EffectiveGameCamera compose_effective_game_camera(
    const GameCameraState& base,
    const CameraModifiers& modifiers) {
    const double zoom = (base.zoom > 0.) ? base.zoom : 1.;
    const double zoomMul = (modifiers.zoomMultiplier > 0.)
        ? modifiers.zoomMultiplier
        : 1.;
    return {
        base.positionX + modifiers.translationOffsetX,
        base.positionY + modifiers.translationOffsetY,
        zoom * zoomMul,
        base.rotation + modifiers.rotationOffset,
    };
}

ViewCamera2D view_camera_from_editor(const EditorCamera& camera) {
    const double zoom = (camera.zoom > 0.) ? camera.zoom : 1.;
    return {
        camera.positionX,
        camera.positionY,
        0.,
        0.,
        zoom,
    };
}

} // namespace ArtCade::Presentation
