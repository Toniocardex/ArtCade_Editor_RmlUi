#pragma once

#include "core/types.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
class ProjectDocument;

struct HierarchyRenameDraft {
    SceneId sceneId;
    EntityId entityId = INVALID_ENTITY;
    std::string originalName;
    std::string editedName;
};

// Refreshes scene tabs and the entity tree. Holds only presentation state
// (layer collapse, rename draft, reveal) — never authoring authority (ADR-0023).
class HierarchyPanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator);

    void toggleLayerCollapsed(const SceneId& sceneId, const std::string& layerId);
    /** Expand the instance's layer if collapsed and scroll it into view after refresh. */
    void requestReveal(const SceneId& sceneId, EntityId id, const std::string& layerId);

    void beginRename(const SceneId& sceneId, EntityId id, const std::string& name);
    void cancelRename();
    bool hasRenameDraft() const { return renameDraft_.entityId != INVALID_ENTITY; }
    const HierarchyRenameDraft& renameDraft() const { return renameDraft_; }

private:
    void reconcileCollapseState(const ProjectDocument& doc);

    std::unordered_map<SceneId, std::unordered_set<std::string>> collapsedLayers_;
    HierarchyRenameDraft renameDraft_{};
    EntityId pendingRevealId_ = INVALID_ENTITY;
};

} // namespace ArtCade::EditorNative
