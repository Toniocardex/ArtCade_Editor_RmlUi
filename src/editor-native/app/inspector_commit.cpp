#include "editor-native/app/inspector_commit.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/model/authored_transform.h"
#include "core/color-hex.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

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

int hexDigitValue(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

unsigned channelToByte(float channel) {
    const float clamped = clamp01(channel);
    return static_cast<unsigned>(std::lround(clamped * 255.0f));
}

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

std::string formatColorHexRgb(const Vec4& color) {
    return ArtCade::formatColorHexRgb(color);
}

std::optional<ColorRgb> parseColorHexRgb(const std::string& text) {
    const auto parsed = ArtCade::parseColorHexRgb(text);
    if (!parsed) return std::nullopt;
    return ColorRgb{parsed->r, parsed->g, parsed->b};
}

bool incompleteColorHexBuffer(const std::string& text) {
    if (text.empty()) return true;
    std::string hex = text;
    if (hex.front() == '#') hex.erase(hex.begin());
    if (hex.empty()) return true;   // just "#"
    if (hex.size() > 6) return false;
    for (unsigned char c : hex) {
        if (hexDigitValue(c) < 0) return false;
    }
    // Valid complete lengths are 3 and 6; anything else with only hex digits is
    // still being typed.
    return hex.size() != 3 && hex.size() != 6;
}

std::string formatOpacityPercent(float alpha) {
    const float percent = clamp01(alpha) * 100.0f;
    return std::to_string(static_cast<int>(std::lround(percent)));
}

std::optional<float> parseOpacityPercent(const std::string& text) {
    std::string raw = text;
    if (!raw.empty() && raw.back() == '%') raw.pop_back();
    const std::optional<float> parsed = parseNumberField(raw);
    if (!parsed) return std::nullopt;
    return clamp01(*parsed / 100.0f);
}

OpacitySliderChangeDisposition classifyOpacitySliderChange(bool dragActive) {
    return dragActive ? OpacitySliderChangeDisposition::PreviewOnly
                      : OpacitySliderChangeDisposition::CommitImmediately;
}

bool sameSceneBackgroundColor(const Vec4& a, const Vec4& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

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
