#include "editor-native/ui/logic_board_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"
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

std::string option(const std::string& value, const std::string& label, bool selected) {
    return "<option value=\"" + escapeRml(value) + "\""
         + (selected ? " selected=\"selected\"" : "") + ">"
         + escapeRml(label) + "</option>";
}

std::string actionArg(const LogicRuleId& ruleId, std::size_t index) {
    return ruleId + "|" + std::to_string(index);
}

} // namespace

void LogicBoardPanel::refresh(Rml::ElementDocument* document,
                              const EditorCoordinator& coordinator) const {
    if (!document) return;
    Rml::Element* root = document->GetElementById("logic-board-panel");
    if (!root) return;

    const LogicBoardEditorState& view = coordinator.state().logicBoardEditor;
    const bool playing = coordinator.isPlaying();
    std::vector<ObjectTypeId> typeIds;
    typeIds.reserve(coordinator.document().data().objectTypes.size());
    for (const auto& [id, unused] : coordinator.document().data().objectTypes)
        typeIds.push_back(id);
    std::sort(typeIds.begin(), typeIds.end());

    std::string html = "<div class=\"logic-head\"><span class=\"logic-title\">Logic Board</span>";
    if (!typeIds.empty()) {
        html += "<select class=\"logic-search\" data-action=\"select-logic-object-type\">";
        for (const ObjectTypeId& id : typeIds) {
            const EntityDef& type = coordinator.document().data().objectTypes.at(id);
            html += option(id, type.name.empty() ? id : type.name,
                           view.objectTypeId && *view.objectTypeId == id);
        }
        html += "</select>";
    }
    html += "</div>";
    html += "<div class=\"logic-tabs\">"
            "<button class=\"logic-tab";
    if (view.tab == LogicBoardTab::Rules) html += " active";
    html += "\" data-action=\"logic-tab-rules\">Rules</button>"
            "<button class=\"logic-tab";
    if (view.tab == LogicBoardTab::GeneratedLua) html += " active";
    html += "\" data-action=\"logic-tab-lua\">Generated Lua</button></div>";

    if (typeIds.empty()) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Object Types</div>"
                "<div class=\"logic-muted\">Create an Object Type before adding gameplay logic.</div></div>";
        root->SetInnerRML(html);
        return;
    }

    const ObjectTypeId selectedId = view.objectTypeId && coordinator.document().hasObjectType(*view.objectTypeId)
        ? *view.objectTypeId : typeIds.front();
    const EntityDef& objectType = coordinator.document().data().objectTypes.at(selectedId);
    if (!objectType.logicBoard) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Logic Board</div>"
                "<div class=\"logic-muted\">This board belongs to the Object Type and applies to every instance.</div>"
                "<button class=\"logic-btn primary";
        if (playing) html += " disabled";
        html += "\" data-action=\"create-logic-board\">Create Logic Board</button></div>";
        root->SetInnerRML(html);
        return;
    }

    const LogicBoardDef& board = *objectType.logicBoard;
    const Logic::LogicCompileResult compiled = Logic::compileBoard(selectedId, board);

    if (view.tab == LogicBoardTab::GeneratedLua) {
        html += "<div class=\"logic-scroll\">";
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
        root->SetInnerRML(html);
        return;
    }

    html += "<div class=\"logic-tools\"><input class=\"logic-search\" type=\"text\""
            " data-action=\"commit-logic-search\" placeholder=\"Search rules\" value=\""
         + escapeRml(view.search) + "\"/><button class=\"logic-btn primary";
    if (playing) html += " disabled";
    html += "\" data-action=\"add-logic-rule\">Add Rule</button>"
            "<button class=\"logic-btn danger";
    if (playing) html += " disabled";
    html += "\" data-action=\"remove-logic-board\">Remove Board</button></div>"
            "<div class=\"logic-scroll\">";

    const std::string query = lower(view.search);
    for (std::size_t ruleIndex = 0; ruleIndex < board.rules.size(); ++ruleIndex) {
        const LogicRuleDef& rule = board.rules[ruleIndex];
        std::string searchable = lower(rule.id + " " + rule.trigger.typeId);
        for (const LogicBlockDef& action : rule.actions) searchable += " " + lower(action.typeId);
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;

        html += "<div class=\"logic-rule" + std::string(rule.enabled ? "" : " off") + "\">"
                "<div class=\"logic-rule-head\"><span class=\"logic-rule-id\">"
             + escapeRml(rule.id) + "</span>";
        auto button = [&](const char* action, const char* label, bool disabled = false) {
            html += "<button class=\"logic-icon-btn";
            if (playing || disabled) html += " disabled";
            html += "\" data-action=\"" + std::string(action) + "\" data-arg=\""
                 + escapeRml(rule.id) + "\">" + label + "</button>";
        };
        button("toggle-logic-rule", rule.enabled ? "Enabled" : "Disabled");
        button("move-logic-rule-up", "↑", ruleIndex == 0);
        button("move-logic-rule-down", "↓", ruleIndex + 1 == board.rules.size());
        button("remove-logic-rule", "Delete");
        html += "</div><div class=\"logic-block\"><div><span class=\"logic-block-label\">EVENT</span>";
        html += "<select data-action=\"change-logic-trigger\" data-arg=\"" + escapeRml(rule.id) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += ">" + option(Logic::kOnStart, "On Start", rule.trigger.typeId == Logic::kOnStart)
             + option(Logic::kKeyPressed, "Key Pressed", rule.trigger.typeId == Logic::kKeyPressed)
             + "</select></div>";
        if (rule.trigger.typeId == Logic::kKeyPressed) {
            LogicKey selectedKey = LogicKey::Space;
            if (const LogicPropertyDef* p = property(rule.trigger, "key"))
                if (const auto* key = std::get_if<LogicKey>(&p->value)) selectedKey = *key;
            html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">KEY</span>"
                    "<select data-action=\"set-logic-key\" data-arg=\"" + escapeRml(rule.id) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += ">";
            for (LogicKey key : Logic::supportedLogicKeys())
                html += option(Logic::logicKeyName(key), Logic::logicKeyName(key), key == selectedKey);
            html += "</select></div>";
        }
        html += "</div>";

        for (std::size_t actionIndex = 0; actionIndex < rule.actions.size(); ++actionIndex) {
            const LogicBlockDef& action = rule.actions[actionIndex];
            const std::string arg = actionArg(rule.id, actionIndex);
            html += "<div class=\"logic-block action\"><div class=\"logic-action-row\"><div class=\"logic-action-main\">"
                    "<span class=\"logic-block-label\">ACTION</span><select data-action=\"change-logic-action\" data-arg=\""
                 + escapeRml(arg) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += ">" + option(Logic::kSetVisible, "Set Visible", action.typeId == Logic::kSetVisible)
                 + option(Logic::kSetPosition, "Set Position", action.typeId == Logic::kSetPosition)
                 + "</select></div><button class=\"logic-icon-btn";
            if (playing || actionIndex == 0) html += " disabled";
            html += "\" data-action=\"move-logic-action-up\" data-arg=\"" + escapeRml(arg) + "\">↑</button>"
                    "<button class=\"logic-icon-btn";
            if (playing || actionIndex + 1 == rule.actions.size()) html += " disabled";
            html += "\" data-action=\"move-logic-action-down\" data-arg=\"" + escapeRml(arg) + "\">↓</button>"
                    "<button class=\"logic-icon-btn";
            if (playing || rule.actions.size() == 1) html += " disabled";
            html += "\" data-action=\"remove-logic-action\" data-arg=\"" + escapeRml(arg) + "\">Delete</button></div>";
            if (action.typeId == Logic::kSetVisible) {
                bool visible = true;
                if (const LogicPropertyDef* p = property(action, "visible"))
                    if (const auto* v = std::get_if<bool>(&p->value)) visible = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self · visible</span>"
                        "<button class=\"logic-btn";
                if (playing) html += " disabled";
                html += "\" data-action=\"toggle-logic-visible\" data-arg=\"" + escapeRml(arg) + "\">"
                     + std::string(visible ? "true" : "false") + "</button></div>";
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
        html += "<button class=\"logic-btn";
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
    root->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
