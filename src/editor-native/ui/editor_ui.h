#pragma once

#include "core/types.h"
#include "editor-native/commands/domain_change.h"
#include "editor-native/commands/editor_invalidation.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/app/sfx_batch.h"
#include "editor-native/ui/assets_panel.h"
#include "editor-native/ui/console_panel.h"
#include "editor-native/ui/generated_sfx_editor_controller.h"
#include "editor-native/ui/hierarchy_panel.h"
#include "editor-native/ui/inspector_panel.h"
#include "editor-native/ui/tile_palette_dock_panel.h"
#include "editor-native/ui/logic_board_editor_controller.h"
#include "editor-native/ui/script_editor_controller.h"
#include "editor-native/ui/sprite_animation_editor_controller.h"
#include "editor-native/ui/tileset_editor_controller.h"
#include "editor-native/ui/ui_markup.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace Rml { class ElementDocument; class EventListener; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
enum class AssetKind;   // defined in app/asset_import.h
struct ViewportPointerReadout;

// Target kind of the hierarchy context menu (scene tab vs entity row).
enum class HierarchyMenuKind { Scene, Entity };

// Target kind of the Assets row menu ("⌄" affordance on an asset row).
enum class AssetMenuKind { Image, Animation, Tileset, GeneratedSfx, Audio, Font, Script };
// Parses the kind tag carried by open-asset-menu args ("image", "anim", ...).
std::optional<AssetMenuKind> parseAssetMenuKind(const std::string& tag);

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
    // Called after the host's one RmlUi Context::Update() for the frame. Some
    // presentation-only state (currently Logic Board scroll) needs final
    // layout metrics and must not trigger a second update.
    void restoreAfterRmlLayout();
    bool isBound() const { return listener_ != nullptr; }

    // Commit the focused data-action="commit-*" field through its normal blur
    // path before Save/New/Open/Close/Play or selection navigation can inspect
    // stale document state. Invalid/incomplete buffers retain focus and block.
    PendingEditResult resolvePendingEdits();

    bool isPlaying() const;
    // ADR-0004: native input router calls these before global shortcuts while
    // a Logic Board key field is armed. They never store platform key codes.
    bool hasLogicKeyCapture() const;
    bool captureLogicKey(LogicKey key);
    bool cancelLogicKeyCapture();

    // Project file operations live in the application layer (it owns the texture
    // cache it must clear on replace, and the platform file pickers). The UI
    // only triggers them; it never touches files or the renderer. Unset handlers
    // make the corresponding toolbar action a no-op.
    using ProjectFileRequest = std::function<void()>;
    void setProjectFileHandlers(ProjectFileRequest newProject,
                                ProjectFileRequest open,
                                ProjectFileRequest save,
                                ProjectFileRequest saveAs);
    void setPlayHandlers(ProjectFileRequest playProject,
                         ProjectFileRequest playCurrentScene);
    // Import copies a file into the project via the canonical importAsset
    // pipeline; it needs the filesystem and a saved project, so it lives in the
    // application. Every kind converges on this one handler.
    using ImportAssetRequest = std::function<void(AssetKind)>;
    void setImportHandler(ImportAssetRequest importAsset);
    void setCreateScriptHandler(ProjectFileRequest createScript);
    using ScriptAssetRequest = std::function<void(const AssetId&)>;
    using ScriptCloseRequest = std::function<void(const AssetId&)>;
    void setRemoveScriptHandler(ScriptAssetRequest removeScript);
    void setScriptEditorHandlers(ScriptAssetRequest openScript,
                                 ScriptAssetRequest saveScript,
                                 ProjectFileRequest saveAllScripts,
                                 ScriptCloseRequest closeScript,
                                 ProjectFileRequest restartAndApplyScripts);
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
    // slicing dirty -> themed Apply/Discard/Cancel confirm) because only it
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

    using GeneratedSfxRequest = GeneratedSfxEditorController::GeneratedSfxRequest;
    void setGeneratedSfxHandlers(GeneratedSfxRequest preview,
                                 WorkspaceRequest stopPreview,
                                 GeneratedSfxRequest generate);
    void setGeneratedSfxDiagnosticHandler(GeneratedSfxRequest dismissDiagnostic);
    using GeneratedSfxCreateFromCurrentRequest =
        GeneratedSfxEditorController::CreateFromCurrentRequest;
    void setGeneratedSfxCreateFromCurrentHandler(
        GeneratedSfxCreateFromCurrentRequest request);
    using GeneratedSfxDeleteRequest = GeneratedSfxEditorController::DeleteRequest;
    void setGeneratedSfxDeleteHandler(GeneratedSfxDeleteRequest request);
    void setSfxBatchHandlers(WorkspaceRequest regenerateAllStale,
                             WorkspaceRequest cancelBatch,
                             WorkspaceRequest dismissSummary);
    void setSfxBatchState(SfxBatchState state);
    // Generate writes into the project's canonical generated-audio folder,
    // which requires a saved project path -- a constraint the application
    // layer already enforces (it logs a Console error and no-ops otherwise).
    // Surfacing it here too means the SFX panel's own status line explains
    // the constraint before the click, not only after it via a Console
    // message the user has to go looking for.
    using ProjectSavedQuery = std::function<bool()>;
    void setProjectSavedQuery(ProjectSavedQuery query);
    using GeneratedSfxGenerationAvailability =
        ArtCade::EditorNative::GeneratedSfxGenerationAvailability;
    using GeneratedSfxGenerationAvailabilityQuery =
        std::function<GeneratedSfxGenerationAvailability(const std::string&)>;
    void setGeneratedSfxGenerationAvailabilityQuery(
        GeneratedSfxGenerationAvailabilityQuery query);
    using GeneratedSfxStatusQuery = GeneratedSfxEditorController::StatusQuery;
    void setGeneratedSfxStatusQuery(GeneratedSfxStatusQuery query);
    // Called by the application right after RegisterGeneratedSfxOutputCommand
    // commits. Shows a one-shot "Audio asset generated" confirmation until
    // the next SFX panel interaction of any kind.
    void notifyGeneratedSfxOutputReady(const std::string& id);
    void notifyGeneratedSfxStatusChanged();
    void validateSfxCreateFromCurrentName(const std::string& value);
    void confirmSfxCreateFromCurrent(const std::string& value);
    void closeSfxCreateFromCurrentDialog();

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
    bool hasOpenContextMenu() const;

    // Shared themed confirm modal (UI-local; Escape → Cancel).
    enum class ConfirmChoice { Cancel, Secondary, Primary };
    using ConfirmResultHandler = std::function<void(ConfirmChoice)>;
    using UnsavedResultHandler = std::function<void(UnsavedChoice)>;
    using BoolResultHandler = std::function<void(bool confirmed)>;
    bool hasOpenConfirm() const;
    void cancelConfirm();   // Escape / dismiss → Cancel continuation
    // Returns false if a confirm is already open (caller should not proceed).
    bool openConfirm(std::string title, std::string copy, std::string hint,
                     std::string secondaryLabel, std::string primaryLabel,
                     std::string primaryClass, ConfirmResultHandler onResult);
    bool promptSaveDiscardCancel(std::string title, std::string copy, std::string hint,
                                 UnsavedResultHandler onResult,
                                 std::string discardLabel = "Don't Save",
                                 std::string saveLabel = "Save");
    bool promptDangerConfirm(std::string title, std::string copy, std::string hint,
                             std::string primaryLabel, BoolResultHandler onResult);
    bool promptWarningConfirm(std::string title, std::string copy, std::string hint,
                              std::string primaryLabel, BoolResultHandler onResult);

    // Copies the selected Console message (full model text) to the clipboard via
    // raylib's SetClipboardText. The single entry point shared by the Copy button
    // and Ctrl+C. Returns false (no-op) when nothing is selected.
    bool copySelectedConsoleMessage();

    // F2 affordance for Scene Layer inline rename. Delegates to the Inspector's
    // single beginSceneLayerRename path; no document mutation happens here.
    void beginActiveSceneLayerRename();
    // Viewport drag preview for the selected entity transform. Presentation only:
    // the model still changes once, on mouse release, through SetEntityTransformCommand.
    void showEntityPositionPreview(EntityId entity, Vec2 position);
    // Pointer world/cell readout in the status bar (Edit mode, mouse over the
    // scene). Presentation-only per-frame update: change-guarded, no
    // invalidation, never touches authoring state.
    void showViewportPointerReadout(const ViewportPointerReadout& readout);
    // Called once per frame by the application (same push pattern as
    // showViewportPointerReadout): the header's Stop button lives in static
    // RML outside the invalidation-driven body, and playback can end on its
    // own (the clip finishes) without any action passing through here, so it
    // can't be a plain refreshGeneratedSfxEditor()-on-invalidation update.
    void syncGeneratedSfxPreviewPlaying(bool playing);

    // Called by the listener; routes one UI interaction to command/intent.
    void handleAction(const std::string& action, const std::string& arg,
                      const std::string& value);
    // Splitter drag: clamps via ResizePanelIntent and re-lays out the panel.
    void handleDrag(const std::string& action, float mouseX, float mouseY);
    // Generated SFX macro slider drag lifecycle. A macro edit is an undoable
    // recipe field (unlike a splitter's panel width), so unlike handleDrag it
    // must not submit a Command on every "change" tick during a drag -- only
    // once, on release. See editor_ui.cpp's Listener for the dragstart/change/
    // dragend discipline this backs.
    void beginSfxMacroDrag(const std::string& macroId);
    // "change" fires on every drag tick, a plain click-to-position, and a
    // keyboard arrow nudge alike. While a drag session is open for this
    // macroId, this only updates the live preview (no Command); otherwise
    // (click/keyboard, no session open) it commits immediately, same as
    // every other field in this editor.
    void handleSfxMacroChange(const std::string& macroId, float value);
    void commitSfxMacroDrag();

    // Static textarea event bridge. Text remains authoritative in
    // ScriptEditorState; these calls never create EditorCommands.
    void handleScriptTextChanged(const std::string& value);
    void handleScriptCursorChanged();
    void setScriptEditorFocused(bool focused);
    void handleScriptEditorShortcut(int key, bool control, bool shift, bool alt = false);

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
    void refreshGeneratedSfxEditor();
    // Modal actions may still be inside RmlUi event dispatch. Rebuild their
    // generated markup from processFrame, after the target element is released.
    void deferGeneratedSfxRefresh();
    bool handleGeneratedSfxAction(const std::string& action, const std::string& arg,
                                  const std::string& value);
    void commitGridCellSize(const std::string& text);
    void showPendingHierarchyMenu();   // consumes the deferred menu request
    void showPendingAssetMenu();       // same, for the Assets row menu
    // Logic Board's Object Type picker: a floating menu (like the other
    // context menus) positioned off the trigger's own on-screen box, rather
    // than an in-flow list — .logic-head is never inside a scrollable
    // ancestor, so there is no clipping risk to justify pushing the board
    // down every time it opens, the way the in-flow pattern otherwise would.
    void toggleLogicTypeMenu();
    // Logic Board's "..." menu (Remove Logic Board). Same floating-menu
    // mechanism as toggleLogicTypeMenu, but the menu content is static (one
    // entry), so there's nothing to stamp in on open.
    void toggleLogicMoreMenu();
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
    ScriptEditorController              scriptEditor_;
    GeneratedSfxEditorController        generatedSfxEditor_;
    HierarchyPanel                      hierarchy_;
    InspectorPanel                      inspector_;
    TilePaletteDockPanel                tilePaletteDock_;
    ConsolePanel                        console_;
    AssetsPanel                         assets_;
    std::unique_ptr<Rml::EventListener> listener_;
    ProjectFileRequest                  newProjectRequest_;
    ProjectFileRequest                  openProjectRequest_;
    ProjectFileRequest                  saveProjectRequest_;
    ProjectFileRequest                  saveProjectAsRequest_;
    ProjectFileRequest                  playProjectRequest_;
    ProjectFileRequest                  playCurrentSceneRequest_;
    ImportAssetRequest                  importAssetRequest_;
    ProjectFileRequest                  createScriptRequest_;
    ScriptAssetRequest                  removeScriptRequest_;
    ScriptAssetRequest                  openScriptRequest_;
    ScriptAssetRequest                  saveScriptRequest_;
    WorkspaceRequest                    saveAllScriptsRequest_;
    ScriptCloseRequest                  closeScriptRequest_;
    ProjectFileRequest                  restartScriptsRequest_;
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
    bool                                logicMoreMenuVisible_ = false;
    struct PendingEditorConfirm {
        std::string title;
        std::string copy;
        std::string hint;
        std::string secondaryLabel;   // empty → two-button layout
        std::string primaryLabel;
        std::string primaryClass;     // "primary" | "danger"
        ConfirmResultHandler onResult;
    };
    std::optional<PendingEditorConfirm> pendingConfirm_;
    void refreshConfirmModal();
    bool handleConfirmAction(const std::string& action);
    // ADR-0013 — remove controller still referenced by Logic (three-button confirm).
    void openComponentLogicRemovePrompt(ComponentKind kind, ObjectTypeId objectTypeId,
                                        std::string displayName,
                                        std::size_t actionCount, std::size_t conditionCount,
                                        std::size_t triggerCount, LogicRuleId focusRuleId,
                                        std::string focusBlockTypeId);
    std::string                         pointerReadout_;   // last coords text shown
    // Modal RML rebuild is deferred until the active event dispatch ends.
    bool                                generatedSfxRefreshPending_ = false;
};

} // namespace ArtCade::EditorNative
