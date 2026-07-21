#pragma once

#include "core/types.h"
#include "editor-native/commands/logic_board_commands.h"
#include "logic-core.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

class ProjectDocument;

struct LogicPropertyAddress {
    LogicRuleId ruleId;
    LogicPropertyTarget target = LogicPropertyTarget::Trigger;
    std::size_t blockIndex = 0;
};

/** Stable controller address: rule|t/a/c|index|property. */
std::string encodeLogicPropertyAddress(
    const LogicPropertyAddress& address, const std::string& propertyKey);

/**
 * Descriptor-driven read-only projection of every editable property in a block.
 * Composite animation asset/clip is intentionally left to the existing atomic
 * renderer; all scalar, Vec2 and reference types use this path.
 */
std::string renderLogicProperties(
    const ProjectDocument& document,
    const LogicBlockDef& block,
    const LogicPropertyAddress& address,
    const std::string& openDropdownId,
    bool playing);

} // namespace ArtCade::EditorNative
