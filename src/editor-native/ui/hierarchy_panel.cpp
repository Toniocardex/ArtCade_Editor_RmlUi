#include "editor-native/ui/hierarchy_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <map>
#include <string>

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
        tabs += "</div>";
    }
    setHtml(document, "scene-tabs", tabs);

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
            rows += "</div>";
        }
    }
    if (rows.empty()) rows = "<div class=\"tree-empty\">No entities</div>";
    setHtml(document, "hierarchy-list", rows);

    // -- Action button availability reflects authoritative state ---------------
    // Disabling is UX only; the commands still validate their inputs. Every
    // button here mutates the authoring document, so all are disabled while Play
    // runs (the authoring document is frozen). Scene tabs and entity rows above
    // stay clickable — selection and scene navigation are workspace-only.
    const bool playing        = coordinator.isPlaying();
    const bool hasActiveScene = scene != nullptr;
    const bool hasSelection   = selected != INVALID_ENTITY;
    const auto setEnabled = [&](const char* id, bool enabled) {
        if (Rml::Element* el = document->GetElementById(id))
            el->SetClass("disabled", !enabled);
    };
    setEnabled("btn-add-scene",  !playing);
    setEnabled("btn-del-scene",  hasActiveScene && !playing);
    setEnabled("btn-add-entity", hasActiveScene && !playing);
    // +Instance needs a selected entity to know which object type to instance;
    // an empty catalog therefore disables it (no entity -> no selection).
    setEnabled("btn-add-instance", hasSelection && !playing);
    setEnabled("btn-del-entity", hasSelection && !playing);
    // "Start" sets the active scene as start scene — pointless if it already is.
    setEnabled("btn-set-start",  hasActiveScene && activeSceneId != startSceneId && !playing);
}

} // namespace ArtCade::EditorNative
