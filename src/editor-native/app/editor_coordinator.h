#pragma once

#include "core/types.h"
#include "editor-native/app/command_stack.h"
#include "editor-native/commands/editor_command.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/editor_operation_result.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/model/editor_ui_state.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/script_source_stamp.h"

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

struct ConsoleMessage {
    enum class Level { Info, Warning, Error };
    struct ScriptSource {
        AssetId scriptAssetId;
        std::string path;
        int line = 0;
        int column = 0;
    };
    Level       level = Level::Info;
    std::string text;
    std::optional<ScriptSource> scriptSource;
};

// Runtime/editor navigation only: never serialized, undoable, or visible to
// PlaySession. It remembers the non-Scene workspace that initiated a test;
// Logic carries extra board context while Script state already remains alive
// in EditorState and needs only its workspace mode restored.
struct PlayNavigationState {
    CenterWorkspaceMode originWorkspace = CenterWorkspaceMode::Scene;
    std::optional<ObjectTypeId> originObjectTypeId;
    LogicBoardTab originLogicTab = LogicBoardTab::Rules;
    std::string originLogicSearch;
    bool autoSwitchedToScene = false;
    bool returnToOriginArmed = false;
};

enum class PlayLaunchKind { Project, CurrentScene };

// Exact launch target for the disposable runtime. Current Scene is pinned to
// the scene that was launched, so workspace navigation during Play cannot
// silently retarget Restart & Apply.
struct PlayLaunchState {
    PlayLaunchKind kind = PlayLaunchKind::Project;
    SceneId sceneId;
};

// Pure clipboard rendering of a console message. Copies the full, unabbreviated
// model text (not whatever the panel may truncate) prefixed with the level, so a
// shared error reads e.g. "[Error] Open failed: ...". Lives in editor-core (no
// raylib/RmlUi) and is unit-tested; the clipboard call itself stays in the UI.
std::string formatConsoleMessageForClipboard(const ConsoleMessage& message);

// =============================================================================
// EditorCoordinator — the one and only coordinator (prompt §5).
//
// Owns the ProjectDocument, EditorState, SelectionState, EditorUiState and per-scene view
// state. Executes commands, applies intents, accumulates explicit invalidation
// and exposes read-only queries to the panels. It draws nothing, contains no
// RML/RCSS, owns no renderer, and is not a service locator.
//
// Communication between panels passes through here:
//   Hierarchy click → apply(SelectEntityIntent) → SelectionState updated
//                   → invalidate Hierarchy | Inspector | Viewport
// =============================================================================
class EditorCoordinator {
public:
    EditorCoordinator() = default;
    explicit EditorCoordinator(ProjectDoc doc);

    // ---- queries -------------------------------------------------------------
    const ProjectDocument& document()  const { return document_; }
    const SelectionState&  selection() const { return state_.selection; }
    const EditorUiState&   uiState()   const { return uiState_; }
    const EditorState&     state()     const { return state_; }
    // The tool a tilemap gesture should actually use right now: the momentary
    // override (Eraser via right-click) if one is in progress, else the
    // user's own persistently selected tool. Never writes activeTool itself.
    EditorTool effectiveTilemapTool() const {
        return state_.tilemapEditor.temporaryToolOverride.value_or(state_.activeTool);
    }
    // Cancels any pending stroke/rectangle and drops a momentary tool override.
    // Returns true iff something was actually cancelled. The single shared
    // primitive for "a gesture in progress no longer applies" - called by a
    // selection change, the global Escape router (editor_app.cpp), and
    // tilemap_paint_input.cpp's own focus-loss handling. Workspace-only: no
    // dirty/revision/undo.
    bool cancelPendingTilemapGesture();
    const EditorSceneViewState& sceneView(const SceneId& id) const;
    // The layer authoring operations actually target: the workspace's own
    // activeLayerId if it's set and still resolves to a real layer, else the
    // scene's default layer. Empty if @p sceneId doesn't resolve. Mirrors the
    // normalization the Layers section UI already does when highlighting the
    // active row - the single source of truth both now share.
    std::string activeLayerId(const SceneId& sceneId) const;
    // Workspace-only: mark a scene's editor view as auto-fitted (one-time). Never
    // touches ProjectDocument, revision, dirty or history.
    void markSceneViewInitialized(const SceneId& id);
    const std::vector<ConsoleMessage>& consoleLog() const { return console_; }
    // Read-only lookup by index for the Console panel's clipboard copy. Returns
    // nullptr for nullopt or an out-of-range index, so a stale selection is safe.
    const ConsoleMessage* consoleMessage(std::optional<std::size_t> index) const;

    // ---- command path (authoring; undoable) ---------------------------------
    /** Run a command by value, e.g. execute(SetEntityTransformCommand{scene, id, patch}). */
    template <class CommandT>
    EditorOperationResult execute(CommandT command) {
        return executeOwned(std::make_unique<CommandT>(std::move(command)), nullptr);
    }

    template <class CommandT>
    EditorOperationResult executeWithSideEffect(
        CommandT command,
        std::unique_ptr<EditorCommandSideEffect> sideEffect) {
        return executeOwned(std::make_unique<CommandT>(std::move(command)),
                            std::move(sideEffect));
    }

    bool                  canUndo() const { return history_.canUndo(); }
    bool                  canRedo() const { return history_.canRedo(); }
    std::size_t           undoSize() const { return history_.size(); }
    std::size_t           redoSize() const { return history_.redoSize(); }
    EditorOperationResult undo();
    EditorOperationResult redo();
    EditorOperationResult replaceProject(ProjectDocument replacement);
    EditorOperationResult markProjectSaved();
    EditorCommandSideEffectResult validateCommandSideEffectRebase(
        const std::filesystem::path& previousRoot,
        const std::filesystem::path& nextRoot);
    void rebaseCommandSideEffects(const std::filesystem::path& previousRoot,
                                  const std::filesystem::path& nextRoot);

    // ---- Play / Stop (runtime session; the document is never mutated) --------
    // The two modes have distinct targets: Play Project uses the document's
    // start scene, Play Current Scene uses the editor's active scene. Each is
    // available only when its target identifies an existing scene; the guard
    // lives here, not only in the toolbar, so a shortcut, menu or programmatic
    // call cannot bypass it. A rejected Play mutates nothing and invalidates
    // nothing (no session, no revision, no dirty, no invalidation).
    bool isPlaying()            const { return playSession_.has_value(); }
    bool canPlayProject()       const;
    bool canPlayCurrentScene()  const;
    const PlaySession* playSession() const {
        return playSession_ ? &*playSession_ : nullptr;
    }
    EditorOperationResult playProject();
    EditorOperationResult playProject(const std::vector<Scripts::ScriptProgram>& scripts);
    EditorOperationResult playCurrentScene();
    EditorOperationResult playCurrentScene(const std::vector<Scripts::ScriptProgram>& scripts);
    EditorOperationResult restartPlaying(
        const std::vector<Scripts::ScriptProgram>& scripts);
    EditorOperationResult stopPlaying();
    bool scriptRestartRequired() const {
        return isPlaying() && !outdatedPlayScriptAssets_.empty();
    }

    // Runtime simulation step for the active Play session (authored motion).
    // No-op when not playing; never an EditorCommand, never touches the document.
    void advanceRuntime(float dt);

    // Input-driven runtime step (TopDownController). Same guarantees as
    // advanceRuntime; no-op when not playing.
    void updateRuntime(const RuntimeInputSnapshot& input, float dt);

    // Moves out every Play Sound request queued since the last call. The
    // caller (EditorApp) is responsible for actual playback (PlaySession
    // stays free of Raylib); returns empty when not playing.
    std::vector<RuntimeAudioCommand> drainAudioCommands();

    // Per-frame step of the Sprite Animation Editor clip preview. Workspace
    // state only (previewElapsed/previewFrameIndex), mirroring advanceRuntime:
    // never a Command, never the document, never revision/dirty/history. No-op
    // unless the editor is open with a playing preview on a non-empty clip.
    void advanceSpriteAnimationPreview(float dt);

    // ---- intent path (workspace/editor state) -------------------------------
    EditorOperationResult apply(const SelectEntityIntent& intent);
    EditorOperationResult apply(const CreateGeneratedSfxIntent& intent);
    EditorOperationResult apply(const DuplicateGeneratedSfxIntent& intent);
    EditorOperationResult apply(const RenameGeneratedSfxIntent& intent);
    EditorOperationResult apply(const UpdateGeneratedSfxRecipeIntent& intent);
    EditorOperationResult apply(const SelectSceneIntent& intent);
    EditorOperationResult apply(const SwitchCenterWorkspaceIntent& intent);
    EditorOperationResult apply(const OpenLogicBoardIntent& intent);
    EditorOperationResult apply(const SetLogicBoardTabIntent& intent);
    EditorOperationResult apply(const SetLogicBoardSearchIntent& intent);
    EditorOperationResult apply(const ChangeLogicTriggerTypeIntent& intent);
    EditorOperationResult apply(const AddLogicActionTypeIntent& intent);
    EditorOperationResult apply(const ChangeLogicActionTypeIntent& intent);
    EditorOperationResult apply(const AddLogicConditionTypeIntent& intent);
    EditorOperationResult apply(const ChangeLogicConditionTypeIntent& intent);
    EditorOperationResult apply(const OpenScriptBufferIntent& intent);
    EditorOperationResult apply(const ActivateScriptBufferIntent& intent);
    EditorOperationResult apply(const EditScriptBufferIntent& intent);
    EditorOperationResult apply(const SetScriptCursorIntent& intent);
    EditorOperationResult apply(const SetScriptEditorFocusIntent& intent);
    EditorOperationResult apply(const MarkScriptBufferSavedIntent& intent);
    EditorOperationResult apply(const CloseScriptBufferIntent& intent);
    EditorOperationResult apply(const UndoScriptBufferIntent& intent);
    EditorOperationResult apply(const RedoScriptBufferIntent& intent);
    EditorOperationResult apply(const SetScriptSearchIntent& intent);
    EditorOperationResult apply(const SetScriptDiagnosticsIntent& intent);
    EditorOperationResult apply(const SetViewportZoomIntent& intent);
    EditorOperationResult apply(const PanViewportIntent& intent);
    EditorOperationResult apply(const SetSceneGridVisibilityIntent& intent);
    EditorOperationResult apply(const SetSceneGridSnapEnabledIntent& intent);
    EditorOperationResult apply(const SetSceneGridCellSizeIntent& intent);
    EditorOperationResult apply(const OpenSpriteAnimationEditorIntent& intent);
    EditorOperationResult apply(const CloseSpriteAnimationEditorIntent& intent);
    EditorOperationResult apply(const SelectAnimationClipIntent& intent);
    EditorOperationResult apply(const SetAnimationSliceGridIntent& intent);
    EditorOperationResult apply(const SetSpriteSheetZoomIntent& intent);
    EditorOperationResult apply(const PanSpriteSheetIntent& intent);
    EditorOperationResult apply(const SetAnimationPreviewPlayingIntent& intent);
    EditorOperationResult apply(const SetAnimationPreviewFrameIntent& intent);
    EditorOperationResult apply(const StepAnimationPreviewIntent& intent);
    EditorOperationResult apply(const OpenTilesetEditorIntent& intent);
    EditorOperationResult apply(const CloseTilesetEditorIntent& intent);
    EditorOperationResult apply(const SetPendingTilesetSlicingIntent& intent);
    EditorOperationResult apply(const SetTilesetEditorZoomIntent& intent);
    EditorOperationResult apply(const PanTilesetEditorIntent& intent);
    EditorOperationResult apply(const SelectTilesetTileIntent& intent);
    EditorOperationResult apply(const SetHierarchyFilterIntent& intent);
    EditorOperationResult apply(const SetAssetFilterIntent& intent);
    EditorOperationResult apply(const SetConsoleFilterIntent& intent);
    EditorOperationResult apply(const SetConsoleShowInfoIntent& intent);
    EditorOperationResult apply(const SetConsoleShowWarningIntent& intent);
    EditorOperationResult apply(const SetConsoleShowErrorIntent& intent);
    EditorOperationResult apply(const SetActiveLayerIntent& intent);
    EditorOperationResult apply(const ToggleLayerEditorVisibilityIntent& intent);
    EditorOperationResult apply(const SetActiveToolIntent& intent);
    EditorOperationResult apply(const BeginTemporaryToolOverrideIntent& intent);
    EditorOperationResult apply(const EndTemporaryToolOverrideIntent& intent);
    EditorOperationResult apply(const BeginTilePaintStrokeIntent& intent);
    EditorOperationResult apply(const UpdateTilePaintStrokeIntent& intent);
    EditorOperationResult apply(const EndTilePaintStrokeIntent& intent);
    EditorOperationResult apply(const CancelTilePaintStrokeIntent& intent);
    EditorOperationResult apply(const SelectPaintTileIntent& intent);
    EditorOperationResult apply(const SetHoveredTilemapCellIntent& intent);
    EditorOperationResult apply(const SetRectangleShapeModeIntent& intent);
    EditorOperationResult apply(const BeginTileRectangleIntent& intent);
    EditorOperationResult apply(const UpdateTileRectangleIntent& intent);
    EditorOperationResult apply(const CommitTileRectangleIntent& intent);
    EditorOperationResult apply(const CancelTileRectangleIntent& intent);
    EditorOperationResult apply(const FillTilemapIntent& intent);
    EditorOperationResult apply(const ToggleConsoleIntent& intent);
    EditorOperationResult apply(const RevealInspectorPropertyIntent& intent);
    EditorOperationResult apply(const ResizePanelIntent& intent);

    // Consumes a pending Inspector navigation request (one-shot).
    std::optional<InspectorRevealRequest> takeInspectorRevealRequest();

    // ---- console -------------------------------------------------------------
    void logInfo(std::string text);
    void logWarning(std::string text);
    void logError(std::string text);
    // Saved-source diagnostics produced at the application Play boundary.
    // Derived console state only; never mutates ProjectDocument or Undo.
    void reportScriptDiagnostics(const AssetId& scriptAssetId,
                                 const std::vector<ScriptDiagnostic>& diagnostics);
    // Diagnostic output only: not authoring, not undoable. Same category as
    // logInfo/logWarning/logError above, so clearing it is a direct mutator
    // rather than a Command or Intent.
    void clearConsole();

    // ---- frame ---------------------------------------------------------------
    /** Returns the accumulated invalidation and clears it (once per frame). */
    EditorInvalidation consumeInvalidations();
    EditorInvalidation pendingInvalidations() const { return pending_; }

private:
    EditorOperationResult executeOwned(
        std::unique_ptr<EditorCommand> command,
        std::unique_ptr<EditorCommandSideEffect> sideEffect);
    // Workspace-only apply(SomeIntent) overloads funnel their result through
    // here. Authoring intents delegate to executeOwned() through one concrete
    // Command, which already reports rejected mutations as errors.
    EditorOperationResult finishIntent(EditorOperationResult result);
    void accumulate(EditorInvalidation invalidation) { pending_ |= invalidation; }
    void appendConsole(ConsoleMessage::Level level, std::string text);
    void replaceScriptDiagnostics(const AssetId& scriptAssetId,
                                  const std::vector<ScriptDiagnostic>& diagnostics);
    void recordAppliedScriptSources(
        const std::vector<Scripts::ScriptProgram>& scripts);

    // After a structural command (or its undo) mutates the document, the
    // workspace may reference a scene or entity that no longer exists. This
    // brings EditorState back to a valid state in the same operation — it
    // normalizes the active scene, clears a dangling selection and prunes
    // per-scene view state — and returns the extra invalidation that change
    // implies. It restores the document's validity in the workspace, never the
    // UI history: an undone delete does not re-select what it brings back.
    EditorInvalidation reconcileWorkspace();

    // Wraps reconcileWorkspace() for the three command-path call sites
    // (executeOwned/undo/redo): accumulates its invalidation as before, then
    // — kept out of reconcileWorkspace() itself, since that is infrastructural
    // and also runs on paths (load, replace, deletes) where an active-layer
    // change is not a "move" worth announcing — compares the active layer
    // before/after and, if an entity is selected and the layer genuinely
    // changed to a hidden one, posts a single edge-triggered Info message.
    void reconcileWorkspaceAndAnnounce();

    // True iff the current selection is a live instance, in the active scene,
    // whose tilemap is genuinely editable right now (has a TilemapComponent,
    // sits on the active layer, and that layer isn't locked). Pure query - no
    // mutation, callable freely.
    bool selectionSupportsTilemapTool() const;
    // Mutable, idempotent reconciliation of tool/gesture/tile-selection state
    // against selectionSupportsTilemapTool() - NOT a pure function. When the
    // selection no longer supports a tilemap tool: cancels any pending
    // gesture, clears selectedTileId, and falls the active tool back to
    // Select if it was a tilemap tool (Select/Pan are left untouched). When it
    // does, only reconciles selectedTileId (see reconcileSelectedTileAgainstTileset).
    // Called from reconcileWorkspace() (covers every Command) and directly
    // from the selection/tool-changing Intents (which never go through
    // reconcileWorkspace()). Workspace-only: no dirty/revision/undo.
    void reconcileTilemapEditingContext();
    // Sub-step of reconcileTilemapEditingContext()'s "editable" branch: if
    // selectedTileId doesn't name a tile in the selected instance's own
    // tileset (e.g. after switching to a different tilemap, or the tileset
    // was resliced/changed), clear it. Never implicitly selects a substitute -
    // an unset tile forces the user to pick one before Brush can paint again,
    // rather than silently painting with a tile they didn't choose.
    void reconcileSelectedTileAgainstTileset();

    ProjectDocument                                  document_;
    EditorState                                      state_;
    EditorUiState                                    uiState_;
    EditorSceneViewState                             defaultSceneView_{};
    CommandStack                                     history_;
    std::vector<ConsoleMessage>                      console_;
    std::optional<PlaySession>                       playSession_;
    std::optional<PlayNavigationState>               playNavigation_;
    std::optional<PlayLaunchState>                   playLaunch_;
    // Play-scoped comparison data, not saved-source authority: fingerprints of
    // bytes already materialized by the active runtime plus the linked assets
    // whose subsequently saved bytes differ. Cleared on Stop/new launch.
    std::unordered_map<AssetId, ScriptSourceStamp>   appliedPlayScriptSources_;
    std::unordered_set<AssetId>                      outdatedPlayScriptAssets_;
    EditorInvalidation                               pending_ = EditorInvalidation::None;
};

} // namespace ArtCade::EditorNative
