#include "editor-native/ui/inspector_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <cstdio>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", std::trunc(v));
    return buf;
}

// Tabler icon glyph span (PUA codepoint passed as an RML char reference).
std::string icon(const char* cp) {
    return std::string("<span class=\"icon\">") + cp + "</span>";
}

// Display label of a Sprite Animation asset: its authored name when the id
// still resolves, with the historical ".anim" suffix stripped either way
// (assetDisplayName). Ids in data-args stay untouched.
std::string animationAssetLabel(const EditorCoordinator& coordinator, const AssetId& id) {
    const SpriteAnimationAssetDef* asset = coordinator.document().findSpriteAnimationAsset(id);
    return assetDisplayName(asset ? asset->name : std::string(), id);
}

// SpriteAnimatorComponent::initialClipId is a stable id, not a display value
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

// True once some clip in some Sprite Animation asset already sources this
// image - i.e. the raw sheet has been turned into an animation, so offering
// it again as a plain static Source is clutter, not a real choice.
bool imageHasDerivedAnimation(const EditorCoordinator& coordinator, const AssetId& imageId) {
    for (const SpriteAnimationAssetDef& asset : coordinator.document().data().spriteAnimationAssets) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.imageId == imageId) return true;
        }
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
                          const char* unit, bool disabled) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span><input type=\"text\" class=\"prop-input\" data-action=\"";
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
std::string header(const char* iconCp, const char* title, const char* badge,
                   const char* badgeClass, const char* removeAction, bool playing) {
    std::string h = "<div class=\"comp-header\"><span class=\"comp-title\">";
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
    return h;
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
// region). Raw text centred via line-height, caret pinned right (see the
// RmlUi flex/raw-text note in controls.rcss).
std::string dropdownTrigger(const char* label, const char* dropdownId,
                            const std::string& valueText, bool open, bool disabled) {
    std::string row = "<div class=\"prop-row\"><span class=\"prop-label\">";
    row += label;
    row += "</span><div class=\"drop-trigger";
    if (open) row += " open";
    if (disabled) row += " disabled";
    row += "\"";
    if (!disabled) {
        row += " data-action=\"toggle-inspector-dropdown\" data-arg=\"";
        row += dropdownId;
        row += "\"";
    }
    row += ">" + escapeRml(valueText)
         + "<span class=\"drop-caret\">&#xeb5d;</span></div></div>";
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
        html += header("&#xeaad;", "Project", "", "", "", playing);
        html += field("Name", "commit-project-name", projectName, playing);
        if (!scene) {
            layerRename_.reset();
            html += "<p class=\"inspector-empty\">No scene open</p>";
            body->SetInnerRML(html);
            return;
        }
        if (playing) layerRename_.reset();
        const bool isStart = coordinator.document().startSceneId() == activeScene;
        const std::string btn = playing ? "panel-btn disabled" : "panel-btn";

        // -- GENERAL -----------------------------------------------------------
        html += header("&#xeb34;", "General", "", "", "", playing);
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

        // -- WORLD BOUNDS (world units; resizing never moves instances) ---------
        html += header("&#xf22f;", "World Bounds", "", "", "", playing);
        html += fieldWithUnit("Width", "commit-scene-width", num(scene->worldSize.x), "wu", playing);
        html += fieldWithUnit("Height", "commit-scene-height", num(scene->worldSize.y), "wu", playing);
        // Fit View lives in the toolbar's view group (audit 7.4): a camera action
        // belongs to the Scene View, not to scene properties.

        // -- LAYERS (per-scene render order; top row = foreground) -------------
        html += header("&#xee9e;", "Layers", "", "", "", playing);
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
        html += header("&#xea06;", "Diagnostics", "", "", "", playing);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Outside bounds</span>"
                "<span class=\"prop-readonly";
        if (outside > 0) html += " warn";
        html += "\">" + std::to_string(outside) + "</span></div>";

        body->SetInnerRML(html);
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
          "&mdash; not protected by this layer's lock.</div>"
        : "";

    const auto& types = coordinator.document().data().objectTypes;
    const auto typeIt = types.find(inst->objectTypeId);
    const EntityDef* type = (typeIt != types.end()) ? &typeIt->second : nullptr;

    std::string html;

    if (instanceLocked) {
        html += "<div class=\"outside-warning panel-top\"><span class=\"icon\">&#xeae2;</span>"
                "<span>This entity belongs to a locked layer.</span></div>";
    }

    // -- Identity (not a component) -------------------------------------------
    html += header("&#xeb34;", "Identity", "", "", "", playing);
    html += field("Name", "commit-name", inst->instanceName, instanceDisabled);
    // Renaming this field renames the shared ObjectTypeDef (every instance of
    // this type reflects the new name), not just this instance - unlike "Name"
    // above. Disabled when objectTypeId doesn't resolve to a real catalog entry
    // (legacy/dangling data), since there is nothing to rename in that case.
    const std::string typeLabel = type ? type->name : inst->objectTypeId;
    html += field("Type", "commit-type-name", typeLabel, playing || !type);

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
    html += header("&#xf22f;", "Transform", "INSTANCE", "", "", instanceDisabled);
    html += field("Position X", "commit-pos-x", num(inst->transform.position.x), instanceDisabled,
                  "inspector-pos-x");
    html += field("Position Y", "commit-pos-y", num(inst->transform.position.y), instanceDisabled,
                  "inspector-pos-y");
    const SceneFrameSnapshot frame = collectSceneFrameSnapshot(
        coordinator.document(), coordinator.state().activeSceneId, selected);
    if (const std::optional<WorldRect> bounds = editorBoundsForEntity(frame, selected)) {
        html += outsideSceneWarning(classifySceneContainment(*bounds, frame.worldSize), instanceDisabled);
    }

    // -- Sprite Renderer (instance override, or inherited from the type) ------
    const SpriteRenderView resolved =
        resolveSpriteRenderer(coordinator.document(), coordinator.state().activeSceneId, selected);
    const bool spriteOverride = inst->spriteRenderer.has_value();
    const bool spriteInherited = !spriteOverride
                              && resolved.origin == ComponentOrigin::EntityDefinition;
    if (spriteOverride) {
        const SpriteRendererComponent& sr = *inst->spriteRenderer;
        html += header("&#xeb0a;", "Sprite Renderer", "OVERRIDE", "override",
                       "remove-sprite-renderer", instanceDisabled);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Visible</span>"
                "<button class=\"" + instanceBtn + "\" data-action=\"toggle-sprite-visible\">";
        html += sr.visible ? "On" : "Off";
        html += "</button></div>";
        const std::string sourceLabel = !sr.animationAssetId.empty()
            ? animationAssetLabel(coordinator, sr.animationAssetId)
            : (sr.imageAssetId.empty() ? std::string("(none)") : sr.imageAssetId);
        const bool sourceOpen = openDropdownId_ == "sprite-source" && !instanceDisabled;
        html += dropdownTrigger("Source", "sprite-source", sourceLabel, sourceOpen,
                                instanceDisabled);
        if (sourceOpen) {
            html += "<div class=\"drop-list\">";
            const bool noneSelected = sr.animationAssetId.empty() && sr.imageAssetId.empty();
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
                const bool isCurrent = sr.animationAssetId.empty() && asset.assetId == sr.imageAssetId;
                if (isCurrent || !imageHasDerivedAnimation(coordinator, asset.assetId)) {
                    pickableImages.push_back(&asset);
                }
            }
            if (!pickableImages.empty()) {
                html += "<div class=\"asset-group-title\">Images</div>";
                for (const ImageAssetDef* asset : pickableImages) {
                    const bool isCurrent =
                        sr.animationAssetId.empty() && asset->assetId == sr.imageAssetId;
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
                    const bool isCurrent = asset.id == sr.animationAssetId;
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
        if (!sr.animationAssetId.empty()) {
            // Navigates to view/edit the shared animation asset, not this
            // instance - unaffected by this instance's own layer lock.
            html += "<button class=\"" + btn + "\" data-action=\"open-sprite-animation\" data-arg=\""
                  + escapeRml(sr.animationAssetId)
                  + "\"><span class=\"icon\">&#xeb0a;</span>Open Animation Editor</button>";
        }
        if (inst->spriteAnimator.has_value()) {
            const SpriteAnimatorComponent& animator = *inst->spriteAnimator;
            html += header("&#xeb0a;", "Sprite Animator", "INSTANCE", "", "", instanceDisabled);
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Initial Clip</span>"
                    "<span class=\"prop-readonly\">"
                  + escapeRml(animationClipDisplayName(coordinator, sr.animationAssetId,
                                                       animator.initialClipId))
                  + "</span></div>";
            html += field("Speed", "commit-animator-speed", num(animator.playbackSpeed), instanceDisabled);
            html += "<div class=\"prop-row\"><span class=\"prop-label\">Auto Play</span>"
                    "<button class=\"" + instanceBtn + "\" data-action=\"toggle-animator-autoplay\">"
                  + (animator.autoPlay ? std::string("On") : std::string("Off"))
                  + "</button></div>";
        }
    } else if (spriteInherited) {
        // Inherited from the object type — read-only until overridden (no remove:
        // the type sprite is not the instance's to drop).
        html += header("&#xeb0a;", "Sprite Renderer", "INHERITED", "", "", instanceDisabled);
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Image</span>"
                "<span class=\"prop-readonly\">"
              + (resolved.assetId.empty() ? std::string("(none)") : escapeRml(resolved.assetId))
              + "</span></div>";
        html += "<button class=\"" + instanceBtn + "\" data-action=\"add-sprite-renderer\">"
                "<span class=\"icon\">&#xeb0b;</span>Add Override</button>";
    }

    // -- Tilemap (instance-owned per ADR-0001) --------------------------------
    if (inst->tilemap.has_value()) {
        const TilemapComponent& tm = *inst->tilemap;
        const TilesetAsset* tmTileset = coordinator.document().findTilesetAsset(tm.tilesetAssetId);
        html += header("&#xf22f;", "Tilemap", "INSTANCE", "", "remove-tilemap-component", instanceDisabled);
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

        // Contextual paint toolbar + tile palette - only meaningful once the
        // tileset resolves and has at least one sliced tile.
        if (!tmTileset) {
            html += "<div class=\"tile-palette-empty\">Tileset is missing.</div>";
        } else if (!coordinator.document().findImageAsset(tmTileset->imageAssetId)) {
            html += "<div class=\"tile-palette-empty\">Tileset image is missing.</div>";
        } else if (tmTileset->tiles.empty()) {
            html += "<div class=\"tile-palette-empty\">This tileset has no sliced tiles."
                    "<br/><button class=\"panel-btn\" data-action=\"open-tilemap-tileset-editor\">"
                    "Open Tileset Editor</button></div>";
        } else {
            // Reads the *effective* tool (persistent selection, or the
            // momentary Eraser-via-right-click override while it's active) so
            // the Eraser button visibly lights up during that gesture without
            // the persistent selection (e.g. Brush) ever actually changing.
            const EditorTool activeTool = coordinator.effectiveTilemapTool();
            html += "<div class=\"mode-block\"><span class=\"mode-label\">Tool</span>"
                    "<div class=\"mode-options\">";
            const auto toolOption = [&](EditorTool tool, const char* action, const char* label) {
                html += "<button class=\"panel-btn mode-option";
                if (activeTool == tool) html += " active";
                if (instanceDisabled) html += " disabled";
                html += "\" data-action=\"";
                html += action;
                html += "\">";
                html += label;
                html += "</button>";
            };
            toolOption(EditorTool::Brush, "select-tilemap-brush", "Brush");
            toolOption(EditorTool::Eraser, "select-tilemap-eraser", "Eraser");
            toolOption(EditorTool::Picker, "select-tilemap-picker", "Picker");
            toolOption(EditorTool::Rectangle, "select-tilemap-rectangle", "Rectangle");
            toolOption(EditorTool::Fill, "select-tilemap-fill", "Fill");
            html += "</div></div>";

            if (activeTool == EditorTool::Rectangle) {
                const bool outline = coordinator.state().tilemapEditor.rectangleOutlineMode;
                html += "<div class=\"mode-block\"><span class=\"mode-label\">Shape</span>"
                        "<div class=\"mode-options\">";
                const auto shapeOption = [&](bool isOutline, const char* action, const char* label) {
                    html += "<button class=\"panel-btn mode-option";
                    if (outline == isOutline) html += " active";
                    if (instanceDisabled) html += " disabled";
                    html += "\" data-action=\"";
                    html += action;
                    html += "\">";
                    html += label;
                    html += "</button>";
                };
                shapeOption(false, "select-tilemap-rectangle-solid", "Solid");
                shapeOption(true, "select-tilemap-rectangle-outline", "Outline");
                html += "</div></div>";
            }

            // Visual palette: each slot is a transparent div raylib paints the
            // cropped tile texture into (see tile_palette_renderer.h) - RmlUi
            // only owns layout and click/dblclick hit-testing here. A tile is
            // a visual element, not a string, so a text list of "tile-1",
            // "tile-2", ... is deliberately not the primary way to pick one.
            const std::optional<TileId>& selectedTileId = coordinator.state().tilemapEditor.selectedTileId;
            html += "<div class=\"asset-group-title\">Tiles</div>";
            html += "<div class=\"tile-palette\" id=\"tile-palette\">";
            for (std::size_t i = 0; i < tmTileset->tiles.size(); ++i) {
                const TileDefinition& tile = tmTileset->tiles[i];
                const std::string tooltip = "Tile " + std::to_string(i + 1) + " - ID: " + tile.id
                    + " - Source: " + std::to_string(tile.x) + "," + std::to_string(tile.y)
                    + " " + std::to_string(tile.width) + "x" + std::to_string(tile.height) + "px";
                html += "<div id=\"tile-thumb-" + std::to_string(i) + "\" class=\"tile-thumb\""
                        " data-action=\"select-tilemap-tile\" data-arg=\"" + escapeRml(tile.id) + "\""
                        " data-dbl-action=\"open-tilemap-tileset-editor\""
                        " title=\"" + escapeRml(tooltip) + "\"></div>";
            }
            html += "</div>";

            // "Tile N" reflects the tile's position in the palette (there is
            // no authored display name until per-tile metadata ships); the
            // stable TileId stays visible, but secondary.
            for (std::size_t i = 0; i < tmTileset->tiles.size(); ++i) {
                if (!selectedTileId || tmTileset->tiles[i].id != *selectedTileId) continue;
                html += "<div class=\"tile-palette-selected\">Selected: <span class=\"value\">Tile "
                      + std::to_string(i + 1) + "</span> - " + escapeRml(*selectedTileId) + "</div>";
                break;
            }
        }
    }

    // -- Box Collider 2D (object-type owned) ----------------------------------
    const BoxCollider2DComponent* collider =
        (type && type->boxCollider2D) ? &*type->boxCollider2D : nullptr;
    if (collider) {
        html += header("&#xeca9;", "Box Collider 2D", "TYPE", "", "remove-box-collider", playing);
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
        html += header("&#xf22f;", "Linear Mover", "TYPE", "", "remove-linear-mover", playing);
        html += typeOwnedLockNote;
        html += field("Direction X", "commit-mover-dir-x", num(mover->directionX), playing);
        html += field("Direction Y", "commit-mover-dir-y", num(mover->directionY), playing);
        html += field("Speed", "commit-mover-speed", num(mover->speed), playing);
    }

    // -- Top Down Controller (object-type owned) ------------------------------
    const TopDownControllerComponent* controller =
        (type && type->topDownController) ? &*type->topDownController : nullptr;
    if (controller) {
        html += header("&#xec8e;", "Top Down Controller", "TYPE", "", "remove-top-down", playing);
        html += typeOwnedLockNote;
        html += field("Speed", "commit-topdown-speed", num(controller->maxSpeed), playing);
    }

    // -- Platformer Controller (object-type owned) ----------------------------
    const PlatformerControllerComponent* platformer =
        (type && type->platformerController) ? &*type->platformerController : nullptr;
    if (platformer) {
        html += header("&#xec8e;", "Platformer Controller", "TYPE", "", "remove-platformer", playing);
        html += typeOwnedLockNote;
        html += field("Move Speed", "commit-platformer-move", num(platformer->maxSpeed), playing);
        html += field("Jump Speed", "commit-platformer-jump", num(platformer->jumpForce), playing);
        html += field("Gravity", "commit-platformer-gravity", num(platformer->customGravity), playing);
    }

    // -- Add Component menu (only addable components; one movement driver) -----
    const bool hasDriver = type
        && (type->linearMover || type->topDownController || type->platformerController);
    struct Addable { const char* label; const char* action; bool show; };
    const Addable addable[] = {
        // Sprite override is instance-level (works even without an object type) -
        // gated by instanceLocked, unlike the object-type-owned entries below.
        {"Sprite Renderer", "add-sprite-renderer",
            !instanceLocked && !spriteOverride && resolved.origin == ComponentOrigin::None},
        {"Box Collider 2D", "add-box-collider", type && !collider},
        // The three movement drivers are mutually exclusive: offer none once one exists.
        {"Top Down Controller", "add-top-down", type && !hasDriver},
        {"Platformer Controller", "add-platformer", type && !hasDriver},
        {"Linear Mover", "add-linear-mover", type && !hasDriver},
        // Instance-level like Sprite Renderer; needs at least one tileset to
        // reference (auto-assigns the first one - the tileset picker above
        // lets it be changed afterward).
        {"Tilemap", "add-tilemap-component",
            !instanceLocked && !inst->tilemap.has_value()
                && !coordinator.document().data().tilesets.empty()},
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
                html += "<div class=\"add-entry\" data-action=\"";
                html += a.action;
                html += "\">";
                html += a.label;
                html += "</div>";
            }
            html += "</div>";
        }
        html += "</div>";
    }

    body->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
