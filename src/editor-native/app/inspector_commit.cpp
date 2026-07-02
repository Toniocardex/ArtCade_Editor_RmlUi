#include "editor-native/app/inspector_commit.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/entity_commands.h"

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

EditorOperationResult commitInspectorPositionX(EditorCoordinator& coordinator,
                                               EntityId           entityId,
                                               const std::string& text) {
    const std::optional<float> parsed = parseNumberField(text);
    if (!parsed) {
        const auto result = EditorOperationResult::failure("Position X is not a number");
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

    const Vec2 next{*parsed, instance->transform.position.y};
    return coordinator.execute(SetEntityPositionCommand{sceneId, entityId, next});
}

EditorOperationResult commitInspectorPositionY(EditorCoordinator& coordinator,
                                               EntityId           entityId,
                                               const std::string& text) {
    const std::optional<float> parsed = parseNumberField(text);
    if (!parsed) {
        const auto result = EditorOperationResult::failure("Position Y is not a number");
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
    const Vec2 next{instance->transform.position.x, *parsed};
    return coordinator.execute(SetEntityPositionCommand{sceneId, entityId, next});
}

} // namespace ArtCade::EditorNative
