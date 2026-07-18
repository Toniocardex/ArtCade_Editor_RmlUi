#include "editor-native/app/inspector_commit.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/model/authored_transform.h"

#include <cmath>
#include <cstddef>

namespace ArtCade::EditorNative {

std::optional<float> parseNumberField(const std::string& text) {
    if (text.empty()) return std::nullopt;
    const char last = text.back();
    if (last == 'e' || last == 'E' || last == '+' || last == '-') {
        return std::nullopt; // still an edit buffer, not a committed number
    }
    try {
        std::size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) return std::nullopt;   // trailing garbage
        if (!std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;   // not-a-number / out-of-range
    }
}

namespace {

const char* fieldLabel(InspectorTransformField field) {
    switch (field) {
    case InspectorTransformField::PositionX: return "Position X";
    case InspectorTransformField::PositionY: return "Position Y";
    case InspectorTransformField::RotationDegrees: return "Rotation";
    case InspectorTransformField::ScaleX: return "Scale X";
    case InspectorTransformField::ScaleY: return "Scale Y";
    }
    return "Transform";
}

} // namespace

EditorOperationResult commitInspectorTransformField(EditorCoordinator& coordinator,
                                                    EntityId entityId,
                                                    InspectorTransformField field,
                                                    const std::string& text) {
    const std::optional<float> parsed = parseNumberField(text);
    if (!parsed) {
        const auto result = EditorOperationResult::failure(
            std::string(fieldLabel(field)) + " is not a number");
        coordinator.logError(result.error);
        return result;
    }

    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneInstanceDef* instance =
        coordinator.document().findInstanceInScene(sceneId, entityId);
    if (!instance) {
        const auto result = EditorOperationResult::failure("No selected instance");
        coordinator.logError(result.error);
        return result;
    }

    AuthoredTransformPatch patch;
    switch (field) {
    case InspectorTransformField::PositionX:
        patch.position = Vec2{*parsed, instance->transform.position.y};
        break;
    case InspectorTransformField::PositionY:
        patch.position = Vec2{instance->transform.position.x, *parsed};
        break;
    case InspectorTransformField::RotationDegrees:
        patch.rotationRadians = *parsed * kDegToRad;
        break;
    case InspectorTransformField::ScaleX:
        patch.scale = Vec2{*parsed, instance->transform.scale.y};
        break;
    case InspectorTransformField::ScaleY:
        patch.scale = Vec2{instance->transform.scale.x, *parsed};
        break;
    }

    return coordinator.execute(SetEntityTransformCommand{sceneId, entityId, std::move(patch)});
}

} // namespace ArtCade::EditorNative
