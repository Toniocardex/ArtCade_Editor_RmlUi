#include "editor-native/commands/script_attachment_commands.h"

#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Assets;

const ScriptComponent* scriptsOf(const ProjectDocument& document,
                                 const ObjectTypeId& objectTypeId) {
    const EntityDef* type = document.findObjectType(objectTypeId);
    return type && type->scripts ? &*type->scripts : nullptr;
}

std::string validateCandidate(const ProjectDocument& document,
                              const ScriptComponent& scripts) {
    std::unordered_set<ScriptAttachmentId> ids;
    for (const ScriptAttachmentDef& attachment : scripts.attachments) {
        if (attachment.id.empty()) return "Script attachment id cannot be empty";
        if (!ids.insert(attachment.id).second) return "Duplicate Script attachment id";
        if (attachment.scriptAssetId.empty()
            || !document.hasScriptAsset(attachment.scriptAssetId)) {
            return "Script attachment references an unknown Script Asset";
        }
    }
    return {};
}

template <typename Replace>
EditorOperationResult commit(ProjectDocument& document,
                             const ObjectTypeId& objectTypeId,
                             ScriptComponent next,
                             std::optional<ScriptComponent>& before,
                             bool& captured,
                             Replace&& replace) {
    const EntityDef* type = document.findObjectType(objectTypeId);
    if (!type) return EditorOperationResult::failure("Unknown Object Type: " + objectTypeId);
    const std::string invalid = validateCandidate(document, next);
    if (!invalid.empty()) return EditorOperationResult::failure(invalid);
    if (!captured) {
        before = type->scripts;
        captured = true;
    }
    std::optional<ScriptComponent> replacement;
    if (!next.attachments.empty()) replacement = std::move(next);
    const bool hadComponent = type->scripts.has_value();
    const bool hasComponent = replacement.has_value();
    if (!replace(std::move(replacement)))
        return EditorOperationResult::failure("Could not update Object Type scripts");
    const DomainChange change = !hadComponent && hasComponent
        ? DomainChange::objectTypeComponentAdded(objectTypeId, ComponentKind::Scripts)
        : hadComponent && !hasComponent
            ? DomainChange::objectTypeComponentRemoved(objectTypeId, ComponentKind::Scripts)
            : DomainChange::objectTypeComponentChanged(objectTypeId, ComponentKind::Scripts);
    return EditorOperationResult::success(
        kInvalidation, change);
}

template <typename Replace>
EditorOperationResult restore(
                              const ProjectDocument& document,
                              const ObjectTypeId& objectTypeId,
                              const std::optional<ScriptComponent>& before,
                              bool captured,
                              Replace&& replace) {
    const EntityDef* current = document.findObjectType(objectTypeId);
    if (!captured || !current) {
        return EditorOperationResult::failure("Could not undo Script attachment change");
    }
    const bool hadComponent = current->scripts.has_value();
    const bool hasComponent = before.has_value();
    if (!replace(before))
        return EditorOperationResult::failure("Could not undo Script attachment change");
    const DomainChange change = !hadComponent && hasComponent
        ? DomainChange::objectTypeComponentAdded(objectTypeId, ComponentKind::Scripts)
        : hadComponent && !hasComponent
            ? DomainChange::objectTypeComponentRemoved(objectTypeId, ComponentKind::Scripts)
            : DomainChange::objectTypeComponentChanged(objectTypeId, ComponentKind::Scripts);
    return EditorOperationResult::success(
        kInvalidation, change);
}

} // namespace

AddScriptAttachmentCommand::AddScriptAttachmentCommand(
    ObjectTypeId objectTypeId, ScriptAttachmentDef attachment, std::size_t index)
    : objectTypeId_(std::move(objectTypeId)), attachment_(std::move(attachment)),
      index_(index) {}

EditorOperationResult AddScriptAttachmentCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown Object Type: " + objectTypeId_);
    ScriptComponent next = type->scripts.value_or(ScriptComponent{});
    if (index_ > next.attachments.size())
        return EditorOperationResult::failure("Invalid Script attachment insertion index");
    next.attachments.insert(next.attachments.begin() + static_cast<std::ptrdiff_t>(index_),
                            attachment_);
    return commit(document, objectTypeId_, std::move(next), before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

EditorOperationResult AddScriptAttachmentCommand::undo(ProjectDocument& document) {
    return restore(document, objectTypeId_, before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

RemoveScriptAttachmentCommand::RemoveScriptAttachmentCommand(
    ObjectTypeId objectTypeId, ScriptAttachmentId attachmentId)
    : objectTypeId_(std::move(objectTypeId)), attachmentId_(std::move(attachmentId)) {}

EditorOperationResult RemoveScriptAttachmentCommand::apply(ProjectDocument& document) {
    const ScriptComponent* current = scriptsOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Script attachments");
    ScriptComponent next = *current;
    const auto it = std::find_if(next.attachments.begin(), next.attachments.end(),
        [&](const ScriptAttachmentDef& value) { return value.id == attachmentId_; });
    if (it == next.attachments.end())
        return EditorOperationResult::failure("Unknown Script attachment");
    next.attachments.erase(it);
    return commit(document, objectTypeId_, std::move(next), before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

EditorOperationResult RemoveScriptAttachmentCommand::undo(ProjectDocument& document) {
    return restore(document, objectTypeId_, before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

MoveScriptAttachmentCommand::MoveScriptAttachmentCommand(
    ObjectTypeId objectTypeId, ScriptAttachmentId attachmentId,
    std::size_t destinationIndex)
    : objectTypeId_(std::move(objectTypeId)), attachmentId_(std::move(attachmentId)),
      destinationIndex_(destinationIndex) {}

EditorOperationResult MoveScriptAttachmentCommand::apply(ProjectDocument& document) {
    const ScriptComponent* current = scriptsOf(document, objectTypeId_);
    if (!current || destinationIndex_ >= current->attachments.size())
        return EditorOperationResult::failure("Invalid Script attachment destination");
    const auto found = std::find_if(current->attachments.begin(), current->attachments.end(),
        [&](const ScriptAttachmentDef& value) { return value.id == attachmentId_; });
    if (found == current->attachments.end())
        return EditorOperationResult::failure("Unknown Script attachment");
    const std::size_t sourceIndex = static_cast<std::size_t>(
        found - current->attachments.begin());
    if (sourceIndex == destinationIndex_)
        return EditorOperationResult::success(EditorInvalidation::None);
    ScriptComponent next = *current;
    ScriptAttachmentDef moved = std::move(next.attachments[sourceIndex]);
    next.attachments.erase(next.attachments.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
    next.attachments.insert(
        next.attachments.begin() + static_cast<std::ptrdiff_t>(destinationIndex_),
        std::move(moved));
    return commit(document, objectTypeId_, std::move(next), before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

EditorOperationResult MoveScriptAttachmentCommand::undo(ProjectDocument& document) {
    return restore(document, objectTypeId_, before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

SetScriptAttachmentEnabledCommand::SetScriptAttachmentEnabledCommand(
    ObjectTypeId objectTypeId, ScriptAttachmentId attachmentId, bool enabled)
    : objectTypeId_(std::move(objectTypeId)), attachmentId_(std::move(attachmentId)),
      enabled_(enabled) {}

EditorOperationResult SetScriptAttachmentEnabledCommand::apply(ProjectDocument& document) {
    const ScriptComponent* current = scriptsOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Script attachments");
    ScriptComponent next = *current;
    const auto found = std::find_if(next.attachments.begin(), next.attachments.end(),
        [&](const ScriptAttachmentDef& value) { return value.id == attachmentId_; });
    if (found == next.attachments.end())
        return EditorOperationResult::failure("Unknown Script attachment");
    if (found->enabled == enabled_)
        return EditorOperationResult::success(EditorInvalidation::None);
    found->enabled = enabled_;
    return commit(document, objectTypeId_, std::move(next), before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

EditorOperationResult SetScriptAttachmentEnabledCommand::undo(ProjectDocument& document) {
    return restore(document, objectTypeId_, before_, captured_,
        [&](std::optional<ScriptComponent> replacement) {
            return document.replaceScriptComponent(objectTypeId_, std::move(replacement));
        });
}

ScriptAttachmentId nextScriptAttachmentId(const ScriptComponent& scripts) {
    std::unordered_set<ScriptAttachmentId> existing;
    for (const ScriptAttachmentDef& attachment : scripts.attachments)
        existing.insert(attachment.id);
    for (std::size_t index = 1; index <= scripts.attachments.size() + 1; ++index) {
        const ScriptAttachmentId candidate = "script-" + std::to_string(index);
        if (existing.count(candidate) == 0) return candidate;
    }
    return "script-" + std::to_string(scripts.attachments.size() + 1);
}

} // namespace ArtCade::EditorNative
