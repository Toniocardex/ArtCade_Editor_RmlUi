#pragma once

#include "core/types.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/box_collider_geometry.h"

#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

struct SceneFrameCollider {
    EntityId entityId = INVALID_ENTITY;
    WorldRect worldBounds;
    bool enabled = false;
    BoxColliderMode mode = BoxColliderMode::Solid;
    bool selected = false;
};

std::vector<SceneFrameCollider> collectBoxColliderBounds(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId selectedEntity,
    const SceneTransformPreview* preview = nullptr);

} // namespace ArtCade::EditorNative
