#include "editor-native/ui/hierarchy_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matchesFilter(const std::string& name, const std::string& filter) {
    if (filter.empty()) return true;
    return lower(name).find(lower(filter)) != std::string::npos;
}

// Tabler icon glyph (PUA codepoint) for a hierarchy row, chosen by object type.
const char* typeIcon(const std::string& typeId) {
    const std::string t = lower(typeId);
    if (t.find("player") != std::string::npos) return "&#xeb4d;"; // user
    if (t.find("crate")  != std::string::npos) return "&#xea45;"; // box
    if (t.find("coin")   != std::string::npos) return "&#xeb82;"; // coin
    if (t.find("enemy")  != std::string::npos) return "&#xeb8e;"; // ghost
    return "&#xfa97;";                                            // cube (default)
}

void setHtml(Rml::ElementDocument* document, const char* id, const std::string& html) {
    if (Rml::Element* el = document->GetElementById(id)) el->SetInnerRML(html);
}

// "Use Existing Type": one .menu-entry per catalog EntityDef, sorted by
// display name with the type id as tie-break (ProjectDoc.objectTypes is an
// unordered_map; iteration order is not stable, and default-named types all
// read "Entity" — without the tie-break equal names would shuffle between
// refreshes). Each entry places a new instance of that type via
// add-instance-of-type, without needing a prior selection — the counterpart
// to "Create Instance", which derives its type from one. The section label
// and the id hint exist because the list is the CATALOG of types, not the
// scene's instances: without them five default-named types render as five
// indistinguishable "Entity" rows glued under the create actions.
std::string useExistingTypeList(const ProjectDocument& doc, bool disabled) {
    std::vector<const EntityDef*> sorted;
    sorted.reserve(doc.data().objectTypes.size());
    for (const auto& [id, type] : doc.data().objectTypes) sorted.push_back(&type);
    std::sort(sorted.begin(), sorted.end(), [](const EntityDef* a, const EntityDef* b) {
        if (a->name != b->name) return a->name < b->name;
        return a->className < b->className;
    });

    if (sorted.empty()) return {};   // no dead separator when the catalog is empty

    std::string html = "<div class=\"menu-separator\"></div>"
                       "<div class=\"menu-section\">Object types</div>";
    for (const EntityDef* type : sorted) {
        html += "<div class=\"menu-entry";
        if (disabled) html += " disabled";
        html += "\" data-action=\"add-instance-of-type\" data-arg=\""
              + escapeRml(type->className) + "\" title=\"New instance of "
              + escapeRml(type->className) + "\">" + escapeRml(type->name)
              + "<span class=\"menu-entry-hint\">" + escapeRml(type->className)
              + "</span></div>";
    }
    return html;
}

} // namespace

void HierarchyPanel::refresh(Rml::ElementDocument* document,
                             const EditorCoordinator& coordinator) const {
    if (!document) return;
    const ProjectDocument& doc = coordinator.document();
    const SceneId& activeSceneId = coordinator.state().activeSceneId;

    // -- Scene tabs (sorted by id for a stable order) --------------------------
    const SceneId& startSceneId = doc.startSceneId();
    std::map<SceneId, std::string> scenesSorted;
    for (const auto& [id, scene] : doc.data().scenes) scenesSorted[id] = scene.name;

    std::string tabs;
    for (const auto& [id, name] : scenesSorted) {
        const bool active = (id == activeSceneId);
        tabs += "<div class=\"tab";
        if (active) tabs += " active";
        tabs += "\" data-action=\"select-scene\" data-arg=\"" + escapeRml(id) + "\">";
        // Star icon marks the start scene.
        if (id == startSceneId) tabs += "<span class=\"icon ico-start\">&#xeb2e;</span>";
        tabs += escapeRml(name);
        // Per-scene menu (Set as Start / Delete) — its own action wins over the tab's.
        tabs += "<span class=\"tab-menu\" data-action=\"open-scene-menu\" data-arg=\""
              + escapeRml(id) + "\">&#xeb5d;</span>";
        tabs += "</div>";
    }
    setHtml(document, "scene-tabs", tabs);
    // With no scenes the strip is an empty dark band: collapse it and let the
    // single "No scenes yet" line below carry the whole empty state.
    if (Rml::Element* strip = document->GetElementById("scene-tabs")) {
        strip->SetClass("hidden", scenesSorted.empty());
    }

    // -- Entity tree of the active scene --------------------------------------
    const std::string filter = coordinator.uiState().hierarchyFilter;
    const EntityId selected = coordinator.selection().primaryEntity;
    const bool playing = coordinator.isPlaying();

    std::string rows;
    const SceneDef* scene = doc.findScene(activeSceneId);
    // One instance row, shared by the grouped (layered) and flat (legacy)
    // renderings. `grouped` only adds the indent class.
    const auto instanceRow = [&](const SceneInstanceDef& inst, bool grouped) {
        std::string row = "<div class=\"tree-row";
        if (grouped) row += " in-layer";
        if (inst.id == selected) row += " selected";
        row += "\" data-action=\"select-entity\" data-arg=\""
             + std::to_string(inst.id) + "\">";
        row += "<span class=\"icon row-icon\">";
        // A tilemap-owning instance shows the grid glyph (the same one the
        // Tile Palette's grid toggle uses) instead of the type icon, so the
        // paintable strata read at a glance in the tree.
        row += inst.tilemap.has_value() ? "&#xea3b;" : typeIcon(inst.objectTypeId);
        row += "</span>";
        row += "<span class=\"row-name\">" + escapeRml(inst.instanceName) + "</span>";
        row += "<span class=\"row-type\">" + escapeRml(inst.objectTypeId) + "</span>";
        // Per-entity menu (Create Instance / Delete).
        row += "<span class=\"row-menu\" data-action=\"open-entity-menu\" data-arg=\""
             + std::to_string(inst.id) + "\">&#xeb5d;</span>";
        row += "</div>";
        return row;
    };
    bool anyRowMatchedFilter = false;
    if (scene && !scene->instances.empty() && !scene->layers.empty()) {
        // Layered scene: one group per layer, background (layers[0]) at the
        // top - the list reads in render order, exactly like the stack the
        // user is composing. Headers always render (an empty layer is a valid
        // create target: click it -> active layer -> Create Tilemap Entity).
        const EditorSceneViewState& view = coordinator.sceneView(activeSceneId);
        const std::string activeLayer = coordinator.activeLayerId(activeSceneId);
        for (const SceneLayerDef& layer : scene->layers) {
            const bool isActive = layer.id == activeLayer;
            const bool isHidden = view.hiddenLayerIds.count(layer.id) > 0;
            rows += "<div class=\"layer-group-header";
            if (isActive) rows += " active";
            if (isHidden) rows += " hidden";
            // Clicking the header makes the layer active (workspace intent);
            // the eye/lock children carry their own actions, which win.
            rows += "\" data-action=\"select-layer\" data-arg=\""
                  + escapeRml(layer.id) + "\">";
            rows += "<span class=\"layer-eye\" data-action=\"toggle-layer-visible\""
                    " data-arg=\"" + escapeRml(layer.id) + "\" title=\""
                  + (isHidden ? "Show layer in editor" : "Hide layer in editor")
                  + "\"><span class=\"icon\">&#xea9a;</span></span>";
            rows += "<span class=\"layer-lock";
            if (layer.locked) rows += " locked";
            rows += "\" data-action=\"toggle-layer-locked\" data-arg=\""
                  + escapeRml(layer.id) + "\" title=\""
                  + (layer.locked ? "Unlock layer" : "Lock layer")
                  + "\"><span class=\"icon\">"
                  + (layer.locked ? "&#xeae2;" : "&#xeae1;") + "</span></span>";
            rows += "<span class=\"layer-group-name\">";
            if (isActive) rows += "&#x25cf; ";   // active marker, as in the Inspector
            rows += escapeRml(layer.name) + "</span>";
            rows += "</div>";
            for (const SceneInstanceDef& inst : scene->instances) {
                if (doc.effectiveLayerId(activeSceneId, inst) != layer.id) continue;
                if (!matchesFilter(inst.instanceName, filter)) continue;
                anyRowMatchedFilter = true;
                rows += instanceRow(inst, /*grouped*/ true);
            }
        }
    } else if (scene) {
        // Legacy scene without layers: the flat list, unchanged.
        for (const SceneInstanceDef& inst : scene->instances) {
            if (!matchesFilter(inst.instanceName, filter)) continue;
            anyRowMatchedFilter = true;
            rows += instanceRow(inst, /*grouped*/ false);
        }
    }
    if (!scene) {
        // The viewport owns the primary Create Scene call to action.
        rows = "<div class=\"tree-empty\">No scene open.</div>";
    } else if (scene->instances.empty()) {
        rows = "<div class=\"hierarchy-empty-state\">"
               "<span class=\"hierarchy-empty-icon\">&#xef94;</span>"
               "<span class=\"hierarchy-empty-title\">Your scene is empty</span>"
               "<span class=\"hierarchy-empty-copy\">Create an entity to start building this scene.</span>"
               "<button class=\"panel-btn hierarchy-empty-action";
        if (playing) rows += " disabled";
        rows += "\" data-action=\"add-entity\">Create Entity</button></div>";
    } else if (!anyRowMatchedFilter) {
        rows = "<div class=\"tree-empty\">No instances match the current filter.</div>";
    } else {
        // What the list contains: instances of the active scene (their object
        // type shows as the trailing tag), grouped by render layer when the
        // scene has layers.
        rows = "<div class=\"tree-section\">"
             + std::string(scene->layers.empty() ? "Instances" : "Layers")
             + "</div>" + rows;
    }
    setHtml(document, "hierarchy-list", rows);

    // -- Create-menu availability reflects authoritative state -----------------
    // Disabling is UX only; the commands still validate their inputs. Every
    // entry mutates the authoring document, so all are disabled while Play
    // runs (the authoring document is frozen). Scene tabs and entity rows above
    // stay clickable — selection and scene navigation are workspace-only.
    // Delete / Set as Start live in the per-item context menus (open-scene-menu /
    // open-entity-menu), not as permanent buttons.
    const bool hasActiveScene = scene != nullptr;
    const bool hasSelection   = selected != INVALID_ENTITY;
    const auto setEnabled = [&](const char* id, bool enabled) {
        if (Rml::Element* el = document->GetElementById(id))
            el->SetClass("disabled", !enabled);
    };
    setEnabled("btn-create",            !playing);
    setEnabled("create-scene-entry",    !playing);
    setEnabled("create-entity-entry",   hasActiveScene && !playing);
    // Affordance only (the action preflights for real): greyed without a
    // scene or during Play; a missing tileset still fails with the explicit
    // "Import a tileset first" message rather than a mute disabled entry.
    setEnabled("create-tilemap-entity-entry", hasActiveScene && !playing);
    // Create Instance needs a selected entity to know which object type to
    // instance; an empty catalog therefore disables it (no entity -> no selection).
    setEnabled("create-instance-entry", hasSelection && !playing);
    setHtml(document, "create-instance-of-type-list", useExistingTypeList(doc, playing));
}

} // namespace ArtCade::EditorNative
