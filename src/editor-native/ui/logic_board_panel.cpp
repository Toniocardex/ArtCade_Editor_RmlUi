#include "editor-native/ui/logic_board_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/ui_markup.h"
#include "logic-core.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ArtCade::EditorNative {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const LogicPropertyDef* property(const LogicBlockDef& block, const char* key) {
    return Logic::findProperty(block, key);
}

std::string number(float value) {
    std::ostringstream out;
    out.precision(7);
    out << value;
    return out.str();
}

std::string actionArg(const LogicRuleId& ruleId, std::size_t index) {
    return ruleId + "|" + std::to_string(index);
}

// A `.drop-list` entry: the current value marks itself and just closes the
// list on click (re-picking itself is a no-op); every other entry dispatches
// `pickAction` with the value carried in data-value (never data-arg, which
// stays the addressing key for the actions that need one — see the
// data-value contract enforced by LogicBoardEditorController). `pickArg`, when
// non-empty, is the addressing key riding
// alongside (e.g. the rule a Key picker belongs to).
std::string dropEntry(const std::string& label, const std::string& value, bool isCurrent,
                      const std::string& closeDropdownId, const char* pickAction,
                      const std::string& pickArg) {
    std::string html = "<div class=\"drop-entry";
    if (isCurrent) html += " selected";
    html += "\"";
    if (isCurrent) {
        html += " data-action=\"toggle-logic-dropdown\" data-arg=\""
              + escapeRml(closeDropdownId) + "\"";
    } else {
        html += " data-action=\"" + std::string(pickAction) + "\"";
        if (!pickArg.empty()) html += " data-arg=\"" + escapeRml(pickArg) + "\"";
        html += " data-value=\"" + escapeRml(value) + "\"";
    }
    html += ">";
    if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
    html += escapeRml(label) + "</div>";
    return html;
}

// A 2-5 option button row (`.mode-block`/`.mode-options`/`.panel-btn.mode-
// option`) — the same shape used for Box Collider Mode and Tilemap Tool.
// Each option carries the addressing key in data-arg (unchanged from what
// the native <select> it replaces used to receive) and the new value in
// data-value.
std::string modeOption(const char* label, const std::string& value, bool active,
                       const char* action, const std::string& arg, bool disabled) {
    std::string html = "<button class=\"panel-btn mode-option";
    if (active) html += " active";
    if (disabled) html += " disabled";
    html += "\" data-action=\"" + std::string(action) + "\" data-arg=\"" + escapeRml(arg)
         + "\" data-value=\"" + escapeRml(value) + "\">" + label + "</button>";
    return html;
}

} // namespace

void LogicBoardPanel::refresh(Rml::ElementDocument* document,
                              const EditorCoordinator& coordinator) const {
    if (!document) return;
    Rml::Element* root = document->GetElementById("logic-board-panel");
    if (!root) return;

    const LogicBoardEditorState& view = coordinator.state().logicBoardEditor;
    const bool playing = coordinator.isPlaying();
    if (playing) openDropdownId_.clear();
    if (scrollObjectTypeId_ == view.objectTypeId) {
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            scrollTop_ = scroll->GetScrollTop();
    } else {
        scrollObjectTypeId_ = view.objectTypeId;
        scrollTop_ = 0.f;
        openDropdownId_.clear();
    }
    if (lastTab_ != view.tab) {
        lastTab_ = view.tab;
        openDropdownId_.clear();
    }
    std::vector<ObjectTypeId> typeIds;
    typeIds.reserve(coordinator.document().data().objectTypes.size());
    for (const auto& [id, unused] : coordinator.document().data().objectTypes)
        typeIds.push_back(id);
    std::sort(typeIds.begin(), typeIds.end());

    const auto instanceCountFor = [&](const ObjectTypeId& objectTypeId) {
        std::size_t count = 0;
        for (const auto& [sceneId, scene] : coordinator.document().data().scenes)
            for (const SceneInstanceDef& instance : scene.instances)
                if (instance.objectTypeId == objectTypeId) ++count;
        return count;
    };

    const ObjectTypeId selectedId = view.objectTypeId
        && coordinator.document().hasObjectType(*view.objectTypeId)
        ? *view.objectTypeId : (typeIds.empty() ? ObjectTypeId{} : typeIds.front());
    const EntityDef* selectedType = selectedId.empty()
        ? nullptr : &coordinator.document().data().objectTypes.at(selectedId);
    const std::string selectedName = selectedType && !selectedType->name.empty()
        ? selectedType->name : selectedId;
    const std::size_t sharedCount = selectedId.empty() ? 0 : instanceCountFor(selectedId);

    std::string html = "<div class=\"logic-head\"><div class=\"logic-heading\">"
                       "<span class=\"logic-title\">Logic Board";
    if (!selectedName.empty()) html += " · " + escapeRml(selectedName);
    html += "</span><span class=\"logic-owner\">OBJECT TYPE · ";
    if (selectedName.empty()) {
        html += "No target";
    } else {
        html += "Shared by " + std::to_string(sharedCount)
             + (sharedCount == 1 ? " instance" : " instances");
    }
    html += "</span></div>";
    const auto render = [&]() {
        root->SetInnerRML(html);
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            scroll->SetScrollTop(scrollTop_);
    };
    if (!typeIds.empty()) {
        // .logic-head is a flex row: the trigger and its list must share one
        // non-flex wrapper here, or the list renders as a sibling flex item
        // beside the trigger instead of stacking below it (the Inspector's
        // dropdowns avoid this because their .drop-list is emitted after the
        // enclosing .prop-row already closed, i.e. never inside a flex row).
        const bool typeOpen = openDropdownId_ == "object-type" && !playing;
        html += "<div class=\"logic-type-picker\">";
        html += dropdownTriggerMarkup(selectedName, "toggle-logic-dropdown", "object-type",
                                      typeOpen, playing);
        if (typeOpen) {
            html += "<div class=\"drop-list\">";
            for (const ObjectTypeId& id : typeIds) {
                const EntityDef& type = coordinator.document().data().objectTypes.at(id);
                html += dropEntry(type.name.empty() ? id : type.name, id, id == selectedId,
                                  "object-type", "select-logic-object-type", "");
            }
            html += "</div>";
        }
        html += "</div>";
    }
    html += "</div>";

    if (typeIds.empty()) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Object Types</div>"
                "<div class=\"logic-muted\">Create an Object Type before adding gameplay logic.</div></div>";
        render();
        return;
    }

    const EntityDef& objectType = coordinator.document().data().objectTypes.at(selectedId);
    if (!objectType.logicBoard) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Logic Board</div>"
                "<div class=\"logic-muted\">This board belongs to the Object Type and applies to every instance.</div>"
                "<button class=\"logic-btn primary";
        if (playing) html += " disabled";
        html += "\" data-action=\"create-logic-board\">Create Logic Board</button></div>";
        render();
        return;
    }

    const LogicBoardDef& board = *objectType.logicBoard;
    const Logic::LogicCompileResult compiled = Logic::compileBoard(selectedId, board);

    if (view.tab == LogicBoardTab::GeneratedLua) {
        html += "<div id=\"logic-scroll\" class=\"logic-scroll\">";
        for (const Logic::LogicDiagnostic& diagnostic : compiled.diagnostics) {
            html += "<div class=\"logic-diagnostic\">" + escapeRml(diagnostic.code)
                 + " · " + escapeRml(diagnostic.ruleId) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — "
                 + escapeRml(diagnostic.message) + "</div>";
        }
        if (!compiled.programs.empty())
            html += "<div class=\"logic-code\">" + escapeRml(compiled.programs.front().source) + "</div>";
        else
            html += "<div class=\"logic-empty\"><div class=\"logic-muted\">No source is generated while blocking diagnostics exist.</div></div>";
        html += "</div>";
        render();
        return;
    }

    html += "<div class=\"logic-tools\"><button class=\"logic-btn danger";
    if (playing) html += " disabled";
    html += "\" data-action=\"remove-logic-board\">Remove Board</button></div>"
            "<div id=\"logic-scroll\" class=\"logic-scroll\">";

    const std::string query = lower(view.search);
    for (std::size_t ruleIndex = 0; ruleIndex < board.rules.size(); ++ruleIndex) {
        const LogicRuleDef& rule = board.rules[ruleIndex];
        std::string searchable = lower(rule.id + " " + rule.trigger.typeId);
        for (const LogicBlockDef& action : rule.actions) searchable += " " + lower(action.typeId);
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;

        html += "<div class=\"logic-rule" + std::string(rule.enabled ? "" : " off") + "\">"
                "<div class=\"logic-rule-head\"><span class=\"logic-rule-id\">"
             + escapeRml(rule.id) + "</span>";
        html += "<button class=\"panel-btn\"";
        if (playing) html += " disabled=\"disabled\"";
        html += " data-action=\"toggle-logic-rule\" data-arg=\"" + escapeRml(rule.id) + "\">"
             + std::string(rule.enabled ? "On" : "Off") + "</button>";
        auto iconButton = [&](const char* action, const char* label, bool disabled = false) {
            html += "<button class=\"logic-icon-btn";
            if (playing || disabled) html += " disabled";
            html += "\" data-action=\"" + std::string(action) + "\" data-arg=\""
                 + escapeRml(rule.id) + "\">" + label + "</button>";
        };
        iconButton("move-logic-rule-up", "↑", ruleIndex == 0);
        iconButton("move-logic-rule-down", "↓", ruleIndex + 1 == board.rules.size());
        html += "<button class=\"comp-remove";
        if (playing) html += " disabled";
        html += "\" data-action=\"remove-logic-rule\" data-arg=\"" + escapeRml(rule.id)
             + "\" title=\"Delete rule\">" + iconMarkup("&#xeb41;") + "</button>";
        html += "</div><div class=\"logic-block\"><span class=\"mode-label\">EVENT</span>"
                "<div class=\"mode-options\">";
        html += modeOption("On Start", Logic::kOnStart, rule.trigger.typeId == Logic::kOnStart,
                          "change-logic-trigger", rule.id, playing);
        html += modeOption("Key Pressed", Logic::kKeyPressed, rule.trigger.typeId == Logic::kKeyPressed,
                          "change-logic-trigger", rule.id, playing);
        html += "</div>";
        if (rule.trigger.typeId == Logic::kKeyPressed) {
            LogicKey selectedKey = LogicKey::Space;
            if (const LogicPropertyDef* p = property(rule.trigger, "key"))
                if (const auto* key = std::get_if<LogicKey>(&p->value)) selectedKey = *key;
            const std::string keyDropdownId = "key|" + rule.id;
            const bool keyOpen = openDropdownId_ == keyDropdownId && !playing;
            html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">KEY</span>";
            html += dropdownTriggerMarkup(Logic::logicKeyName(selectedKey), "toggle-logic-dropdown",
                                          keyDropdownId, keyOpen, playing);
            html += "</div>";
            if (keyOpen) {
                html += "<div class=\"drop-list logic-key-list\">";
                for (LogicKey key : Logic::supportedLogicKeys()) {
                    const std::string keyName = Logic::logicKeyName(key);
                    html += dropEntry(keyName, keyName, key == selectedKey, keyDropdownId,
                                      "set-logic-key", rule.id);
                }
                html += "</div>";
            }
        }
        html += "</div>";

        for (std::size_t actionIndex = 0; actionIndex < rule.actions.size(); ++actionIndex) {
            const LogicBlockDef& action = rule.actions[actionIndex];
            const std::string arg = actionArg(rule.id, actionIndex);
            html += "<div class=\"logic-block action\"><div class=\"logic-action-row\"><div class=\"logic-action-main\">"
                    "<span class=\"mode-label\">ACTION</span><div class=\"mode-options\">";
            html += modeOption("Set Visible", Logic::kSetVisible, action.typeId == Logic::kSetVisible,
                              "change-logic-action", arg, playing);
            html += modeOption("Set Position", Logic::kSetPosition, action.typeId == Logic::kSetPosition,
                              "change-logic-action", arg, playing);
            html += "</div></div><button class=\"logic-icon-btn";
            if (playing || actionIndex == 0) html += " disabled";
            html += "\" data-action=\"move-logic-action-up\" data-arg=\"" + escapeRml(arg) + "\">↑</button>"
                    "<button class=\"logic-icon-btn";
            if (playing || actionIndex + 1 == rule.actions.size()) html += " disabled";
            html += "\" data-action=\"move-logic-action-down\" data-arg=\"" + escapeRml(arg) + "\">↓</button>"
                    "<button class=\"comp-remove";
            if (playing || rule.actions.size() == 1) html += " disabled";
            html += "\" data-action=\"remove-logic-action\" data-arg=\"" + escapeRml(arg)
                 + "\" title=\"Delete action\">" + iconMarkup("&#xeb41;") + "</button></div>";
            if (action.typeId == Logic::kSetVisible) {
                bool visible = true;
                if (const LogicPropertyDef* p = property(action, "visible"))
                    if (const auto* v = std::get_if<bool>(&p->value)) visible = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self · visible</span>"
                        "<button class=\"logic-btn";
                if (playing) html += " disabled";
                html += "\" data-action=\"toggle-logic-visible\" data-arg=\"" + escapeRml(arg) + "\">"
                     + std::string(visible ? "On" : "Off") + "</button></div>";
            } else if (action.typeId == Logic::kSetPosition) {
                Vec2 position{};
                if (const LogicPropertyDef* p = property(action, "position"))
                    if (const auto* v = std::get_if<Vec2>(&p->value)) position = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self · X</span>"
                        "<input type=\"text\" data-action=\"commit-logic-position-x\" data-arg=\"" + escapeRml(arg)
                     + "\" value=\"" + number(position.x) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/><span class=\"logic-block-label\">Y</span>"
                        "<input type=\"text\" data-action=\"commit-logic-position-y\" data-arg=\"" + escapeRml(arg)
                     + "\" value=\"" + number(position.y) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
            }
            html += "</div>";
        }
        html += "<button class=\"logic-btn logic-add-action";
        if (playing || rule.actions.size() >= Logic::kMaxActionsPerRule) html += " disabled";
        html += "\" data-action=\"add-logic-action\" data-arg=\"" + escapeRml(rule.id)
             + "\">+ Add Action</button>";
        for (const Logic::LogicDiagnostic& diagnostic : compiled.diagnostics) {
            if (diagnostic.ruleId != rule.id) continue;
            html += "<div class=\"logic-diagnostic\">" + escapeRml(diagnostic.code) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — " + escapeRml(diagnostic.message) + "</div>";
        }
        html += "</div>";
    }
    html += "</div>";
    render();
}

void LogicBoardPanel::toggleDropdown(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator,
                                     const std::string& dropdownId) {
    openDropdownId_ = (openDropdownId_ == dropdownId) ? std::string() : dropdownId;
    refresh(document, coordinator);
}

} // namespace ArtCade::EditorNative
