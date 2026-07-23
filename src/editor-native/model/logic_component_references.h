#pragma once

#include "editor-native/commands/domain_change.h"
#include "core/types.h"
#include "logic-core.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

enum class LogicReferenceSlot { Trigger, Condition, Action };

struct LogicComponentReference {
    ObjectTypeId objectTypeId;
    LogicRuleId ruleId;
    LogicReferenceSlot slot = LogicReferenceSlot::Action;
    std::size_t blockIndex = 0;
    std::string typeId;
    std::string displayName;
};

struct LogicComponentReferenceReport {
    std::vector<LogicComponentReference> references;

    [[nodiscard]] std::size_t triggerCount() const;
    [[nodiscard]] std::size_t conditionCount() const;
    [[nodiscard]] std::size_t actionCount() const;
    [[nodiscard]] bool empty() const { return references.empty(); }
};

/** Maps editor ComponentKind to Logic required-component, or nullopt if N/A. */
[[nodiscard]] std::optional<Logic::LogicRequiredComponent> logicRequiredComponentFor(
    ComponentKind kind);

/**
 * Canonical Logic Board references that require @p component on @p objectTypeId.
 * Dialogs, banners, bulk repair, and tests must consume this — no ad-hoc scans.
 */
[[nodiscard]] LogicComponentReferenceReport collectComponentLogicReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId,
    ComponentKind component);

/**
 * Blocks that are currently LB_INCOMPATIBLE given the Object Type's components
 * (any required component missing). Used by bulk repair after controller removal.
 */
[[nodiscard]] LogicComponentReferenceReport collectIncompatibleLogicReferences(
    const ProjectDocument& document,
    const ObjectTypeId& objectTypeId);

} // namespace ArtCade::EditorNative
