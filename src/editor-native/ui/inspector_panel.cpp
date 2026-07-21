#include "editor-native/ui/inspector_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/ui_markup.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

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
                   const char* removeAction, bool playing) {
    std::string h = "[[inspector-section:" + std::string(sectionId) + "]]";
    h += "<div class=\"comp-header\"><span id=\"inspector-section-";
    h += sectionId;
    h += "-toggle\" class=\"comp-title comp-toggle\""
         " data-action=\"toggle-inspector-section\" data-arg=\"";
    h += sectionId;
    h += "\"><span class=\"comp-caret\">";
    h += collapsed ? "&#xeb5f;" : "&#xeb5d;";
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
    if (removeAction && *removeAction) {
        h += "<span class=\"comp-remove";
        if (playing) h += " disabled";
        h += "\" data-action=\"";
        h += removeAction;
        h += "\">";
        h += icon("&#xeb41;");
        h += "</span>";
    }
    h += "</div>";
    h += "[[inspector-body:" + std::string(sectionId) + "]]";
    return h;
}

constexpr std::string_view kSectionsEnd = "[[inspector-sections-end]]";

bool knownSection(std::string_view id) {
    static constexpr std::string_view ids[] = {
        "project", "general", "world-bounds", "layers", "diagnostics",
        "identity", "transform", "sprite-renderer", "sprite-animator",
        "tilemap", "scripts", "box-collider", "linear-mover", "top-down-controller",
        "platformer-controller",
    };
    for (std::string_view known : ids) if (known == id) return true;
    return false;
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
        "<span class=\"icon\">&#xea06;</span><span>";
    html += label;
    html += "</span></div>";
    html += "<button class=\"";
    html += playing ? "panel-btn disabled" : "panel-btn";
    html += "\" data-action=\"bring-entity-into-scene\">"
            "<span class=\"icon\">&#xea5f;</span>Bring Into Scene</button>";
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
                            const std::string& valueText, bool open, bool disabled) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span>";
    row += dropdownTriggerMarkup(valueText, "toggle-inspector-dropdown", dropdownId,
                                 open, disabled);
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
    refresh(document, coordinator);
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
    if (!document || coordinator.selection().primaryEntity != entity) return;
    if (!coordinator.document().findInstanceInScene(coordinator.state().activeSceneId, entity))
        return;

    const auto setValue = [&](const char* id, const std::string& value) {
        Rml::Element* element = document->GetElementById(id);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element)) {
            control->SetValue(value);
        }
    };
    setValue("inspector-pos-x", num(position.x));
    setValue("inspector-pos-y", num(position.y));
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

    const EntityId selected = coordinator.selection().primaryEntity;
    const SceneInstanceDef* inst =
        coordinator.document().findInstanceInScene(coordinator.state().activeSceneId,
                                                   selected);

    // No entity selected: show the Scene Inspector for the active scene (same
    // panel, two modes — the authority is the existing activeSceneId, no new
    // selectedSceneId). With no active scene at all, fall back to the empty hint.
    if (!inst) {
        lastEntity_ = INVALID_ENTITY;
        addMenuOpen_ = false;
        openDropdownId_.clear();   // value dropdowns exist only in entity mode
        reconcileSceneLayerRenameUiState(coordinator);
        const bool playing = coordinator.isPlaying();
        const SceneId& activeScene = coordinator.state().activeSceneId;
        const SceneDef* scene = coordinator.document().findScene(activeScene);
        const std::string projectName = coordinator.document().data().projectName.empty()
            ? std::string("Untitled")
            : coordinator.document().data().projectName;
        std::string html;
        // Folder glyph, not the plus: "+" is the add-action icon everywhere else
        // (+ Create, + Import, + Add Layer), so "+ PROJECT" read as a button.
        html += header("project", isSectionCollapsed("project"),
                       "&#xeaad;", "Project", "", "", "", playing);
        html += field("Name", "commit-project-name", projectName, playing,
                      "inspector-project-name");
        if (!scene) {
            layerRename_.reset();
            html += "<p class=\"inspector-empty\">No scene open</p>";
            body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
            return;
        }
        if (playing) layerRename_.reset();
        const bool isStart = coordinator.document().startSceneId() == activeScene;
        const std::string btn = playing ? "panel-btn disabled" : "panel-btn";

        // -- GENERAL -----------------------------------------------------------
        html += header("general", isSectionCollapsed("general"),
                       "&#xeb34;", "General", "", "", "", playing);
        html += field("Name", "commit-scene-name", scene->name, playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">ID</span>"
                "<span class=\"prop-readonly\">" + escapeRml(scene->id) + "</span></div>";
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Start</span>";
        if (isStart) {
            html += "<span class=\"prop-readonly\"><span class=\"icon\">&#xeb2e;</span> "
                    "Start scene</span>";
        } else {
            html += "<button class=\"" + btn + "\" data-action=\"set-start-scene\">"
                    "Set as Start</button>";
        }
        html += "</div>";
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Entities</span>"
                "<span class=\"prop-readonly\">"
              + std::to_string(scene->instances.size()) + "</span></div>";
        html += field("Background R", "commit-scene-background-r",
                      num(scene->backgroundColor.r), playing);
        html += field("Background G", "commit-scene-background-g",
                      num(scene->backgroundColor.g), playing);
        html += field("Background B", "commit-scene-background-b",
                      num(scene->backgroundColor.b), playing);
        html += field("Background A", "commit-scene-background-a",
                      num(scene->backgroundColor.a), playing);

        // -- WORLD BOUNDS (world units; resizing never moves instances) ---------
        html += header("world-bounds", isSectionCollapsed("world-bounds"),
                       "&#xf22f;", "World Bounds", "", "", "", playing);
        html += fieldWithUnit("Width", "commit-scene-width", num(scene->worldSize.x), "wu", playing);
        html += fieldWithUnit("Height", "commit-scene-height", num(scene->worldSize.y), "wu", playing);
        // Fit View lives in the toolbar's view group (audit 7.4): a camera action
        // belongs to the Scene View, not to scene properties.

        // -- LAYERS (per-scene render order; top row = foreground) -------------
        html += header("layers", isSectionCollapsed("layers"),
                       "&#xee9e;", "Layers", "", "", "", playing);
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
                  + escapeRml(layer.id) + "\"><span class=\"icon\">&#xea9a;</span></span>";
            html += "<span class=\"layer-lock";
            if (layer.locked) html += " locked";
            html += "\" data-action=\"toggle-layer-locked\" data-arg=\"" + escapeRml(layer.id)
                  + "\" title=\"" + (layer.locked ? "Unlock layer" : "Lock layer")
                  + "\"><span class=\"icon\">"
                  + (layer.locked ? "&#xeae2;" : "&#xeae1;") + "</span></span>";
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
        if (!playing)
            html += "<button class=\"" + btn + " layer-add-btn\" data-action=\"add-layer\">"
                    "<span class=\"icon\">&#xeb0b;</span>Add Layer</button>";

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
                       "&#xea06;", "Diagnostics", "", "", "", playing);
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
        if (layerRename_) focusSceneLayerRenameInput(document);
        return;
    }

    const bool playing = coordinator.isPlaying();
    layerRename_.reset();
    // The Add menu and value dropdowns are transient: a new selection or
    // entering Play closes them.
    if (selected != lastEntity_) {
        addMenuOpen_ = false;
        openDropdownId_.clear();
        lastEntity_ = selected;
    }
    if (playing) { addMenuOpen_ = false; openDropdownId_.clear(); }

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
        html += "<div class=\"outside-warning panel-top\"><span class=\"icon\">&#xeae2;</span>"
                "<span>This entity belongs to a locked layer.</span></div>";
    }

    // -- Identity (not a component) -------------------------------------------
    html += header("identity", isSectionCollapsed("identity"),
                   "&#xeb34;", "Identity", "", "", "", playing);
    html += field("Name", "commit-name", inst->instanceName, instanceDisabled);
    // Renaming this field renames the shared ObjectTypeDef (every instance of
    // this type reflects the new name), not just this instance - unlike "Name"
    // above. Disabled when objectTypeId doesn't resolve to a real catalog entry
    // (legacy/dangling data), since there is nothing to rename in that case.
    const std::string typeLabel = type ? type->name : inst->objectTypeId;
    html += field("Type", "commit-type-name", typeLabel, playing || !type);
    html += "<div class=\"prop-row\"><span class=\"prop-label\">Entity Visible</span>"
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
                html += "<div class=\"drop-entry";
                if (isCurrent) html += " selected";
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
                if (targetLocked) html += "<span class=\"icon\">&#xeae2;</span> ";
                else if (targetHidden) html += "<span class=\"icon\">&#xea9a;</span> ";
                html += escapeRml(l.name) + "</div>";
            }
            html += "</div>";
        }
    }

    // -- Transform (instance-owned; structural, no remove) --------------------
    html += header("transform", isSectionCollapsed("transform"),
                   "&#xf22f;", "Transform", "INSTANCE", "", "", instanceDisabled);
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
    if (type && type->spriteRenderer && presentation.renderer) {
        const SpriteRendererComponent& sr = *presentation.renderer;
        html += header("sprite-renderer", isSectionCollapsed("sprite-renderer"),
                       "&#xeb0a;", "Sprite Renderer", "OBJECT TYPE", "",
                       "remove-sprite-renderer", playing);
        html += typeOwnedLockNote;
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Visible</span>"
                "<button class=\"" + instanceBtn + "\" data-action=\"toggle-sprite-visible\">";
        html += sr.visible ? "On" : "Off";
        html += "</button>";
        if (inst->spriteRendererOverride && inst->spriteRendererOverride->visible) {
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
            html += "<div class=\"drop-entry";
            if (noneSelected) html += " selected";
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
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
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
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
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
                  + "\"><span class=\"icon\">&#xeb0a;</span>Open Animation Editor</button>";
        }
        if (inst->spriteRendererOverride) {
            html += "<button class=\"" + instanceBtn
                  + "\" data-action=\"reset-sprite-override\">Reset to Object Type</button>";
        }
        if (type->spriteAnimator && presentation.animator) {
            const SpriteAnimatorComponent& animator = *presentation.animator;
            const ResolvedSpriteAnimator resolvedAnimator =
                resolveSpriteAnimator(*type, *inst);
            const bool inherited = resolvedAnimator.origin == ComponentOrigin::EntityDefinition
                && !resolvedAnimator.explicitPlaybackSpeed
                && !resolvedAnimator.explicitAutoPlay
                && !resolvedAnimator.explicitDefaultClip
                && !resolvedAnimator.explicitAnimationAsset;
            std::string animatorBadge = "OBJECT TYPE";
            if (resolvedAnimator.origin == ComponentOrigin::InstanceOverride) {
                animatorBadge = "INSTANCE OVERRIDE";
            } else if (inherited) {
                animatorBadge = "INHERITED";
            }
            html += header("sprite-animator", isSectionCollapsed("sprite-animator"),
                           "&#xeb0a;", "Sprite Animator", animatorBadge.c_str(), "",
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
                        html += "<div class=\"drop-entry";
                        if (isCurrent) html += " selected";
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
                       "&#xf22f;", "Tilemap", "INSTANCE", "", "remove-tilemap-component", instanceDisabled);
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
                    html += "<div class=\"drop-entry";
                    if (isCurrent) html += " selected";
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
                       "&#xf1b7;", "Scripts", "OBJECT TYPE", "", "", playing);
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
                    html += "<div class=\"drop-entry\" data-action=\"attach-script\" data-arg=\""
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
                       "&#xeca9;", "Box Collider 2D", "TYPE", "", "remove-box-collider", playing);
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
                       "&#xf22f;", "Linear Mover", "TYPE", "", "remove-linear-mover", playing);
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
                       "&#xec8e;", "Top Down Controller", "TYPE", "", "remove-top-down", playing);
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
                       "&#xec8e;", "Platformer Controller", "TYPE", "", "remove-platformer", playing);
        html += typeOwnedLockNote;
        html += field("Move Speed", "commit-platformer-move", num(platformer->maxSpeed), playing);
        html += field("Jump Speed", "commit-platformer-jump", num(platformer->jumpForce), playing);
        html += field("Gravity", "commit-platformer-gravity", num(platformer->customGravity), playing);
    }

    html += kSectionsEnd;

    // -- Add Component menu (only addable components; one movement driver) -----
    const bool hasDriver = type
        && (type->linearMover || type->topDownController || type->platformerController);
    bool hasAnimationWithClip = false;
    for (const SpriteAnimationAssetDef& asset :
         coordinator.document().data().spriteAnimationAssets) {
        if (!asset.clips.empty()) {
            hasAnimationWithClip = true;
            break;
        }
    }
    const char* animatorPreflight = type && !type->spriteRenderer
        ? "Add Sprite Renderer first"
        : "Create a Sprite Animation asset with at least one clip first";
    struct Addable {
        const char* label;
        const char* action;
        bool show;
        bool enabled;
        const char* disabledReason;
    };
    const Addable addable[] = {
        // Sprite capability is Object-Type-owned, like the gameplay components.
        {"Sprite Renderer", "add-sprite-renderer",
            type && !type->spriteRenderer, true, ""},
        {"Sprite Animator", "add-sprite-animator",
            type && !type->spriteAnimator,
            type && type->spriteRenderer && hasAnimationWithClip,
            animatorPreflight},
        {"Box Collider 2D", "add-box-collider", type && !collider, true, ""},
        // The three movement drivers are mutually exclusive: offer none once one exists.
        {"Top Down Controller", "add-top-down", type && !hasDriver, true, ""},
        {"Platformer Controller", "add-platformer", type && !hasDriver, true, ""},
        {"Linear Mover", "add-linear-mover", type && !hasDriver, true, ""},
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
                "<span class=\"icon\">&#xeb0b;</span>Add Component</div>";
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

    body->SetInnerRML(finalizeSectionMarkup(html, collapsedSections_));
}

} // namespace ArtCade::EditorNative
