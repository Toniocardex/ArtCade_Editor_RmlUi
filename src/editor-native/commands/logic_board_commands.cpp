#include "editor-native/commands/logic_board_commands.h"

#include "editor-native/model/project_document.h"
#include "logic-core.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kLogicInvalidation = EditorInvalidation::LogicBoard;

const LogicBoardDef* boardOf(const ProjectDocument& document, const ObjectTypeId& id) {
    const EntityDef* type = document.findObjectType(id);
    return type && type->logicBoard ? &*type->logicBoard : nullptr;
}

LogicRuleDef* ruleOf(LogicBoardDef& board, const LogicRuleId& id) {
    const auto it = std::find_if(board.rules.begin(), board.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == id; });
    return it == board.rules.end() ? nullptr : &*it;
}

bool sameBoard(const LogicBoardDef& a, const LogicBoardDef& b) {
    return Logic::logicBoardToJson(a) == Logic::logicBoardToJson(b);
}

std::string validationError(const ProjectDocument& document, const ObjectTypeId& objectTypeId,
                            const LogicBoardDef& board) {
    const auto diagnostics = Logic::validateBoard(objectTypeId, board,
        document.findObjectType(objectTypeId), &document.data());
    return diagnostics.empty() ? std::string{}
                               : diagnostics.front().code + ": " + diagnostics.front().message;
}

EditorOperationResult changed(const ObjectTypeId& id) {
    return EditorOperationResult::success(kLogicInvalidation,
                                          DomainChange::logicBoardChanged(id));
}

LogicBlockDef defaultBlock(const std::string& typeId, Logic::BlockKind expected) {
    LogicBlockDef block;
    const Logic::LogicBlockDescriptor* descriptor = Logic::findDescriptor(typeId);
    if (!descriptor || descriptor->kind != expected) return block;
    block.typeId = descriptor->typeId;
    for (const auto& property : descriptor->properties)
        block.properties.push_back({property.key, property.defaultValue});
    return block;
}

void assignDefaultCollisionObjectType(const ProjectDocument& document, LogicBlockDef& block) {
    if (block.typeId != Logic::kOtherIsObjectType) return;
    const auto property = std::find_if(block.properties.begin(), block.properties.end(),
        [](const LogicPropertyDef& value) { return value.key == "objectTypeId"; });
    if (property == block.properties.end()) return;
    std::vector<ObjectTypeId> ids;
    ids.reserve(document.data().objectTypes.size());
    for (const auto& [id, unused] : document.data().objectTypes) {
        (void)unused;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    if (!ids.empty()) property->value = LogicStringValue{ids.front()};
}

} // namespace

#define COMMIT_NEXT_BOARD(nextBoard) do { \
    const LogicBoardDef* currentBoard = boardOf(document, objectTypeId_); \
    if (!currentBoard) return EditorOperationResult::failure("Object Type has no Logic Board"); \
    if (sameBoard(*currentBoard, (nextBoard))) return EditorOperationResult::success(EditorInvalidation::None); \
    const std::string invalid = validationError(document, objectTypeId_, (nextBoard)); \
    if (!invalid.empty()) return EditorOperationResult::failure(invalid); \
    if (!before_) before_ = *currentBoard; \
    if (!document.replaceLogicBoard(objectTypeId_, (nextBoard))) \
        return EditorOperationResult::failure("Cannot update Logic Board"); \
    return changed(objectTypeId_); \
} while (false)

#define UNDO_BOARD() do { \
    if (!before_ || !document.replaceLogicBoard(objectTypeId_, *before_)) \
        return EditorOperationResult::failure("Cannot undo Logic Board change"); \
    return changed(objectTypeId_); \
} while (false)

CreateLogicBoardCommand::CreateLogicBoardCommand(ObjectTypeId id) : objectTypeId_(std::move(id)) {}
EditorOperationResult CreateLogicBoardCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown Object Type: " + objectTypeId_);
    if (type->logicBoard) return EditorOperationResult::failure("Object Type already has a Logic Board");
    LogicBoardDef board;
    board.id = "logic:" + objectTypeId_;
    if (!document.replaceLogicBoard(objectTypeId_, board))
        return EditorOperationResult::failure("Cannot create Logic Board");
    return changed(objectTypeId_);
}
EditorOperationResult CreateLogicBoardCommand::undo(ProjectDocument& document) {
    if (!document.replaceLogicBoard(objectTypeId_, std::nullopt))
        return EditorOperationResult::failure("Cannot undo Logic Board creation");
    return changed(objectTypeId_);
}

RemoveLogicBoardCommand::RemoveLogicBoardCommand(ObjectTypeId id) : objectTypeId_(std::move(id)) {}
EditorOperationResult RemoveLogicBoardCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* board = boardOf(document, objectTypeId_);
    if (!board) return EditorOperationResult::failure("Object Type has no Logic Board");
    if (!removed_) removed_ = *board;
    if (!document.replaceLogicBoard(objectTypeId_, std::nullopt))
        return EditorOperationResult::failure("Cannot remove Logic Board");
    return changed(objectTypeId_);
}
EditorOperationResult RemoveLogicBoardCommand::undo(ProjectDocument& document) {
    if (!removed_ || !document.replaceLogicBoard(objectTypeId_, *removed_))
        return EditorOperationResult::failure("Cannot undo Logic Board removal");
    return changed(objectTypeId_);
}

AddLogicRuleCommand::AddLogicRuleCommand(ObjectTypeId id, LogicRuleDef rule, std::size_t index)
    : objectTypeId_(std::move(id)), rule_(std::move(rule)), index_(index) {}
EditorOperationResult AddLogicRuleCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current || index_ > current->rules.size())
        return EditorOperationResult::failure("Invalid Logic rule insertion");
    LogicBoardDef next = *current;
    next.rules.insert(next.rules.begin() + static_cast<std::ptrdiff_t>(index_), rule_);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicRuleCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicRuleCommand::RemoveLogicRuleCommand(ObjectTypeId id, LogicRuleId ruleId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)) {}
EditorOperationResult RemoveLogicRuleCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    const auto it = std::find_if(next.rules.begin(), next.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == ruleId_; });
    if (it == next.rules.end()) return EditorOperationResult::failure("Unknown Logic rule");
    next.rules.erase(it);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicRuleCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicRuleCommand::MoveLogicRuleCommand(ObjectTypeId id, LogicRuleId ruleId, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index) {}
EditorOperationResult MoveLogicRuleCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current || index_ >= current->rules.size())
        return EditorOperationResult::failure("Invalid Logic rule destination");
    LogicBoardDef next = *current;
    const auto it = std::find_if(next.rules.begin(), next.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == ruleId_; });
    if (it == next.rules.end()) return EditorOperationResult::failure("Unknown Logic rule");
    const std::size_t from = static_cast<std::size_t>(it - next.rules.begin());
    if (from == index_) return EditorOperationResult::success(EditorInvalidation::None);
    LogicRuleDef moved = std::move(*it);
    next.rules.erase(it);
    next.rules.insert(next.rules.begin() + static_cast<std::ptrdiff_t>(index_), std::move(moved));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicRuleCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicRuleEnabledCommand::SetLogicRuleEnabledCommand(ObjectTypeId id, LogicRuleId ruleId, bool enabled)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), enabled_(enabled) {}
EditorOperationResult SetLogicRuleEnabledCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    rule->enabled = enabled_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicRuleEnabledCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ReplaceLogicTriggerCommand::ReplaceLogicTriggerCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                       LogicBlockDef trigger)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), trigger_(std::move(trigger)) {}
EditorOperationResult ReplaceLogicTriggerCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    rule->trigger = trigger_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ReplaceLogicTriggerCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

AddLogicActionCommand::AddLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                             LogicBlockDef action, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)),
      action_(std::move(action)), index_(index) {}
EditorOperationResult AddLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ > rule->actions.size())
        return EditorOperationResult::failure("Invalid Logic action insertion");
    rule->actions.insert(rule->actions.begin() + static_cast<std::ptrdiff_t>(index_), action_);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicActionCommand::RemoveLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index) {}
EditorOperationResult RemoveLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ >= rule->actions.size())
        return EditorOperationResult::failure("Unknown Logic action");
    if (rule->actions.size() == 1)
        return EditorOperationResult::failure("A Logic rule needs at least one action");
    rule->actions.erase(rule->actions.begin() + static_cast<std::ptrdiff_t>(index_));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicActionCommand::MoveLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                               std::size_t from, std::size_t to)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), from_(from), to_(to) {}
EditorOperationResult MoveLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || from_ >= rule->actions.size() || to_ >= rule->actions.size())
        return EditorOperationResult::failure("Invalid Logic action move");
    if (from_ == to_) return EditorOperationResult::success(EditorInvalidation::None);
    LogicBlockDef moved = std::move(rule->actions[from_]);
    rule->actions.erase(rule->actions.begin() + static_cast<std::ptrdiff_t>(from_));
    rule->actions.insert(rule->actions.begin() + static_cast<std::ptrdiff_t>(to_), std::move(moved));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ChangeLogicActionTypeCommand::ChangeLogicActionTypeCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                           std::size_t index, std::string typeId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index),
      typeId_(std::move(typeId)) {}
EditorOperationResult ChangeLogicActionTypeCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ >= rule->actions.size())
        return EditorOperationResult::failure("Unknown Logic action");
    LogicBlockDef replacement = defaultBlock(typeId_, Logic::BlockKind::Action);
    if (replacement.typeId.empty()) return EditorOperationResult::failure("Unknown Logic action type");
    rule->actions[index_] = std::move(replacement);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ChangeLogicActionTypeCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

AddLogicConditionCommand::AddLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                   LogicBlockDef condition, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)),
      condition_(std::move(condition)), index_(index) {}
EditorOperationResult AddLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ > rule->conditions.size())
        return EditorOperationResult::failure("Invalid Logic condition insertion");
    LogicBlockDef condition = condition_;
    assignDefaultCollisionObjectType(document, condition);
    rule->conditions.insert(rule->conditions.begin() + static_cast<std::ptrdiff_t>(index_), std::move(condition));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicConditionCommand::RemoveLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index) {}
EditorOperationResult RemoveLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ >= rule->conditions.size())
        return EditorOperationResult::failure("Unknown Logic condition");
    rule->conditions.erase(rule->conditions.begin() + static_cast<std::ptrdiff_t>(index_));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicConditionCommand::MoveLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                     std::size_t from, std::size_t to)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), from_(from), to_(to) {}
EditorOperationResult MoveLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || from_ >= rule->conditions.size() || to_ >= rule->conditions.size())
        return EditorOperationResult::failure("Invalid Logic condition move");
    if (from_ == to_) return EditorOperationResult::success(EditorInvalidation::None);
    LogicBlockDef moved = std::move(rule->conditions[from_]);
    rule->conditions.erase(rule->conditions.begin() + static_cast<std::ptrdiff_t>(from_));
    rule->conditions.insert(rule->conditions.begin() + static_cast<std::ptrdiff_t>(to_), std::move(moved));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ChangeLogicConditionTypeCommand::ChangeLogicConditionTypeCommand(
    ObjectTypeId id, LogicRuleId ruleId, std::size_t index, std::string typeId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index),
      typeId_(std::move(typeId)) {}
EditorOperationResult ChangeLogicConditionTypeCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ >= rule->conditions.size())
        return EditorOperationResult::failure("Unknown Logic condition");
    LogicBlockDef replacement = defaultBlock(typeId_, Logic::BlockKind::Condition);
    if (replacement.typeId.empty()) return EditorOperationResult::failure("Unknown Logic condition type");
    assignDefaultCollisionObjectType(document, replacement);
    rule->conditions[index_] = std::move(replacement);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ChangeLogicConditionTypeCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicPropertyCommand::SetLogicPropertyCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                 LogicPropertyTarget target, std::size_t blockIndex,
                                                 std::string key, LogicValue value)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), target_(target),
      blockIndex_(blockIndex), propertyKey_(std::move(key)), value_(std::move(value)) {}
EditorOperationResult SetLogicPropertyCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    LogicBlockDef* block = nullptr;
    switch (target_) {
        case LogicPropertyTarget::Trigger:
            block = &rule->trigger;
            break;
        case LogicPropertyTarget::Action:
            block = blockIndex_ < rule->actions.size() ? &rule->actions[blockIndex_] : nullptr;
            break;
        case LogicPropertyTarget::Condition:
            block = blockIndex_ < rule->conditions.size() ? &rule->conditions[blockIndex_] : nullptr;
            break;
    }
    if (!block) return EditorOperationResult::failure("Unknown Logic block");
    auto it = std::find_if(block->properties.begin(), block->properties.end(),
        [&](const LogicPropertyDef& property) { return property.key == propertyKey_; });
    if (it == block->properties.end()) return EditorOperationResult::failure("Unknown Logic property");
    it->value = value_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicPropertyCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

LogicRuleId nextLogicRuleId(const LogicBoardDef& board) {
    int maxOrdinal = 0;
    for (const LogicRuleDef& rule : board.rules) {
        if (rule.id.rfind("rule-", 0) != 0) continue;
        const int ordinal = std::atoi(rule.id.c_str() + 5);
        maxOrdinal = std::max(maxOrdinal, ordinal);
    }
    return "rule-" + std::to_string(maxOrdinal + 1);
}

#undef COMMIT_NEXT_BOARD
#undef UNDO_BOARD

} // namespace ArtCade::EditorNative

