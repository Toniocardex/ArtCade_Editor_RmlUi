#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_invalidation.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/ui/assets_panel.h"
#include "editor-native/ui/console_panel.h"
#include "editor-native/ui/hierarchy_panel.h"
#include "editor-native/ui/inspector_panel.h"
#include "editor-native/ui/logic_board_editor_controller.h"
#include "editor-native/ui/sprite_animation_editor_controller.h"
#include "editor-native/ui/tileset_editor_controller.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Rml { class ElementDocument; class EventListener; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
enum class AssetKind;   // defined in app/asset_import.h
struct ViewportPointerReadout;

// Target kind of the hierarchy context menu (scene tab vs entity row).
enum class HierarchyMenuKind { Scene, Entity };

// Target kind of the Assets row menu ("⌄" affordance on an asset row).
enum class AssetMenuKind { Image, Animation, Tileset, Audio, Font };
// Parses the kind tag carried by open-asset-menu args ("image", "anim", ...).
std::optional<AssetMenuKind> parseAssetMenuKind(const std::string& tag);

/** Escape &, <, > so authored names are safe inside generated RML. */
std::string escapeRml(const std::string& text);

// Presentation name of an asset: the authored name (id fallback) minus the
// ".anim"/".tileset" suffix that generated ids historically embed — group
// titles and kind icons already say the kind, so the suffix is display noise.
// Display only: ids, args and the document are never touched.
std::string assetDisplayName(const std::string& name, const std::string& assetId);

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
             Rml::ElementDocument* animationDocument,
             Rml::ElementDocument* tilesetDocument);
    ~EditorUi();

    void bind();           // attach the listener + do the initial full refresh
    void detach();         // remove listeners and invalidate observed documents; idempotent
    void processFrame();   // consume invalidations and refresh affected panels
    bool isBound() const { return listener_ != nullptr; }

    // Commit the focused data-action="commit-*" field through its normal blur
    // path before Save/New/Open/Close/Play or selection navigation can inspect
    // stale document state. Invalid/incomplete buffers retain focus and block.
    PendingEditResult resolvePendingEdits();

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
    // Apply the pending tileset slicing. Needs the source image's real pixel
    // dimensions (TextureCache), which only the application knows; computes
    // tilesForSlicing + reconcileTiles and executes ChangeTilesetSlicingCommand.
    void setTilesetApplySlicingHandler(WorkspaceRequest applyTilesetSlicing);
    // Close the Tileset Editor. The application owns the guard (pending
    // slicing dirty -> native Apply/Discard/Cancel prompt) because only it
    // can run the apply flow; unset falls back to closing unconditionally.
    void setTilesetCloseHandler(WorkspaceRequest closeTileset);
    // Create a tileset from an image asset. The application slices the
    // default grid at creation (it has the image's pixel dimensions via
    // TextureCache); unset falls back to creating with no tiles.
    using CreateTilesetRequest = std::function<void(const AssetId&)>;
    void setCreateTilesetFromImageHandler(CreateTilesetRequest createTileset);
    // Read-only projection of an image asset's pixel size (TextureCache is
    // application-owned). Feeds the Tileset Editor's inline slicing feedback
    // ("4 x 3 = 12 tiles, covers the whole sheet"); the core still revalidates
    // on Apply - this is presentation only, never a second authority. nullopt
    // = not loaded; the UI shows a neutral placeholder.
    using ImageSizeProvider =
        std::function<std::optional<std::pair<int, int>>(const AssetId&)>;
    void setTilesetImageSizeProvider(ImageSizeProvider imageSize);

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
    // Assets row menu ("⌄" on an asset row); same deferred-show pattern.
    void requestAssetContextMenu(AssetMenuKind kind, std::string assetId,
                                 int physicalX, int physicalY);
    // Hide / hit-test cover every open context menu (viewport + hierarchy + assets).
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
    // Pointer world/cell readout in the status bar (Edit mode, mouse over the
    // scene). Presentation-only per-frame update: change-guarded, no
    // invalidation, never touches authoring state.
    void showViewportPointerReadout(const ViewportPointerReadout& readout);

    // Called by the listener; routes one UI interaction to command/intent.
    void handleAction(const std::string& action, const std::string& arg,
                      const std::string& value);
    // Splitter drag: clamps via ResizePanelIntent and re-lays out the panel.
    void handleDrag(const std::string& action, float mouseX, float mouseY);

private:
    class Listener;   // defined in editor_ui.cpp

    // handleAction() dispatch, split by domain (one else-if chain per group,
    // moved verbatim out of the single ~470-line function it used to be).
    // Each returns whether it recognized the action; handleAction() tries
    // them in turn and returns on the first match, same as the flat chain
    // did. Only the two domains that read the current selection take it as
    // a parameter instead of re-reading coordinator_.selection() themselves.
    bool handleProjectFileAction(const std::string& action, const std::string& arg,
                                 const std::string& value);
    bool handleConsoleAction(const std::string& action, const std::string& arg,
                             const std::string& value);
    bool handleAssetsAction(const std::string& action, const std::string& arg,
                            const std::string& value);
    bool handleToolbarAction(const std::string& action, const std::string& arg,
                             const std::string& value);
    bool handleHierarchyAction(const std::string& action, const std::string& arg,
                               const std::string& value, EntityId selected);
    bool handleInspectorAction(const std::string& action, const std::string& arg,
                               const std::string& value, EntityId selected);

    void applyInvalidations(EditorInvalidation flags);
    void refreshToolbar();
    void refreshStatusBar();
    void refreshCenterWorkspace();
    void updateZoomReadout();   // toolbar zoom %, refreshed on Viewport invalidation
    void commitGridCellSize(const std::string& text);
    void showPendingHierarchyMenu();   // consumes the deferred menu request
    void showPendingAssetMenu();       // same, for the Assets row menu
    // Logic Board's Object Type picker: a floating menu (like the other
    // context menus) positioned off the trigger's own on-screen box, rather
    // than an in-flow list — .logic-head is never inside a scrollable
    // ancestor, so there is no clipping risk to justify pushing the board
    // down every time it opens, the way the in-flow pattern otherwise would.
    void toggleLogicTypeMenu();
    // Applies EditorUiState.consoleVisible to the actual panel (Layout invalidation).
    void refreshLayout();
    // The scene the Scene View camera (zoom/pan) is currently showing: the
    // PlaySession's scene while playing (it can differ from the workspace's
    // activeSceneId if the user navigated Hierarchy meanwhile), otherwise the
    // workspace's own active scene. One shared definition so every zoom/pan
    // read agrees on "which scene" — a previous copy of this ternary omitted
    // the Play case and drifted from the other three.
    SceneId currentViewSceneId() const;

    EditorCoordinator&                  coordinator_;
    Rml::ElementDocument*               document_;
    Rml::ElementDocument*               animationDocument_;
    Rml::ElementDocument*               tilesetDocument_;
    SpriteAnimationEditorController     spriteAnimationEditor_;
    TilesetEditorController             tilesetEditor_;
    LogicBoardEditorController          logicBoardEditor_;
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
    EntityPlacementRequest              addEntityRequest_;
    EntityPlacementRequest              addInstanceRequest_;
    EntityPlacementRequest              createEntityHereRequest_;
    EntityPlacementRequest              createInstanceHereRequest_;
    WorkspaceRequest                    fitViewRequest_;
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
    bool                                assetsContextMenuVisible_ = false;
    // Deferred Assets row menu request (applied on the next processFrame).
    struct PendingAssetMenu {
        AssetMenuKind kind;
        std::string   assetId;
        int           x = 0;
        int           y = 0;
    };
    std::optional<PendingAssetMenu>     pendingAssetMenu_;
    bool                                logicTypeMenuVisible_ = false;
    std::string                         pointerReadout_;   // last coords text shown
};

} // namespace ArtCade::EditorNative
