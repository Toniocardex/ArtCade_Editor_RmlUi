#include "editor-native/ui/logic_board_editor_controller.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/logic_board_commands.h"
#include "logic-core.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

std::vector<std::string> splitPipe(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find('|', start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

} // namespace

LogicBoardEditorController::LogicBoardEditorController(
    EditorCoordinator& coordinator, Rml::ElementDocument* document)
    : coordinator_(coordinator), document_(document) {}

void LogicBoardEditorController::detach() {
    panel_.closeDropdown();
    document_ = nullptr;
}

void LogicBoardEditorController::refresh() {
    panel_.refresh(document_, coordinator_);
}

void LogicBoardEditorController::toggleDropdown(const std::string& dropdownId) {
    panel_.toggleDropdown(document_, coordinator_, dropdownId);
}

void LogicBoardEditorController::closeDropdown() {
    panel_.closeDropdown();
}

bool LogicBoardEditorController::handleAction(
    const std::string& action, const std::string& arg, const std::string& value,
    const WorkspaceSwitchPreparation& prepareWorkspaceSwitch) {
    if (action == "open-scene-workspace") {
        if (prepareWorkspaceSwitch) prepareWorkspaceSwitch();
        coordinator_.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Scene});
        return true;
    }
    if (action == "open-logic-workspace") {
        if (prepareWorkspaceSwitch) prepareWorkspaceSwitch();
        const SceneInstanceDef* selected = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (selected) coordinator_.apply(OpenLogicBoardIntent{selected->objectTypeId});
        else coordinator_.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Logic});
        return true;
    }
    if (action == "select-logic-object-type") {
        if (!value.empty()) coordinator_.apply(OpenLogicBoardIntent{value});
        return true;
    }
    if (action == "logic-tab-rules" || action == "logic-tab-lua") {
        coordinator_.apply(SetLogicBoardTabIntent{
            action == "logic-tab-lua" ? LogicBoardTab::GeneratedLua : LogicBoardTab::Rules});
        return true;
    }
    if (action == "commit-logic-search") {
        coordinator_.apply(SetLogicBoardSearchIntent{value});
        return true;
    }

    const auto& view = coordinator_.state().logicBoardEditor;
    if (!view.objectTypeId || !coordinator_.document().hasObjectType(*view.objectTypeId))
        return action.rfind("logic-", 0) != std::string::npos;
    const ObjectTypeId objectTypeId = *view.objectTypeId;
    const EntityDef& objectType = coordinator_.document().data().objectTypes.at(objectTypeId);

    if (action == "validate-logic-board") {
        if (!objectType.logicBoard) return true;
        const Logic::LogicCompileResult result =
            Logic::compileBoard(objectTypeId, *objectType.logicBoard);
        if (result.diagnostics.empty()) {
            coordinator_.logInfo("Logic valid · " + objectTypeId);
        } else {
            for (const Logic::LogicDiagnostic& diagnostic : result.diagnostics) {
                const std::string message = "Logic "
                    + std::string(diagnostic.severity == Logic::DiagnosticSeverity::Error
                                      ? "error" : "warning")
                    + " · " + objectTypeId + " · " + diagnostic.ruleId
                    + " · " + diagnostic.message;
                if (diagnostic.severity == Logic::DiagnosticSeverity::Error)
                    coordinator_.logError(message);
                else
                    coordinator_.logWarning(message);
            }
        }
        return true;
    }

    const bool authoringAction = action == "create-logic-board" || action == "remove-logic-board"
        || action == "add-logic-rule" || action == "remove-logic-rule"
        || action == "move-logic-rule-up" || action == "move-logic-rule-down"
        || action == "toggle-logic-rule" || action == "change-logic-trigger"
        || action == "set-logic-key" || action == "add-logic-action"
        || action == "remove-logic-action" || action == "move-logic-action-up"
        || action == "move-logic-action-down" || action == "change-logic-action"
        || action == "toggle-logic-visible" || action == "commit-logic-position-x"
        || action == "commit-logic-position-y";
    if (coordinator_.isPlaying() && authoringAction) return true;

    if (action == "create-logic-board") {
        coordinator_.execute(CreateLogicBoardCommand{objectTypeId});
        return true;
    }
    if (action == "remove-logic-board") {
        coordinator_.execute(RemoveLogicBoardCommand{objectTypeId});
        return true;
    }
    if (!objectType.logicBoard) return authoringAction;
    const LogicBoardDef& board = *objectType.logicBoard;
    const auto ruleById = [&](const LogicRuleId& id) -> const LogicRuleDef* {
        for (const LogicRuleDef& rule : board.rules) if (rule.id == id) return &rule;
        return nullptr;
    };
    const auto ruleIndex = [&](const LogicRuleId& id) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < board.rules.size(); ++i)
            if (board.rules[i].id == id) return i;
        return std::nullopt;
    };
    const auto parseActionArg = [&](const std::string& encoded,
                                    LogicRuleId& ruleId,
                                    std::size_t& index) -> bool {
        const std::vector<std::string> parts = splitPipe(encoded);
        if (parts.size() != 2 || parts[1].empty()) return false;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(parts[1].c_str(), &end, 10);
        if (!end || *end != '\0') return false;
        ruleId = parts[0];
        index = static_cast<std::size_t>(parsed);
        return true;
    };

    if (action == "add-logic-rule") {
        if (view.tab != LogicBoardTab::Rules) return true;
        coordinator_.execute(AddLogicRuleCommand{
            objectTypeId, Logic::makeDefaultRule(nextLogicRuleId(board)), board.rules.size()});
    } else if (action == "remove-logic-rule") {
        coordinator_.execute(RemoveLogicRuleCommand{objectTypeId, arg});
    } else if (action == "move-logic-rule-up" || action == "move-logic-rule-down") {
        if (const auto index = ruleIndex(arg)) {
            const std::size_t destination = action == "move-logic-rule-up"
                ? (*index == 0 ? 0 : *index - 1)
                : std::min(*index + 1, board.rules.size() - 1);
            coordinator_.execute(MoveLogicRuleCommand{objectTypeId, arg, destination});
        }
    } else if (action == "toggle-logic-rule") {
        if (const LogicRuleDef* rule = ruleById(arg))
            coordinator_.execute(SetLogicRuleEnabledCommand{objectTypeId, arg, !rule->enabled});
    } else if (action == "change-logic-trigger") {
        LogicBlockDef trigger = Logic::makeDefaultTrigger();
        if (value == Logic::kKeyPressed) {
            trigger.typeId = Logic::kKeyPressed;
            trigger.properties = {{"key", LogicKey::Space}};
        }
        coordinator_.execute(ReplaceLogicTriggerCommand{objectTypeId, arg, std::move(trigger)});
    } else if (action == "set-logic-key") {
        if (const auto key = Logic::logicKeyFromName(value)) {
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, arg, LogicPropertyTarget::Trigger, 0, "key", *key});
        }
    } else if (action == "add-logic-action") {
        if (const LogicRuleDef* rule = ruleById(arg)) {
            coordinator_.execute(AddLogicActionCommand{
                objectTypeId, arg, Logic::makeDefaultAction(), rule->actions.size()});
        }
    } else if (action == "remove-logic-action" || action == "move-logic-action-up"
               || action == "move-logic-action-down" || action == "change-logic-action"
               || action == "toggle-logic-visible" || action == "commit-logic-position-x"
               || action == "commit-logic-position-y") {
        LogicRuleId ruleId;
        std::size_t index = 0;
        if (!parseActionArg(arg, ruleId, index)) return true;
        const LogicRuleDef* rule = ruleById(ruleId);
        if (!rule || index >= rule->actions.size()) return true;
        if (action == "remove-logic-action") {
            coordinator_.execute(RemoveLogicActionCommand{objectTypeId, ruleId, index});
        } else if (action == "move-logic-action-up" || action == "move-logic-action-down") {
            const std::size_t destination = action == "move-logic-action-up"
                ? (index == 0 ? 0 : index - 1)
                : std::min(index + 1, rule->actions.size() - 1);
            coordinator_.execute(MoveLogicActionCommand{objectTypeId, ruleId, index, destination});
        } else if (action == "change-logic-action") {
            coordinator_.execute(ChangeLogicActionTypeCommand{objectTypeId, ruleId, index, value});
        } else if (action == "toggle-logic-visible") {
            bool visible = true;
            if (const LogicPropertyDef* p = Logic::findProperty(rule->actions[index], "visible"))
                if (const auto* current = std::get_if<bool>(&p->value)) visible = *current;
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index, "visible", !visible});
        } else {
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed) {
                coordinator_.logError("Logic position must be a finite number");
                return true;
            }
            Vec2 position{};
            if (const LogicPropertyDef* p = Logic::findProperty(rule->actions[index], "position"))
                if (const auto* current = std::get_if<Vec2>(&p->value)) position = *current;
            if (action == "commit-logic-position-x") position.x = *parsed;
            else position.y = *parsed;
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index, "position", position});
        }
    } else {
        return false;
    }
    return true;
}

} // namespace ArtCade::EditorNative
