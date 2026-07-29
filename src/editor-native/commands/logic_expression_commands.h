#pragma once

#include "editor-native/commands/editor_command.h"
#include "logic-core.h"

#include <cstddef>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

enum class LogicNumericComponent { X, Y };

struct LogicNumberExpressionAddress {
    ObjectTypeId objectTypeId;
    LogicBoardId boardId;
    LogicRuleId ruleId;
    LogicActionId actionId;
    std::string parameterId;
    LogicNumericComponent component = LogicNumericComponent::X;
};

/** ADR-0028: replace one Vec2 component NumberExpression (Set Position.X/Y). */
class SetLogicNumberExpressionCommand final : public EditorCommand {
public:
    SetLogicNumberExpressionCommand(LogicNumberExpressionAddress address,
                                    NumberExpression expression);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetLogicNumberExpression"; }

private:
    LogicNumberExpressionAddress address_;
    NumberExpression next_;
    std::optional<LogicBoardDef> before_;
};

} // namespace ArtCade::EditorNative
