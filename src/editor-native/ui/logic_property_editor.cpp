#include "editor-native/ui/logic_property_editor.h"

#include "editor-native/model/project_document.h"
#include "editor-native/ui/ui_markup.h"
#include "logic-number-expression-format.h"
#include "logic-number-expression-parse.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

std::string number(double value) {
    std::ostringstream out;
    out.precision(7);
    out << value;
    return out.str();
}

const LogicPropertyDef* valueOf(const LogicBlockDef& block, const std::string& key) {
    return Logic::findProperty(block, key);
}

std::string targetToken(LogicPropertyTarget target) {
    switch (target) {
    case LogicPropertyTarget::Action: return "a";
    case LogicPropertyTarget::Condition: return "c";
    case LogicPropertyTarget::Trigger:
    default: return "t";
    }
}

std::string entry(const std::string& label, const std::string& value,
                  bool selected, const std::string& dropdownId,
                  const std::string& address) {
    std::string html = "<button class=\"drop-entry";
    if (selected) html += " selected";
    html += "\" ";
    if (selected) {
        html += "data-action=\"toggle-logic-dropdown\" data-arg=\""
              + escapeRml(dropdownId) + "\"";
    } else {
        html += "data-action=\"pick-logic-property\" data-arg=\""
              + escapeRml(address) + "\" data-value=\"" + escapeRml(value) + "\"";
    }
    html += ">" + escapeRml(label) + "</button>";
    return html;
}

std::string dropdown(const std::string& label, const std::string& dropdownId,
                     const std::string& entries, bool open, bool playing) {
    // A logic property row is horizontal (label + editor). Keep the trigger
    // and its in-flow list in a vertical wrapper so every property dropdown
    // expands beneath its own field rather than becoming a sibling on the
    // right of the row.
    std::string html = "<div class=\"logic-property-dropdown\">";
    html += dropdownTriggerMarkup(
        label, "toggle-logic-dropdown", dropdownId, open, playing);
    if (open && !playing) html += "<div class=\"drop-list\">" + entries + "</div>";
    html += "</div>";
    return html;
}

std::string stringValue(const LogicPropertyDef* property) {
    if (!property) return {};
    if (const auto* value = std::get_if<LogicStringValue>(&property->value)) return value->value;
    if (const auto* value = std::get_if<LogicAssetReference>(&property->value)) return value->id;
    if (const auto* value = std::get_if<LogicVariableReference>(&property->value)) return value->id;
    return {};
}

bool keyMatchesSearch(const std::string& name, const std::string& query) {
    if (query.empty()) return true;
    std::string normalized = name;
    std::string needle = query;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized.find(needle) != std::string::npos;
}

bool completionMatches(const std::string& candidate, const std::string& fragment) {
    if (fragment.empty()) return true;
    std::string a = candidate;
    std::string b = fragment;
    const auto fold = [](std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    };
    fold(a);
    fold(b);
    return a.rfind(b, 0) == 0 || a.find(b) != std::string::npos;
}

/**
 * The in-flow completion list (ADR-0029). Not a popup: the Logic Board panel
 * scrolls and clips absolutely-positioned children, which is the same reason
 * the property dropdowns render in flow.
 */
std::string renderLogicExpressionCompletionsInner(
    const ProjectDocument& document, const EntityDef* owner,
    const std::string& address, const std::string& text) {
    const std::string fragment = Logic::numberExpressionTokenPrefix(text);

    struct Entry { std::string insert; std::string label; std::string summary; };
    std::vector<Entry> matches;
    for (const Logic::NumberExpressionCompletion& entry
         : Logic::numberExpressionCompletions()) {
        if (completionMatches(entry.label, fragment))
            matches.push_back({entry.insert, entry.label, entry.summary});
    }
    // The token comes from logic-core, never from string-concatenation here: a
    // variable named with a space needs the quoted form, and a hand-built
    // "$" + key would offer text that does not parse.
    const auto addVariables = [&](const std::vector<GameVariableDefinition>& source,
                                  NumberVariableScope scope, const char* scopeName) {
        for (const GameVariableDefinition& variable : source) {
            if (variable.type != GameVariableDefinition::Type::Number) continue;
            if (variable.key.empty()) continue;
            const std::string token =
                Logic::numberExpressionVariableToken(scope, variable.key);
            if (!completionMatches(token, fragment)) continue;
            matches.push_back({token, token, scopeName});
        }
    };
    if (owner) addVariables(owner->localVariables, NumberVariableScope::Local,
                            "Object variable");
    addVariables(document.data().globalVariables, NumberVariableScope::Global,
                 "Project variable");

    std::string html;
    if (matches.empty()) {
        html += "<span class=\"logic-expression-completion-empty\">"
                "Nothing matches — type a number, or clear to start over</span>";
    }
    for (const Entry& entry : matches) {
        html += "<button class=\"logic-expression-completion\""
                " data-action=\"pick-logic-expression-completion\" data-arg=\""
              + escapeRml(address) + "\" data-value=\"" + escapeRml(entry.insert)
              + "\"><span class=\"logic-expression-completion-label\">"
              + escapeRml(entry.label)
              + "</span><span class=\"logic-expression-completion-summary\">"
              + escapeRml(entry.summary) + "</span></button>";
    }
    return html;
}

std::string booleanOption(const char* label, const char* optionValue, bool selected,
                          const std::string& action, const std::string& arg,
                          bool disabled) {
    std::string html = "<button class=\"logic-toggle-option";
    if (selected) html += " selected";
    if (disabled) html += " disabled";
    html += "\" data-action=\"" + action + "\" data-arg=\"" + escapeRml(arg)
          + "\" data-value=\"" + optionValue + "\">" + label + "</button>";
    return html;
}

} // namespace

std::string renderLogicExpressionCompletionEntries(
    const ProjectDocument& document, const EntityDef* owner,
    const std::string& address, const std::string& draftText) {
    return renderLogicExpressionCompletionsInner(document, owner, address, draftText);
}

std::string renderLogicBooleanChoice(
    const std::string& action, const std::string& arg, bool value, bool disabled) {
    return "<div class=\"logic-toggle\">"
         + booleanOption("On", "true", value, action, arg, disabled)
         + booleanOption("Off", "false", !value, action, arg, disabled)
         + "</div>";
}

std::string encodeLogicPropertyAddress(
    const LogicPropertyAddress& address, const std::string& propertyKey) {
    return address.ruleId + "|" + targetToken(address.target) + "|"
         + std::to_string(address.blockIndex) + "|" + propertyKey;
}

std::string renderLogicProperties(
    const ProjectDocument& document,
    const EntityDef* owner,
    const LogicBlockDef& block,
    const LogicPropertyAddress& address,
    const std::string& openDropdownId,
    const LogicKeyBindingEditorState& keyBinding,
    const LogicExpressionFieldState& expressionField,
    bool playing) {
    const Logic::LogicBlockDescriptor* descriptor = Logic::findDescriptor(block.typeId);
    if (!descriptor) return {};

    std::string html;
    for (const Logic::LogicPropertyDescriptor& property : descriptor->properties) {
        if (property.semantic == Logic::LogicPropertySemantic::HiddenSelfTarget) continue;
        const LogicPropertyDef* current = valueOf(block, property.key);
        const std::string encoded = encodeLogicPropertyAddress(address, property.key);
        const std::string dropdownId = "property|" + encoded;
        const bool open = openDropdownId == dropdownId;
        // A Vec2 owns two full-width rows, so its label sits above them rather
        // than centred against the pair — inline, the label floats beside a
        // two-line control and the axis fields start at an arbitrary offset.
        const bool stacked = property.valueKind == Logic::LogicValueKind::Vec2;
        html += std::string("<div class=\"logic-inline logic-property")
              + (stacked ? " stacked" : "")
              + "\"><span class=\"logic-block-label\">"
              + escapeRml(Logic::propertyDisplayName(property)) + "</span>";

        if (property.valueKind == Logic::LogicValueKind::Bool) {
            const bool value = current && std::get_if<bool>(&current->value)
                ? std::get<bool>(current->value) : false;
            html += renderLogicBooleanChoice(
                "set-logic-property-bool", encoded, value, playing);
        } else if (property.valueKind == Logic::LogicValueKind::Number
                   || property.valueKind == Logic::LogicValueKind::Integer) {
            double value = 0.0;
            if (current) {
                // ADR-0029: a Number property holds a NumberExpression. This
                // branch renders the literal field, so a dynamic value simply
                // has no literal to show — the expression field handles those.
                if (const auto* expr = std::get_if<NumberExpression>(&current->value))
                    value = literalNumberValue(*expr).value_or(0.0);
                else if (const auto* integerValue = std::get_if<int64_t>(&current->value))
                    value = static_cast<double>(*integerValue);
            }
            html += "<input type=\"text\" class=\"logic-value-input\""
                    " data-action=\"commit-logic-property\" data-arg=\""
                  + escapeRml(encoded) + "\" value=\"" + number(value) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "/>";
        } else if (property.valueKind == Logic::LogicValueKind::Vec2) {
            LogicVec2Value value = LogicVec2Value::literal(0.0, 0.0);
            if (current) {
                if (const auto* vec = std::get_if<LogicVec2Value>(&current->value))
                    value = *vec;
            }
            const auto litX = literalNumberValue(value.x);
            const auto litY = literalNumberValue(value.y);
            const bool dynamicX = !litX.has_value();
            const bool dynamicY = !litY.has_value();
            const bool expressionEnabled =
                property.numericExpressionPolicy
                == Logic::NumericExpressionPolicy::PerComponentNumberExpression;
            html += "<div class=\"logic-vec2-group\">";
            auto emitAxis = [&](const char* label, const char* component,
                                const NumberExpression& expr,
                                const std::optional<double>& lit) {
                const std::string axisAddress = encoded + "|" + component;
                html += "<div class=\"logic-parameter-row\"><span class=\"logic-parameter-label\">"
                      + escapeRml(label) + "</span>";
                if (!expressionEnabled) {
                    // LiteralOnly: a plain number field, unchanged.
                    html += "<input type=\"text\" class=\"logic-value-input\""
                            " data-action=\"commit-logic-property-component\" data-arg=\""
                          + escapeRml(axisAddress) + "\" value=\""
                          + number(lit ? static_cast<float>(*lit) : 0.f) + "\"";
                    if (playing) html += " disabled=\"disabled\"";
                    html += "/></div>";
                    return;
                }
                // ADR-0029: one control for both states. The field holds the
                // Code form, so a literal and an expression are the same
                // affordance and reverting costs a keystroke.
                const bool focused = expressionField.focusAddress == axisAddress;
                const bool hasError = focused && !expressionField.errorMessage.empty();
                // Once the author has typed, the draft is the field's text —
                // including when they have emptied it.
                const std::string shown = focused && expressionField.edited
                    ? expressionField.draftText
                    : Logic::formatNumberExpression(
                          expr, Logic::NumberExpressionFormatStyle::Code);
                // A stable id on the focused field only, so refresh() can put
                // the caret back after it rebuilds the panel — the same
                // mechanism logic-key-search-input already relies on.
                html += "<input type=\"text\"";
                if (focused) html += " id=\"logic-expression-input\"";
                html += " class=\"logic-value-input logic-expression-input";
                if (hasError) html += " invalid";
                html += "\" data-action=\"edit-logic-expression\" data-arg=\""
                      + escapeRml(axisAddress) + "\" value=\"" + escapeRml(shown) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
                if (hasError) {
                    html += "<div class=\"logic-expression-error\"><span>"
                          + escapeRml(expressionField.errorMessage) + "</span></div>";
                }
                if (focused && !playing) {
                    // The draft only — never `shown`. Filtering by the value
                    // already in the field made focusing a plain `160` match
                    // nothing, which is the opposite of a discovery surface.
                    // The container carries a stable id so a keystroke can
                    // replace its entries without rebuilding the field itself.
                    html += std::string("<div id=\"")
                          + kLogicExpressionCompletionsId
                          + "\" class=\"logic-expression-completions\">"
                          + renderLogicExpressionCompletionsInner(
                                document, owner, axisAddress, expressionField.draftText)
                          + "</div>";
                }
            };
            emitAxis("X", "x", value.x, litX);
            emitAxis("Y", "y", value.y, litY);
            html += "</div>";
        } else if (property.valueKind == Logic::LogicValueKind::Key) {
            LogicKey selected = LogicKey::Space;
            if (current) if (const auto* key = std::get_if<LogicKey>(&current->value)) selected = *key;
            const bool capturing = keyBinding.captureAddress == encoded;
            const bool searching = keyBinding.searchAddress == encoded;
            html += "<div class=\"logic-key-binding\"><button class=\"logic-key-capture";
            if (capturing) html += " capturing";
            if (playing) html += " disabled";
            html += "\" data-action=\"begin-logic-key-capture\" data-arg=\""
                  + escapeRml(encoded) + "\">"
                  + (capturing ? std::string("Press a supported key…")
                               : escapeRml(Logic::logicKeyName(selected)))
                  + "</button><button class=\"logic-key-search-toggle";
            if (playing) html += " disabled";
            html += "\" data-action=\"toggle-logic-key-search\" data-arg=\""
                  + escapeRml(encoded) + "\">Search key…</button>";
            if (searching && !playing) {
                html += "<div class=\"logic-key-search\"><input id=\"logic-key-search-input\""
                        " type=\"text\" placeholder=\"Search key…\" data-action=\"filter-logic-key-search\""
                        " data-arg=\"" + escapeRml(encoded) + "\" value=\""
                      + escapeRml(keyBinding.searchQuery) + "\"/><button class=\"logic-key-search-cancel\""
                        " data-action=\"toggle-logic-key-search\" data-arg=\""
                      + escapeRml(encoded) + "\">Cancel</button><div class=\"logic-key-results\">";
                bool hasResult = false;
                for (LogicKey key : Logic::supportedLogicKeys()) {
                    const std::string name = Logic::logicKeyName(key);
                    if (!keyMatchesSearch(name, keyBinding.searchQuery)) continue;
                    hasResult = true;
                    html += "<button class=\"logic-key-result";
                    if (key == selected) html += " selected";
                    html += "\" data-action=\"pick-logic-key-binding\" data-arg=\""
                          + escapeRml(encoded) + "\" data-value=\"" + escapeRml(name)
                          + "\">" + escapeRml(name) + "</button>";
                }
                if (!hasResult) html += "<span class=\"logic-key-empty\">No supported key</span>";
                html += "</div></div>";
            }
            html += "</div>";
        } else if (property.semantic == Logic::LogicPropertySemantic::CompareOperator
                   || property.semantic == Logic::LogicPropertySemantic::TopDownDirection
                   || property.semantic == Logic::LogicPropertySemantic::SpriteFacing
                   || property.semantic == Logic::LogicPropertySemantic::PlatformerDirection
                   || property.semantic == Logic::LogicPropertySemantic::PlatformerMotionState) {
            const std::string selected = stringValue(current);
            std::string entries;
            for (const std::string& option : property.options)
                entries += entry(option, option, option == selected, dropdownId, encoded);
            html += dropdown(selected, dropdownId, entries, open, playing);
        } else if (property.semantic == Logic::LogicPropertySemantic::ObjectTypeReference) {
            const std::string selected = stringValue(current);
            std::vector<std::string> ids;
            for (const auto& [id, unused] : document.data().objectTypes) {
                (void)unused; ids.push_back(id);
            }
            std::sort(ids.begin(), ids.end());
            std::string entries;
            if (property.allowEmpty)
                entries += entry("Any", "", selected.empty(), dropdownId, encoded);
            for (const std::string& id : ids) {
                const EntityDef* type = document.findObjectType(id);
                entries += entry(type && !type->name.empty() ? type->name : id, id,
                                 id == selected, dropdownId, encoded);
            }
            html += dropdown(selected.empty() ? "Any" : selected, dropdownId, entries, open, playing);
        } else if (property.semantic == Logic::LogicPropertySemantic::SceneReference) {
            const std::string selected = stringValue(current);
            std::vector<const SceneDef*> scenes;
            scenes.reserve(document.data().scenes.size());
            for (const auto& [id, scene] : document.data().scenes) {
                (void)id; scenes.push_back(&scene);
            }
            std::sort(scenes.begin(), scenes.end(),
                [](const SceneDef* a, const SceneDef* b) { return a->id < b->id; });
            std::string entries;
            for (const SceneDef* scene : scenes) {
                entries += entry(scene->name.empty() ? scene->id : scene->name, scene->id,
                                 scene->id == selected, dropdownId, encoded);
            }
            const SceneDef* selectedScene =
                selected.empty() ? nullptr : document.findScene(selected);
            const std::string label = selected.empty()
                ? std::string("Select scene")
                : (selectedScene && !selectedScene->name.empty() ? selectedScene->name
                                                                 : selected);
            html += dropdown(label, dropdownId, entries, open, playing);
        } else if (property.semantic == Logic::LogicPropertySemantic::GlobalVariable) {
            const std::string selected = stringValue(current);
            const auto required = Logic::requiredVariableType(block.typeId);
            std::string entries;
            for (const GameVariableDefinition& variable : document.data().globalVariables) {
                if (required && variable.type != *required) continue;
                entries += entry(variable.key, variable.key, variable.key == selected,
                                 dropdownId, encoded);
            }
            if (entries.empty()) {
                html += "<button class=\"logic-btn\" data-action=\"toggle-global-variables\">"
                        "Create compatible variable</button>";
            } else {
                html += dropdown(selected.empty() ? "Select variable" : selected,
                                 dropdownId, entries, open, playing);
            }
        } else if (property.semantic == Logic::LogicPropertySemantic::SpriteAnimationAsset) {
            const std::string selected = stringValue(current);
            std::string entries;
            for (const SpriteAnimationAssetDef& asset : document.data().spriteAnimationAssets)
                entries += entry(asset.name.empty() ? asset.id : asset.name, asset.id,
                                 asset.id == selected, dropdownId, encoded);
            html += dropdown(selected.empty() ? "Select animation" : selected,
                             dropdownId, entries, open, playing);
        } else if (property.semantic == Logic::LogicPropertySemantic::AnimationClip) {
            const std::string selected = stringValue(current);
            std::string assetId;
            if (const LogicPropertyDef* asset = valueOf(block, "animationAssetId"))
                assetId = stringValue(asset);
            std::string entries;
            if (const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId)) {
                for (const SpriteAnimationClipDef& clip : asset->clips)
                    entries += entry(clip.name.empty() ? clip.id : clip.name, clip.id,
                                     clip.id == selected, dropdownId, encoded);
            }
            html += dropdown(selected.empty() ? "Select clip" : selected,
                             dropdownId, entries, open, playing);
        } else if (property.semantic == Logic::LogicPropertySemantic::StaticAudioAsset) {
            const std::string selected = stringValue(current);
            std::string entries;
            for (const AudioAssetDef& asset : document.data().audioAssets) {
                if (asset.loadMode != AudioLoadMode::StaticSound) continue;
                entries += entry(asset.name.empty() ? asset.assetId : asset.name, asset.assetId,
                                 asset.assetId == selected, dropdownId, encoded);
            }
            html += dropdown(selected.empty() ? "Select sound" : selected,
                             dropdownId, entries, open, playing);
        } else {
            const std::string value = stringValue(current);
            html += "<input type=\"text\" data-action=\"commit-logic-property\" data-arg=\""
                  + escapeRml(encoded) + "\" value=\"" + escapeRml(value) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "/>";
        }
        html += "</div>";
    }
    return html;
}

} // namespace ArtCade::EditorNative
