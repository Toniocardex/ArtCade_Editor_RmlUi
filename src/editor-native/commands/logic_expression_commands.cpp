#include "editor-native/commands/logic_expression_commands.h"

#include "editor-native/model/project_document.h"
#include "logic-number-expression-validation.h"

#include <algorithm>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kLogicInvalidation = EditorInvalidation::LogicBoard;

const LogicBoardDef* boardOf(const ProjectDocument& document, const ObjectTypeId& id) {
    const EntityDef* type = document.findObjectType(id);
    return type && type->logicBoard ? &*type->logicBoard : nullptr;
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

} // namespace

SetLogicNumberExpressionCommand::SetLogicNumberExpressionCommand(
    LogicNumberExpressionAddress address, NumberExpression expression)
    : address_(std::move(address)), next_(std::move(expression)) {}

EditorOperationResult SetLogicNumberExpressionCommand::apply(ProjectDocument& document) {
    const LogicBoardDef* current = boardOf(document, address_.objectTypeId);
    if (!current || current->id != address_.boardId)
        return EditorOperationResult::failure("Unknown or replaced Logic Board");
    LogicBoardDef next = *current;
    auto ruleIt = std::find_if(next.rules.begin(), next.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == address_.ruleId; });
    const auto actionDef = ruleIt == next.rules.end()
        ? std::vector<LogicActionDef>::iterator{}
        : std::find_if(ruleIt->actions.begin(), ruleIt->actions.end(),
            [&](const LogicActionDef& candidate) {
                return candidate.id == address_.actionId;
            });
    if (ruleIt == next.rules.end() || actionDef == ruleIt->actions.end())
        return EditorOperationResult::failure("Logic action not found");
    LogicBlockDef& action = actionDef->block;
    const Logic::LogicBlockDescriptor* block = Logic::findDescriptor(action.typeId);
    if (!block) return EditorOperationResult::failure("Unknown Logic action");
    const auto propertyDesc = std::find_if(
        block->properties.begin(), block->properties.end(),
        [&](const Logic::LogicPropertyDescriptor& property) {
            return property.key == address_.parameterId;
        });
    if (propertyDesc == block->properties.end()
        || propertyDesc->valueKind != Logic::LogicValueKind::Vec2
        || propertyDesc->numericExpressionPolicy
            != Logic::NumericExpressionPolicy::PerComponentNumberExpression) {
        return EditorOperationResult::failure(
            "Dynamic expressions are not enabled for this property");
    }
    auto propertyIt = std::find_if(action.properties.begin(), action.properties.end(),
        [&](const LogicPropertyDef& property) {
            return property.key == address_.parameterId;
        });
    if (propertyIt == action.properties.end())
        return EditorOperationResult::failure("Missing parameter value");
    auto* vec = std::get_if<LogicVec2Value>(&propertyIt->value);
    if (!vec) return EditorOperationResult::failure("Parameter value is not a Vec2 expression");

    const Logic::NumberExpressionValidationResult validation =
        Logic::validateNumberExpression(next_, [&]() {
            Logic::NumberExpressionContext context;
            context.globalVariables = &document.data().globalVariables;
            if (const EntityDef* type = document.findObjectType(address_.objectTypeId))
                context.localVariables = &type->localVariables;
            if (const LogicBoardDef* board = boardOf(document, address_.objectTypeId)) {
                for (const LogicRuleDef& rule : board->rules) {
                    if (rule.id != address_.ruleId) continue;
                    context.deltaSecondsAvailable =
                        rule.trigger.typeId == Logic::kEveryFrame;
                    break;
                }
            }
            return context;
        }());
    if (!validation.ok) {
        return EditorOperationResult::failure(validation.message.empty()
            ? validation.errorCode : validation.message);
    }

    NumberExpression& target =
        address_.component == LogicNumericComponent::X ? vec->x : vec->y;
    if (sameNumberExpression(target, next_))
        return EditorOperationResult::success(EditorInvalidation::None);
    target = next_;

    if (sameBoard(*current, next))
        return EditorOperationResult::success(EditorInvalidation::None);
    const std::string invalid = validationError(document, address_.objectTypeId, next);
    if (!invalid.empty()) return EditorOperationResult::failure(invalid);
    if (!before_) before_ = *current;
    if (!document.replaceLogicBoard(address_.objectTypeId, next))
        return EditorOperationResult::failure("Cannot update Logic Board");
    return changed(address_.objectTypeId);
}

EditorOperationResult SetLogicNumberExpressionCommand::undo(ProjectDocument& document) {
    if (!before_ || !document.replaceLogicBoard(address_.objectTypeId, *before_))
        return EditorOperationResult::failure("Cannot undo Logic Board change");
    return changed(address_.objectTypeId);
}

} // namespace ArtCade::EditorNative
