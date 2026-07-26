#include "editor-native/commands/text_component_commands.h"

#include "editor-native/model/project_document.h"
#include "core/text-component-format.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kTextInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

EditorOperationResult validateBinding(const ProjectDocument& document,
                                      const ObjectTypeId& objectTypeId,
                                      const TextComponent& component) {
    if (component.bindKey.empty()) return EditorOperationResult::success(EditorInvalidation::None);
    const auto scope = textBindingScopeFromString(component.bindScope);
    if (!scope) {
        return EditorOperationResult::failure("Text bindScope must be global or local");
    }
    if (*scope == TextBindingScope::Global) {
        const auto& globals = document.data().globalVariables;
        const auto it = std::find_if(
            globals.begin(), globals.end(),
            [&](const GameVariableDefinition& def) { return def.key == component.bindKey; });
        if (it == globals.end()) {
            return EditorOperationResult::failure(
                "Text bindKey \"" + component.bindKey + "\" is not a global variable");
        }
        if (!isTextFormatCompatibleWithVariableType(component.format, it->type)) {
            return EditorOperationResult::failure(
                "Text format is incompatible with bound global variable type");
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
            "Text bindKey \"" + component.bindKey + "\" is not a local variable on this Object Type");
    }
    if (!isTextFormatCompatibleWithVariableType(component.format, it->type)) {
        return EditorOperationResult::failure(
            "Text format is incompatible with bound local variable type");
    }
    return EditorOperationResult::success(EditorInvalidation::None);
}

DomainChange domainFor(const ObjectTypeId& objectTypeId,
                       const std::optional<TextComponent>& previous,
                       const std::optional<TextComponent>& next) {
    if (!previous && next) {
        return DomainChange::objectTypeComponentAdded(objectTypeId, ComponentKind::Text);
    }
    if (previous && !next) {
        return DomainChange::objectTypeComponentRemoved(objectTypeId, ComponentKind::Text);
    }
    return DomainChange::objectTypeComponentChanged(objectTypeId, ComponentKind::Text);
}

} // namespace

SetObjectTypeTextComponentCommand::SetObjectTypeTextComponentCommand(
    ObjectTypeId objectTypeId, std::optional<TextComponent> next)
    : objectTypeId_(std::move(objectTypeId)), next_(std::move(next)) {}

EditorOperationResult SetObjectTypeTextComponentCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);

    if (next_) {
        TextComponent normalized = *next_;
        // Empty bindKey is valid while choosing a variable (Local, or Global
        // mid-pick). None clears the key and sets bindScope=global in the UI.
        // Do not coerce empty→global here — that made Local unselectable.
        if (const auto anchor = textAnchorFromString(normalized.align)) {
            normalized.align = textAnchorToString(*anchor);
        }
        normalized.color.a = 1.f;
        std::string error;
        if (!validateTextComponent(normalized, error)) {
            return EditorOperationResult::failure(error);
        }
        const EditorOperationResult binding = validateBinding(document, objectTypeId_, normalized);
        if (!binding.ok) return binding;
        next_ = std::move(normalized);
    }

    if (type->text.has_value() == next_.has_value()
        && (!next_ || sameTextComponent(*type->text, *next_))) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    if (!captured_) {
        previous_ = type->text;
        captured_ = true;
    }

    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).text = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kTextInvalidation, domainFor(objectTypeId_, previous_, next_));
}

EditorOperationResult SetObjectTypeTextComponentCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end()) {
        return EditorOperationResult::failure("Object Type is missing");
    }
    type->second.text = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kTextInvalidation, domainFor(objectTypeId_, next_, previous_));
}

} // namespace ArtCade::EditorNative
