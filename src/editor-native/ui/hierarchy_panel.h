#pragma once

#include "core/types.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
class ProjectDocument;

// Refreshes scene tabs and the entity tree. Holds only local presentation state
// (layer collapse and reveal), never authoring data.
class HierarchyPanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator);

    void toggleLayerCollapsed(const SceneId& sceneId, const std::string& layerId);
    /** Expand the instance's layer if collapsed and scroll it into view after refresh. */
    void requestReveal(const SceneId& sceneId, EntityId id, const std::string& layerId);

private:
    void reconcileCollapseState(const ProjectDocument& doc);

    std::unordered_map<SceneId, std::unordered_set<std::string>> collapsedLayers_;
    EntityId pendingRevealId_ = INVALID_ENTITY;
};

} // namespace ArtCade::EditorNative
