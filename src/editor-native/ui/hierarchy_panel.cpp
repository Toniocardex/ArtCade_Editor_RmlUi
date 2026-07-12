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
// display name (ProjectDoc.objectTypes is an unordered_map; iteration order
// is not stable or meaningful to a user). Each entry places a new instance of
// that type via add-instance-of-type, without needing a prior selection —
// the counterpart to "Create Instance", which derives its type from one.
std::string useExistingTypeList(const ProjectDocument& doc, bool disabled) {
    std::vector<const EntityDef*> sorted;
    sorted.reserve(doc.data().objectTypes.size());
    for (const auto& [id, type] : doc.data().objectTypes) sorted.push_back(&type);
    std::sort(sorted.begin(), sorted.end(), [](const EntityDef* a, const EntityDef* b) {
        return a->name < b->name;
    });

    if (sorted.empty()) return {};   // no dead separator when the catalog is empty

    std::string html = "<div class=\"menu-separator\"></div>";
    for (const EntityDef* type : sorted) {
        html += "<div class=\"menu-entry";
        if (disabled) html += " disabled";
        html += "\" data-action=\"add-instance-of-type\" data-arg=\""
              + escapeRml(type->className) + "\">" + escapeRml(type->name) + "</div>";
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

    std::string rows;
    const SceneDef* scene = doc.findScene(activeSceneId);
    if (scene) {
        for (const SceneInstanceDef& inst : scene->instances) {
            if (!matchesFilter(inst.instanceName, filter)) continue;
            rows += "<div class=\"tree-row";
            if (inst.id == selected) rows += " selected";
            rows += "\" data-action=\"select-entity\" data-arg=\""
                  + std::to_string(inst.id) + "\">";
            rows += "<span class=\"icon row-icon\">";
            rows += typeIcon(inst.objectTypeId);
            rows += "</span>";
            rows += "<span class=\"row-name\">" + escapeRml(inst.instanceName) + "</span>";
            rows += "<span class=\"row-type\">" + escapeRml(inst.objectTypeId) + "</span>";
            // Per-entity menu (Create Instance / Delete).
            rows += "<span class=\"row-menu\" data-action=\"open-entity-menu\" data-arg=\""
                  + std::to_string(inst.id) + "\">&#xeb5d;</span>";
            rows += "</div>";
        }
    }
    if (rows.empty()) {
        // Without a scene, "No entities" is noise (entities cannot exist yet)
        // and competes with the viewport's Create Scene call to action.
        rows = scene
            ? "<div class=\"tree-empty\">No entities in this scene.<br/>"
              "Create an entity, then place instances.</div>"
            : "<div class=\"tree-empty\">No scenes yet.</div>";
    } else {
        // What the list contains: instances of the active scene (their object
        // type shows as the trailing tag).
        rows = "<div class=\"tree-section\">Instances</div>" + rows;
    }
    setHtml(document, "hierarchy-list", rows);

    // -- Create-menu availability reflects authoritative state -----------------
    // Disabling is UX only; the commands still validate their inputs. Every
    // entry mutates the authoring document, so all are disabled while Play
    // runs (the authoring document is frozen). Scene tabs and entity rows above
    // stay clickable — selection and scene navigation are workspace-only.
    // Delete / Set as Start live in the per-item context menus (open-scene-menu /
    // open-entity-menu), not as permanent buttons.
    const bool playing        = coordinator.isPlaying();
    const bool hasActiveScene = scene != nullptr;
    const bool hasSelection   = selected != INVALID_ENTITY;
    const auto setEnabled = [&](const char* id, bool enabled) {
        if (Rml::Element* el = document->GetElementById(id))
            el->SetClass("disabled", !enabled);
    };
    setEnabled("btn-create",            !playing);
    setEnabled("create-scene-entry",    !playing);
    setEnabled("create-entity-entry",   hasActiveScene && !playing);
    // Create Instance needs a selected entity to know which object type to
    // instance; an empty catalog therefore disables it (no entity -> no selection).
    setEnabled("create-instance-entry", hasSelection && !playing);
    setHtml(document, "create-instance-of-type-list", useExistingTypeList(doc, playing));
}

} // namespace ArtCade::EditorNative
