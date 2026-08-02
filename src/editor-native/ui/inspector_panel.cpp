#include "editor-native/ui/inspector_panel.h"
#include "editor-native/ui/ui_icons.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/project_defaults.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/scene_viewport_presets.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/ui_markup.h"
#include "core/text-component-format.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

std::string num(float v, int precision = 3) {
    return formatAuthoringFloat(v, precision);
}

// Tabler icon glyph span (PUA codepoint passed as an RML char reference).
std::string icon(const char* cp) { return iconMarkup(cp); }

// Display label of a Sprite Animation asset: its authored name when the id
// still resolves, with the historical ".anim" suffix stripped either way
// (assetDisplayName). Ids in data-args stay untouched.
std::string animationAssetLabel(const EditorCoordinator& coordinator, const AssetId& id) {
    const SpriteAnimationAssetDef* asset = coordinator.document().findSpriteAnimationAsset(id);
    return assetDisplayName(asset ? asset->name : std::string(), id);
}

// SpriteAnimatorComponent::defaultClipId is a stable id, not a display value
// (the Sprite Animation Editor's rename only ever touches SpriteAnimationClipDef
// ::name, never ::id, precisely so this reference survives a rename). Look the
// clip up and show its current name instead, matching how the Sprite Animation
// Editor itself lists clips. Falls back to the raw id if the clip was since
// removed, so a stale reference is still visible rather than blank.
std::string animationClipDisplayName(const EditorCoordinator& coordinator,
                                     const AssetId& animationAssetId,
                                     const std::string& clipId) {
    if (const SpriteAnimationAssetDef* asset =
            coordinator.document().findSpriteAnimationAsset(animationAssetId)) {
        for (const SpriteAnimationClipDef& clip : asset->clips) {
            if (clip.id == clipId) return clip.name;
        }
    }
    return clipId;
}

// True once some Sprite Animation asset already sources this image - i.e. the
// raw sheet has been turned into an animation, so offering it again as a plain
// static Source is clutter, not a real choice.
bool imageHasDerivedAnimation(const EditorCoordinator& coordinator, const AssetId& imageId) {
    for (const SpriteAnimationAssetDef& asset : coordinator.document().data().spriteAnimationAssets) {
        if (asset.sourceImageAssetId == imageId) return true;
    }
    return false;
}

// An editable property row. Disabled (read-only) while Play freezes the document.
std::string field(const char* label, const char* action, const std::string& value,
                  bool disabled, const char* id = nullptr) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span><input type=\"text\" class=\"prop-input\"";
    if (id && *id) {
        row += " id=\"";
        row += id;
        row += "\"";
    }
    row += " data-action=\"";
    row += action;
    row += "\" value=\"" + escapeRml(value) + "\"";
    if (disabled) row += " disabled=\"disabled\"";
    row += "/></div>";
    return row;
}

// Like field(), with a trailing unit suffix (e.g. "wu") shown after the input.
std::string fieldWithUnit(const char* label, const char* action, const std::string& value,
                          const char* unit, bool disabled, const char* id = nullptr) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span><input type=\"text\" class=\"prop-input\"";
    if (id && *id) {
        row += " id=\"";
        row += id;
        row += "\"";
    }
    row += " data-action=\"";
    row += action;
    row += "\" value=\"" + escapeRml(value) + "\"";
    if (disabled) row += " disabled=\"disabled\"";
    row += "/><span class=\"prop-unit\">";
    row += unit;
    row += "</span></div>";
    return row;
}

// A component section header: icon + NAME, an ownership badge, and an optional
// remove (x). `badge`/`removeAction` empty are skipped — Identity has neither, a
// structural section (Transform) has a badge but no remove.
std::string header(const char* sectionId, bool collapsed, const char* iconCp,
                   const char* title, const char* badge, const char* badgeClass,
                   const char* removeAction, bool playing, const char* helpTooltip = nullptr) {
    std::string h = "[[inspector-section:" + std::string(sectionId) + "]]";
    h += "<div class=\"comp-header\"><span id=\"inspector-section-";
    h += sectionId;
    h += "-toggle\" class=\"comp-title comp-toggle\""
         " data-action=\"toggle-inspector-section\" data-arg=\"";
    h += sectionId;
    h += "\"><span class=\"comp-caret\">";
    h += collapsed ? "" UI_ICON_COLLAPSE "" : "" UI_ICON_EXPAND "";
    h += "</span>";
    h += icon(iconCp);
    h += title;
    h += "</span>";
    if (badge && *badge) {
        h += "<span class=\"comp-badge ";
        h += (badgeClass ? badgeClass : "");
        h += "\">";
        h += badge;
        h += "</span>";
    }
    if (helpTooltip && *helpTooltip) {
        h += "<button id=\"inspector-section-";
        h += sectionId;
        h += "-help\" class=\"comp-help\" type=\"button\" title=\"";
        h += helpTooltip;
        h += "\" aria-label=\"";
        h += helpTooltip;
        h += "\">?</button>";
    }
    if (removeAction && *removeAction) {
        h += "<span class=\"comp-remove";
        if (playing) h += " disabled";
        h += "\" data-action=\"";
        h += removeAction;
        h += "\">";
        h += icon("" UI_ICON_DELETE "");
        h += "</span>";
    }
    h += "</div>";
    h += "[[inspector-body:" + std::string(sectionId) + "]]";
    return h;
}

constexpr std::string_view kSectionsEnd = "[[inspector-sections-end]]";

bool knownSection(std::string_view id) {
    static constexpr std::string_view ids[] = {
        "project", "general", "appearance", "world-bounds", "game-view", "layers", "diagnostics",
        "identity", "object-type-components", "transform", "sprite", "sprite-renderer", "sprite-animator",
        "tilemap", "camera-target", "scripts", "box-collider", "linear-mover", "top-down-controller",
        "platformer-controller", "auto-destroy", "text", "gauge", "object-variables",
    };
    for (std::string_view known : ids) if (known == id) return true;
    return false;
}

bool isSceneDropdown(std::string_view dropdownId) {
    return dropdownId == "game-view-preset";
}

bool isEntityDropdown(std::string_view dropdownId) {
    return dropdownId == "layer"
        || dropdownId == "sprite-source"
        || dropdownId == "sprite-default-clip"
        || dropdownId == "animator-default-clip"
        || dropdownId == "tilemap-tileset"
        || dropdownId == "script-attach"
        || dropdownId == "text-binding"
        || dropdownId == "text-variable"
        || dropdownId == "text-format"
        || dropdownId == "text-align"
        || dropdownId == "text-font"
        || dropdownId == "gauge-binding"
        || dropdownId == "gauge-variable"
        || dropdownId == "gauge-direction"
        // One per variable, so the key rides in the id: "object-variable-type|<key>".
        // Without this the refresh that follows the toggle cleared it again and
        // the list never appeared.
        || dropdownId.rfind("object-variable-type|", 0) == 0;
}

std::string truncateDisplayName(const std::string& name, std::size_t maxChars) {
    if (name.size() <= maxChars) return name;
    if (maxChars <= 3) return name.substr(0, maxChars);
    return name.substr(0, maxChars - 3) + "...";
}

std::string finalizeSectionMarkup(const std::string& marked,
                                  const std::unordered_set<std::string>& collapsed) {
    constexpr std::string_view sectionPrefix = "[[inspector-section:";
    constexpr std::string_view bodyPrefix = "[[inspector-body:";
    constexpr std::string_view suffix = "]]";
    std::string result;
    std::size_t cursor = 0;
    while (true) {
        const std::size_t section = marked.find(sectionPrefix, cursor);
        const std::size_t end = marked.find(kSectionsEnd, cursor);
        if (section == std::string::npos || (end != std::string::npos && end < section)) {
            const std::size_t stop = end == std::string::npos ? marked.size() : end;
            result.append(marked, cursor, stop - cursor);
            if (end != std::string::npos) cursor = end + kSectionsEnd.size();
            else return result;
            result.append(marked, cursor, std::string::npos);
            return result;
        }
        result.append(marked, cursor, section - cursor);
        const std::size_t sectionIdStart = section + sectionPrefix.size();
        const std::size_t sectionIdEnd = marked.find(suffix, sectionIdStart);
        if (sectionIdEnd == std::string::npos) return marked;
        const std::string id = marked.substr(sectionIdStart, sectionIdEnd - sectionIdStart);
        const std::string bodyMarker = std::string(bodyPrefix) + id + std::string(suffix);
        const std::size_t body = marked.find(bodyMarker, sectionIdEnd + suffix.size());
        if (body == std::string::npos) return marked;
        result.append(marked, sectionIdEnd + suffix.size(),
                      body - (sectionIdEnd + suffix.size()));
        const std::size_t nextSection = marked.find(sectionPrefix, body + bodyMarker.size());
        const std::size_t sectionsEnd = marked.find(kSectionsEnd, body + bodyMarker.size());
        std::size_t bodyEnd = marked.size();
        if (nextSection != std::string::npos) bodyEnd = nextSection;
        if (sectionsEnd != std::string::npos && sectionsEnd < bodyEnd) bodyEnd = sectionsEnd;
        if (collapsed.count(id) == 0) {
            result.append(marked, body + bodyMarker.size(),
                          bodyEnd - (body + bodyMarker.size()));
        }
        cursor = bodyEnd;
    }
}

std::string outsideSceneWarning(SceneContainment containment, bool playing) {
    if (containment == SceneContainment::Inside) return {};
    const char* label = containment == SceneContainment::FullyOutside
        ? "Outside scene bounds"
        : "Partially outside scene bounds";
    std::string html = "<div class=\"outside-warning\">"
        "<span class=\"icon\">" UI_ICON_DIAGNOSTICS "</span><span>";
    html += label;
    html += "</span></div>";
    html += "<button class=\"";
    html += playing ? "panel-btn disabled" : "panel-btn";
    html += "\" data-action=\"bring-entity-into-scene\">"
            "<span class=\"icon\">" UI_ICON_BRING_INTO_SCENE "</span>Bring Into Scene</button>";
    return html;
}

std::string BoxColliderModeLabel(BoxColliderMode mode) {
    switch (mode) {
        case BoxColliderMode::Solid: return "Solid";
        case BoxColliderMode::Trigger: return "Trigger";
        case BoxColliderMode::OneWayPlatform: return "One Way Platform";
    }
    return "Solid";
}

const SceneLayerDef* findLayer(const SceneDef& scene, const std::string& layerId) {
    for (const SceneLayerDef& layer : scene.layers) {
        if (layer.id == layerId) return &layer;
    }
    return nullptr;
}

// A one-row value dropdown trigger filling the value slot of a prop-row. The
// option list is emitted separately by the caller (in-flow, Add Component
// pattern — a floating popup would be clipped by the Inspector's scroll
// region). Wraps the panel-agnostic dropdownTriggerMarkup() in the
// Inspector's own prop-row/prop-label shell and its dedicated toggle action.
std::string dropdownTrigger(const char* label, const char* dropdownId,
                            const std::string& valueText, bool open, bool disabled,
                            const std::string& extraClass = "") {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span>";
    row += dropdownTriggerMarkup(valueText, "toggle-inspector-dropdown", dropdownId,
                                 open, disabled, extraClass);
    row += "</div>";
    return row;
}

// -- Object Variables (ADR-0031) ---------------------------------------------

bool isObjectVariableTextAction(std::string_view action) {
    return action == "commit-object-variable-key"
        || action == "commit-object-variable-default"
        || action == "commit-object-variable-description"
        || action == "commit-instance-variable-override";
}

const GameVariableDefinition* findObjectVariable(const EntityDef& type,
                                                 const std::string& key) {
    for (const GameVariableDefinition& variable : type.localVariables)
        if (variable.key == key) return &variable;
    return nullptr;
}

const char* variableTypeLabel(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Number:  return "Number";
    case GameVariableDefinition::Type::Boolean: return "Boolean";
    case GameVariableDefinition::Type::String:  return "String";
    }
    return "Number";
}

const char* variableTypeToken(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Number:  return "number";
    case GameVariableDefinition::Type::Boolean: return "boolean";
    case GameVariableDefinition::Type::String:  return "string";
    }
    return "number";
}

std::string variableValueText(const GameVariableValue& value) {
    if (const auto* number = std::get_if<double>(&value))
        return num(static_cast<float>(*number));
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "True" : "False";
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    return {};
}

/**
 * One editable value row: the Object Type's own, or an instance's override.
 * Only rendered for a value that exists — an instance without an override gets
 * an explicit Override button instead of an empty box, because RmlUi has no
 * placeholder and a blank field said nothing at all.
 */
std::string variableValueRow(const char* label, GameVariableDefinition::Type type,
                             const GameVariableValue& value, const char* commitAction,
                             const char* toggleAction, const std::string& key,
                             bool playing, const std::string& trailing = {},
                             const std::string* draftValue = nullptr,
                             bool draftInvalid = false) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span>";
    if (type == GameVariableDefinition::Type::Boolean) {
        const bool on = std::get_if<bool>(&value) && std::get<bool>(value);
        row += "<button class=\"panel-btn";
        if (on) row += " active";
        if (playing) row += " disabled";
        row += "\" data-action=\"";
        row += toggleAction;
        row += "\" data-arg=\"" + escapeRml(key) + "\">";
        row += on ? "True" : "False";
        row += "</button>";
    } else {
        row += "<input type=\"text\" class=\"prop-input";
        if (draftInvalid) row += " object-variable-draft-invalid";
        row += "\"";
        if (draftValue) row += " id=\"object-variable-draft-input\"";
        row += " data-action=\"";
        row += commitAction;
        row += "\" data-arg=\"" + escapeRml(key) + "\" value=\"";
        row += escapeRml(draftValue ? *draftValue : variableValueText(value));
        row += "\"";
        if (playing) row += " disabled=\"disabled\"";
        row += "/>";
    }
    row += trailing;
    row += "</div>";
    return row;
}

} // namespace

void InspectorPanel::toggleAddMenu(Rml::ElementDocument* document,
                                   const EditorCoordinator& coordinator) {
    addMenuOpen_ = !addMenuOpen_;
    refresh(document, coordinator);
}

void InspectorPanel::toggleDropdown(Rml::ElementDocument* document,
                                    const EditorCoordinator& coordinator,
                                    const std::string& dropdownId) {
    openDropdownId_ = (openDropdownId_ == dropdownId) ? std::string() : dropdownId;
    dropdownNav_.resetSession();
    refresh(document, coordinator);
}

void InspectorPanel::dismissTransientMenus(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator) {
    if (!addMenuOpen_ && openDropdownId_.empty()) return;
    addMenuOpen_ = false;
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    refresh(document, coordinator);
}

void InspectorPanel::moveDropdownHighlight(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator,
                                           int delta) {
    if (openDropdownId_.empty()) return;
    dropdownNav_.move(delta);
    refresh(document, coordinator);
}

std::optional<DropdownNavEntry> InspectorPanel::dropdownHighlightCommit() const {
    return dropdownNav_.commit();
}

bool InspectorPanel::isSectionCollapsed(const std::string& sectionId) const {
    return collapsedSections_.count(sectionId) != 0;
}

void InspectorPanel::toggleSection(Rml::ElementDocument* document,
                                   const EditorCoordinator& coordinator,
                                   const std::string& sectionId) {
    if (!knownSection(sectionId)) return;
    if (isSectionCollapsed(sectionId)) collapsedSections_.erase(sectionId);
    else collapsedSections_.insert(sectionId);
    // These are local presentation affordances and cannot remain meaningful
    // once their containing section disappears.
    addMenuOpen_ = false;
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    refresh(document, coordinator);
}

void InspectorPanel::beginSceneLayerRename(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator,
                                           const std::string& layerId) {
    if (coordinator.isPlaying() || coordinator.selection().primaryEntity != INVALID_ENTITY) {
        layerRename_.reset();
        refresh(document, coordinator);
        return;
    }

    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!scene) return;
    const SceneLayerDef* layer = findLayer(*scene, layerId);
    if (!layer) return;

    layerRename_ = SceneLayerRenameUiState{sceneId, layerId, layer->name, {}};
    refresh(document, coordinator);
}

void InspectorPanel::beginActiveSceneLayerRename(Rml::ElementDocument* document,
                                                 const EditorCoordinator& coordinator) {
    if (coordinator.isPlaying() || coordinator.selection().primaryEntity != INVALID_ENTITY) {
        return;
    }
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!scene || scene->layers.empty()) return;

    beginSceneLayerRename(document, coordinator, coordinator.activeLayerId(sceneId));
}

void InspectorPanel::commitSceneLayerRename(Rml::ElementDocument* document,
                                            EditorCoordinator& coordinator,
                                            const std::string& requestedName) {
    if (!layerRename_) return;

    if (!reconcileSceneLayerRenameUiState(coordinator)) {
        refresh(document, coordinator);
        return;
    }

    const SceneDef* scene = coordinator.document().findScene(layerRename_->sceneId);
    const SceneLayerDef* layer = scene ? findLayer(*scene, layerRename_->layerId) : nullptr;
    if (!layer) {
        layerRename_.reset();
        refresh(document, coordinator);
        return;
    }

    if (requestedName == layer->name) {
        layerRename_.reset();
        refresh(document, coordinator);
        return;
    }

    const SceneId sceneId = layerRename_->sceneId;
    const std::string layerId = layerRename_->layerId;
    EditorOperationResult result =
        coordinator.execute(RenameSceneLayerCommand{sceneId, layerId, requestedName});
    if (result.ok) {
        layerRename_.reset();
        refresh(document, coordinator);
        return;
    }

    layerRename_->draftName = requestedName;
    layerRename_->validationError = result.error;
    refresh(document, coordinator);
    focusSceneLayerRenameInput(document);
}

void InspectorPanel::cancelSceneLayerRename(Rml::ElementDocument* document,
                                            const EditorCoordinator& coordinator) {
    layerRename_.reset();
    refresh(document, coordinator);
}

void InspectorPanel::showEntityPositionPreview(Rml::ElementDocument* document,
                                               const EditorCoordinator& coordinator,
                                               EntityId entity,
                                               Vec2 position) {
    ArtCade::Transform xf;
    xf.position = position;
    if (const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
            coordinator.state().activeSceneId, entity)) {
        xf = inst->transform;
        xf.position = position;
    }
    showEntityTransformPreview(document, coordinator, entity, xf);
}

void InspectorPanel::showEntityTransformPreview(Rml::ElementDocument* document,
                                                const EditorCoordinator& coordinator,
                                                EntityId entity,
                                                const ArtCade::Transform& transform) {
    if (!document || coordinator.selection().primaryEntity != entity) return;
    if (!coordinator.document().findInstanceInScene(coordinator.state().activeSceneId, entity))
        return;

    const auto setValue = [&](const char* id, const std::string& value) {
        Rml::Element* element = document->GetElementById(id);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element)) {
            control->SetValue(value);
        }
    };
    setValue("inspector-pos-x", num(transform.position.x));
    setValue("inspector-pos-y", num(transform.position.y));
    setValue("inspector-scale-x", num(transform.scale.x));
    setValue("inspector-scale-y", num(transform.scale.y));
}

bool InspectorPanel::reconcileSceneLayerRenameUiState(const EditorCoordinator& coordinator) {
    if (!layerRename_) return true;
    if (coordinator.isPlaying()
        || coordinator.selection().primaryEntity != INVALID_ENTITY
        || coordinator.state().activeSceneId != layerRename_->sceneId) {
        layerRename_.reset();
        return false;
    }
    const SceneDef* scene = coordinator.document().findScene(layerRename_->sceneId);
    if (!scene || !findLayer(*scene, layerRename_->layerId)) {
        layerRename_.reset();
        return false;
    }
    return true;
}

void InspectorPanel::focusSceneLayerRenameInput(Rml::ElementDocument* document) {
    if (!document) return;
    Rml::Element* input = document->GetElementById("layer-rename-input");
    if (!input) return;
    input->Focus(true);
    if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
        control->Select();
    }
}

void InspectorPanel::revealTilemapCellSize(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator) {
    if (!document) return;
    Rml::Element* width = document->GetElementById("inspector-tilemap-cell-width");
    if (!width) return;

    width->ScrollIntoView(Rml::ScrollIntoViewOptions{
        Rml::ScrollAlignment::Start, Rml::ScrollAlignment::Nearest,
        Rml::ScrollBehavior::Smooth});
    width->SetClass("inspector-reveal-highlight", true);
    width->Focus(true);
    if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(width)) {
        control->Select();
    }

    const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
        coordinator.state().activeSceneId, coordinator.selection().primaryEntity);
    if (!inst) return;
    if (!coordinator.document().isInstanceLayerLocked(coordinator.state().activeSceneId, *inst)) {
        return;
    }

    const std::string layerId =
        coordinator.document().effectiveLayerId(coordinator.state().activeSceneId, *inst);
    const SceneDef* scene = coordinator.document().findScene(coordinator.state().activeSceneId);
    std::string layerName = layerId;
    if (scene) {
        for (const SceneLayerDef& layer : scene->layers) {
            if (layer.id == layerId) {
                layerName = layer.name;
                break;
            }
        }
    }
    const std::string tooltip =
        "Cell size cannot be edited because layer \"" + layerName + "\" is locked.";
    width->SetAttribute("title", tooltip);
}

void InspectorPanel::consumeInspectorReveal(Rml::ElementDocument* document,
                                            EditorCoordinator& coordinator) {
    const std::optional<InspectorRevealRequest> request =
        coordinator.takeInspectorRevealRequest();
    if (!request) return;
    if (coordinator.selection().primaryEntity != request->entityId) return;

    switch (request->property) {
        case InspectorProperty::TilemapCellSize:
            if (collapsedSections_.erase("tilemap") != 0)
                refresh(document, coordinator);
            revealTilemapCellSize(document, coordinator);
            break;
    }
}

void InspectorPanel::refresh(Rml::ElementDocument* document,
                             const EditorCoordinator& coordinator) {
    if (!document) return;
    Rml::Element* body = document->GetElementById("inspector-body");
    if (!body) return;

    // ADR-0034/0035: rebuilt fresh every paint by whichever dropdown block is
    // open; stays empty otherwise, so a stale index from a previously open
    // dropdown can't mismatch this frame's list.
    dropdownNav_.clearEntries();

    const EntityId selected = coordinator.selection().primaryEntity;
    const SceneInstanceDef* inst =
        coordinator.document().findInstanceInScene(coordinator.state().activeSceneId,
                                                   selected);
    reconcileObjectVariableDraft(coordinator);

    // A selected catalog definition has an Inspector mode of its own. This is
    // deliberately not represented by a synthetic instance, so a zero-use
    // Object Type stays editable and the two selection authorities remain
    // mutually exclusive.
    if (coordinator.selection().selectedObjectTypeId) {
        const ObjectTypeId& typeId = *coordinator.selection().selectedObjectTypeId;
        const EntityDef* type = coordinator.document().findObjectType(typeId);
        if (type) {
            lastEntity_ = INVALID_ENTITY;
            if (typeId != lastObjectTypeId_) {
                addMenuOpen_ = false;
                openDropdownId_.clear();
                dropdownNav_.resetSession();
                lastObjectTypeId_ = typeId;
            }
            const bool playing = coordinator.isPlaying();
            std::size_t sceneCount = 0;
            if (const SceneDef* activeScene = coordinator.document().findScene(
                    coordinator.state().activeSceneId)) {
                for (const SceneInstanceDef& candidate : activeScene->instances)
                    if (candidate.objectTypeId == typeId) ++sceneCount;
            }
            std::string html = "<div class=\"inspector-scene-context\" title=\""
                + escapeRml(typeId) + "\"><span class=\"context-kind\">Object Type</span>"
                  "<span class=\"context-separator\">&#183;</span><span class=\"context-name\">"
                + escapeRml(truncateDisplayName(type->name, 28)) + "</span></div>";
            html += "<div class=\"inspector-breadcrumb\" data-action=\"deselect-entity\" "
                    "title=\"Back to Scene properties\">Scene</div>";
            html += header("identity", isSectionCollapsed("identity"),
                           "" UI_ICON_GENERAL "", "Object Type", "TYPE", "type", "", playing);
            html += field("Name", "commit-type-name", type->name, playing, "inspector-type-name");
            html += "<div class=\"prop-row prop-meta\"><span class=\"prop-label\">ID</span>"
                    "<span class=\"prop-meta-value\">" + escapeRml(typeId) + "</span></div>";
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Instances here</span>"
                    "<span class=\"prop-readonly\">" + std::to_string(sceneCount) + "</span></div>";
            html += header("object-type-components", isSectionCollapsed("object-type-components"),
                           "" UI_ICON_OBJECT "", "Components", "TYPE", "type", "", playing);
            const bool hasDriver = type->linearMover || type->topDownController
                || type->platformerController;
            struct AddableTypeComponent {
                const char* label;
                const char* action;
                bool available;
            };
            const AddableTypeComponent addable[] = {
                {"Sprite", "add-sprite-renderer",
                 !type->spritePresentation && !type->spriteRenderer},
                {"Box Collider 2D", "add-box-collider", !type->boxCollider2D},
                {"Top Down Controller", "add-top-down", !hasDriver},
                {"Platformer Controller", "add-platformer", !hasDriver},
                {"Linear Mover", "add-linear-mover", !hasDriver},
                {"Auto Destroy", "add-auto-destroy", !type->autoDestroy},
                {"Text", "add-text", !type->text},
                {"Gauge", "add-gauge", !type->gauge},
            };
            bool hasAddable = false;
            for (const AddableTypeComponent& candidate : addable)
                hasAddable = hasAddable || candidate.available;
            if (hasAddable) {
                html += "<div class=\"add-component\"><div class=\"add-component-btn";
                if (playing) html += " disabled";
                if (addMenuOpen_ && !playing) html += " open";
                html += "\" data-action=\"toggle-add-component\"><span class=\"icon\">"
                        UI_ICON_ADD "</span>Add Component</div>";
                if (addMenuOpen_ && !playing) {
                    html += "<div class=\"add-list\">";
                    for (const AddableTypeComponent& candidate : addable) {
                        if (!candidate.available) continue;
                        html += "<div class=\"add-entry\" data-action=\""
                              + std::string(candidate.action) + "\">"
                              + candidate.label + "</div>";
                    }
                    html += "</div>";
                }
                html += "</div>";
            }
            body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
            return;
        }
    }

    // No entity selected: show the Scene Inspector for the active scene (same
    // panel, two modes — the authority is the existing activeSceneId, no new
    // selectedSceneId). With no active scene at all, fall back to the empty hint.
    if (!inst) {
        lastEntity_ = INVALID_ENTITY;
        lastObjectTypeId_.clear();
        addMenuOpen_ = false;
        reconcileOpenDropdownForScene();
        reconcileSceneLayerRenameUiState(coordinator);
        const bool playing = coordinator.isPlaying();
        const SceneId& activeScene = coordinator.state().activeSceneId;
        const SceneDef* scene = coordinator.document().findScene(activeScene);
        reconcileBackgroundDraft(coordinator, /*sceneMode=*/true);
        // A live Opacity drag owns the range element; SetInnerRML would steal
        // pointer capture and drop dragend. Preview stays surgical until commit.
        if (backgroundDraft_ && backgroundDraft_->dragActive) {
            applyBackgroundOpacityPreview(document, backgroundDraft_->preview);
            return;
        }
        const std::string projectName = coordinator.document().data().projectName.empty()
            ? std::string("Untitled")
            : coordinator.document().data().projectName;
        std::string html;
        if (scene) {
            html += "<div class=\"inspector-scene-context\" title=\""
                  + escapeRml(scene->name) + "\">"
                    "<span class=\"context-kind\">Scene</span>"
                    "<span class=\"context-separator\">&#183;</span>"
                    "<span class=\"context-name\">"
                  + escapeRml(truncateDisplayName(scene->name, 28))
                  + "</span></div>";
        }
        // Folder glyph, not the plus: "+" is the add-action icon everywhere else
        // (+ Create, + Import, + Add Layer), so "+ PROJECT" read as a button.
        html += header("project", isSectionCollapsed("project"),
                       "" UI_ICON_OPEN "", "Project", "", "", "", playing);
        html += field("Name", "commit-project-name", projectName, playing,
                      "inspector-project-name");
        if (!scene) {
            layerRename_.reset();
            backgroundDraft_.reset();
            html += "<p class=\"inspector-empty\">No scene open</p>";
            body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
            return;
        }
        if (playing) layerRename_.reset();
        const bool isStart = coordinator.document().startSceneId() == activeScene;
        const std::string btn = playing ? "panel-btn disabled" : "panel-btn";

        // -- GENERAL -----------------------------------------------------------
        html += header("general", isSectionCollapsed("general"),
                       "" UI_ICON_GENERAL "", "General", "", "", "", playing);
        html += field("Name", "commit-scene-name", scene->name, playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Start</span>";
        if (isStart) {
            html += "<span class=\"prop-start-badge\"><span class=\"icon\">" UI_ICON_START_SCENE "</span>"
                    "<span>Current Start Scene</span></span>";
        } else {
            html += "<button class=\"" + btn + "\" data-action=\"set-start-scene\">"
                    "Set as Start Scene</button>";
        }
        html += "</div>";
        html += "<div class=\"prop-subheading\">Metadata</div>";
        html += "<div class=\"prop-row prop-meta\"><span class=\"prop-label\">ID</span>"
                "<span class=\"prop-meta-value\">" + escapeRml(scene->id) + "</span>";
        if (!playing) {
            html += "<button class=\"prop-copy\" data-action=\"copy-scene-id\" "
                    "title=\"Copy scene ID\"><span class=\"icon\">" UI_ICON_COPY "</span></button>";
        }
        html += "</div>";
        html += "<div class=\"prop-row prop-meta\"><span class=\"prop-label\">Entities</span>"
                "<span class=\"prop-meta-value\">"
              + std::to_string(scene->instances.size()) + "</span></div>";

        // -- APPEARANCE (ADR-0020) ----------------------------------------------
        const Vec4 bgColor = (backgroundDraft_ && backgroundDraft_->dragActive)
            ? backgroundDraft_->preview : scene->backgroundColor;
        const std::string hex = formatColorHexRgb(bgColor);
        const std::string opacity = formatOpacityPercent(bgColor.a);
        html += header("appearance", isSectionCollapsed("appearance"),
                       "" UI_ICON_APPEARANCE "", "Appearance", "", "", "", playing);
        html += "<div class=\"prop-color-block\">"
                "<span class=\"prop-label\">Background</span>"
                "<div class=\"color-field\">";
        if (playing) {
            html += "<div id=\"scene-bg-swatch\" class=\"color-swatch disabled\" "
                    "title=\"Background color\">";
        } else {
            html += "<div id=\"scene-bg-swatch\" class=\"color-swatch\" "
                    "data-action=\"pick-scene-background-color\" "
                    "title=\"Choose background color\">";
        }
        html += "<div class=\"color-swatch-checker\"></div></div>";
        html += "<input id=\"scene-bg-hex\" type=\"text\" class=\"prop-input color-hex\""
                " data-action=\"commit-scene-background-hex\" value=\""
              + escapeRml(hex) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/>";
        html += "<button class=\"color-reset";
        if (playing) html += " disabled";
        html += "\" data-action=\"reset-scene-background\" "
                "title=\"Reset to default scene background\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "><span class=\"icon\">" UI_ICON_RESET "</span></button>";
        html += "</div></div>";
        html += "<div class=\"prop-row opacity-row\">"
                "<span class=\"prop-label\">Opacity</span>"
                "<input id=\"scene-bg-opacity-slider\" type=\"range\" "
                "class=\"inspector-opacity-slider\" "
                "data-action=\"change-scene-background-opacity\" "
                "min=\"0\" max=\"100\" step=\"1\" value=\""
              + escapeRml(opacity) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/>"
                "<input id=\"scene-bg-opacity-value\" type=\"text\" "
                "class=\"prop-input opacity-value\" "
                "data-action=\"commit-scene-background-opacity\" value=\""
              + escapeRml(opacity) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/><span class=\"prop-unit\">%</span></div>";

        // -- WORLD BOUNDS (world units; resizing never moves instances) ---------
        html += header("world-bounds", isSectionCollapsed("world-bounds"),
                       "" UI_ICON_SPATIAL "", "World Bounds", "", "", "", playing);
        html += fieldWithUnit("Width", "commit-scene-width", num(scene->worldSize.x), "wu", playing);
        html += fieldWithUnit("Height", "commit-scene-height", num(scene->worldSize.y), "wu", playing);
        // Fit View lives in the toolbar's view group (audit 7.4): a camera action
        // belongs to the Scene View, not to scene properties.

        // -- GAME VIEW (ADR-0018): visible area at Play; not world bounds --------
        html += header("game-view", isSectionCollapsed("game-view"),
                       "" UI_ICON_GAME_VIEW "", "Game View", "", "", "", playing);
        {
            const bool presetOpen = openDropdownId_ == "game-view-preset" && !playing;
            const std::string presetLabel = sceneViewportPresetLabel(scene->viewportSize);
            html += dropdownTrigger("Preset", "game-view-preset", presetLabel, presetOpen, playing);
            if (presetOpen) {
                html += "<div class=\"drop-list\">";
                for (const SceneViewportPreset& preset : kSceneViewportPresets) {
                    const bool selected = matchesWholePixelSize(scene->viewportSize, preset.size);
                    const std::string presetArg =
                        std::to_string(static_cast<int>(preset.size.x)) + "x"
                        + std::to_string(static_cast<int>(preset.size.y));
                    const std::size_t navIndex = dropdownNav_.push(
                        {"set-scene-viewport-preset", presetArg, "", selected});
                    html += "<div class=\"drop-entry";
                    if (selected) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"set-scene-viewport-preset\" data-arg=\""
                          + presetArg + "\">";
                    if (selected) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                    html += escapeRml(std::string(preset.label) + " - "
                                      + sceneViewportSizeText(preset.size));
                    html += "</div>";
                }
                html += "</div>";
            }
        }
        html += fieldWithUnit("Width", "commit-scene-viewport-width",
                              num(scene->viewportSize.x), "px", playing);
        html += fieldWithUnit("Height", "commit-scene-viewport-height",
                              num(scene->viewportSize.y), "px", playing);
        if (scene->viewportSize.x > scene->worldSize.x
            || scene->viewportSize.y > scene->worldSize.y) {
            html += "<div class=\"prop-hint warn\">Game View exceeds World Bounds</div>";
        }

        // -- LAYER MANAGER (per-scene render order; top row = foreground) ------
        html += header("layers", isSectionCollapsed("layers"),
                       "" UI_ICON_LAYER_MANAGER "", "Layer Manager", "", "", "", playing);
        const EditorSceneViewState& view = coordinator.sceneView(activeScene);
        const std::string activeLayer = coordinator.activeLayerId(activeScene);
        // Render rows reversed so the foreground layer (last in scene.layers) is on top.
        for (std::size_t i = scene->layers.size(); i-- > 0;) {
            const SceneLayerDef& layer = scene->layers[i];
            const bool isActive  = layer.id == activeLayer;
            const bool isHidden  = view.hiddenLayerIds.count(layer.id) > 0;
            const bool isDefault = layer.id == scene->defaultLayerId;
            html += "<div class=\"layer-row";
            if (isActive) html += " active";
            if (isHidden) html += " hidden";
            html += "\">";
            html += "<span class=\"layer-eye\" data-action=\"toggle-layer-visible\" data-arg=\""
                  + escapeRml(layer.id) + "\"><span class=\"icon\">" UI_ICON_HIDDEN "</span></span>";
            html += "<span class=\"layer-lock";
            if (layer.locked) html += " locked";
            html += "\" data-action=\"toggle-layer-locked\" data-arg=\"" + escapeRml(layer.id)
                  + "\" title=\"" + (layer.locked ? "Unlock layer" : "Lock layer")
                  + "\"><span class=\"icon\">"
                  + (layer.locked ? "" UI_ICON_LOCKED "" : "" UI_ICON_UNLOCKED "") + "</span></span>";
            const bool renaming = layerRename_
                && layerRename_->sceneId == activeScene
                && layerRename_->layerId == layer.id;
            if (renaming) {
                html += "<input id=\"layer-rename-input\" type=\"text\""
                        " class=\"layer-rename-input\" data-action=\"commit-layer-rename\""
                        " data-arg=\"" + escapeRml(layer.id) + "\" value=\""
                      + escapeRml(layerRename_->draftName) + "\"/>";
            } else {
                html += "<span class=\"layer-name\" data-action=\"select-layer\""
                        " data-dbl-action=\"begin-layer-rename\" data-arg=\""
                      + escapeRml(layer.id) + "\">";
                if (isActive) html += "&#x25cf; ";   // active marker
                html += escapeRml(layer.name) + "</span>";
            }
            if (!playing) {
                // Reorder arrows only when there is an order to change; the top
                // and bottom rows keep an inert (disabled) arrow so the arrow
                // columns stay aligned across rows. "Up" = toward foreground =
                // a higher index in scene->layers.
                if (scene->layers.size() > 1) {
                    const bool canUp   = i + 1 < scene->layers.size();
                    const bool canDown = i > 0;
                    html += "<span class=\"layer-btn";
                    if (!canUp) html += " disabled";
                    html += "\"";
                    if (canUp) html += " data-action=\"move-layer-up\" data-arg=\""
                                     + escapeRml(layer.id) + "\"";
                    html += ">&#x2191;</span>";
                    html += "<span class=\"layer-btn";
                    if (!canDown) html += " disabled";
                    html += "\"";
                    if (canDown) html += " data-action=\"move-layer-down\" data-arg=\""
                                       + escapeRml(layer.id) + "\"";
                    html += ">&#x2193;</span>";
                }
                if (!isDefault)
                    html += "<span class=\"layer-remove\" data-action=\"remove-layer\" data-arg=\""
                          + escapeRml(layer.id) + "\">&#xd7;</span>";
            }
            html += "</div>";
            if (renaming && !layerRename_->validationError.empty()) {
                html += "<div class=\"layer-rename-error\">"
                      + escapeRml(layerRename_->validationError) + "</div>";
            }
        }

        // ADR-0056: active-layer parallax settings (presentation-only; Play preview).
        // Presets lead with the visual result; exact per-axis factors remain available
        // as a secondary fine-tune path for asymmetric and advanced effects.
        const bool activeLayerValid =
            !activeLayer.empty()
            && coordinator.document().hasLayer(activeScene, activeLayer);
        if (activeLayerValid) {
            const SceneLayerSettings settings =
                coordinator.document().effectiveLayerSettings(activeScene, activeLayer);
            std::string activeName = activeLayer;
            for (const SceneLayerDef& layer : scene->layers) {
                if (layer.id == activeLayer) {
                    activeName = layer.name;
                    break;
                }
            }
            const bool parallaxDisabled = playing;
            const auto isUniformParallax = [&](float factor) {
                return settings.parallax.x == factor && settings.parallax.y == factor;
            };
            html += "<div class=\"layer-settings\">";
            html += "<div class=\"layer-settings-head\">"
                    "<span class=\"layer-settings-title\">Parallax</span>"
                    "<span class=\"layer-settings-layer\">"
                  + escapeRml(activeName) + " layer</span></div>";
            html += "<div class=\"layer-settings-copy\">Choose how this layer moves "
                    "with the camera.</div>";
            html += "<div class=\"mode-block layer-parallax-presets\">"
                    "<span class=\"mode-label\">Camera movement</span>"
                    "<div class=\"mode-options\">";
            const auto preset = [&](const char* label, const char* title,
                                    const char* action, const char* arg, bool active) {
                html += "<button type=\"button\" class=\"panel-btn mode-option";
                if (active) html += " active";
                if (parallaxDisabled) html += " disabled";
                html += "\" data-action=\"" + std::string(action) + "\"";
                if (arg && *arg) html += " data-arg=\"" + std::string(arg) + "\"";
                html += " title=\"" + std::string(title) + "\" aria-pressed=\"";
                html += active ? "true" : "false";
                html += "\"";
                if (parallaxDisabled) html += " disabled=\"disabled\"";
                html += ">" + std::string(label) + "</button>";
            };
            preset("Fixed", "Stays fixed to the game view", "apply-layer-parallax-preset",
                   "0", isUniformParallax(0.f));
            preset("Far", "Moves slowly like a distant background", "apply-layer-parallax-preset",
                   "0.5", isUniformParallax(0.5f));
            preset("Normal", "Moves normally with the world", "reset-layer-parallax",
                   "", isUniformParallax(1.f));
            preset("Near", "Moves faster like a foreground", "apply-layer-parallax-preset",
                   "1.5", isUniformParallax(1.5f));
            html += "</div></div>";
            html += "<div class=\"prop-subheading layer-fine-tune-title\" "
                    "title=\"Use different horizontal and vertical movement for advanced effects.\">"
                    "Fine tune</div>";
            html += field("Horizontal", "commit-layer-parallax-x",
                          num(settings.parallax.x), parallaxDisabled);
            html += field("Vertical", "commit-layer-parallax-y",
                          num(settings.parallax.y), parallaxDisabled);
            html += "<div class=\"prop-hint layer-play-hint\"><span class=\"icon\">"
                    UI_ICON_PLAY "</span>Preview the effect in Play mode.</div>";
            html += "</div>";
        }

        if (!playing)
            html += "<button class=\"" + btn + " layer-add-btn\" data-action=\"add-layer\">"
                    "<span class=\"icon\">" UI_ICON_ADD "</span>Add Layer</button>";

        // -- DIAGNOSTICS (derived query, recomputed each refresh) --------------
        const SceneFrameSnapshot diag =
            collectSceneFrameSnapshot(coordinator.document(), activeScene, INVALID_ENTITY);
        int outside = 0;
        for (const SceneFrameEntity& e : diag.entities) {
            if (const std::optional<WorldRect> b = editorBoundsForEntity(diag, e.entityId))
                if (classifySceneContainment(*b, diag.worldSize) != SceneContainment::Inside)
                    ++outside;
        }
        html += header("diagnostics", isSectionCollapsed("diagnostics"),
                       "" UI_ICON_DIAGNOSTICS "", "Diagnostics", "", "", "", playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Outside bounds</span>"
                "<span class=\"prop-readonly";
        if (outside > 0) html += " warn";
        html += "\">" + std::to_string(outside) + "</span></div>";
        const auto animationDiagnostics =
            collectAnimationAuthoringDiagnostics(coordinator.document());
        for (const AnimationAuthoringDiagnostic& diagnostic : animationDiagnostics) {
            html += "<div class=\"prop-row\"><span class=\"prop-label warn\">"
                  + escapeRml(diagnostic.code) + "</span>";
            if (!diagnostic.animationAssetId.empty()) {
                html += "<button class=\"panel-btn\" data-action=\"open-sprite-animation\" data-arg=\""
                      + escapeRml(diagnostic.animationAssetId) + "\">"
                      + escapeRml(diagnostic.message) + "</button>";
            } else {
                html += "<span class=\"prop-readonly warn\">"
                      + escapeRml(diagnostic.message) + "</span>";
            }
            html += "</div>";
        }

        body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
        applyBackgroundOpacityPreview(document, bgColor);
        if (layerRename_) focusSceneLayerRenameInput(document);
        return;
    }

    const bool playing = coordinator.isPlaying();
    lastObjectTypeId_.clear();
    layerRename_.reset();
    reconcileBackgroundDraft(coordinator, /*sceneMode=*/false);
    // The Add menu and value dropdowns are transient: a new selection or
    // entering Play closes them. Scene-only dropdowns are cleared via mode
    // reconcile; entity dropdowns survive refresh for the same entity.
    if (selected != lastEntity_) {
        addMenuOpen_ = false;
        openDropdownId_.clear();
        dropdownNav_.resetSession();
        lastEntity_ = selected;
    }
    if (playing) { addMenuOpen_ = false; openDropdownId_.clear(); dropdownNav_.resetSession(); }
    else reconcileOpenDropdownForEntity();

    const std::string btn = playing ? "panel-btn disabled" : "panel-btn";

    // A locked layer makes its instances' own authoring state read-only - but
    // only what's genuinely instance-owned (SceneId + EntityId addressed):
    // transform, name, layer, SpriteRenderer/SpriteAnimator/Tilemap. Box
    // Collider 2D / movers / controllers belong to the shared object type
    // (TYPE badge below) and are deliberately never gated by this - the type
    // can be edited from any of its instances regardless of which layers the
    // others sit on. See ProjectDocument::isInstanceLayerLocked.
    const bool instanceLocked = coordinator.document().isInstanceLayerLocked(
        coordinator.state().activeSceneId, *inst);
    const bool instanceDisabled = playing || instanceLocked;
    const std::string instanceBtn = instanceDisabled ? "panel-btn disabled" : "panel-btn";
    // Shown under every TYPE-badged (object-type-owned) component header while
    // this instance's layer is locked, so it reads as a deliberate exception
    // rather than a bug that Box Collider/movers/controllers stay editable.
    const std::string typeOwnedLockNote = instanceLocked
        ? "<div class=\"type-owned-note\">Shared by all instances of this object type "
          "&#8212; not protected by this layer's lock.</div>"
        : "";

    const auto& types = coordinator.document().data().objectTypes;
    const auto typeIt = types.find(inst->objectTypeId);
    const EntityDef* type = (typeIt != types.end()) ? &typeIt->second : nullptr;

    std::string html;

    // Breadcrumb back to the Scene Inspector: the only deliberate way to
    // deselect (Escape never does - see routeGlobalEscape). Always clickable,
    // even during Play or on a locked layer: selection is workspace state,
    // not authoring, same as picking an entity from the Hierarchy always is.
    if (const SceneDef* activeScene =
            coordinator.document().findScene(coordinator.state().activeSceneId)) {
        html += "<div class=\"inspector-breadcrumb\" data-action=\"deselect-entity\" "
                "title=\"Back to Scene properties\">"
              + escapeRml(activeScene->name) + "</div>";
    }

    if (instanceLocked) {
        html += "<div class=\"outside-warning panel-top\"><span class=\"icon\">" UI_ICON_LOCKED "</span>"
                "<span>This object belongs to a locked layer.</span></div>";
    }

    // -- Object (not a component) ---------------------------------------------
    html += header("identity", isSectionCollapsed("identity"),
                   "" UI_ICON_GENERAL "", "Object", "", "", "", playing);
    // The Object Type owns the sole user-facing name for every placement.
    // Disabled when objectTypeId does not resolve, because there is no shared
    // authoring object to rename.
    const std::string typeLabel = type ? type->name : inst->objectTypeId;
    html += field("Object Name", "commit-type-name", typeLabel, playing || !type);
    if (type) {
        html += "<div class=\"type-owned-note\">Changing this name updates every "
                "instance and its Logic Board label.</div>";
    }
    html += "<div class=\"prop-row\"><span class=\"prop-label\">Visible</span>"
            "<button class=\"" + instanceBtn + "\" data-action=\"toggle-instance-visible\">";
    html += inst->visible ? "On" : "Off";
    html += "</button></div>";

    // Layer picker (only when the scene declares layers; legacy scenes have none).
    const SceneDef* instScene = coordinator.document().findScene(coordinator.state().activeSceneId);
    if (instScene && !instScene->layers.empty()) {
        // effectiveLayerId, not the raw (possibly empty, meaning "default layer")
        // inst->layerId, so the current-layer readout and the selected option
        // stay correct for instances living on the scene's default layer.
        const std::string curLayer =
            coordinator.document().effectiveLayerId(coordinator.state().activeSceneId, *inst);
        std::string curLayerName = curLayer;
        for (const SceneLayerDef& l : instScene->layers)
            if (l.id == curLayer) curLayerName = l.name;
        const bool layerOpen = openDropdownId_ == "layer" && !instanceDisabled;
        html += dropdownTrigger("Layer", "layer", curLayerName, layerOpen, instanceDisabled);
        if (layerOpen) {
            html += "<div class=\"drop-list\">";
            const EditorSceneViewState& instSceneView =
                coordinator.sceneView(coordinator.state().activeSceneId);
            // Reversed so the foreground layer sits on top, matching the scene
            // inspector's Layers list.
            for (std::size_t i = instScene->layers.size(); i-- > 0;) {
                const SceneLayerDef& l = instScene->layers[i];
                const bool isCurrent = l.id == curLayer;
                // A locked *target* layer is shown but unpickable - the Command
                // already rejects it, this just avoids a doomed click. The source
                // layer being locked instead disables the whole picker already,
                // via instanceDisabled (computed above from isInstanceLayerLocked).
                const bool targetLocked = !isCurrent && l.locked;
                const bool targetHidden = instSceneView.hiddenLayerIds.count(l.id) > 0;
                // Locked entries carry no data-action (a click on them just
                // closes the list - see the action.empty() branch in
                // EditorUi::Listener::ProcessEvent), so they are excluded from
                // arrow-key navigation the same way: nothing to commit there.
                std::optional<std::size_t> navIndex;
                if (isCurrent) {
                    navIndex = dropdownNav_.push(
                        {"toggle-inspector-dropdown", "layer", "", /*current=*/true});
                } else if (!targetLocked) {
                    navIndex = dropdownNav_.push(
                        {"set-entity-layer", l.id, "", /*current=*/false});
                }
                const bool highlighted = navIndex.has_value()
                    && dropdownNav_.isHighlighted(*navIndex);
                html += "<div class=\"drop-entry";
                if (isCurrent) html += " selected";
                if (highlighted) html += " highlighted";
                if (targetLocked) html += " disabled";
                html += "\"";
                if (isCurrent) {
                    // Picking the current layer again just closes the list.
                    html += " data-action=\"toggle-inspector-dropdown\" data-arg=\"layer\"";
                } else if (!targetLocked) {
                    html += " data-action=\"set-entity-layer\" data-arg=\"" + escapeRml(l.id) + "\"";
                }
                if (targetLocked) html += " title=\"Layer is locked\"";
                else if (targetHidden) html += " title=\"Layer is hidden\"";
                html += ">";
                if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                if (targetLocked) html += "<span class=\"icon\">" UI_ICON_LOCKED "</span> ";
                else if (targetHidden) html += "<span class=\"icon\">" UI_ICON_HIDDEN "</span> ";
                html += escapeRml(l.name) + "</div>";
            }
            html += "</div>";
        }
    }

    // -- Transform (instance-owned; structural, no remove) --------------------
    html += header("transform", isSectionCollapsed("transform"),
                   "" UI_ICON_SPATIAL "", "Transform", "", "", "", instanceDisabled);
    html += fieldWithUnit("Position X", "commit-transform-position-x",
                          num(inst->transform.position.x), "wu", instanceDisabled,
                          "inspector-pos-x");
    html += fieldWithUnit("Position Y", "commit-transform-position-y",
                          num(inst->transform.position.y), "wu", instanceDisabled,
                          "inspector-pos-y");
    html += fieldWithUnit("Rotation", "commit-transform-rotation",
                          num(inst->transform.rotation * kRadToDeg, 2), "°", instanceDisabled,
                          "inspector-rotation");
    html += field("Scale X", "commit-transform-scale-x", num(inst->transform.scale.x),
                  instanceDisabled, "inspector-scale-x");
    html += field("Scale Y", "commit-transform-scale-y", num(inst->transform.scale.y),
                  instanceDisabled, "inspector-scale-y");
    const SceneFrameSnapshot frame = collectSceneFrameSnapshot(
        coordinator.document(), coordinator.state().activeSceneId, selected);
    if (const std::optional<WorldRect> bounds = editorBoundsForEntity(frame, selected)) {
        html += outsideSceneWarning(classifySceneContainment(*bounds, frame.worldSize), instanceDisabled);
    }

    // -- Sprite presentation: Object Type capability/defaults + instance delta.
    const ResolvedSpritePresentation presentation = type
        ? resolveSpritePresentation(*type, *inst) : ResolvedSpritePresentation{};
    if (type && (type->spritePresentation || type->spriteRenderer) && presentation.renderer) {
        const SpriteRendererComponent& sr = *presentation.renderer;
        const bool unifiedSprite = type->spritePresentation.has_value();
        html += header(unifiedSprite ? "sprite" : "sprite-renderer",
                       isSectionCollapsed(unifiedSprite ? "sprite" : "sprite-renderer"),
                       "" UI_ICON_RENAME "", unifiedSprite ? "Sprite" : "Sprite Renderer", "", "",
                       "remove-sprite-renderer", playing);
        html += typeOwnedLockNote;
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Visible</span>"
                "<button class=\"" + instanceBtn + "\" data-action=\"toggle-sprite-visible\">";
        html += sr.visible ? "On" : "Off";
        html += "</button>";
        if ((unifiedSprite && inst->spritePresentationOverride
             && inst->spritePresentationOverride->visible)
            || (!unifiedSprite && inst->spriteRendererOverride
                && inst->spriteRendererOverride->visible)) {
            html += "<span class=\"comp-badge override\">INSTANCE OVERRIDE</span>";
        }
        html += "</div>";
        const AssetId animationAssetId = presentation.animator
            ? presentation.animator->animationAssetId : AssetId{};
        const std::string sourceLabel = !animationAssetId.empty()
            ? animationAssetLabel(coordinator, animationAssetId)
            : (sr.imageAssetId.empty() ? std::string("(none)") : sr.imageAssetId);
        const bool sourceOpen = openDropdownId_ == "sprite-source" && !playing;
        html += dropdownTrigger("Source", "sprite-source", sourceLabel, sourceOpen, playing);
        if (sourceOpen) {
            html += "<div class=\"drop-list\">";
            const bool noneSelected = animationAssetId.empty() && sr.imageAssetId.empty();
            std::size_t noneNavIndex = dropdownNav_.push(noneSelected
                ? DropdownNavEntry{"toggle-inspector-dropdown", "sprite-source", "", true}
                : DropdownNavEntry{"set-sprite-asset", "", "", false});
            html += "<div class=\"drop-entry";
            if (noneSelected) html += " selected";
            if (dropdownNav_.isHighlighted(noneNavIndex)) html += " highlighted";
            html += noneSelected
                ? "\" data-action=\"toggle-inspector-dropdown\" data-arg=\"sprite-source\">"
                : "\" data-action=\"set-sprite-asset\" data-arg=\"\">";
            if (noneSelected) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
            html += "(none)</div>";

            // An image already turned into an animation is dropped from the
            // Images group (you almost always want the animation from then on) -
            // unless it's the instance's current source, so the picker never
            // hides what's actually assigned.
            std::vector<const ImageAssetDef*> pickableImages;
            for (const ImageAssetDef& asset : coordinator.document().data().imageAssets) {
                const bool isCurrent = animationAssetId.empty() && asset.assetId == sr.imageAssetId;
                if (isCurrent || !imageHasDerivedAnimation(coordinator, asset.assetId)) {
                    pickableImages.push_back(&asset);
                }
            }
            if (!pickableImages.empty()) {
                html += "<div class=\"asset-group-title\">Images</div>";
                for (const ImageAssetDef* asset : pickableImages) {
                    const bool isCurrent =
                        animationAssetId.empty() && asset->assetId == sr.imageAssetId;
                    const std::size_t navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "sprite-source", "", true}
                        : DropdownNavEntry{"set-sprite-asset", asset->assetId, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += isCurrent
                        ? "\" data-action=\"toggle-inspector-dropdown\" data-arg=\"sprite-source\">"
                        : "\" data-action=\"set-sprite-asset\" data-arg=\""
                            + escapeRml(asset->assetId) + "\">";
                    if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                    html += escapeRml(asset->assetId) + "</div>";
                }
            }
            const auto& animations = coordinator.document().data().spriteAnimationAssets;
            if (!animations.empty()) {
                html += "<div class=\"asset-group-title\">Animations</div>";
                for (const SpriteAnimationAssetDef& asset : animations) {
                    const bool isCurrent = asset.id == animationAssetId;
                    const std::size_t navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "sprite-source", "", true}
                        : DropdownNavEntry{"set-sprite-animation", asset.id, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += isCurrent
                        ? "\" data-action=\"toggle-inspector-dropdown\" data-arg=\"sprite-source\">"
                        : "\" data-action=\"set-sprite-animation\" data-arg=\""
                            + escapeRml(asset.id) + "\">";
                    if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                    html += escapeRml(assetDisplayName(asset.name, asset.id)) + "</div>";
                }
            }
            html += "</div>";
        }
        if (!animationAssetId.empty()) {
            // Navigates to view/edit the shared animation asset, not this
            // instance - unaffected by this instance's own layer lock.
            html += "<button class=\"" + btn + "\" data-action=\"open-sprite-animation\" data-arg=\""
                  + escapeRml(animationAssetId)
                  + "\"><span class=\"icon\">" UI_ICON_RENAME "</span>Open Animation Editor</button>";
        }
        if (unifiedSprite && presentation.animator) {
            const SpriteAnimatorComponent& animator = *presentation.animator;
            const SpriteAnimationAssetDef* animatorAsset =
                coordinator.document().findSpriteAnimationAsset(animator.animationAssetId);
            if (animatorAsset && !animatorAsset->clips.empty()) {
                const bool clipOpen = openDropdownId_ == "sprite-default-clip" && !playing;
                html += dropdownTrigger("Default Clip", "sprite-default-clip",
                                        animationClipDisplayName(coordinator,
                                            animator.animationAssetId, animator.defaultClipId),
                                        clipOpen, playing);
                if (clipOpen) {
                    html += "<div class=\"drop-list\">";
                    for (const SpriteAnimationClipDef& clip : animatorAsset->clips) {
                        const bool current = clip.id == animator.defaultClipId;
                        const std::size_t navIndex = dropdownNav_.push(current
                            ? DropdownNavEntry{"toggle-inspector-dropdown", "sprite-default-clip", "", true}
                            : DropdownNavEntry{"set-sprite-default-clip", clip.id, "", false});
                        html += "<div class=\"drop-entry";
                        if (current) html += " selected";
                        if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                        html += current
                            ? "\" data-action=\"toggle-inspector-dropdown\" data-arg=\"sprite-default-clip\">"
                            : "\" data-action=\"set-sprite-default-clip\" data-arg=\""
                                + escapeRml(clip.id) + "\">";
                        if (current) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                        html += escapeRml(clip.name.empty() ? clip.id : clip.name) + "</div>";
                    }
                    html += "</div>";
                }
            }
            html += field("Speed", "commit-sprite-speed", num(animator.playbackSpeed), playing);
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Auto Play</span>"
                    "<button class=\"" + btn + "\" data-action=\"toggle-sprite-autoplay\">"
                    + (animator.autoPlay ? std::string("On") : std::string("Off"))
                    + "</button></div>";
        }
        if ((unifiedSprite && inst->spritePresentationOverride)
            || (!unifiedSprite && inst->spriteRendererOverride)) {
            html += "<button class=\"" + instanceBtn
                  + "\" data-action=\"reset-sprite-override\">Reset to Object Type</button>";
        }
        if (!unifiedSprite && type->spriteAnimator && presentation.animator) {
            const SpriteAnimatorComponent& animator = *presentation.animator;
            const ResolvedSpriteAnimator resolvedAnimator =
                resolveSpriteAnimator(*type, *inst);
            const bool inherited = resolvedAnimator.origin == ComponentOrigin::EntityDefinition
                && !resolvedAnimator.explicitPlaybackSpeed
                && !resolvedAnimator.explicitAutoPlay
                && !resolvedAnimator.explicitDefaultClip
                && !resolvedAnimator.explicitAnimationAsset;
            std::string animatorBadge;
            if (resolvedAnimator.origin == ComponentOrigin::InstanceOverride) {
                animatorBadge = "INSTANCE OVERRIDE";
            } else if (inherited) {
                animatorBadge = "INHERITED";
            }
            html += header("sprite-animator", isSectionCollapsed("sprite-animator"),
                           "" UI_ICON_RENAME "", "Sprite Animator", animatorBadge.c_str(), "",
                           "remove-sprite-animator-type", playing);
            html += typeOwnedLockNote;
            if (inherited) {
                html += "<div class=\"type-owned-note\">Inherited from Object Type. "
                        "Edits change the type for every instance unless you Override.</div>";
            }
            const SpriteAnimationAssetDef* animatorAsset =
                coordinator.document().findSpriteAnimationAsset(animator.animationAssetId);
            const bool clipDisabled =
                resolvedAnimator.origin == ComponentOrigin::InstanceOverride
                    ? instanceDisabled : playing;
            if (animatorAsset && !animatorAsset->clips.empty()) {
                const bool clipOpen =
                    openDropdownId_ == "animator-default-clip" && !clipDisabled;
                html += dropdownTrigger(
                    "Default Clip", "animator-default-clip",
                    animationClipDisplayName(coordinator, animator.animationAssetId,
                                             animator.defaultClipId),
                    clipOpen, clipDisabled);
                if (clipOpen) {
                    html += "<div class=\"drop-list\">";
                    for (const SpriteAnimationClipDef& clip : animatorAsset->clips) {
                        const bool isCurrent = clip.id == animator.defaultClipId;
                        const std::size_t navIndex = dropdownNav_.push(isCurrent
                            ? DropdownNavEntry{"toggle-inspector-dropdown", "animator-default-clip", "", true}
                            : DropdownNavEntry{"set-animator-default-clip", clip.id, "", false});
                        html += "<div class=\"drop-entry";
                        if (isCurrent) html += " selected";
                        if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                        html += isCurrent
                            ? "\" data-action=\"toggle-inspector-dropdown\""
                              " data-arg=\"animator-default-clip\">"
                            : "\" data-action=\"set-animator-default-clip\" data-arg=\""
                                + escapeRml(clip.id) + "\">";
                        if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                        html += escapeRml(clip.name.empty() ? clip.id : clip.name) + "</div>";
                    }
                    html += "</div>";
                }
            } else {
                html += "<div class=\"prop-row\"><span class=\"prop-label\">Default Clip</span>"
                        "<span class=\"prop-readonly warn\">(missing)</span></div>";
            }
            // OT path by default — never silently write instance overrides.
            if (resolvedAnimator.origin == ComponentOrigin::InstanceOverride) {
                html += field("Speed", "commit-animator-speed", num(animator.playbackSpeed),
                              instanceDisabled);
                html += "<div class=\"prop-row\"><span class=\"prop-label\">Auto Play</span>"
                        "<button class=\"" + instanceBtn
                      + "\" data-action=\"toggle-animator-autoplay\">"
                      + (animator.autoPlay ? std::string("On") : std::string("Off"))
                      + "</button></div>";
            } else {
                html += field("Speed", "commit-animator-speed-ot", num(animator.playbackSpeed),
                              playing);
                html += "<div class=\"prop-row\"><span class=\"prop-label\">Auto Play</span>"
                        "<button class=\"" + btn
                      + "\" data-action=\"toggle-animator-autoplay-ot\">"
                      + (animator.autoPlay ? std::string("On") : std::string("Off"))
                      + "</button></div>";
            }
            if (resolvedAnimator.explicitPlaybackSpeed) {
                html += "<div class=\"type-owned-note\">Playback Speed: INSTANCE OVERRIDE</div>";
            }
            if (resolvedAnimator.explicitAutoPlay) {
                html += "<div class=\"type-owned-note\">Auto Play: INSTANCE OVERRIDE</div>";
            }
            if (resolvedAnimator.origin != ComponentOrigin::InstanceOverride) {
                html += "<button class=\"" + instanceBtn
                      + "\" data-action=\"override-animator-instance\">"
                        "Override for this instance</button>";
            }
            if (inst->spriteAnimatorOverride) {
                html += "<button class=\"" + instanceBtn
                      + "\" data-action=\"reset-animator-override\">Reset to Object Type</button>";
            }
        }
    }

    // -- Tilemap (instance-owned per ADR-0001) --------------------------------
    if (inst->tilemap.has_value()) {
        const TilemapComponent& tm = *inst->tilemap;
        const TilesetAsset* tmTileset = coordinator.document().findTilesetAsset(tm.tilesetAssetId);
        html += header("tilemap", isSectionCollapsed("tilemap"),
                       "" UI_ICON_SPATIAL "", "Tilemap", "", "", "remove-tilemap-component", instanceDisabled);
        const std::string tilesetLabel = tmTileset
            ? assetDisplayName(tmTileset->name, tmTileset->assetId)
            : std::string("(missing)");
        if (coordinator.document().data().tilesets.size() > 1) {
            const bool tilesetOpen = openDropdownId_ == "tilemap-tileset" && !instanceDisabled;
            html += dropdownTrigger("Tileset", "tilemap-tileset", tilesetLabel, tilesetOpen,
                                    instanceDisabled);
            if (tilesetOpen) {
                html += "<div class=\"drop-list\">";
                for (const TilesetAsset& ts : coordinator.document().data().tilesets) {
                    const bool isCurrent = ts.assetId == tm.tilesetAssetId;
                    const std::size_t navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "tilemap-tileset", "", true}
                        : DropdownNavEntry{"set-tilemap-tileset", ts.assetId, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += isCurrent
                        ? "\" data-action=\"toggle-inspector-dropdown\" data-arg=\"tilemap-tileset\">"
                        : "\" data-action=\"set-tilemap-tileset\" data-arg=\""
                            + escapeRml(ts.assetId) + "\">";
                    if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
                    html += escapeRml(assetDisplayName(ts.name, ts.assetId)) + "</div>";
                }
                html += "</div>";
            }
        } else {
            // A single tileset is not a choice: keep the plain readout.
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Tileset</span>"
                    "<span class=\"prop-readonly\">" + escapeRml(tilesetLabel) + "</span></div>";
        }
        // cellSize is editable (SetTilemapCellSizeCommand, Slice 4); chunkSize
        // is genuinely immutable after creation (no setter command exists).
        html += field("Cell Width", "commit-tilemap-cell-width", num(tm.cellSize.x), instanceDisabled,
                      "inspector-tilemap-cell-width");
        html += field("Cell Height", "commit-tilemap-cell-height", num(tm.cellSize.y), instanceDisabled,
                      "inspector-tilemap-cell-height");
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Chunk Size</span>"
                "<span class=\"prop-readonly\">" + std::to_string(tm.chunkSize) + "</span></div>";

        // Paint tools and the Tile Palette live in the Scene workspace dock
        // (Tile Palette), not among persistent Tilemap properties. Offer a
        // one-click affordance when the dock is hidden for this session.
        if (!instanceDisabled) {
            if (!tmTileset) {
                html += "<div class=\"tile-palette-empty\">Tileset is missing.</div>";
            } else if (!coordinator.document().findImageAsset(tmTileset->imageAssetId)) {
                html += "<div class=\"tile-palette-empty\">Tileset image is missing.</div>";
            } else if (tmTileset->tiles.empty()) {
                html += "<div class=\"tile-palette-empty\">This tileset has no sliced tiles."
                        "<br/><button class=\"panel-btn\" data-action=\"open-tilemap-tileset-editor\">"
                        "Open Tileset Editor</button></div>";
            } else if (!coordinator.uiState().tilePaletteDockVisible) {
                html += "<button class=\"panel-btn\" data-action=\"show-tile-palette-dock\">"
                        "Show Tile Palette</button>";
            }
        }
    }

    // -- Scripts (Object-Type owned; every instance inherits this order) -------
    if (type) {
        html += header("scripts", isSectionCollapsed("scripts"),
                       "" UI_ICON_SCRIPTS "", "Scripts", "", "", "", playing);
        html += typeOwnedLockNote;
        const ScriptComponent emptyScripts;
        const ScriptComponent& scripts = type->scripts ? *type->scripts : emptyScripts;
        if (scripts.attachments.empty()) {
            html += "<div class=\"type-owned-note\">No scripts attached.</div>";
        }
        for (std::size_t index = 0; index < scripts.attachments.size(); ++index) {
            const ScriptAttachmentDef& attachment = scripts.attachments[index];
            const ScriptAssetDef* asset =
                coordinator.document().findScriptAsset(attachment.scriptAssetId);
            const std::string label = asset
                ? assetDisplayName(asset->name, asset->assetId)
                : std::string("(missing)");
            html += "<div class=\"script-attachment-row\"><button class=\"panel-btn";
            if (playing) html += " disabled";
            html += "\" data-action=\"toggle-script-attachment\" data-arg=\""
                  + escapeRml(attachment.id) + "\">"
                  + (attachment.enabled ? std::string("On") : std::string("Off"))
                  + "</button><button class=\"script-attachment-name\" data-action=\"open-script\" data-arg=\""
                  + escapeRml(attachment.scriptAssetId) + "\">"
                  + escapeRml(label) + "</button>";
            const auto actionButton = [&](const char* action, const char* glyph, bool disabled) {
                html += "<button class=\"script-attachment-action";
                if (disabled || playing) html += " disabled";
                html += "\" data-action=\"";
                html += action;
                html += "\" data-arg=\"" + escapeRml(attachment.id) + "\">";
                html += glyph;
                html += "</button>";
            };
            actionButton("move-script-attachment-up", "&#x2191;", index == 0);
            actionButton("move-script-attachment-down", "&#x2193;",
                         index + 1 == scripts.attachments.size());
            actionButton("remove-script-attachment", "&#x00d7;", false);
            html += "</div>";
        }
        if (!coordinator.document().data().scriptAssets.empty()) {
            const bool attachOpen = openDropdownId_ == "script-attach" && !playing;
            html += dropdownTrigger("Attach", "script-attach", "Choose Script...",
                                    attachOpen, playing);
            if (attachOpen) {
                html += "<div class=\"drop-list\">";
                for (const ScriptAssetDef& asset : coordinator.document().data().scriptAssets) {
                    const std::size_t navIndex = dropdownNav_.push(
                        {"attach-script", asset.assetId, "", false});
                    html += "<div class=\"drop-entry";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"attach-script\" data-arg=\""
                          + escapeRml(asset.assetId) + "\">"
                          + escapeRml(assetDisplayName(asset.name, asset.assetId)) + "</div>";
                }
                html += "</div>";
            }
        } else {
            html += "<button class=\"panel-btn";
            if (playing) html += " disabled";
            html += "\" data-action=\"create-script\">Create Script Asset</button>";
        }
    }

    // -- Box Collider 2D (object-type owned) ----------------------------------
    const BoxCollider2DComponent* collider =
        (type && type->boxCollider2D) ? &*type->boxCollider2D : nullptr;
    if (collider) {
        html += header("box-collider", isSectionCollapsed("box-collider"),
                       "" UI_ICON_COLLIDER "", "Box Collider 2D", "", "", "remove-box-collider", playing);
        html += typeOwnedLockNote;
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Enabled</span>"
                "<button class=\"" + btn + "\" data-action=\"toggle-box-enabled\">";
        html += collider->enabled ? "On" : "Off";
        html += "</button></div>";
        html += "<div class=\"mode-block\"><span class=\"mode-label\">Mode</span>"
                "<div class=\"mode-options\">";
        const auto modeOption = [&](BoxColliderMode mode, const char* arg, const char* label) {
            html += "<button class=\"panel-btn mode-option";
            if (collider->mode == mode) html += " active";
            if (playing) html += " disabled";
            html += "\" data-action=\"set-box-mode\" data-arg=\"";
            html += arg;
            html += "\">";
            html += label;
            html += "</button>";
        };
        modeOption(BoxColliderMode::Solid, "solid", "Solid");
        modeOption(BoxColliderMode::Trigger, "trigger", "Trigger");
        modeOption(BoxColliderMode::OneWayPlatform, "oneWayPlatform", "One Way Platform");
        html += "</div></div>";
        html += field("Offset X", "commit-box-offset-x", num(collider->offset.x), playing);
        html += field("Offset Y", "commit-box-offset-y", num(collider->offset.y), playing);
        html += field("Size W", "commit-box-size-x", num(collider->size.x), playing);
        html += field("Size H", "commit-box-size-y", num(collider->size.y), playing);
    }

    // -- Linear Mover (object-type owned) -------------------------------------
    const LinearMoverComponent* mover =
        (type && type->linearMover) ? &*type->linearMover : nullptr;
    if (mover) {
        html += header("linear-mover", isSectionCollapsed("linear-mover"),
                       "" UI_ICON_SPATIAL "", "Linear Mover", "", "", "remove-linear-mover", playing);
        html += typeOwnedLockNote;
        html += field("Direction X", "commit-mover-dir-x", num(mover->directionX), playing);
        html += field("Direction Y", "commit-mover-dir-y", num(mover->directionY), playing);
        html += field("Speed", "commit-mover-speed", num(mover->speed), playing);
    }

    // -- Top Down Controller (object-type owned) ------------------------------
    const TopDownControllerComponent* controller =
        (type && type->topDownController) ? &*type->topDownController : nullptr;
    if (controller) {
        html += header("top-down-controller", isSectionCollapsed("top-down-controller"),
                       "" UI_ICON_CONTROLLER "", "Top Down Controller", "", "", "remove-top-down", playing);
        html += typeOwnedLockNote;
        html += field("Speed", "commit-topdown-speed", num(controller->maxSpeed), playing);
        html += field("Acceleration", "commit-topdown-acceleration",
                      num(controller->acceleration), playing);
        html += field("Friction", "commit-topdown-friction",
                      num(controller->friction), playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Four Directions</span>"
                "<button class=\"" + btn
              + "\" data-action=\"toggle-topdown-four-directions\">"
              + (controller->fourDirections ? std::string("On") : std::string("Off"))
              + "</button></div>";
    }

    // -- Platformer Controller (object-type owned) ----------------------------
    const PlatformerControllerComponent* platformer =
        (type && type->platformerController) ? &*type->platformerController : nullptr;
    if (platformer) {
        html += header("platformer-controller", isSectionCollapsed("platformer-controller"),
                       "" UI_ICON_CONTROLLER "", "Platformer Controller", "", "", "remove-platformer", playing);
        html += typeOwnedLockNote;
        html += field("Move Speed", "commit-platformer-move", num(platformer->maxSpeed), playing);
        html += field("Jump Speed", "commit-platformer-jump", num(platformer->jumpForce), playing);
        html += field("Gravity", "commit-platformer-gravity", num(platformer->customGravity), playing);
        html += field("Coyote Time", "commit-platformer-coyote", num(platformer->coyoteTime), playing);
        html += field("Jump Buffer", "commit-platformer-jump-buffer", num(platformer->jumpBuffer), playing);
        html += field("Climb Speed", "commit-platformer-climb", num(platformer->climbSpeed), playing);
    }

    // -- Camera Target (instance-owned, one per scene; ADR-0003) ------------
    if (inst->cameraTarget.has_value()) {
        const CameraTargetComponent& target = *inst->cameraTarget;
        html += header("camera-target", isSectionCollapsed("camera-target"),
                       "" UI_ICON_COLLAPSE "", "Camera Target", "", "",
                       "remove-camera-target", instanceDisabled);
        html += field("Offset X", "commit-camera-target-offset-x", num(target.offsetX),
                      instanceDisabled);
        html += field("Offset Y", "commit-camera-target-offset-y", num(target.offsetY),
                      instanceDisabled);
        html += field("Follow Speed", "commit-camera-target-follow-speed",
                      num(target.followSpeed), instanceDisabled);
        html += "<div class=\"type-owned-note\">0 snaps to the target.</div>";
    }

    // -- Auto Destroy (object-type owned) ------------------------------------
    const AutoDestroyComponent* autoDestroy =
        (type && type->autoDestroy) ? &*type->autoDestroy : nullptr;
    if (autoDestroy) {
        html += header("auto-destroy", isSectionCollapsed("auto-destroy"),
                       "" UI_ICON_DELETE "", "Auto Destroy", "", "", "remove-auto-destroy", playing);
        html += typeOwnedLockNote;
        html += fieldWithUnit("Lifetime", "commit-auto-destroy-lifespan",
                              num(autoDestroy->lifespan), "s", playing);
        html += "<div class=\"type-owned-note\">0 disables automatic expiry.</div>";
    }

    // -- Text (object-type owned) --------------------------------------------
    const TextComponent* textComp = (type && type->text) ? &*type->text : nullptr;
    if (textComp) {
        html += header("text", isSectionCollapsed("text"),
                       "" UI_ICON_TEXT "", "Text", "", "", "remove-text", playing);
        html += typeOwnedLockNote;
        html += field("Text", "commit-text-static", textComp->text, playing);
        html += field("Prefix", "commit-text-prefix", textComp->prefix, playing);
        html += field("Suffix", "commit-text-suffix", textComp->suffix, playing);

        const bool bindingLocal = textComp->bindScope == "local";
        // Global with empty key shares storage with None; while the Variable
        // list is open after a Global pick, surface the choice as Global.
        const bool pickingGlobalVariable = !bindingLocal
            && textComp->bindKey.empty()
            && openDropdownId_ == "text-variable";
        const bool bindingNone =
            textComp->bindKey.empty() && !bindingLocal && !pickingGlobalVariable;
        const bool bindingGlobal = !bindingNone && !bindingLocal;
        const std::string bindingLabel = bindingNone ? "None"
            : (bindingLocal ? "Local" : "Global");
        const bool bindingOpen = openDropdownId_ == "text-binding";
        html += dropdownTrigger("Binding", "text-binding", bindingLabel, bindingOpen, playing);
        if (bindingOpen && !playing) {
            html += "<div class=\"drop-list\">";
            const auto bindingEntry = [&](bool current, const char* setAction, const char* label) {
                const std::size_t navIndex = dropdownNav_.push(current
                    ? DropdownNavEntry{"toggle-inspector-dropdown", "text-binding", "", true}
                    : DropdownNavEntry{setAction, "", "", false});
                html += "<div class=\"drop-entry";
                if (current) html += " selected";
                if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                html += "\" data-action=\"";
                html += current ? "toggle-inspector-dropdown\" data-arg=\"text-binding\""
                                : (std::string(setAction) + "\"");
                html += ">";
                html += label;
                html += "</div>";
            };
            bindingEntry(bindingNone, "set-text-binding-none", "None");
            bindingEntry(bindingGlobal, "set-text-binding-global", "Global");
            bindingEntry(bindingLocal, "set-text-binding-local", "Local");
            html += "</div>";
        }

        // Variable picker: Local (even unbound), bound Global, or Global handoff.
        const bool showTextVariable =
            !bindingNone || openDropdownId_ == "text-variable";
        if (showTextVariable) {
            const bool varOpen = openDropdownId_ == "text-variable";
            const std::string varLabel =
                textComp->bindKey.empty() ? "Choose Variable..." : textComp->bindKey;
            html += dropdownTrigger("Variable", "text-variable", varLabel, varOpen, playing);
            if (varOpen && !playing && type) {
                html += "<div class=\"drop-list\">";
                const auto& vars = textComp->bindScope == "local"
                    ? type->localVariables
                    : coordinator.document().data().globalVariables;
                const auto format = textValueFormatFromString(textComp->format)
                    .value_or(TextValueFormat::Text);
                bool any = false;
                for (const GameVariableDefinition& variable : vars) {
                    if (!isTextFormatCompatibleWithVariableType(format, variable.type)) continue;
                    any = true;
                    const bool isCurrent = variable.key == textComp->bindKey;
                    const std::size_t navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "text-variable", "", true}
                        : DropdownNavEntry{"set-text-variable", variable.key, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"";
                    if (isCurrent) {
                        html += "toggle-inspector-dropdown\" data-arg=\"text-variable\"";
                    } else {
                        html += "set-text-variable\" data-arg=\"";
                        html += escapeRml(variable.key);
                        html += "\"";
                    }
                    html += ">";
                    html += escapeRml(variable.key);
                    html += "</div>";
                }
                if (!any) {
                    html += "<div class=\"drop-entry disabled\">No compatible variables</div>";
                }
                html += "</div>";
            }
        }

        const bool formatOpen = openDropdownId_ == "text-format";
        html += dropdownTrigger("Format", "text-format", textComp->format, formatOpen, playing);
        if (formatOpen && !playing) {
            html += "<div class=\"drop-list\">";
            const char* formats[] = {
                "text", "integer", "padded", "time", "percent", "decimals"};
            std::optional<GameVariableDefinition::Type> boundType;
            if (!textComp->bindKey.empty() && type) {
                const auto& vars = textComp->bindScope == "local"
                    ? type->localVariables
                    : coordinator.document().data().globalVariables;
                for (const GameVariableDefinition& variable : vars) {
                    if (variable.key == textComp->bindKey) {
                        boundType = variable.type;
                        break;
                    }
                }
            }
            for (const char* format : formats) {
                const bool compatible = !boundType
                    || isTextFormatCompatibleWithVariableType(format, *boundType);
                const bool isCurrent = textComp->format == format;
                std::optional<std::size_t> navIndex;
                if (compatible) {
                    navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "text-format", "", true}
                        : DropdownNavEntry{"set-text-format", format, "", false});
                }
                html += "<div class=\"drop-entry";
                if (isCurrent) html += " selected";
                if (navIndex && dropdownNav_.isHighlighted(*navIndex)) html += " highlighted";
                if (!compatible) html += " disabled";
                html += "\"";
                if (compatible) {
                    html += " data-action=\"";
                    if (isCurrent) {
                        html += "toggle-inspector-dropdown\" data-arg=\"text-format\"";
                    } else {
                        html += "set-text-format\" data-arg=\"";
                        html += format;
                        html += "\"";
                    }
                } else {
                    html += " title=\"Incompatible with bound variable type\"";
                }
                html += ">";
                html += format;
                html += "</div>";
            }
            html += "</div>";
        }

        if (textComp->format == "padded" || textComp->format == "decimals") {
            html += field("Digits", "commit-text-digits",
                          std::to_string(textComp->digits), playing);
        }
        html += field("Size", "commit-text-size", std::to_string(textComp->size), playing);
        html += field("Color", "commit-text-color", formatColorHexRgb(textComp->color), playing);
        {
            // ADR-0036: picker over ProjectDoc.fontAssets; fontPath == "" is
            // "Default Font" (the Scene View's CanvasFont/Inter fallback).
            // A fontPath that no longer matches any font asset (deleted since
            // assignment) shows the raw path, same "(missing)"-style honesty
            // as the Sprite Source / Tileset pickers elsewhere in this file.
            const auto& fontAssets = coordinator.document().data().fontAssets;
            std::string fontLabel = "Default Font";
            bool fontFound = textComp->fontPath.empty();
            if (!fontFound) {
                fontLabel = textComp->fontPath;
                for (const FontAssetDef& font : fontAssets) {
                    if (font.sourcePath == textComp->fontPath) {
                        fontLabel = assetDisplayName(font.name, font.assetId);
                        fontFound = true;
                        break;
                    }
                }
            }
            const bool fontOpen = openDropdownId_ == "text-font";
            html += dropdownTrigger("Font", "text-font", fontLabel, fontOpen, playing);
            if (fontOpen && !playing) {
                html += "<div class=\"drop-list\">";
                const bool defaultIsCurrent = textComp->fontPath.empty();
                std::size_t navIndex = dropdownNav_.push(defaultIsCurrent
                    ? DropdownNavEntry{"toggle-inspector-dropdown", "text-font", "", true}
                    : DropdownNavEntry{"set-text-font", "", "", false});
                html += "<div class=\"drop-entry";
                if (defaultIsCurrent) html += " selected";
                if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                html += "\" data-action=\"";
                html += defaultIsCurrent ? "toggle-inspector-dropdown\" data-arg=\"text-font\""
                                         : "set-text-font\" data-arg=\"\"";
                html += ">Default Font</div>";
                for (const FontAssetDef& font : fontAssets) {
                    const bool isCurrent = font.sourcePath == textComp->fontPath;
                    navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "text-font", "", true}
                        : DropdownNavEntry{"set-text-font", font.sourcePath, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"";
                    if (isCurrent) {
                        html += "toggle-inspector-dropdown\" data-arg=\"text-font\"";
                    } else {
                        html += "set-text-font\" data-arg=\"" + escapeRml(font.sourcePath) + "\"";
                    }
                    html += ">" + escapeRml(assetDisplayName(font.name, font.assetId)) + "</div>";
                }
                html += "</div>";
            }
        }

        const bool alignOpen = openDropdownId_ == "text-align";
        html += dropdownTrigger("Anchor", "text-align", textComp->align, alignOpen, playing);
        if (alignOpen && !playing) {
            html += "<div class=\"drop-list\">";
            const char* anchors[] = {
                "top-left", "top-center", "top-right",
                "middle-left", "center", "middle-right",
                "bottom-left", "bottom-center", "bottom-right"};
            for (const char* anchor : anchors) {
                const bool isCurrent = textComp->align == anchor;
                const std::size_t navIndex = dropdownNav_.push(isCurrent
                    ? DropdownNavEntry{"toggle-inspector-dropdown", "text-align", "", true}
                    : DropdownNavEntry{"set-text-align", anchor, "", false});
                html += "<div class=\"drop-entry";
                if (isCurrent) html += " selected";
                if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                html += "\" data-action=\"";
                if (isCurrent) {
                    html += "toggle-inspector-dropdown\" data-arg=\"text-align\"";
                } else {
                    html += "set-text-align\" data-arg=\"";
                    html += anchor;
                    html += "\"";
                }
                html += ">";
                html += anchor;
                html += "</div>";
            }
            html += "</div>";
        }
        html += field("Offset X", "commit-text-offset-x", num(textComp->offsetX), playing);
        html += field("Offset Y", "commit-text-offset-y", num(textComp->offsetY), playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">HUD</span>"
                "<div class=\"prop-toggle";
        if (textComp->screenSpace) html += " on";
        if (playing) html += " disabled";
        html += "\" data-action=\"toggle-text-screen-space\"></div></div>";
    }

    // -- Gauge (object-type owned) -------------------------------------------
    const GaugeComponent* gaugeComp = (type && type->gauge) ? &*type->gauge : nullptr;
    if (gaugeComp) {
        html += header("gauge", isSectionCollapsed("gauge"),
                       "" UI_ICON_GAUGE "", "Gauge", "", "", "remove-gauge", playing);
        html += typeOwnedLockNote;
        const bool gaugeBindingLocal = gaugeComp->bindScope == "local";
        const bool pickingGaugeGlobalVariable = !gaugeBindingLocal
            && gaugeComp->bindKey.empty()
            && openDropdownId_ == "gauge-variable";
        const bool gaugeBindingNone =
            gaugeComp->bindKey.empty() && !gaugeBindingLocal && !pickingGaugeGlobalVariable;
        const bool gaugeBindingGlobal = !gaugeBindingNone && !gaugeBindingLocal;
        const std::string gaugeBindingLabel = gaugeBindingNone ? "None"
            : (gaugeBindingLocal ? "Local" : "Global");
        const bool gaugeBindingOpen = openDropdownId_ == "gauge-binding";
        html += dropdownTrigger("Binding", "gauge-binding", gaugeBindingLabel,
                                gaugeBindingOpen, playing);
        if (gaugeBindingOpen && !playing) {
            html += "<div class=\"drop-list\">";
            const auto gaugeBindingEntry = [&](bool current, const char* setAction, const char* label) {
                const std::size_t navIndex = dropdownNav_.push(current
                    ? DropdownNavEntry{"toggle-inspector-dropdown", "gauge-binding", "", true}
                    : DropdownNavEntry{setAction, "", "", false});
                html += "<div class=\"drop-entry";
                if (current) html += " selected";
                if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                html += "\" data-action=\"";
                html += current ? "toggle-inspector-dropdown\" data-arg=\"gauge-binding\""
                                : (std::string(setAction) + "\"");
                html += ">";
                html += label;
                html += "</div>";
            };
            gaugeBindingEntry(gaugeBindingNone, "set-gauge-binding-none", "None");
            gaugeBindingEntry(gaugeBindingGlobal, "set-gauge-binding-global", "Global");
            gaugeBindingEntry(gaugeBindingLocal, "set-gauge-binding-local", "Local");
            html += "</div>";
        }
        const bool showGaugeVariable =
            !gaugeBindingNone || openDropdownId_ == "gauge-variable";
        if (showGaugeVariable) {
            const bool varOpen = openDropdownId_ == "gauge-variable";
            html += dropdownTrigger("Variable", "gauge-variable",
                                    gaugeComp->bindKey.empty() ? "Choose Variable..."
                                                               : gaugeComp->bindKey,
                                    varOpen, playing);
            if (varOpen && !playing && type) {
                html += "<div class=\"drop-list\">";
                const auto& vars = gaugeComp->bindScope == "local"
                    ? type->localVariables
                    : coordinator.document().data().globalVariables;
                bool any = false;
                for (const GameVariableDefinition& variable : vars) {
                    if (!isGaugeCompatibleWithVariableType(variable.type)) continue;
                    any = true;
                    const bool isCurrent = variable.key == gaugeComp->bindKey;
                    const std::size_t navIndex = dropdownNav_.push(isCurrent
                        ? DropdownNavEntry{"toggle-inspector-dropdown", "gauge-variable", "", true}
                        : DropdownNavEntry{"set-gauge-variable", variable.key, "", false});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"";
                    if (isCurrent) {
                        html += "toggle-inspector-dropdown\" data-arg=\"gauge-variable\"";
                    } else {
                        html += "set-gauge-variable\" data-arg=\"";
                        html += escapeRml(variable.key);
                        html += "\"";
                    }
                    html += ">";
                    html += escapeRml(variable.key);
                    html += "</div>";
                }
                if (!any) {
                    html += "<div class=\"drop-entry disabled\">No Number variables</div>";
                }
                html += "</div>";
            }
        }
        html += field("Max Value", "commit-gauge-max", num(gaugeComp->maxValue), playing);
        html += field("Width", "commit-gauge-width", num(gaugeComp->width), playing);
        html += field("Height", "commit-gauge-height", num(gaugeComp->height), playing);
        html += field("Fill", "commit-gauge-fill", formatColorHexRgb(gaugeComp->fillColor), playing);
        html += field("Background", "commit-gauge-bg", formatColorHexRgb(gaugeComp->bgColor), playing);
        const bool dirOpen = openDropdownId_ == "gauge-direction";
        html += dropdownTrigger("Direction", "gauge-direction", gaugeComp->direction,
                                dirOpen, playing);
        if (dirOpen && !playing) {
            html += "<div class=\"drop-list\">";
            const bool horiz = gaugeComp->direction == "horizontal";
            const auto directionEntry = [&](bool current, const char* value, const char* label) {
                const std::size_t navIndex = dropdownNav_.push(current
                    ? DropdownNavEntry{"toggle-inspector-dropdown", "gauge-direction", "", true}
                    : DropdownNavEntry{"set-gauge-direction", value, "", false});
                html += "<div class=\"drop-entry";
                if (current) html += " selected";
                if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                html += "\" data-action=\"";
                html += current ? "toggle-inspector-dropdown\" data-arg=\"gauge-direction\""
                                : ("set-gauge-direction\" data-arg=\"" + std::string(value) + "\"");
                html += ">";
                html += label;
                html += "</div>";
            };
            directionEntry(horiz, "horizontal", "horizontal");
            directionEntry(!horiz, "vertical", "vertical");
            html += "</div>";
        }
        html += field("Offset X", "commit-gauge-offset-x", num(gaugeComp->offsetX), playing);
        html += field("Offset Y", "commit-gauge-offset-y", num(gaugeComp->offsetY), playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">HUD</span>"
                "<div class=\"prop-toggle";
        if (gaugeComp->screenSpace) html += " on";
        if (playing) html += " disabled";
        html += "\" data-action=\"toggle-gauge-screen-space\"></div></div>";
    }

    // -- Object Variables (ADR-0031) -----------------------------------------
    // One section, not two: the definition belongs to the Object Type and the
    // override to this instance, so the same row carries both and the
    // relationship between them stays on screen. A separate "Overrides"
    // section would turn one variable into two apparently independent things.
    if (type) {
        html += header("object-variables", isSectionCollapsed("object-variables"),
                       "" UI_ICON_OBJECT_VARIABLES "", "Object Variables", "", "", "", playing);
        html += typeOwnedLockNote;
        // Said once, in prose, instead of twice per variable in labels too long
        // for the column. With the ownership pills gone this is what carries it.
        if (!type->localVariables.empty()) {
            html += "<div class=\"type-owned-note\">Defaults belong to the Object Type and are "
                    "shared by every instance. An override applies to this one only.</div>";
        } else {
            html += "<p class=\"inspector-empty\">No object variables yet</p>";
        }
        const auto activeDraft = [&](std::string_view action,
                                     const std::string& key) -> const ObjectVariableDraft* {
            if (!objectVariableDraft_
                || objectVariableDraft_->action != action
                || objectVariableDraft_->key != key) {
                return nullptr;
            }
            return &*objectVariableDraft_;
        };
        const auto appendDraftError = [&](const ObjectVariableDraft* draft) {
            if (!draft || draft->error.empty()) return;
            html += "<div class=\"object-variable-draft-error\">"
                  + escapeRml(draft->error) + "</div>";
        };
        for (const GameVariableDefinition& variable : type->localVariables) {
            const std::string safeKey = escapeRml(variable.key);
            const std::string typeDropdownId = "object-variable-type|" + variable.key;
            const bool typeOpen = openDropdownId_ == typeDropdownId && !playing;
            const ObjectVariableDraft* keyDraft =
                activeDraft("commit-object-variable-key", variable.key);

            // Name, type and delete are one line: they are the variable's
            // identity, and three stacked rows made a two-variable type read
            // as a wall.
            html += "<div class=\"object-variable\"><div class=\"object-variable-head\">"
                    "<input type=\"text\" class=\"prop-input object-variable-name";
            if (keyDraft && !keyDraft->error.empty())
                html += " object-variable-draft-invalid";
            html += "\"";
            if (keyDraft) html += " id=\"object-variable-draft-input\"";
            html += " data-action=\"commit-object-variable-key\" data-arg=\""
                  + safeKey + "\" value=\""
                  + escapeRml(keyDraft ? keyDraft->value : variable.key) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "/><div class=\"object-variable-type\">";
            html += dropdownTriggerMarkup(variableTypeLabel(variable.type),
                                          "toggle-inspector-dropdown", typeDropdownId,
                                          typeOpen, playing);
            html += "</div><span class=\"comp-remove";
            if (playing) html += " disabled";
            html += "\" data-action=\"remove-object-variable\" data-arg=\"" + safeKey
                  + "\" title=\"Delete object variable\">" + icon("" UI_ICON_DELETE "")
                  + "</span></div>";
            if (typeOpen) {
                html += "<div class=\"drop-list\">";
                for (const GameVariableDefinition::Type option :
                     {GameVariableDefinition::Type::Number,
                      GameVariableDefinition::Type::Boolean,
                      GameVariableDefinition::Type::String}) {
                    const bool isCurrent = option == variable.type;
                    // Key and type in one arg: a key cannot contain '|'
                    // (project-global-variables-format's charset), so the split
                    // is unambiguous.
                    const std::string typeArg =
                        std::string(variableTypeToken(option)) + "|" + safeKey;
                    const std::size_t navIndex = dropdownNav_.push(
                        {"set-object-variable-type", typeArg, "", isCurrent});
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
                    if (dropdownNav_.isHighlighted(navIndex)) html += " highlighted";
                    html += "\" data-action=\"set-object-variable-type\" data-arg=\""
                          + typeArg + "\">" + variableTypeLabel(option) + "</div>";
                }
                html += "</div>";
            }
            appendDraftError(keyDraft);

            const ObjectVariableDraft* defaultDraft =
                activeDraft("commit-object-variable-default", variable.key);
            html += variableValueRow(
                "Value", variable.type, variable.initialValue,
                "commit-object-variable-default", "toggle-object-variable-default",
                variable.key, playing, {},
                defaultDraft ? &defaultDraft->value : nullptr,
                defaultDraft && !defaultDraft->error.empty());
            appendDraftError(defaultDraft);

            // An instance either has its own value or it does not, and the two
            // states get different controls. A blank box that might be either
            // is the one thing this row must never be.
            const auto overrideIt = inst->localVariableOverrides.find(variable.key);
            if (overrideIt != inst->localVariableOverrides.end()) {
                const ObjectVariableDraft* overrideDraft =
                    activeDraft("commit-instance-variable-override", variable.key);
                // Reset belongs beside the value it undoes, not on its own row.
                html += variableValueRow(
                    "This instance", variable.type, overrideIt->second,
                    "commit-instance-variable-override", "toggle-instance-variable-override",
                    variable.key, playing,
                    /*trailing=*/std::string("<button class=\"panel-btn object-variable-reset")
                        + (playing ? " disabled" : "")
                        + "\" data-action=\"reset-instance-variable-override\" data-arg=\""
                        + safeKey + "\" title=\"Go back to the shared value\">Reset</button>",
                    overrideDraft ? &overrideDraft->value : nullptr,
                    overrideDraft && !overrideDraft->error.empty());
                appendDraftError(overrideDraft);
            } else {
                html += "<div class=\"prop-row\"><span class=\"prop-label\">This instance</span>"
                        "<button class=\"panel-btn";
                if (playing) html += " disabled";
                html += "\" data-action=\"override-instance-variable\" data-arg=\"" + safeKey
                      + "\" title=\"Give this instance a value of its own\">"
                        "Uses the shared value</button></div>";
            }

            const ObjectVariableDraft* descriptionDraft =
                activeDraft("commit-object-variable-description", variable.key);
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Description</span>"
                    "<input type=\"text\" class=\"prop-input";
            if (descriptionDraft && !descriptionDraft->error.empty())
                html += " object-variable-draft-invalid";
            html += "\"";
            if (descriptionDraft) html += " id=\"object-variable-draft-input\"";
            html += " data-action=\"commit-object-variable-description\" data-arg=\""
                  + safeKey + "\" value=\""
                  + escapeRml(descriptionDraft ? descriptionDraft->value
                                               : variable.description) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "/></div>";
            appendDraftError(descriptionDraft);
            html += "</div>";
        }
        html += "<div class=\"prop-row\"><button class=\"panel-btn";
        if (playing) html += " disabled";
        html += "\" data-action=\"add-object-variable\">+ Add Object Variable</button></div>";
    }

    html += kSectionsEnd;

    // -- Add Component menu (only addable components; one movement driver) -----
    const bool hasDriver = type
        && (type->linearMover || type->topDownController || type->platformerController);
    struct Addable {
        const char* label;
        const char* action;
        bool show;
        bool enabled;
        const char* disabledReason;
    };
    const Addable addable[] = {
        // Sprite capability is Object-Type-owned, like the gameplay components.
        {"Sprite", "add-sprite-renderer",
            type && !type->spritePresentation && !type->spriteRenderer, true, ""},
        {"Box Collider 2D", "add-box-collider", type && !collider, true, ""},
        // The three movement drivers are mutually exclusive: offer none once one exists.
        {"Top Down Controller", "add-top-down", type && !hasDriver, true, ""},
        {"Platformer Controller", "add-platformer", type && !hasDriver, true, ""},
        {"Linear Mover", "add-linear-mover", type && !hasDriver, true, ""},
        {"Auto Destroy", "add-auto-destroy", type && !autoDestroy, true, ""},
        {"Text", "add-text", type && !textComp, true, ""},
        {"Gauge", "add-gauge", type && !gaugeComp, true, ""},
        {"Camera Target", "add-camera-target",
            !instanceLocked && !inst->cameraTarget.has_value(), true, ""},
        // Instance-level like Sprite Renderer; needs at least one tileset to
        // reference (auto-assigns the first one - the tileset picker above
        // lets it be changed afterward).
        {"Tilemap", "add-tilemap-component",
            !instanceLocked && !inst->tilemap.has_value()
                && !coordinator.document().data().tilesets.empty(), true, ""},
    };
    bool anyAddable = false;
    for (const Addable& a : addable) anyAddable = anyAddable || a.show;

    if (anyAddable) {
        std::string trigger = "add-component-btn";
        if (playing) trigger += " disabled";
        if (addMenuOpen_ && !playing) trigger += " open";
        html += "<div class=\"add-component\">";
        html += "<div class=\"" + trigger + "\" data-action=\"toggle-add-component\">"
                "<span class=\"icon\">" UI_ICON_ADD "</span>Add Logic Component</div>";
        if (addMenuOpen_ && !playing) {
            html += "<div class=\"add-list\">";
            for (const Addable& a : addable) {
                if (!a.show) continue;
                html += "<div class=\"add-entry";
                if (!a.enabled) html += " disabled";
                html += "\"";
                if (a.enabled) {
                    html += " data-action=\"";
                    html += a.action;
                    html += "\"";
                } else {
                    html += " title=\"";
                    html += escapeRml(a.disabledReason);
                    html += "\"";
                }
                html += ">";
                html += a.label;
                html += "</div>";
            }
            html += "</div>";
        }
        html += "</div>";
    }

    objectVariableDraftRebuilding_ = objectVariableDraft_.has_value();
    body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
    objectVariableDraftRebuilding_ = false;
    focusObjectVariableDraft(document);
}

void InspectorPanel::reconcileOpenDropdownForScene() {
    if (!openDropdownId_.empty() && !isSceneDropdown(openDropdownId_)) {
        openDropdownId_.clear();
        dropdownNav_.resetSession();
    }
}

void InspectorPanel::reconcileOpenDropdownForEntity() {
    if (!openDropdownId_.empty() && !isEntityDropdown(openDropdownId_)) {
        openDropdownId_.clear();
        dropdownNav_.resetSession();
    }
}

void InspectorPanel::reconcileBackgroundDraft(const EditorCoordinator& coordinator,
                                             bool sceneMode) {
    if (!backgroundDraft_) return;

    const SceneDef* scene = sceneMode
        ? coordinator.document().findScene(coordinator.state().activeSceneId)
        : nullptr;
    const bool invalid = !sceneMode
        || coordinator.isPlaying()
        || !scene
        || scene->id != backgroundDraft_->sceneId;
    if (invalid) {
        backgroundDraft_.reset();
        return;
    }

    if (!sameSceneBackgroundColor(scene->backgroundColor, backgroundDraft_->original)
        && !sameSceneBackgroundColor(scene->backgroundColor, backgroundDraft_->preview)) {
        backgroundDraft_.reset();
    }
}

void InspectorPanel::applyBackgroundOpacityPreview(Rml::ElementDocument* document,
                                                   const Vec4& color) {
    if (!document) return;
    const std::string percent = formatOpacityPercent(color.a);
    if (Rml::Element* slider = document->GetElementById("scene-bg-opacity-slider")) {
        slider->SetAttribute("value", percent);
    }
    if (Rml::Element* value = document->GetElementById("scene-bg-opacity-value")) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(value)) {
            control->SetValue(percent);
        } else {
            value->SetAttribute("value", percent);
        }
    }
}

void InspectorPanel::beginBackgroundOpacityDrag(const EditorCoordinator& coordinator) {
    if (coordinator.isPlaying()) return;
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!scene) return;
    backgroundDraft_ = SceneBackgroundOpacityDraft{
        sceneId, scene->backgroundColor, scene->backgroundColor, /*dragActive=*/true};
}

void InspectorPanel::previewBackgroundOpacity(Rml::ElementDocument* document,
                                              const EditorCoordinator& coordinator,
                                              float opacityPercent) {
    if (!backgroundDraft_ || !backgroundDraft_->dragActive) return;
    if (coordinator.state().activeSceneId != backgroundDraft_->sceneId) {
        backgroundDraft_.reset();
        return;
    }
    const float alpha = std::clamp(opacityPercent / 100.f, 0.f, 1.f);
    backgroundDraft_->preview = backgroundDraft_->original;
    backgroundDraft_->preview.a = alpha;
    applyBackgroundOpacityPreview(document, backgroundDraft_->preview);
}

void InspectorPanel::commitBackgroundOpacityDrag(Rml::ElementDocument* document,
                                                 EditorCoordinator& coordinator) {
    if (!backgroundDraft_) return;
    const SceneBackgroundOpacityDraft draft = *backgroundDraft_;
    backgroundDraft_.reset();
    if (!draft.dragActive) return;
    if (sameSceneBackgroundColor(draft.preview, draft.original)) {
        applyBackgroundOpacityPreview(document, draft.original);
        return;
    }
    coordinator.execute(SetSceneBackgroundCommand{draft.sceneId, draft.preview});
}

void InspectorPanel::cancelBackgroundOpacityDrag(Rml::ElementDocument* document,
                                                 const EditorCoordinator& coordinator) {
    if (!backgroundDraft_) return;
    const Vec4 restore = backgroundDraft_->original;
    backgroundDraft_.reset();
    applyBackgroundOpacityPreview(document, restore);
    (void)coordinator;
}

void InspectorPanel::cancelBackgroundOpacityDraft() {
    backgroundDraft_.reset();
}

bool InspectorPanel::backgroundOpacityDragActive() const {
    return backgroundDraft_ && backgroundDraft_->dragActive;
}

void InspectorPanel::beginObjectVariableDraft(const EditorCoordinator& coordinator,
                                              const std::string& action,
                                              const std::string& arg,
                                              const std::string& renderedValue) {
    if (!isObjectVariableTextAction(action) || coordinator.isPlaying()) {
        objectVariableDraft_.reset();
        return;
    }

    const SceneId& sceneId = coordinator.state().activeSceneId;
    const EntityId entityId = coordinator.selection().primaryEntity;
    const SceneInstanceDef* instance =
        coordinator.document().findInstanceInScene(sceneId, entityId);
    const EntityDef* type = instance
        ? coordinator.document().findObjectType(instance->objectTypeId) : nullptr;
    const GameVariableDefinition* definition =
        type ? findObjectVariable(*type, arg) : nullptr;
    const bool overrideExists = instance
        && instance->localVariableOverrides.find(arg)
            != instance->localVariableOverrides.end();
    if (!instance || !type || !definition
        || (action == "commit-instance-variable-override" && !overrideExists)) {
        objectVariableDraft_.reset();
        return;
    }

    if (objectVariableDraft_
        && objectVariableDraft_->sceneId == sceneId
        && objectVariableDraft_->entityId == entityId
        && objectVariableDraft_->objectTypeId == instance->objectTypeId
        && objectVariableDraft_->sourceRevision == coordinator.document().revision()
        && objectVariableDraft_->action == action
        && objectVariableDraft_->key == arg) {
        // A routine refresh rebuilt and re-focused this same field. Its rendered
        // value already came from the draft; do not erase an inline error or
        // replace the text with a document read-back.
        return;
    }

    objectVariableDraft_ = ObjectVariableDraft{
        sceneId,
        entityId,
        instance->objectTypeId,
        coordinator.document().revision(),
        action,
        arg,
        renderedValue,
        {}};
}

void InspectorPanel::updateObjectVariableDraft(const std::string& action,
                                               const std::string& arg,
                                               const std::string& value) {
    if (!objectVariableDraft_
        || objectVariableDraft_->action != action
        || objectVariableDraft_->key != arg) {
        return;
    }
    objectVariableDraft_->value = value;
    objectVariableDraft_->error.clear();
}

std::optional<InspectorPanel::ObjectVariableDraftCommit>
InspectorPanel::objectVariableDraftCommit() const {
    if (!objectVariableDraft_) return std::nullopt;
    return ObjectVariableDraftCommit{
        objectVariableDraft_->action,
        objectVariableDraft_->key,
        objectVariableDraft_->value};
}

void InspectorPanel::setObjectVariableDraftError(std::string error) {
    if (objectVariableDraft_) objectVariableDraft_->error = std::move(error);
}

void InspectorPanel::discardObjectVariableDraft() {
    objectVariableDraft_.reset();
}

void InspectorPanel::focusObjectVariableDraft(Rml::ElementDocument* document) {
    if (!document || !objectVariableDraft_) return;
    if (Rml::Element* input = document->GetElementById("object-variable-draft-input"))
        input->Focus(true);
}

void InspectorPanel::reconcileObjectVariableDraft(
    const EditorCoordinator& coordinator) {
    if (!objectVariableDraft_) return;
    const ObjectVariableDraft& draft = *objectVariableDraft_;
    const SceneInstanceDef* instance =
        coordinator.document().findInstanceInScene(draft.sceneId, draft.entityId);
    const EntityDef* type = instance
        ? coordinator.document().findObjectType(instance->objectTypeId) : nullptr;
    const GameVariableDefinition* definition =
        type ? findObjectVariable(*type, draft.key) : nullptr;
    const bool invalid =
        coordinator.isPlaying()
        || coordinator.state().activeSceneId != draft.sceneId
        || coordinator.selection().primaryEntity != draft.entityId
        || coordinator.document().revision() != draft.sourceRevision
        || !instance
        || instance->objectTypeId != draft.objectTypeId
        || !definition
        || (draft.action == "commit-instance-variable-override"
            && instance->localVariableOverrides.find(draft.key)
                == instance->localVariableOverrides.end());
    if (invalid) objectVariableDraft_.reset();
}

} // namespace ArtCade::EditorNative
