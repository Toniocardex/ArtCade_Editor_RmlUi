#include "editor-native/model/box_collider_view.h"

#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {

std::vector<SceneFrameCollider> collectBoxColliderBounds(const ProjectDocument& document,
                                                         const SceneId& sceneId,
                                                         EntityId selectedEntity) {
    std::vector<SceneFrameCollider> out;
    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) return out;

    const auto& types = document.data().objectTypes;
    for (const SceneInstanceDef& instance : scene->instances) {
        const auto typeIt = types.find(instance.objectTypeId);
        if (typeIt == types.end() || !typeIt->second.boxCollider2D) continue;
        const BoxCollider2DComponent& collider = *typeIt->second.boxCollider2D;
        if (!collider.enabled) continue;
        out.push_back(SceneFrameCollider{
            instance.id,
            boxColliderWorldBounds(instance.transform, collider),
            true,
            collider.mode,
            instance.id == selectedEntity,
        });
    }
    return out;
}

} // namespace ArtCade::EditorNative
