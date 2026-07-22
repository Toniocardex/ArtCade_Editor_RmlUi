#include "editor-native/ui/logic_board_editor_controller.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/global_variable_commands.h"
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

std::string defaultClipId(const SpriteAnimationAssetDef& asset) {
    return asset.clips.empty() ? std::string{} : asset.clips.front().id;
}

struct PropertyAddress {
    LogicRuleId ruleId;
    LogicPropertyTarget target = LogicPropertyTarget::Trigger;
    std::size_t index = 0;
    std::string key;
    std::string component;
};

std::optional<PropertyAddress> parsePropertyAddress(const std::string& encoded) {
    const std::vector<std::string> parts = splitPipe(encoded);
    if (parts.size() != 4 && parts.size() != 5) return std::nullopt;
    char* end = nullptr;
    const unsigned long index = std::strtoul(parts[2].c_str(), &end, 10);
    if (!end || *end != '\0' || parts[0].empty() || parts[3].empty()) return std::nullopt;
    LogicPropertyTarget target = LogicPropertyTarget::Trigger;
    if (parts[1] == "a") target = LogicPropertyTarget::Action;
    else if (parts[1] == "c") target = LogicPropertyTarget::Condition;
    else if (parts[1] != "t") return std::nullopt;
    return PropertyAddress{
        parts[0], target, static_cast<std::size_t>(index), parts[3],
        parts.size() == 5 ? parts[4] : std::string{}};
}

} // namespace

LogicBoardEditorController::LogicBoardEditorController(
    EditorCoordinator& coordinator, Rml::ElementDocument* document)
    : coordinator_(coordinator), document_(document) {}

void LogicBoardEditorController::detach() {
    panel_.closeDropdown();
    panel_.clearKeyBindingEditor();
    document_ = nullptr;
}

void LogicBoardEditorController::refresh() {
    panel_.refresh(document_, coordinator_);
}

void LogicBoardEditorController::restoreAfterLayout() {
    panel_.restoreAfterLayout(document_, coordinator_);
}

void LogicBoardEditorController::toggleDropdown(const std::string& dropdownId) {
    panel_.toggleDropdown(document_, coordinator_, dropdownId);
}

void LogicBoardEditorController::closeDropdown() {
    panel_.closeDropdown();
}

bool LogicBoardEditorController::hasKeyCapture() const {
    return panel_.hasKeyCapture();
}

bool LogicBoardEditorController::captureKey(LogicKey key) {
    if (!panel_.hasKeyCapture() || coordinator_.isPlaying()) return false;
    const std::string address = panel_.keyCaptureAddress();
    panel_.clearKeyBindingEditor();
    return handleAction("pick-logic-key-binding", address, Logic::logicKeyName(key), {});
}

bool LogicBoardEditorController::cancelKeyCapture() {
    if (!panel_.hasKeyCapture()) return false;
    panel_.cancelKeyCapture(document_, coordinator_);
    return true;
}

void LogicBoardEditorController::toggleVariablesDrawer() {
    panel_.toggleVariablesDrawer(document_, coordinator_);
}

void LogicBoardEditorController::toggleRuleCollapsed(const std::string& ruleId) {
    panel_.toggleRuleCollapsed(document_, coordinator_, ruleId);
}

void LogicBoardEditorController::collapseAllRules() {
    panel_.collapseAllRules(document_, coordinator_);
}

void LogicBoardEditorController::expandAllRules() {
    panel_.expandAllRules(document_, coordinator_);
}

bool LogicBoardEditorController::canCollapseAllRules() const {
    return panel_.canCollapseAllRules(coordinator_);
}

bool LogicBoardEditorController::canExpandAllRules() const {
    return panel_.canExpandAllRules(coordinator_);
}

void LogicBoardEditorController::syncResponsiveClass() {
    panel_.syncResponsiveClass(document_);
}

std::string LogicBoardEditorController::objectTypeMenuEntries() const {
    return panel_.objectTypeMenuEntries(coordinator_);
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
    if (action == "begin-logic-key-capture") {
        panel_.beginKeyCapture(document_, coordinator_, arg);
        return true;
    }
    if (action == "toggle-logic-key-search") {
        panel_.toggleKeySearch(document_, coordinator_, arg);
        return true;
    }
    if (action == "filter-logic-key-search") {
        panel_.setKeySearchQuery(document_, coordinator_, arg, value);
        return true;
    }
    if (action == "toggle-global-variables") {
        panel_.toggleVariablesDrawer(document_, coordinator_);
        return true;
    }
    const bool variableAuthoringAction =
        action == "add-global-variable" || action == "remove-global-variable"
        || action == "commit-global-variable-key" || action == "set-global-variable-type"
        || action == "commit-global-variable-value"
        || action == "toggle-global-variable-value"
        || action == "commit-global-variable-description";
    if (variableAuthoringAction) {
        if (coordinator_.isPlaying()) return true;
        if (action == "add-global-variable") {
            GameVariableDefinition definition;
            for (int n = 1;; ++n) {
                definition.key = "variable-" + std::to_string(n);
                const auto& variables = coordinator_.document().data().globalVariables;
                const bool taken = std::any_of(
                    variables.begin(), variables.end(),
                    [&](const GameVariableDefinition& item) {
                        return item.key == definition.key;
                    });
                if (!taken) break;
            }
            coordinator_.execute(AddGlobalVariableCommand{std::move(definition)});
        } else if (action == "remove-global-variable") {
            coordinator_.execute(RemoveGlobalVariableCommand{arg});
        } else if (action == "commit-global-variable-key") {
            coordinator_.execute(RenameGlobalVariableCommand{arg, value});
        } else if (action == "set-global-variable-type") {
            const auto type = value == "boolean" ? GameVariableDefinition::Type::Boolean
                : value == "string" ? GameVariableDefinition::Type::String
                                    : GameVariableDefinition::Type::Number;
            panel_.closeDropdown();
            coordinator_.execute(SetGlobalVariableTypeCommand{arg, type});
        } else if (action == "commit-global-variable-value") {
            const auto& variables = coordinator_.document().data().globalVariables;
            const auto it = std::find_if(
                variables.begin(), variables.end(),
                [&](const GameVariableDefinition& item) { return item.key == arg; });
            if (it == variables.end()) return true;
            if (it->type == GameVariableDefinition::Type::String) {
                coordinator_.execute(SetGlobalVariableInitialValueCommand{arg, value});
            } else if (it->type == GameVariableDefinition::Type::Number) {
                const std::optional<float> parsed = parseNumberField(value);
                if (!parsed) coordinator_.logError("Global variable value must be a finite number");
                else coordinator_.execute(
                    SetGlobalVariableInitialValueCommand{arg, static_cast<double>(*parsed)});
            }
        } else if (action == "toggle-global-variable-value") {
            const auto& variables = coordinator_.document().data().globalVariables;
            const auto it = std::find_if(
                variables.begin(), variables.end(),
                [&](const GameVariableDefinition& item) { return item.key == arg; });
            if (it != variables.end()) {
                if (const auto* current = std::get_if<bool>(&it->initialValue))
                    coordinator_.execute(SetGlobalVariableInitialValueCommand{arg, !*current});
            }
        } else if (action == "commit-global-variable-description") {
            coordinator_.execute(SetGlobalVariableDescriptionCommand{arg, value});
        }
        return true;
    }
    if (action == "change-logic-trigger" || action == "change-logic-action"
        || action == "add-logic-action-type"
        || action == "change-logic-condition" || action == "add-logic-condition-type"
        || action == "set-logic-event-collision-object-type"
        || action == "set-logic-animation-asset" || action == "set-logic-animation-clip"
        || action == "set-logic-execution-mode"
        || action == "pick-logic-property" || action == "pick-logic-key-binding") {
        panel_.closeDropdown();
    }
    if (action == "pick-logic-key-binding") panel_.clearKeyBindingEditor();

    const auto& view = coordinator_.state().logicBoardEditor;
    if (!view.objectTypeId || !coordinator_.document().hasObjectType(*view.objectTypeId))
        return action.rfind("logic-", 0) == 0 || action == "pick-logic-key-binding";
    const ObjectTypeId objectTypeId = *view.objectTypeId;
    const EntityDef& objectType = coordinator_.document().data().objectTypes.at(objectTypeId);

    if (action == "validate-logic-board") {
        if (!objectType.logicBoard) return true;
        const Logic::LogicCompileResult result =
            Logic::compileBoard(objectTypeId, *objectType.logicBoard, &objectType,
                                &coordinator_.document().data());
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
        || action == "add-logic-rule" || action == "duplicate-logic-rule"
        || action == "remove-logic-rule"
        || action == "move-logic-rule-up" || action == "move-logic-rule-down"
        || action == "toggle-logic-rule" || action == "change-logic-trigger"
        || action == "set-logic-key" || action == "add-logic-action-type"
        || action == "remove-logic-action" || action == "move-logic-action-up"
        || action == "move-logic-action-down" || action == "change-logic-action"
        || action == "toggle-logic-visible" || action == "commit-logic-position-x"
        || action == "commit-logic-position-y" || action == "commit-logic-offset-x"
        || action == "commit-logic-offset-y" || action == "set-logic-animation-asset"
        || action == "set-logic-animation-clip" || action == "commit-logic-animation-speed"
        || action == "set-logic-audio-asset" || action == "commit-logic-audio-volume"
        || action == "toggle-logic-event-expected"
        || action == "set-logic-event-collision-object-type"
        || action == "set-logic-execution-mode"
        || action == "add-logic-condition-type"
        || action == "change-logic-condition"
        || action == "remove-logic-condition"
        || action == "move-logic-condition-up"
        || action == "move-logic-condition-down"
        || action == "set-logic-condition-join"
        || action == "toggle-logic-condition-negated"
        || action == "pick-logic-property" || action == "pick-logic-key-binding"
        || action == "commit-logic-property"
        || action == "toggle-logic-property"
        || action == "commit-logic-property-component";
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

    const auto blockAt = [&](const PropertyAddress& address) -> const LogicBlockDef* {
        const LogicRuleDef* rule = ruleById(address.ruleId);
        if (!rule) return nullptr;
        switch (address.target) {
        case LogicPropertyTarget::Trigger: return &rule->trigger;
        case LogicPropertyTarget::Action:
            return address.index < rule->actions.size() ? &rule->actions[address.index] : nullptr;
        case LogicPropertyTarget::Condition:
            return address.index < rule->conditions.size()
                ? &rule->conditions[address.index].block : nullptr;
        }
        return nullptr;
    };
    const auto executeProperty = [&](const PropertyAddress& address, LogicValue propertyValue) {
        coordinator_.execute(SetLogicPropertyCommand{
            objectTypeId, address.ruleId, address.target, address.index,
            address.key, std::move(propertyValue)});
    };

    if (action == "add-logic-condition-type") {
        coordinator_.apply(AddLogicConditionTypeIntent{objectTypeId, arg, value});
        return true;
    }
    if (action == "change-logic-condition" || action == "remove-logic-condition"
        || action == "move-logic-condition-up" || action == "move-logic-condition-down"
        || action == "set-logic-condition-join"
        || action == "toggle-logic-condition-negated") {
        LogicRuleId ruleId;
        std::size_t conditionIndex = 0;
        if (!parseActionArg(arg, ruleId, conditionIndex)) {
            coordinator_.logError("Invalid Logic condition address");
            return true;
        }
        const LogicRuleDef* rule = ruleById(ruleId);
        if (!rule || conditionIndex >= rule->conditions.size()) {
            coordinator_.logError("Unknown Logic condition");
            return true;
        }
        if (action == "change-logic-condition") {
            coordinator_.apply(ChangeLogicConditionTypeIntent{
                objectTypeId, ruleId, conditionIndex, value});
        } else if (action == "remove-logic-condition") {
            coordinator_.execute(RemoveLogicConditionCommand{
                objectTypeId, ruleId, conditionIndex});
        } else if (action == "move-logic-condition-up") {
            if (conditionIndex > 0) coordinator_.execute(MoveLogicConditionCommand{
                objectTypeId, ruleId, conditionIndex, conditionIndex - 1});
        } else if (action == "move-logic-condition-down") {
            if (conditionIndex + 1 < rule->conditions.size())
                coordinator_.execute(MoveLogicConditionCommand{
                    objectTypeId, ruleId, conditionIndex, conditionIndex + 1});
        } else if (action == "set-logic-condition-join") {
            coordinator_.execute(SetLogicConditionJoinCommand{
                objectTypeId, ruleId, conditionIndex,
                value == "or" ? LogicConditionJoin::Or : LogicConditionJoin::And});
        } else {
            coordinator_.execute(SetLogicConditionNegatedCommand{
                objectTypeId, ruleId, conditionIndex,
                !rule->conditions[conditionIndex].negated});
        }
        return true;
    }

    if (action == "pick-logic-property" || action == "pick-logic-key-binding"
        || action == "commit-logic-property"
        || action == "toggle-logic-property"
        || action == "commit-logic-property-component") {
        const std::optional<PropertyAddress> address = parsePropertyAddress(arg);
        if (!address) {
            coordinator_.logError("Invalid Logic property address");
            return true;
        }
        const LogicBlockDef* block = blockAt(*address);
        const Logic::LogicBlockDescriptor* descriptor =
            block ? Logic::findDescriptor(block->typeId) : nullptr;
        const Logic::LogicPropertyDescriptor* propertyDescriptor = nullptr;
        if (descriptor) {
            const auto it = std::find_if(
                descriptor->properties.begin(), descriptor->properties.end(),
                [&](const Logic::LogicPropertyDescriptor& candidate) {
                    return candidate.key == address->key;
                });
            if (it != descriptor->properties.end()) propertyDescriptor = &*it;
        }
        if (!block || !propertyDescriptor) {
            coordinator_.logError("Unknown Logic property");
            return true;
        }
        const LogicPropertyDef* current = Logic::findProperty(*block, address->key);
        if (action == "toggle-logic-property") {
            const bool oldValue = current && std::get_if<bool>(&current->value)
                ? std::get<bool>(current->value) : false;
            executeProperty(*address, !oldValue);
            return true;
        }
        if (action == "commit-logic-property-component") {
            Vec2 next{};
            if (current) if (const auto* valueNow = std::get_if<Vec2>(&current->value)) next = *valueNow;
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed) {
                coordinator_.logError("Logic vector component must be a finite number");
                return true;
            }
            if (address->component == "x") next.x = *parsed;
            else if (address->component == "y") next.y = *parsed;
            else {
                coordinator_.logError("Unknown Logic vector component");
                return true;
            }
            executeProperty(*address, next);
            return true;
        }

        // Animation asset+clip is a coupled invariant: changing either side
        // remains one atomic command, even though both widgets are descriptor-driven.
        if (block->typeId == Logic::kAnimationPlayClip
            && address->target == LogicPropertyTarget::Action
            && (address->key == "animationAssetId" || address->key == "clipId")) {
            AssetId assetId;
            std::string clipId;
            if (const LogicPropertyDef* asset = Logic::findProperty(*block, "animationAssetId"))
                if (const auto* ref = std::get_if<LogicAssetReference>(&asset->value)) assetId = ref->id;
            if (const LogicPropertyDef* clip = Logic::findProperty(*block, "clipId"))
                if (const auto* text = std::get_if<LogicStringValue>(&clip->value)) clipId = text->value;
            if (address->key == "animationAssetId") {
                assetId = value;
                if (const SpriteAnimationAssetDef* asset =
                        coordinator_.document().findSpriteAnimationAsset(assetId))
                    clipId = defaultClipId(*asset);
            } else {
                clipId = value;
            }
            coordinator_.execute(SetLogicAnimationClipCommand{
                objectTypeId, address->ruleId, address->index, assetId, clipId});
            return true;
        }

        switch (propertyDescriptor->valueKind) {
        case Logic::LogicValueKind::Bool:
            executeProperty(*address, value == "true" || value == "1");
            break;
        case Logic::LogicValueKind::Integer: {
            char* end = nullptr;
            const long long parsed = std::strtoll(value.c_str(), &end, 10);
            if (!end || *end != '\0') coordinator_.logError("Logic integer is invalid");
            else executeProperty(*address, static_cast<int64_t>(parsed));
            break;
        }
        case Logic::LogicValueKind::Number: {
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed) coordinator_.logError("Logic number must be finite");
            else executeProperty(*address, static_cast<double>(*parsed));
            break;
        }
        case Logic::LogicValueKind::String:
            executeProperty(*address, LogicStringValue{value});
            break;
        case Logic::LogicValueKind::Asset:
            executeProperty(*address, LogicAssetReference{value});
            break;
        case Logic::LogicValueKind::Variable:
            executeProperty(*address, LogicVariableReference{value});
            break;
        case Logic::LogicValueKind::Key: {
            const std::optional<LogicKey> key = Logic::logicKeyFromName(value);
            if (!key) coordinator_.logError("Unsupported Logic key");
            else executeProperty(*address, *key);
            break;
        }
        case Logic::LogicValueKind::Vec2:
        case Logic::LogicValueKind::Entity:
            coordinator_.logError("Logic property requires a specialized editor");
            break;
        }
        return true;
    }

    if (action == "add-logic-rule") {
        if (view.tab != LogicBoardTab::Rules) return true;
        coordinator_.execute(AddLogicRuleCommand{
            objectTypeId, Logic::makeDefaultRule(nextLogicRuleId(board)), board.rules.size()});
    } else if (action == "duplicate-logic-rule") {
        const EditorOperationResult result = coordinator_.apply(
            DuplicateLogicRuleIntent{objectTypeId, arg});
        if (result.ok) {
            const EntityDef* updatedType = coordinator_.document().findObjectType(objectTypeId);
            if (updatedType && updatedType->logicBoard) {
                const auto source = std::find_if(
                    updatedType->logicBoard->rules.begin(), updatedType->logicBoard->rules.end(),
                    [&](const LogicRuleDef& rule) { return rule.id == arg; });
                if (source != updatedType->logicBoard->rules.end()
                    && source + 1 != updatedType->logicBoard->rules.end())
                    panel_.revealRuleAfterRefresh((source + 1)->id);
            }
        }
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
    } else if (action == "set-logic-execution-mode") {
        if (const auto mode = Logic::logicExecutionModeFromString(value))
            coordinator_.execute(SetLogicRuleExecutionModeCommand{objectTypeId, arg, *mode});
    } else if (action == "change-logic-trigger") {
        coordinator_.apply(ChangeLogicTriggerTypeIntent{objectTypeId, arg, value});
    } else if (action == "set-logic-key") {
        if (const auto key = Logic::logicKeyFromName(value)) {
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, arg, LogicPropertyTarget::Trigger, 0, "key", *key});
        }
    } else if (action == "toggle-logic-event-expected") {
        if (const LogicRuleDef* rule = ruleById(arg)) {
            bool expected = true;
            if (const LogicPropertyDef* p = Logic::findProperty(rule->trigger, "expected"))
                if (const auto* current = std::get_if<bool>(&p->value)) expected = *current;
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, arg, LogicPropertyTarget::Trigger, 0, "expected", !expected});
        }
    } else if (action == "set-logic-event-collision-object-type") {
        coordinator_.execute(SetLogicPropertyCommand{
            objectTypeId, arg, LogicPropertyTarget::Trigger, 0,
            "objectTypeId", LogicStringValue{value}});
    } else if (action == "add-logic-action-type") {
        coordinator_.apply(AddLogicActionTypeIntent{objectTypeId, arg, value});
    } else if (action == "remove-logic-action" || action == "move-logic-action-up"
               || action == "move-logic-action-down" || action == "change-logic-action"
               || action == "toggle-logic-visible" || action == "commit-logic-position-x"
               || action == "commit-logic-position-y" || action == "commit-logic-offset-x"
               || action == "commit-logic-offset-y" || action == "set-logic-animation-asset"
               || action == "set-logic-animation-clip"
               || action == "commit-logic-animation-speed"
               || action == "set-logic-audio-asset" || action == "commit-logic-audio-volume") {
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
            coordinator_.apply(ChangeLogicActionTypeIntent{
                objectTypeId, ruleId, index, value});
        } else if (action == "toggle-logic-visible") {
            bool visible = true;
            if (const LogicPropertyDef* p = Logic::findProperty(rule->actions[index], "visible"))
                if (const auto* current = std::get_if<bool>(&p->value)) visible = *current;
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index, "visible", !visible});
        } else if (action == "commit-logic-position-x" || action == "commit-logic-position-y") {
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
        } else if (action == "commit-logic-offset-x" || action == "commit-logic-offset-y") {
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed) {
                coordinator_.logError("Logic offset must be a finite number");
                return true;
            }
            Vec2 offset{};
            if (const LogicPropertyDef* p = Logic::findProperty(rule->actions[index], "offset"))
                if (const auto* current = std::get_if<Vec2>(&p->value)) offset = *current;
            if (action == "commit-logic-offset-x") offset.x = *parsed;
            else offset.y = *parsed;
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index, "offset", offset});
        } else if (action == "set-logic-animation-asset") {
            const SpriteAnimationAssetDef* asset =
                coordinator_.document().findSpriteAnimationAsset(value);
            if (!asset || asset->clips.empty()) {
                coordinator_.logError("Choose an animation asset with at least one clip");
                return true;
            }
            coordinator_.execute(SetLogicAnimationClipCommand{
                objectTypeId, ruleId, index, asset->id, defaultClipId(*asset)});
        } else if (action == "set-logic-animation-clip") {
            AssetId animationAssetId;
            if (const LogicPropertyDef* p = Logic::findProperty(rule->actions[index],
                                                                "animationAssetId"))
                if (const auto* current = std::get_if<LogicAssetReference>(&p->value))
                    animationAssetId = current->id;
            if (animationAssetId.empty()) {
                coordinator_.logError("Choose an animation asset before choosing a clip");
                return true;
            }
            coordinator_.execute(SetLogicAnimationClipCommand{
                objectTypeId, ruleId, index, animationAssetId, value});
        } else if (action == "commit-logic-animation-speed") {
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed || *parsed <= 0.f) {
                coordinator_.logError("Animation speed must be a positive finite number");
                return true;
            }
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index,
                "speed", static_cast<double>(*parsed)});
        } else if (action == "set-logic-audio-asset") {
            const AudioAssetDef* audio = coordinator_.document().findAudioAsset(value);
            if (!audio || audio->loadMode != AudioLoadMode::StaticSound) {
                coordinator_.logError("Choose a static audio asset");
                return true;
            }
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index,
                "audioAssetId", LogicAssetReference{value}});
        } else if (action == "commit-logic-audio-volume") {
            const std::optional<float> parsed = parseNumberField(value);
            if (!parsed || *parsed < 0.f || *parsed > 1.f) {
                coordinator_.logError("Volume must be between 0 and 1");
                return true;
            }
            coordinator_.execute(SetLogicPropertyCommand{
                objectTypeId, ruleId, LogicPropertyTarget::Action, index,
                "volume", static_cast<double>(*parsed)});
        }
    } else {
        return false;
    }
    return true;
}

} // namespace ArtCade::EditorNative
