#include "editor-native/ui/hierarchy_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/instance_name_policy.h"
#include "editor-native/model/project_document.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

struct HierarchyInstancePresentation {
    EntityId entityId = INVALID_ENTITY;
    std::string instanceName;
    std::string objectTypeName;
    std::string objectTypeId;
    std::string layerName;
    std::string layerId;
    std::size_t sharedTypeCount = 0;
    bool selected = false;
    bool layerHidden = false;
    bool layerLocked = false;
    bool hasOverrides = false;
    bool tilemap = false;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool containsInsensitive(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    return lower(std::string(hay)).find(lower(std::string(needle))) != std::string::npos;
}

HierarchySearchFields toSearchFields(const HierarchyInstancePresentation& item) {
    HierarchySearchFields fields;
    fields.instanceName = item.instanceName;
    fields.objectTypeName = item.objectTypeName;
    fields.objectTypeId = item.objectTypeId;
    fields.layerName = item.layerName;
    fields.entityId = item.entityId;
    return fields;
}

const char* typeIcon(const std::string& typeId) {
    const std::string t = lower(typeId);
    if (t.find("player") != std::string::npos) return "&#xeb4d;";
    if (t.find("crate")  != std::string::npos) return "&#xea45;";
    if (t.find("coin")   != std::string::npos) return "&#xeb82;";
    if (t.find("enemy")  != std::string::npos) return "&#xeb8e;";
    return "&#xfa97;";
}

void setHtml(Rml::ElementDocument* document, const char* id, const std::string& html) {
    if (Rml::Element* el = document->GetElementById(id)) el->SetInnerRML(html);
}

std::string useExistingTypeList(const ProjectDocument& doc, bool disabled) {
    std::vector<const EntityDef*> sorted;
    sorted.reserve(doc.data().objectTypes.size());
    for (const auto& [id, type] : doc.data().objectTypes) sorted.push_back(&type);
    std::sort(sorted.begin(), sorted.end(), [](const EntityDef* a, const EntityDef* b) {
        if (a->name != b->name) return a->name < b->name;
        return a->className < b->className;
    });
    if (sorted.empty()) return {};

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

std::string instanceTooltip(const HierarchyInstancePresentation& item) {
    std::string tip = "Instance: " + escapeRml(item.instanceName)
                    + "&#10;Object Type: " + escapeRml(item.objectTypeName)
                    + "&#10;Type ID: " + escapeRml(item.objectTypeId)
                    + "&#10;Instance ID: " + std::to_string(item.entityId)
                    + "&#10;Layer: " + escapeRml(item.layerName);
    if (item.layerHidden)
        tip += "&#10;Layer is hidden in the Scene View";
    if (item.layerLocked)
        tip += "&#10;Layer is locked";
    return tip;
}

} // namespace

void HierarchyPanel::toggleLayerCollapsed(const SceneId& sceneId,
                                          const std::string& layerId) {
    auto& set = collapsedLayers_[sceneId];
    if (set.count(layerId)) set.erase(layerId);
    else set.insert(layerId);
}

void HierarchyPanel::requestReveal(const SceneId& sceneId, EntityId id,
                                   const std::string& layerId) {
    if (!layerId.empty())
        collapsedLayers_[sceneId].erase(layerId);
    pendingRevealId_ = id;
}

void HierarchyPanel::beginRename(const SceneId& sceneId, EntityId id,
                                 const std::string& name) {
    renameDraft_.sceneId = sceneId;
    renameDraft_.entityId = id;
    renameDraft_.originalName = name;
    renameDraft_.editedName = name;
}

void HierarchyPanel::cancelRename() {
    renameDraft_ = {};
}

void HierarchyPanel::reconcileCollapseState(const ProjectDocument& doc) {
    for (auto it = collapsedLayers_.begin(); it != collapsedLayers_.end();) {
        if (!doc.hasScene(it->first)) {
            it = collapsedLayers_.erase(it);
            continue;
        }
        const SceneDef* scene = doc.findScene(it->first);
        std::unordered_set<std::string> valid;
        if (scene) {
            for (const SceneLayerDef& layer : scene->layers)
                valid.insert(layer.id);
        }
        for (auto lit = it->second.begin(); lit != it->second.end();) {
            if (!valid.count(*lit)) lit = it->second.erase(lit);
            else ++lit;
        }
        ++it;
    }
}

void HierarchyPanel::refresh(Rml::ElementDocument* document,
                             const EditorCoordinator& coordinator) {
    if (!document) return;
    const ProjectDocument& doc = coordinator.document();
    const SceneId& activeSceneId = coordinator.state().activeSceneId;
    reconcileCollapseState(doc);

    const SceneId& startSceneId = doc.startSceneId();
    std::map<SceneId, std::string> scenesSorted;
    for (const auto& [id, scene] : doc.data().scenes) scenesSorted[id] = scene.name;

    std::string tabs;
    for (const auto& [id, name] : scenesSorted) {
        const bool active = (id == activeSceneId);
        tabs += "<div class=\"tab";
        if (active) tabs += " active";
        tabs += "\" data-action=\"select-scene\" data-arg=\"" + escapeRml(id) + "\">";
        if (id == startSceneId) tabs += "<span class=\"icon ico-start\">&#xeb2e;</span>";
        tabs += escapeRml(name);
        tabs += "<span class=\"tab-menu\" data-action=\"open-scene-menu\" data-arg=\""
              + escapeRml(id) + "\">&#xeb5d;</span>";
        tabs += "</div>";
    }
    setHtml(document, "scene-tabs", tabs);
    if (Rml::Element* strip = document->GetElementById("scene-tabs")) {
        strip->SetClass("hidden", scenesSorted.empty());
    }

    const std::string filter = coordinator.uiState().hierarchyFilter;
    const EntityId selected = coordinator.selection().primaryEntity;
    const bool playing = coordinator.isPlaying();
    const SceneDef* scene = doc.findScene(activeSceneId);

    // Cancel rename when scene/selection/Play/instance become invalid.
    if (renameDraft_.entityId != INVALID_ENTITY) {
        const bool sceneOk = renameDraft_.sceneId == activeSceneId;
        const bool instOk = sceneOk
            && doc.findInstanceInScene(activeSceneId, renameDraft_.entityId) != nullptr;
        if (playing || !sceneOk || !instOk)
            cancelRename();
    }

    // Stable filter field (same pattern as Assets): never rebuild while focused.
    if (Rml::Element* slot = document->GetElementById("hierarchy-search-slot")) {
        if (!slot->HasChildNodes()) {
            slot->SetInnerRML(
                "<span class=\"hierarchy-search-icon\">&#xeb1c;</span>"
                "<input id=\"hierarchy-filter\" type=\"text\""
                " class=\"hierarchy-search-field\" data-action=\"set-hierarchy-filter\""
                " placeholder=\"Search hierarchy...\"/>");
        }
    }
    if (Rml::Element* filterEl = document->GetElementById("hierarchy-filter")) {
        Rml::Context* context = document->GetContext();
        if (!context || context->GetFocusElement() != filterEl) {
            filterEl->SetAttribute("value", filter);
            if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(filterEl))
                control->SetValue(filter);
        }
    }

    std::unordered_map<ObjectTypeId, std::size_t> typeUseCount;
    if (scene) {
        for (const SceneInstanceDef& inst : scene->instances)
            ++typeUseCount[inst.objectTypeId];
    }

    const auto buildPresentation = [&](const SceneInstanceDef& inst)
        -> HierarchyInstancePresentation {
        HierarchyInstancePresentation item;
        item.entityId = inst.id;
        item.instanceName = inst.instanceName;
        item.objectTypeId = inst.objectTypeId;
        item.objectTypeName = inst.objectTypeId;
        if (const EntityDef* type = doc.findObjectType(inst.objectTypeId))
            item.objectTypeName = type->name;
        item.layerId = doc.effectiveLayerId(activeSceneId, inst);
        item.layerName = item.layerId;
        if (scene) {
            for (const SceneLayerDef& layer : scene->layers) {
                if (layer.id == item.layerId) {
                    item.layerName = layer.name;
                    item.layerLocked = layer.locked;
                    break;
                }
            }
        }
        item.sharedTypeCount = typeUseCount[inst.objectTypeId];
        item.selected = inst.id == selected;
        item.hasOverrides = hasInstanceOverrides(inst);
        item.tilemap = inst.tilemap.has_value();
        const EditorSceneViewState& view = coordinator.sceneView(activeSceneId);
        item.layerHidden = view.hiddenLayerIds.count(item.layerId) > 0;
        return item;
    };

    const auto instanceRow = [&](const HierarchyInstancePresentation& item,
                                 bool grouped) {
        std::string row = "<div id=\"hierarchy-instance-"
                        + std::to_string(item.entityId) + "\" class=\"tree-row";
        if (grouped) row += " in-layer";
        if (item.selected) row += " selected";
        if (item.layerHidden) row += " layer-hidden";
        if (item.layerLocked) row += " layer-locked";
        row += "\" data-action=\"select-entity\" data-arg=\""
             + std::to_string(item.entityId) + "\" title=\""
             + instanceTooltip(item) + "\">";
        row += "<span class=\"icon row-icon\">";
        row += item.tilemap ? "&#xea3b;" : typeIcon(item.objectTypeId);
        row += "</span>";

        if (renameDraft_.entityId == item.entityId
            && renameDraft_.sceneId == activeSceneId) {
            row += "<input id=\"hierarchy-rename-input\" class=\"row-name-input\" type=\"text\" "
                   "data-action=\"commit-hierarchy-rename\" data-arg=\""
                 + std::to_string(item.entityId) + "\" value=\""
                 + escapeRml(renameDraft_.editedName) + "\"/>";
        } else {
            row += "<span class=\"row-name\" title=\"" + escapeRml(item.instanceName)
                 + "\" data-dbl-action=\"begin-hierarchy-rename\" data-arg=\""
                 + std::to_string(item.entityId) + "\">"
                 + escapeRml(item.instanceName) + "</span>";
        }

        row += "<span class=\"row-indicators\">";
        if (item.hasOverrides) {
            // ADR-0023: local Object Type overrides — a bullet, not a Tabler glyph.
            row += "<span class=\"row-override-indicator\" title=\"This instance "
                   "overrides properties from its Object Type\"><span>•</span></span>";
        }
        if (item.sharedTypeCount > 1) {
            row += "<span class=\"icon row-shared-type\" title=\"Object Type: "
                 + escapeRml(item.objectTypeName) + "&#10;Stable ID: "
                 + escapeRml(item.objectTypeId) + "&#10;"
                 + std::to_string(item.sharedTypeCount)
                 + " instances in this scene share this type\">&#xeade;</span>";
        }
        row += "</span>";
        row += "<span class=\"row-menu\" data-action=\"open-entity-menu\" data-arg=\""
             + std::to_string(item.entityId) + "\">&#xeb5d;</span>";
        row += "</div>";
        return row;
    };

    std::string rows;
    bool anyRowMatchedFilter = false;
    const bool filterActive = !filter.empty();

    if (scene && !scene->instances.empty() && !scene->layers.empty()) {
        const EditorSceneViewState& view = coordinator.sceneView(activeSceneId);
        const std::string activeLayer = coordinator.activeLayerId(activeSceneId);
        auto& collapsed = collapsedLayers_[activeSceneId];

        for (const SceneLayerDef& layer : scene->layers) {
            std::size_t totalOnLayer = 0;
            std::size_t visibleOnLayer = 0;
            std::vector<HierarchyInstancePresentation> visible;
            for (const SceneInstanceDef& inst : scene->instances) {
                if (doc.effectiveLayerId(activeSceneId, inst) != layer.id) continue;
                ++totalOnLayer;
                HierarchyInstancePresentation item = buildPresentation(inst);
                if (!hierarchyInstanceMatches(toSearchFields(item), filter)) continue;
                ++visibleOnLayer;
                visible.push_back(std::move(item));
            }

            const bool layerNameMatch = containsInsensitive(layer.name, filter);
            if (filterActive && visible.empty() && !layerNameMatch) continue;

            if (filterActive && layerNameMatch && visible.empty()) {
                for (const SceneInstanceDef& inst : scene->instances) {
                    if (doc.effectiveLayerId(activeSceneId, inst) != layer.id) continue;
                    visible.push_back(buildPresentation(inst));
                }
                visibleOnLayer = visible.size();
            }

            const bool isActive = layer.id == activeLayer;
            const bool isHidden = view.hiddenLayerIds.count(layer.id) > 0;
            // Filter temporarily forces layers with matches open; collapse state
            // itself is not mutated.
            const bool isCollapsed = !filterActive && collapsed.count(layer.id) > 0;

            rows += "<div class=\"layer-group-header";
            if (isActive) rows += " active";
            if (isHidden) rows += " hidden";
            rows += "\" data-action=\"select-layer\" data-arg=\""
                  + escapeRml(layer.id) + "\">";
            rows += "<span class=\"layer-chevron\" data-action=\"toggle-hierarchy-layer\" "
                    "data-arg=\"" + escapeRml(layer.id) + "\"><span class=\"icon\">"
                  + (isCollapsed ? "&#xea61;" : "&#xea62;") + "</span></span>";
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
            if (isActive) rows += "<span class=\"layer-active-dot\">&#x25cf;</span> ";
            rows += escapeRml(layer.name) + "</span>";
            rows += "<span class=\"layer-count\">";
            if (filterActive)
                rows += std::to_string(visibleOnLayer) + "/" + std::to_string(totalOnLayer);
            else
                rows += std::to_string(totalOnLayer);
            rows += "</span></div>";

            if (!isCollapsed) {
                for (const HierarchyInstancePresentation& item : visible) {
                    anyRowMatchedFilter = true;
                    rows += instanceRow(item, true);
                }
            } else if (!visible.empty()) {
                anyRowMatchedFilter = true;
            }
        }
    } else if (scene) {
        for (const SceneInstanceDef& inst : scene->instances) {
            HierarchyInstancePresentation item = buildPresentation(inst);
            if (!hierarchyInstanceMatches(toSearchFields(item), filter)) continue;
            anyRowMatchedFilter = true;
            rows += instanceRow(item, false);
        }
    }

    if (!scene) {
        rows = "<div class=\"tree-empty\">No scene open.</div>";
    } else if (scene->instances.empty()) {
        rows = "<div class=\"hierarchy-empty-state\">"
               "<span class=\"hierarchy-empty-icon\">&#xef94;</span>"
               "<span class=\"hierarchy-empty-title\">Your scene is empty</span>"
               "<span class=\"hierarchy-empty-copy\">Create an entity to start building this scene.</span>"
               "<button class=\"panel-btn hierarchy-empty-action";
        if (playing) rows += " disabled";
        rows += "\" data-action=\"add-entity\">Create Entity</button></div>";
    } else if (!anyRowMatchedFilter && filterActive) {
        rows = "<div class=\"tree-empty\">No instances match the current filter.</div>";
    } else if (!rows.empty() && scene && !scene->instances.empty()) {
        rows = "<div class=\"tree-section\">"
             + std::string(scene->layers.empty() ? "Instances" : "Layers")
             + "</div>" + rows;
    }
    setHtml(document, "hierarchy-list", rows);

    if (pendingRevealId_ != INVALID_ENTITY) {
        const std::string eid = "hierarchy-instance-" + std::to_string(pendingRevealId_);
        if (Rml::Element* el = document->GetElementById(eid.c_str()))
            el->ScrollIntoView();
        pendingRevealId_ = INVALID_ENTITY;
    }

    if (renameDraft_.entityId != INVALID_ENTITY) {
        if (Rml::Element* input = document->GetElementById("hierarchy-rename-input"))
            input->Focus(true);
    }

    const bool hasActiveScene = scene != nullptr;
    if (Rml::Element* entry = document->GetElementById("create-scene-entry"))
        entry->SetClass("disabled", playing);
    if (Rml::Element* entry = document->GetElementById("create-entity-entry"))
        entry->SetClass("disabled", !hasActiveScene || playing);
    if (Rml::Element* entry = document->GetElementById("create-tilemap-entity-entry"))
        entry->SetClass("disabled", !hasActiveScene || playing);
    if (Rml::Element* entry = document->GetElementById("create-instance-entry"))
        entry->SetClass("disabled",
                        !hasActiveScene || selected == INVALID_ENTITY || playing);
    setHtml(document, "create-instance-of-type-list",
            useExistingTypeList(doc, playing || !hasActiveScene));
}

} // namespace ArtCade::EditorNative
