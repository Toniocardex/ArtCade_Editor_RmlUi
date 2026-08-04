#include "editor-native/model/box_collider_view.h"

#include "editor-native/model/project_document.h"
#include "core/box-collider-resolve.h"

namespace ArtCade::EditorNative {

std::vector<SceneFrameCollider> collectBoxColliderBounds(
    const ProjectDocument& document,
    const SceneId& sceneId,
    EntityId selectedEntity,
    const SceneTransformPreview* preview) {
    std::vector<SceneFrameCollider> out;
    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) return out;

    const auto& types = document.data().objectTypes;
    for (const SceneInstanceDef& instance : scene->instances) {
        const auto typeIt = types.find(instance.objectTypeId);
        if (typeIt == types.end()) continue;
        const EffectiveBoxCollider2D effective =
            resolveEffectiveBoxCollider2D(typeIt->second, instance);
        if (!effective.present) continue;
        const BoxCollider2DComponent& collider = effective.value;
        if (!collider.enabled) continue;
        const Transform& xf =
            (preview && preview->entityId == instance.id) ? preview->transform
                                                          : instance.transform;
        out.push_back(SceneFrameCollider{
            instance.id,
            boxColliderWorldBounds(xf, collider),
            true,
            collider.mode,
            instance.id == selectedEntity,
        });
    }
    return out;
}

} // namespace ArtCade::EditorNative
