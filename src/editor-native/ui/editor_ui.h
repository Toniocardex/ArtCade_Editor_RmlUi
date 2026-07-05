#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_invalidation.h"
#include "editor-native/ui/assets_panel.h"
#include "editor-native/ui/console_panel.h"
#include "editor-native/ui/hierarchy_panel.h"
#include "editor-native/ui/inspector_panel.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Rml { class ElementDocument; class EventListener; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
enum class AssetKind;   // defined in app/asset_import.h

// Target kind of the hierarchy context menu (scene tab vs entity row).
enum class HierarchyMenuKind { Scene, Entity };

/** Escape &, <, > so authored names are safe inside generated RML. */
std::string escapeRml(const std::string& text);

// =============================================================================
// EditorUi — owns the panels and the single RmlUi event listener, and turns
// coordinator invalidation into targeted panel refreshes (prompt §11).
//
// The listener is the only place RmlUi events enter the editor; it forwards
// every interaction to the coordinator as a command or intent. Panels are
// refreshed strictly in response to invalidation, never every frame.
// =============================================================================
class EditorUi {
public:
    EditorUi(EditorCoordinator& coordinator, Rml::ElementDocument* document,
             Rml::ElementDocument* animationDocument);
    ~EditorUi();

    void bind();           // attach the listener + do the initial full refresh
    void processFrame();   // consume invalidations and refresh affected panels

    bool isPlaying() const;

    // Project file operations live in the application layer (it owns the texture
    // cache it must clear on replace, and the platform file pickers). The UI
    // only triggers them; it never touches files or the renderer. Unset handlers
    // make the corresponding toolbar action a no-op.
    using ProjectFileRequest = std::function<void()>;
    void setProjectFileHandlers(ProjectFileRequest newProject,
                                ProjectFileRequest open,
                                ProjectFileRequest save,
                                ProjectFileRequest saveAs);
    // Import copies a file into the project via the canonical importAsset
    // pipeline; it needs the filesystem and a saved project, so it lives in the
    // application. Every kind converges on this one handler.
    using ImportAssetRequest = std::function<void(AssetKind)>;
    void setImportHandler(ImportAssetRequest importAsset);
    // Import an image straight from the Sprite Animation Editor, reusing the same
    // importAsset pipeline. Returns the new image id (nullopt when cancelled or on
    // failure) so the editor can start a new animation on it in one gesture.
    using ImportImageRequest = std::function<std::optional<AssetId>()>;
    void setImportImageForAnimationHandler(ImportImageRequest importImage);
    // Fit the SceneView camera to the active scene's bounds. Needs the viewport
    // pixel rect, which only the application knows; it is workspace-only (camera
    // intents, no command). Unset makes the action a no-op.
    using WorkspaceRequest = std::function<void()>;
    void setFitViewHandler(WorkspaceRequest fitView);
    void setAnimationSliceHandler(WorkspaceRequest sliceAnimation);

    using EntityPlacementRequest = std::function<void()>;
    void setEntityPlacementHandlers(EntityPlacementRequest addEntity,
                                    EntityPlacementRequest addInstance,
                                    EntityPlacementRequest createEntityHere,
                                    EntityPlacementRequest createInstanceHere);

    void showViewportContextMenu(int physicalX, int physicalY, bool canCreateInstance);
    // Hierarchy context menu (scene tab / entity row "▾" affordance). The show is
    // deferred to processFrame: the application's same-frame outside-click check
    // would otherwise measure a menu whose layout is not resolved yet.
    void requestHierarchyContextMenu(HierarchyMenuKind kind, std::string targetId,
                                     int physicalX, int physicalY);
    // Hide / hit-test cover every open context menu (viewport + hierarchy).
    void hideContextMenus();
    bool isContextMenuHit(int physicalX, int physicalY) const;

    // Copies the selected Console message (full model text) to the clipboard via
    // raylib's SetClipboardText. The single entry point shared by the Copy button
    // and Ctrl+C. Returns false (no-op) when nothing is selected.
    bool copySelectedConsoleMessage();

    // F2 affordance for Scene Layer inline rename. Delegates to the Inspector's
    // single beginSceneLayerRename path; no document mutation happens here.
    void beginActiveSceneLayerRename();
    // Viewport drag preview for the selected entity transform. Presentation only:
    // the model still changes once, on mouse release, through SetEntityPositionCommand.
    void showEntityPositionPreview(EntityId entity, Vec2 position);
    // Pointer world-position readout in the toolbar (Edit mode, mouse over the
    // scene). Presentation-only per-frame update like the animation playhead:
    // change-guarded, no invalidation, never touches authoring state.
    void showPointerWorldPosition(const std::optional<Vec2>& worldPosition);

    // Called by the listener; routes one UI interaction to command/intent.
    void handleAction(const std::string& action, const std::string& arg,
                      const std::string& value);
    // Splitter drag: clamps via ResizePanelIntent and re-lays out the panel.
    void handleDrag(const std::string& action, float mouseX, float mouseY);

private:
    class Listener;   // defined in editor_ui.cpp

    void applyInvalidations(EditorInvalidation flags);
    void refreshSpriteAnimationEditor();
    // Per-frame, class-only sync of the preview transport and timeline playhead.
    // The playhead advances without invalidation, and a markup rebuild would
    // steal focus from the Name/FPS inputs, so this never touches innerRML.
    void updateSpriteAnimationPlayhead();
    void refreshToolbar();
    void updateZoomReadout();   // toolbar zoom %, refreshed on Viewport invalidation
    void commitGridCellSize(const std::string& text);
    void showPendingHierarchyMenu();   // consumes the deferred menu request

    EditorCoordinator&                  coordinator_;
    Rml::ElementDocument*               document_;
    Rml::ElementDocument*               animationDocument_;
    HierarchyPanel                      hierarchy_;
    InspectorPanel                      inspector_;
    ConsolePanel                        console_;
    AssetsPanel                         assets_;
    std::unique_ptr<Rml::EventListener> listener_;
    ProjectFileRequest                  newProjectRequest_;
    ProjectFileRequest                  openProjectRequest_;
    ProjectFileRequest                  saveProjectRequest_;
    ProjectFileRequest                  saveProjectAsRequest_;
    ImportAssetRequest                  importAssetRequest_;
    ImportImageRequest                  importImageForAnimationRequest_;
    EntityPlacementRequest              addEntityRequest_;
    EntityPlacementRequest              addInstanceRequest_;
    EntityPlacementRequest              createEntityHereRequest_;
    EntityPlacementRequest              createInstanceHereRequest_;
    WorkspaceRequest                    fitViewRequest_;
    WorkspaceRequest                    sliceAnimationRequest_;
    bool                                viewportContextMenuVisible_ = false;
    bool                                hierarchyContextMenuVisible_ = false;
    // Deferred hierarchy menu request (applied on the next processFrame).
    struct PendingHierarchyMenu {
        HierarchyMenuKind kind;
        std::string       targetId;
        int               x = 0;
        int               y = 0;
    };
    std::optional<PendingHierarchyMenu> pendingHierarchyMenu_;
    std::string                         pointerReadout_;   // last coords text shown
    std::string                         spriteAnimationEditorMarkup_;
    // Chips currently in the timeline; bounds the playhead class sweep.
    std::size_t                         spriteAnimationTimelineCount_ = 0;
};

} // namespace ArtCade::EditorNative
