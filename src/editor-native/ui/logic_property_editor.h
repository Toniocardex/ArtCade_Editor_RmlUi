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

// Pure presentation state for every Key-valued Logic property. The encoded
// address remains the controller's stable target; no UI key code is stored in
// ProjectDocument.
struct LogicKeyBindingEditorState {
    std::string captureAddress;
    std::string searchAddress;
    std::string searchQuery;
};

/** Stable controller address: rule|t/a/c|index|property. */
std::string encodeLogicPropertyAddress(
    const LogicPropertyAddress& address, const std::string& propertyKey);

/**
 * The Logic Board's boolean control: both options rendered, the current one
 * selected. A single button printing only the current value reads as a label —
 * nothing about it says the state can be changed. Each option *sets* its value
 * rather than toggling, so clicking the selected one is a no-op instead of
 * flipping it.
 *
 * @p action receives the option's value as data-value ("true"/"false").
 */
std::string renderLogicBooleanChoice(
    const std::string& action, const std::string& arg, bool value, bool disabled);

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
    const LogicKeyBindingEditorState& keyBinding,
    bool playing);

} // namespace ArtCade::EditorNative
