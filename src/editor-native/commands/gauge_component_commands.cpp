#include "editor-native/commands/gauge_component_commands.h"

#include "editor-native/model/project_document.h"
#include "core/text-component-format.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kGaugeInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

bool nearlyEqual(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

bool sameColor(const Vec4& a, const Vec4& b) {
    return nearlyEqual(a.r, b.r) && nearlyEqual(a.g, b.g)
        && nearlyEqual(a.b, b.b) && nearlyEqual(a.a, b.a);
}

EditorOperationResult validateBinding(const ProjectDocument& document,
                                      const ObjectTypeId& objectTypeId,
                                      const GaugeComponent& component) {
    if (component.bindKey.empty()) return EditorOperationResult::success(EditorInvalidation::None);
    const auto scope = textBindingScopeFromString(component.bindScope);
    if (!scope) {
        return EditorOperationResult::failure("Gauge bindScope must be global or local");
    }
    if (*scope == TextBindingScope::Global) {
        const auto& globals = document.data().globalVariables;
        const auto it = std::find_if(
            globals.begin(), globals.end(),
            [&](const GameVariableDefinition& def) { return def.key == component.bindKey; });
        if (it == globals.end()) {
            return EditorOperationResult::failure(
                "Gauge bindKey \"" + component.bindKey + "\" is not a global variable");
        }
        if (!isGaugeCompatibleWithVariableType(it->type)) {
            return EditorOperationResult::failure(
                "Gauge requires a Number variable");
        }
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    const EntityDef* type = document.findObjectType(objectTypeId);
    if (!type) return EditorOperationResult::failure("Unknown object type");
    const auto it = std::find_if(
        type->localVariables.begin(), type->localVariables.end(),
        [&](const GameVariableDefinition& def) { return def.key == component.bindKey; });
    if (it == type->localVariables.end()) {
        return EditorOperationResult::failure(
            "Gauge bindKey \"" + component.bindKey
            + "\" is not a local variable on this Object Type");
    }
    if (!isGaugeCompatibleWithVariableType(it->type)) {
        return EditorOperationResult::failure("Gauge requires a Number variable");
    }
    return EditorOperationResult::success(EditorInvalidation::None);
}

DomainChange domainFor(const ObjectTypeId& objectTypeId,
                       const std::optional<GaugeComponent>& previous,
                       const std::optional<GaugeComponent>& next) {
    if (!previous && next) {
        return DomainChange::objectTypeComponentAdded(objectTypeId, ComponentKind::Gauge);
    }
    if (previous && !next) {
        return DomainChange::objectTypeComponentRemoved(objectTypeId, ComponentKind::Gauge);
    }
    return DomainChange::objectTypeComponentChanged(objectTypeId, ComponentKind::Gauge);
}

} // namespace

bool sameGaugeComponent(const GaugeComponent& lhs, const GaugeComponent& rhs) {
    return lhs.bindKey == rhs.bindKey
        && lhs.bindScope == rhs.bindScope
        && nearlyEqual(lhs.maxValue, rhs.maxValue)
        && nearlyEqual(lhs.width, rhs.width)
        && nearlyEqual(lhs.height, rhs.height)
        && sameColor(lhs.fillColor, rhs.fillColor)
        && sameColor(lhs.bgColor, rhs.bgColor)
        && lhs.direction == rhs.direction
        && nearlyEqual(lhs.offsetX, rhs.offsetX)
        && nearlyEqual(lhs.offsetY, rhs.offsetY)
        && lhs.screenSpace == rhs.screenSpace;
}

SetObjectTypeGaugeComponentCommand::SetObjectTypeGaugeComponentCommand(
    ObjectTypeId objectTypeId, std::optional<GaugeComponent> next)
    : objectTypeId_(std::move(objectTypeId)), next_(std::move(next)) {}

EditorOperationResult SetObjectTypeGaugeComponentCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);

    if (next_) {
        GaugeComponent normalized = *next_;
        // Empty bindKey is valid while choosing a variable (see Text command).
        normalized.fillColor.a = 1.f;
        normalized.bgColor.a = 1.f;
        std::string error;
        if (!validateGaugeComponent(normalized, error)) {
            return EditorOperationResult::failure(error);
        }
        const EditorOperationResult binding = validateBinding(document, objectTypeId_, normalized);
        if (!binding.ok) return binding;
        next_ = std::move(normalized);
    }

    if (type->gauge.has_value() == next_.has_value()
        && (!next_ || sameGaugeComponent(*type->gauge, *next_))) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    if (!captured_) {
        previous_ = type->gauge;
        captured_ = true;
    }

    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).gauge = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kGaugeInvalidation, domainFor(objectTypeId_, previous_, next_));
}

EditorOperationResult SetObjectTypeGaugeComponentCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end()) {
        return EditorOperationResult::failure("Object Type is missing");
    }
    type->second.gauge = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kGaugeInvalidation, domainFor(objectTypeId_, next_, previous_));
}

} // namespace ArtCade::EditorNative
