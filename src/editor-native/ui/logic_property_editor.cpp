#include "editor-native/ui/logic_property_editor.h"

#include "editor-native/model/project_document.h"
#include "editor-native/ui/ui_markup.h"
#include "logic-number-expression-format.h"

#include <algorithm>
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
    const LogicBlockDef& block,
    const LogicPropertyAddress& address,
    const std::string& openDropdownId,
    const LogicKeyBindingEditorState& keyBinding,
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
        html += "<div class=\"logic-inline logic-property\"><span class=\"logic-block-label\">"
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
                if (const auto* numberValue = std::get_if<double>(&current->value)) value = *numberValue;
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
                                const NumberExpression& expr, bool dynamic,
                                const std::optional<double>& lit) {
                html += "<div class=\"logic-parameter-row\"><span class=\"logic-parameter-label\">"
                      + escapeRml(label) + "</span>";
                if (dynamic && expressionEnabled) {
                    html += "<button class=\"logic-expression-summary\" data-action=\""
                            "open-number-expression-editor\" data-arg=\""
                          + escapeRml(encoded + "|" + component) + "\"";
                    if (playing) html += " disabled=\"disabled\"";
                    html += "><span class=\"logic-expression-badge\">fx</span>"
                            "<span class=\"logic-expression-summary-text\">"
                          + escapeRml(Logic::formatNumberExpression(
                                expr, Logic::NumberExpressionFormatStyle::Compact))
                          + "</span></button>"
                            "<button class=\"logic-expression-edit\" data-action=\""
                            "open-number-expression-editor\" data-arg=\""
                          + escapeRml(encoded + "|" + component) + "\"";
                    if (playing) html += " disabled=\"disabled\"";
                    html += "><span class=\"logic-expression-edit-label\">Edit</span></button>";
                } else {
                    html += "<input type=\"text\" class=\"logic-value-input\""
                            " data-action=\"commit-logic-property-component\" data-arg=\""
                          + escapeRml(encoded + "|" + component) + "\" value=\""
                          + number(lit ? static_cast<float>(*lit) : 0.f) + "\"";
                    if (playing) html += " disabled=\"disabled\"";
                    html += "/>";
                    if (expressionEnabled) {
                        html += "<button class=\"logic-expression-button\" data-action=\""
                                "open-number-expression-editor\" data-arg=\""
                              + escapeRml(encoded + "|" + component) + "\"";
                        if (playing) html += " disabled=\"disabled\"";
                        html += "><span class=\"logic-expression-button-label\">fx</span></button>";
                    }
                }
                html += "</div>";
            };
            emitAxis("X", "x", value.x, dynamicX, litX);
            emitAxis("Y", "y", value.y, dynamicY, litY);
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
