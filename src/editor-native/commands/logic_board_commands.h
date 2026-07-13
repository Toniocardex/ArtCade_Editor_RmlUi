#pragma once

#include "editor-native/commands/editor_command.h"
#include "core/types.h"

#include <cstddef>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class CreateLogicBoardCommand final : public EditorCommand {
public:
    explicit CreateLogicBoardCommand(ObjectTypeId objectTypeId);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "CreateLogicBoard"; }
private:
    ObjectTypeId objectTypeId_;
};

class RemoveLogicBoardCommand final : public EditorCommand {
public:
    explicit RemoveLogicBoardCommand(ObjectTypeId objectTypeId);
    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
    const char* name() const override { return "RemoveLogicBoard"; }
private:
    ObjectTypeId objectTypeId_;
    std::optional<LogicBoardDef> removed_;
};

#define ARTCADE_LOGIC_BOARD_COMMAND_COMMON(ClassName) \
    EditorOperationResult apply(ProjectDocument&) override; \
    EditorOperationResult undo(ProjectDocument&) override; \
    const char* name() const override { return #ClassName; }

class AddLogicRuleCommand final : public EditorCommand {
public:
    AddLogicRuleCommand(ObjectTypeId objectTypeId, LogicRuleDef rule, std::size_t index);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(AddLogicRule)
private:
    ObjectTypeId objectTypeId_; LogicRuleDef rule_; std::size_t index_ = 0;
    std::optional<LogicBoardDef> before_;
};

class RemoveLogicRuleCommand final : public EditorCommand {
public:
    RemoveLogicRuleCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(RemoveLogicRule)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; std::optional<LogicBoardDef> before_;
};

class MoveLogicRuleCommand final : public EditorCommand {
public:
    MoveLogicRuleCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId, std::size_t index);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(MoveLogicRule)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; std::size_t index_ = 0;
    std::optional<LogicBoardDef> before_;
};

class SetLogicRuleEnabledCommand final : public EditorCommand {
public:
    SetLogicRuleEnabledCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId, bool enabled);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(SetLogicRuleEnabled)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; bool enabled_ = true;
    std::optional<LogicBoardDef> before_;
};

class ReplaceLogicTriggerCommand final : public EditorCommand {
public:
    ReplaceLogicTriggerCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId,
                               LogicBlockDef trigger);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(ReplaceLogicTrigger)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; LogicBlockDef trigger_;
    std::optional<LogicBoardDef> before_;
};

class AddLogicActionCommand final : public EditorCommand {
public:
    AddLogicActionCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId,
                          LogicBlockDef action, std::size_t index);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(AddLogicAction)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; LogicBlockDef action_; std::size_t index_ = 0;
    std::optional<LogicBoardDef> before_;
};

class RemoveLogicActionCommand final : public EditorCommand {
public:
    RemoveLogicActionCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId, std::size_t index);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(RemoveLogicAction)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; std::size_t index_ = 0;
    std::optional<LogicBoardDef> before_;
};

class MoveLogicActionCommand final : public EditorCommand {
public:
    MoveLogicActionCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId,
                           std::size_t from, std::size_t to);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(MoveLogicAction)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; std::size_t from_ = 0, to_ = 0;
    std::optional<LogicBoardDef> before_;
};

class ChangeLogicActionTypeCommand final : public EditorCommand {
public:
    ChangeLogicActionTypeCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId,
                                 std::size_t index, std::string typeId);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(ChangeLogicActionType)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; std::size_t index_ = 0; std::string typeId_;
    std::optional<LogicBoardDef> before_;
};

enum class LogicPropertyTarget { Trigger, Action };

class SetLogicPropertyCommand final : public EditorCommand {
public:
    SetLogicPropertyCommand(ObjectTypeId objectTypeId, LogicRuleId ruleId,
                            LogicPropertyTarget target, std::size_t actionIndex,
                            std::string propertyKey, LogicValue value);
    ARTCADE_LOGIC_BOARD_COMMAND_COMMON(SetLogicProperty)
private:
    ObjectTypeId objectTypeId_; LogicRuleId ruleId_; LogicPropertyTarget target_;
    std::size_t actionIndex_ = 0; std::string propertyKey_; LogicValue value_;
    std::optional<LogicBoardDef> before_;
};

#undef ARTCADE_LOGIC_BOARD_COMMAND_COMMON

LogicRuleId nextLogicRuleId(const LogicBoardDef& board);

} // namespace ArtCade::EditorNative

