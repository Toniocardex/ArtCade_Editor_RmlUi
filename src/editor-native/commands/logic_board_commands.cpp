#include "editor-native/commands/logic_board_commands.h"

#include "editor-native/model/logic_component_references.h"
#include "editor-native/model/project_document.h"
#include "logic-core.h"
#include "project-global-variables-format.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kLogicInvalidation = EditorInvalidation::LogicBoard;
constexpr EditorInvalidation kContextVariableInvalidation =
    EditorInvalidation::LogicBoard | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;

const LogicBoardDef* boardOf(const ProjectDocument& document, const ObjectTypeId& id) {
    const EntityDef* type = document.findObjectType(id);
    return type && type->logicBoard ? &*type->logicBoard : nullptr;
}

LogicRuleDef* ruleOf(LogicBoardDef& board, const LogicRuleId& id) {
    const auto it = std::find_if(board.rules.begin(), board.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == id; });
    return it == board.rules.end() ? nullptr : &*it;
}

LogicActionBranchDef* branchOf(LogicRuleDef& rule, const LogicActionBranchId& id) {
    const auto it = std::find_if(rule.branches.begin(), rule.branches.end(),
        [&](const LogicActionBranchDef& branch) { return branch.id == id; });
    return it == rule.branches.end() ? nullptr : &*it;
}

std::vector<LogicConditionClause>* conditionsOf(LogicRuleDef& rule,
                                                const LogicActionBranchId& branchId) {
    if (branchId.empty()) return &rule.conditions;
    LogicActionBranchDef* branch = branchOf(rule, branchId);
    return branch ? &branch->conditions : nullptr;
}

LogicBlockDef* blockOf(LogicRuleDef& rule, LogicPropertyTarget target,
                       std::size_t blockIndex, const LogicActionBranchId& branchId) {
    switch (target) {
    case LogicPropertyTarget::Trigger:
        return &rule.trigger;
    case LogicPropertyTarget::Action:
        if (LogicActionBranchDef* branch = branchOf(rule, branchId);
            branch && blockIndex < branch->actions.size()) return &branch->actions[blockIndex];
        return nullptr;
    case LogicPropertyTarget::Condition:
        if (std::vector<LogicConditionClause>* conditions = conditionsOf(rule, branchId);
            conditions && blockIndex < conditions->size()) return &(*conditions)[blockIndex].block;
        return nullptr;
    }
    return nullptr;
}

EntityDef* objectTypeOf(ProjectDoc& project, const ObjectTypeId& id) {
    const auto it = project.objectTypes.find(id);
    return it == project.objectTypes.end() ? nullptr : &it->second;
}

std::string stagedValidationError(const ProjectDoc& project,
                                  const ObjectTypeId& objectTypeId,
                                  const EntityDef& objectType,
                                  const LogicBoardDef& board) {
    return Logic::firstLogicErrorMessage(Logic::validateBoard(
        objectTypeId, board, &objectType, &project,
        Logic::LogicValidationPurpose::StructuralCommit));
}

const Logic::LogicPropertyDescriptor* descriptorProperty(
    const LogicBlockDef& block, const std::string& key) {
    const Logic::LogicBlockDescriptor* descriptor = Logic::findDescriptor(block.typeId);
    if (!descriptor) return nullptr;
    const auto it = std::find_if(
        descriptor->properties.begin(), descriptor->properties.end(),
        [&](const Logic::LogicPropertyDescriptor& property) {
            return property.key == key;
        });
    return it == descriptor->properties.end() ? nullptr : &*it;
}

bool sameBoard(const LogicBoardDef& a, const LogicBoardDef& b) {
    return Logic::logicBoardToJson(a) == Logic::logicBoardToJson(b);
}

std::string validationError(const ProjectDocument& document, const ObjectTypeId& objectTypeId,
                            const LogicBoardDef& board) {
    return Logic::firstLogicErrorMessage(Logic::validateBoard(
        objectTypeId, board, document.findObjectType(objectTypeId), &document.data(),
        Logic::LogicValidationPurpose::StructuralCommit));
}

EditorOperationResult changed(const ObjectTypeId& id) {
    return EditorOperationResult::success(kLogicInvalidation,
                                          DomainChange::logicBoardChanged(id));
}

LogicBlockDef defaultBlock(const std::string& typeId, Logic::BlockKind expected) {
    return Logic::makeDefaultBlock(typeId, expected);
}

LogicBlockDef defaultEventBlock(const std::string& typeId) {
    return Logic::makeDefaultEventBlock(typeId);
}

LogicConditionClause makeConditionClause(LogicBlockDef block) {
    LogicConditionClause clause;
    clause.block = std::move(block);
    return clause;
}

void assignDefaultCollisionObjectType(const ProjectDocument& document, LogicBlockDef& block) {
    if (block.typeId != Logic::kOtherIsObjectType
        && block.typeId != Logic::kCollisionEnter
        && block.typeId != Logic::kCollisionExit) return;
    const auto property = std::find_if(block.properties.begin(), block.properties.end(),
        [](const LogicPropertyDef& value) { return value.key == "objectTypeId"; });
    if (property == block.properties.end()) return;
    // Collision events keep an empty filter (= any Other). Legacy Other Is
    // Object Type still needs a concrete default so Authoring can save drafts.
    if (block.typeId == Logic::kCollisionEnter || block.typeId == Logic::kCollisionExit) return;
    std::vector<ObjectTypeId> ids;
    ids.reserve(document.data().objectTypes.size());
    for (const auto& [id, unused] : document.data().objectTypes) {
        (void)unused;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    if (!ids.empty()) property->value = LogicStringValue{ids.front()};
}

std::string defaultClipId(const SpriteAnimationAssetDef& asset) {
    return asset.clips.empty() ? std::string{} : asset.clips.front().id;
}

void assignDefaultAnimationClip(const ProjectDocument& document, LogicBlockDef& block) {
    if (block.typeId != Logic::kAnimationPlayClip) return;
    std::vector<const SpriteAnimationAssetDef*> assets;
    assets.reserve(document.data().spriteAnimationAssets.size());
    for (const SpriteAnimationAssetDef& asset : document.data().spriteAnimationAssets) {
        if (!asset.clips.empty()) assets.push_back(&asset);
    }
    std::sort(assets.begin(), assets.end(),
        [](const SpriteAnimationAssetDef* a, const SpriteAnimationAssetDef* b) {
            return a->id < b->id;
        });
    if (assets.empty()) return;
    for (LogicPropertyDef& property : block.properties) {
        if (property.key == "animationAssetId") {
            const auto* current = std::get_if<LogicAssetReference>(&property.value);
            if (!current || current->id.empty()) property.value = LogicAssetReference{assets.front()->id};
        } else if (property.key == "clipId") {
            const auto* current = std::get_if<LogicStringValue>(&property.value);
            if (!current || current->value.empty())
                property.value = LogicStringValue{defaultClipId(*assets.front())};
        }
    }
}

// Deterministic, not "whatever an unordered_map happens to iterate first":
// the first StaticSound audio asset by sorted AssetId, or left empty (with a
// visible validator diagnostic) if the project has none yet.
void assignDefaultAudioAsset(const ProjectDocument& document, LogicBlockDef& block) {
    if (block.typeId != Logic::kAudioPlaySound) return;
    std::vector<const AudioAssetDef*> assets;
    assets.reserve(document.data().audioAssets.size());
    for (const AudioAssetDef& asset : document.data().audioAssets) {
        if (asset.loadMode == AudioLoadMode::StaticSound) assets.push_back(&asset);
    }
    std::sort(assets.begin(), assets.end(),
        [](const AudioAssetDef* a, const AudioAssetDef* b) {
            return a->assetId < b->assetId;
        });
    if (assets.empty()) return;
    for (LogicPropertyDef& property : block.properties) {
        if (property.key != "audioAssetId") continue;
        const auto* current = std::get_if<LogicAssetReference>(&property.value);
        if (!current || current->id.empty())
            property.value = LogicAssetReference{assets.front()->assetId};
    }
}

// Deterministic like the audio default above: the first scene by sorted
// SceneId. A project always has at least one scene, so Go To Scene never
// lands with an empty (immediately-red) reference.
void assignDefaultScene(const ProjectDocument& document, LogicBlockDef& block) {
    if (block.typeId != Logic::kSceneGoTo) return;
    std::vector<SceneId> ids;
    ids.reserve(document.data().scenes.size());
    for (const auto& [id, scene] : document.data().scenes) {
        (void)scene; ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) return;
    for (LogicPropertyDef& property : block.properties) {
        if (property.key != "sceneId") continue;
        const auto* current = std::get_if<LogicStringValue>(&property.value);
        if (!current || current->value.empty())
            property.value = LogicStringValue{ids.front()};
    }
}

void assignContextualDefaults(const ProjectDocument& document, LogicBlockDef& block) {
    assignDefaultCollisionObjectType(document, block);
    assignDefaultAnimationClip(document, block);
    assignDefaultAudioAsset(document, block);
    assignDefaultScene(document, block);
    Logic::applyDeterministicVariableDefault(document.data(), block);
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

DuplicateLogicRuleCommand::DuplicateLogicRuleCommand(ObjectTypeId id, LogicRuleId sourceRuleId,
                                                     LogicRuleDef clone, std::size_t index)
    : objectTypeId_(std::move(id)), sourceRuleId_(std::move(sourceRuleId)),
      clone_(std::move(clone)), index_(index) {}
EditorOperationResult DuplicateLogicRuleCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current || clone_.id.empty() || index_ > current->rules.size())
        return EditorOperationResult::failure("Invalid Logic rule duplication");
    const auto source = std::find_if(current->rules.begin(), current->rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == sourceRuleId_; });
    if (source == current->rules.end()
        || static_cast<std::size_t>(source - current->rules.begin()) + 1 != index_)
        return EditorOperationResult::failure("Unknown or moved Logic source rule");
    const bool idTaken = std::any_of(current->rules.begin(), current->rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == clone_.id; });
    if (idTaken) return EditorOperationResult::failure("Duplicate Logic rule id");
    LogicBoardDef next = *current;
    next.rules.insert(next.rules.begin() + static_cast<std::ptrdiff_t>(index_), clone_);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult DuplicateLogicRuleCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

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

AddLogicActionBranchCommand::AddLogicActionBranchCommand(
    ObjectTypeId id, LogicRuleId ruleId, LogicActionBranchDef branch, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), branch_(std::move(branch)), index_(index) {}
EditorOperationResult AddLogicActionBranchCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || branch_.id.empty() || index_ > rule->branches.size()
        || rule->branches.size() >= Logic::kMaxLogicActionBranchesPerRule
        || branchOf(*rule, branch_.id))
        return EditorOperationResult::failure("Invalid Logic action group insertion");
    rule->branches.insert(rule->branches.begin() + static_cast<std::ptrdiff_t>(index_), branch_);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicActionBranchCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicActionBranchCommand::RemoveLogicActionBranchCommand(
    ObjectTypeId id, LogicRuleId ruleId, LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), branchId_(std::move(branchId)) {}
EditorOperationResult RemoveLogicActionBranchCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || rule->branches.size() <= 1) return EditorOperationResult::failure("A Logic rule needs one action group");
    const auto it = std::find_if(rule->branches.begin(), rule->branches.end(),
        [&](const LogicActionBranchDef& branch) { return branch.id == branchId_; });
    if (it == rule->branches.end()) return EditorOperationResult::failure("Unknown Logic action group");
    rule->branches.erase(it);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicActionBranchCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicActionBranchCommand::MoveLogicActionBranchCommand(
    ObjectTypeId id, LogicRuleId ruleId, LogicActionBranchId branchId, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), branchId_(std::move(branchId)), index_(index) {}
EditorOperationResult MoveLogicActionBranchCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || index_ >= rule->branches.size()) return EditorOperationResult::failure("Invalid Logic action group move");
    const auto it = std::find_if(rule->branches.begin(), rule->branches.end(),
        [&](const LogicActionBranchDef& branch) { return branch.id == branchId_; });
    if (it == rule->branches.end()) return EditorOperationResult::failure("Unknown Logic action group");
    const std::size_t from = static_cast<std::size_t>(it - rule->branches.begin());
    if (from == index_) return EditorOperationResult::success(EditorInvalidation::None);
    LogicActionBranchDef moved = std::move(*it);
    rule->branches.erase(it);
    rule->branches.insert(rule->branches.begin() + static_cast<std::ptrdiff_t>(index_), std::move(moved));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicActionBranchCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

DuplicateLogicActionBranchCommand::DuplicateLogicActionBranchCommand(
    ObjectTypeId id, LogicRuleId ruleId, LogicActionBranchId sourceBranchId,
    LogicActionBranchDef copy, std::size_t index)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), sourceBranchId_(std::move(sourceBranchId)),
      copy_(std::move(copy)), index_(index) {}
EditorOperationResult DuplicateLogicActionBranchCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule || !branchOf(*rule, sourceBranchId_) || copy_.id.empty()
        || branchOf(*rule, copy_.id) || index_ > rule->branches.size()
        || rule->branches.size() >= Logic::kMaxLogicActionBranchesPerRule)
        return EditorOperationResult::failure("Invalid Logic action group duplicate");
    rule->branches.insert(rule->branches.begin() + static_cast<std::ptrdiff_t>(index_), copy_);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult DuplicateLogicActionBranchCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicActionBranchExecutionModeCommand::SetLogicActionBranchExecutionModeCommand(
    ObjectTypeId id, LogicRuleId ruleId, LogicActionBranchId branchId, LogicExecutionMode mode)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), branchId_(std::move(branchId)), mode_(mode) {}
EditorOperationResult SetLogicActionBranchExecutionModeCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch) return EditorOperationResult::failure("Unknown Logic action group");
    branch->executionMode = mode_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicActionBranchExecutionModeCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ReplaceLogicTriggerCommand::ReplaceLogicTriggerCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                       LogicBlockDef trigger)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), trigger_(std::move(trigger)) {}
EditorOperationResult ReplaceLogicTriggerCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    if (trigger_.typeId.empty()) return EditorOperationResult::failure("Unknown Logic event type");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    LogicBlockDef trigger = trigger_;
    assignContextualDefaults(document, trigger);
    rule->trigger = std::move(trigger);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ReplaceLogicTriggerCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

AddLogicActionCommand::AddLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                             LogicBlockDef action, std::size_t index,
                                             LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)),
      action_(std::move(action)), index_(index), branchId_(std::move(branchId)) {}
EditorOperationResult AddLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch || index_ > branch->actions.size())
        return EditorOperationResult::failure("Invalid Logic action insertion");
    LogicBlockDef action = action_;
    assignContextualDefaults(document, action);
    auto& actions = branch->actions;
    actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(index_), std::move(action));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicActionCommand::RemoveLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId, std::size_t index,
                                                   LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index), branchId_(std::move(branchId)) {}
EditorOperationResult RemoveLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch || index_ >= branch->actions.size())
        return EditorOperationResult::failure("Unknown Logic action");
    auto& actions = branch->actions;
    actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(index_));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicActionCommand::MoveLogicActionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                               std::size_t from, std::size_t to,
                                               LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), from_(from), to_(to), branchId_(std::move(branchId)) {}
EditorOperationResult MoveLogicActionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch || from_ >= branch->actions.size() || to_ >= branch->actions.size())
        return EditorOperationResult::failure("Invalid Logic action move");
    if (from_ == to_) return EditorOperationResult::success(EditorInvalidation::None);
    auto& actions = branch->actions;
    LogicBlockDef moved = std::move(actions[from_]);
    actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(from_));
    actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(to_), std::move(moved));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicActionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ChangeLogicActionTypeCommand::ChangeLogicActionTypeCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                           std::size_t index, std::string typeId,
                                                           LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index),
      typeId_(std::move(typeId)), branchId_(std::move(branchId)) {}
EditorOperationResult ChangeLogicActionTypeCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch || index_ >= branch->actions.size())
        return EditorOperationResult::failure("Unknown Logic action");
    LogicBlockDef replacement = defaultBlock(typeId_, Logic::BlockKind::Action);
    if (replacement.typeId.empty()) return EditorOperationResult::failure("Unknown Logic action type");
    assignContextualDefaults(document, replacement);
    branch->actions[index_] = std::move(replacement);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ChangeLogicActionTypeCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

AddLogicConditionCommand::AddLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                   LogicBlockDef condition, std::size_t index,
                                                   LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)),
      condition_(std::move(condition)), index_(index), branchId_(std::move(branchId)) {}
EditorOperationResult AddLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || index_ > conditions->size())
        return EditorOperationResult::failure("Invalid Logic condition insertion");
    LogicBlockDef condition = condition_;
    assignContextualDefaults(document, condition);
    conditions->insert(conditions->begin() + static_cast<std::ptrdiff_t>(index_),
                            makeConditionClause(std::move(condition)));
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult AddLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RemoveLogicConditionCommand::RemoveLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                         std::size_t index, LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index), branchId_(std::move(branchId)) {}
EditorOperationResult RemoveLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || index_ >= conditions->size())
        return EditorOperationResult::failure("Unknown Logic condition");
    conditions->erase(conditions->begin() + static_cast<std::ptrdiff_t>(index_));
    if (!conditions->empty()) conditions->front().joinBefore = LogicConditionJoin::And;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RemoveLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

MoveLogicConditionCommand::MoveLogicConditionCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                     std::size_t from, std::size_t to,
                                                     LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), from_(from), to_(to), branchId_(std::move(branchId)) {}
EditorOperationResult MoveLogicConditionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || from_ >= conditions->size() || to_ >= conditions->size())
        return EditorOperationResult::failure("Invalid Logic condition move");
    if (from_ == to_) return EditorOperationResult::success(EditorInvalidation::None);
    LogicConditionClause moved = std::move((*conditions)[from_]);
    conditions->erase(conditions->begin() + static_cast<std::ptrdiff_t>(from_));
    conditions->insert(conditions->begin() + static_cast<std::ptrdiff_t>(to_), std::move(moved));
    conditions->front().joinBefore = LogicConditionJoin::And;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult MoveLogicConditionCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

ChangeLogicConditionTypeCommand::ChangeLogicConditionTypeCommand(
    ObjectTypeId id, LogicRuleId ruleId, std::size_t index, std::string typeId,
    LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index),
      typeId_(std::move(typeId)), branchId_(std::move(branchId)) {}
EditorOperationResult ChangeLogicConditionTypeCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || index_ >= conditions->size())
        return EditorOperationResult::failure("Unknown Logic condition");
    LogicBlockDef replacement = defaultBlock(typeId_, Logic::BlockKind::Condition);
    if (replacement.typeId.empty()) return EditorOperationResult::failure("Unknown Logic condition type");
    assignContextualDefaults(document, replacement);
    (*conditions)[index_].block = std::move(replacement);
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult ChangeLogicConditionTypeCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicConditionJoinCommand::SetLogicConditionJoinCommand(
    ObjectTypeId id, LogicRuleId ruleId, std::size_t index, LogicConditionJoin join,
    LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), index_(index),
      branchId_(std::move(branchId)), join_(join) {}
EditorOperationResult SetLogicConditionJoinCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || index_ >= conditions->size())
        return EditorOperationResult::failure("Unknown Logic condition");
    if (index_ == 0 && join_ != LogicConditionJoin::And)
        return EditorOperationResult::failure("First Logic condition must use AND");
    (*conditions)[index_].joinBefore = join_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicConditionJoinCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicConditionNegatedCommand::SetLogicConditionNegatedCommand(
    ObjectTypeId id, LogicRuleId ruleId, std::size_t index, bool negated,
    LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)),
      index_(index), branchId_(std::move(branchId)), negated_(negated) {}
EditorOperationResult SetLogicConditionNegatedCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    std::vector<LogicConditionClause>* conditions = rule ? conditionsOf(*rule, branchId_) : nullptr;
    if (!conditions || index_ >= conditions->size())
        return EditorOperationResult::failure("Unknown Logic condition");
    (*conditions)[index_].negated = negated_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicConditionNegatedCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

SetLogicPropertyCommand::SetLogicPropertyCommand(ObjectTypeId id, LogicRuleId ruleId,
                                                 LogicPropertyTarget target, std::size_t blockIndex,
                                                 std::string key, LogicValue value,
                                                 LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), target_(target),
      blockIndex_(blockIndex), propertyKey_(std::move(key)), value_(std::move(value)), branchId_(std::move(branchId)) {}
EditorOperationResult SetLogicPropertyCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    LogicBlockDef* block = blockOf(*rule, target_, blockIndex_, branchId_);
    if (!block) return EditorOperationResult::failure("Unknown Logic block");
    auto it = std::find_if(block->properties.begin(), block->properties.end(),
        [&](const LogicPropertyDef& property) { return property.key == propertyKey_; });
    if (it == block->properties.end()) return EditorOperationResult::failure("Unknown Logic property");
    it->value = value_;
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicPropertyCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

CreateAndAssignGlobalVariableCommand::CreateAndAssignGlobalVariableCommand(
    ObjectTypeId objectTypeId, LogicRuleId ruleId, LogicPropertyTarget target,
    std::size_t blockIndex, std::string propertyKey,
    GameVariableDefinition definition, LogicActionBranchId branchId)
    : objectTypeId_(std::move(objectTypeId)), ruleId_(std::move(ruleId)),
      target_(target), blockIndex_(blockIndex),
      propertyKey_(std::move(propertyKey)), definition_(std::move(definition)),
      branchId_(std::move(branchId)) {}

EditorOperationResult CreateAndAssignGlobalVariableCommand::apply(
    ProjectDocument& document) {
    ProjectDoc staged = document.data();
    EntityDef* objectType = objectTypeOf(staged, objectTypeId_);
    if (!objectType) return EditorOperationResult::failure("Unknown Object Type");
    if (!objectType->logicBoard)
        return EditorOperationResult::failure("Object Type has no Logic Board");

    LogicRuleDef* rule = ruleOf(*objectType->logicBoard, ruleId_);
    if (!rule) return EditorOperationResult::failure("Unknown Logic rule");
    LogicBlockDef* block = blockOf(*rule, target_, blockIndex_, branchId_);
    if (!block) return EditorOperationResult::failure("Unknown Logic block");

    const auto property = std::find_if(
        block->properties.begin(), block->properties.end(),
        [&](const LogicPropertyDef& candidate) {
            return candidate.key == propertyKey_;
        });
    if (property == block->properties.end())
        return EditorOperationResult::failure("Unknown Logic property");

    const Logic::LogicPropertyDescriptor* propertyDescriptor =
        descriptorProperty(*block, propertyKey_);
    if (!propertyDescriptor
        || propertyDescriptor->semantic
            != Logic::LogicPropertySemantic::GlobalVariable
        || propertyDescriptor->valueKind != Logic::LogicValueKind::Variable) {
        return EditorOperationResult::failure(
            "Logic property is not a project variable reference");
    }

    const auto requiredType = Logic::requiredVariableType(block->typeId);
    if (!requiredType || *requiredType != definition_.type)
        return EditorOperationResult::failure(
            "Project variable type is incompatible with this Logic block");
    if (!std::holds_alternative<LogicVariableReference>(property->value))
        return EditorOperationResult::failure(
            "Logic property has an invalid value kind");

    const auto duplicate = std::find_if(
        staged.globalVariables.begin(), staged.globalVariables.end(),
        [&](const GameVariableDefinition& variable) {
            return variable.key == definition_.key;
        });
    if (duplicate != staged.globalVariables.end())
        return EditorOperationResult::failure("Global variable key already exists");

    const LogicValue capturedPrevious = property->value;
    staged.globalVariables.push_back(definition_);
    std::string error;
    if (!ProjectJson::validate_current_global_variables_document(
            staged.globalVariables, error)) {
        return EditorOperationResult::failure(error);
    }

    property->value = LogicVariableReference{definition_.key};
    if (const std::string boardError = stagedValidationError(
            staged, objectTypeId_, *objectType, *objectType->logicBoard);
        !boardError.empty()) {
        return EditorOperationResult::failure(boardError);
    }

    if (!previousValue_) previousValue_ = capturedPrevious;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kContextVariableInvalidation, DomainChange::projectChanged());
}

EditorOperationResult CreateAndAssignGlobalVariableCommand::undo(
    ProjectDocument& document) {
    if (!previousValue_)
        return EditorOperationResult::failure(
            "Cannot undo project variable creation before apply");

    ProjectDoc staged = document.data();
    EntityDef* objectType = objectTypeOf(staged, objectTypeId_);
    if (!objectType || !objectType->logicBoard)
        return EditorOperationResult::failure(
            "Cannot undo: Logic Board no longer exists");
    LogicRuleDef* rule = ruleOf(*objectType->logicBoard, ruleId_);
    LogicBlockDef* block =
        rule ? blockOf(*rule, target_, blockIndex_, branchId_) : nullptr;
    if (!block)
        return EditorOperationResult::failure(
            "Cannot undo: Logic block no longer exists");
    const auto property = std::find_if(
        block->properties.begin(), block->properties.end(),
        [&](const LogicPropertyDef& candidate) {
            return candidate.key == propertyKey_;
        });
    if (property == block->properties.end())
        return EditorOperationResult::failure(
            "Cannot undo: Logic property no longer exists");
    const auto* currentReference =
        std::get_if<LogicVariableReference>(&property->value);
    if (!currentReference || currentReference->id != definition_.key)
        return EditorOperationResult::failure(
            "Cannot undo: Logic property no longer references the created variable");

    const auto variable = std::find_if(
        staged.globalVariables.begin(), staged.globalVariables.end(),
        [&](const GameVariableDefinition& candidate) {
            return candidate.key == definition_.key;
        });
    if (variable == staged.globalVariables.end())
        return EditorOperationResult::failure(
            "Cannot undo: created project variable no longer exists");

    property->value = *previousValue_;
    staged.globalVariables.erase(variable);
    std::string error;
    if (!ProjectJson::validate_current_global_variables_document(
            staged.globalVariables, error)) {
        return EditorOperationResult::failure(error);
    }
    if (const std::string boardError = stagedValidationError(
            staged, objectTypeId_, *objectType, *objectType->logicBoard);
        !boardError.empty()) {
        return EditorOperationResult::failure(boardError);
    }

    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kContextVariableInvalidation, DomainChange::projectChanged());
}

SetLogicAnimationClipCommand::SetLogicAnimationClipCommand(
    ObjectTypeId id, LogicRuleId ruleId, std::size_t actionIndex,
    AssetId animationAssetId, std::string clipId, LogicActionBranchId branchId)
    : objectTypeId_(std::move(id)), ruleId_(std::move(ruleId)), actionIndex_(actionIndex),
      animationAssetId_(std::move(animationAssetId)), clipId_(std::move(clipId)),
      branchId_(std::move(branchId)) {}

EditorOperationResult SetLogicAnimationClipCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    LogicBoardDef next = *current;
    LogicRuleDef* rule = ruleOf(next, ruleId_);
    LogicActionBranchDef* branch = rule ? branchOf(*rule, branchId_) : nullptr;
    if (!branch || actionIndex_ >= branch->actions.size())
        return EditorOperationResult::failure("Unknown Logic action");
    LogicBlockDef& action = branch->actions[actionIndex_];
    if (action.typeId != Logic::kAnimationPlayClip)
        return EditorOperationResult::failure("Logic action is not Play Clip");

    bool changedAsset = false;
    bool changedClip = false;
    for (LogicPropertyDef& property : action.properties) {
        if (property.key == "animationAssetId") {
            property.value = LogicAssetReference{animationAssetId_};
            changedAsset = true;
        } else if (property.key == "clipId") {
            property.value = LogicStringValue{clipId_};
            changedClip = true;
        }
    }
    if (!changedAsset || !changedClip)
        return EditorOperationResult::failure("Play Clip action is missing properties");
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult SetLogicAnimationClipCommand::undo(ProjectDocument& document) { UNDO_BOARD(); }

RepairIncompatibleLogicCommand::RepairIncompatibleLogicCommand(ObjectTypeId id,
                                                               IncompatibleLogicRepair repair)
    : objectTypeId_(std::move(id)), repair_(repair) {}

EditorOperationResult RepairIncompatibleLogicCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object Type has no Logic Board");
    const EntityDef* owner = document.findObjectType(objectTypeId_);
    if (!owner) return EditorOperationResult::failure("Unknown Object Type: " + objectTypeId_);

    const LogicComponentReferenceReport hits =
        collectIncompatibleLogicReferences(document, objectTypeId_);
    if (hits.empty()) return EditorOperationResult::success(EditorInvalidation::None);

    LogicBoardDef next = *current;
    auto ruleIncompatible = [&](const LogicRuleDef& rule) {
        for (const LogicComponentReference& ref : hits.references) {
            if (ref.ruleId == rule.id) return true;
        }
        return false;
    };
    auto actionIncompatible = [&](const LogicRuleId& ruleId,
                                  const LogicActionBranchId& branchId,
                                  std::size_t index) {
        for (const LogicComponentReference& ref : hits.references) {
            if (ref.ruleId == ruleId && ref.slot == LogicReferenceSlot::Action
                && ref.branchId == branchId && ref.blockIndex == index) {
                return true;
            }
        }
        return false;
    };

    switch (repair_) {
    case IncompatibleLogicRepair::DisableAffectedRules:
        for (LogicRuleDef& rule : next.rules) {
            if (ruleIncompatible(rule)) rule.enabled = false;
        }
        break;
    case IncompatibleLogicRepair::RemoveAffectedActions:
        for (LogicRuleDef& rule : next.rules) {
            for (LogicActionBranchDef& branch : rule.branches) {
                for (std::size_t i = branch.actions.size(); i > 0; --i) {
                    const std::size_t index = i - 1;
                    if (actionIncompatible(rule.id, branch.id, index)) {
                        branch.actions.erase(
                            branch.actions.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                }
            }
        }
        break;
    case IncompatibleLogicRepair::RemoveAffectedRules:
        next.rules.erase(std::remove_if(next.rules.begin(), next.rules.end(),
                                        [&](const LogicRuleDef& rule) {
                                            return ruleIncompatible(rule);
                                        }),
                         next.rules.end());
        break;
    }
    COMMIT_NEXT_BOARD(next);
}
EditorOperationResult RepairIncompatibleLogicCommand::undo(ProjectDocument& document) {
    UNDO_BOARD();
}

LogicRuleId nextLogicRuleId(const LogicBoardDef& board) {
    int maxOrdinal = 0;
    for (const LogicRuleDef& rule : board.rules) {
        if (rule.id.rfind("rule-", 0) != 0) continue;
        const int ordinal = std::atoi(rule.id.c_str() + 5);
        maxOrdinal = std::max(maxOrdinal, ordinal);
    }
    return "rule-" + std::to_string(maxOrdinal + 1);
}

LogicActionBranchId nextLogicActionBranchId(const LogicRuleDef& rule) {
    int maxOrdinal = 0;
    for (const LogicActionBranchDef& branch : rule.branches) {
        if (branch.id.rfind("branch-", 0) != 0) continue;
        const int ordinal = std::atoi(branch.id.c_str() + 7);
        maxOrdinal = std::max(maxOrdinal, ordinal);
    }
    return "branch-" + std::to_string(maxOrdinal + 1);
}

#undef COMMIT_NEXT_BOARD
#undef UNDO_BOARD

} // namespace ArtCade::EditorNative
