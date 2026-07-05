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

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

struct ConsoleMessage {
    enum class Level { Info, Warning, Error };
    Level       level = Level::Info;
    std::string text;
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
    const EditorSceneViewState& sceneView(const SceneId& id) const;
    // Workspace-only: mark a scene's editor view as auto-fitted (one-time). Never
    // touches ProjectDocument, revision, dirty or history.
    void markSceneViewInitialized(const SceneId& id);
    const std::vector<ConsoleMessage>& consoleLog() const { return console_; }
    // Read-only lookup by index for the Console panel's clipboard copy. Returns
    // nullptr for nullopt or an out-of-range index, so a stale selection is safe.
    const ConsoleMessage* consoleMessage(std::optional<std::size_t> index) const;

    // ---- command path (authoring; undoable) ---------------------------------
    /** Run a command by value, e.g. execute(SetEntityPositionCommand{scene, id, pos}). */
    template <class CommandT>
    EditorOperationResult execute(CommandT command) {
        return executeOwned(std::make_unique<CommandT>(std::move(command)));
    }

    bool                  canUndo() const { return history_.canUndo(); }
    bool                  canRedo() const { return history_.canRedo(); }
    std::size_t           undoSize() const { return history_.size(); }
    std::size_t           redoSize() const { return history_.redoSize(); }
    EditorOperationResult undo();
    EditorOperationResult redo();
    EditorOperationResult replaceProject(ProjectDocument replacement);
    EditorOperationResult markProjectSaved();

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
    EditorOperationResult playCurrentScene();
    EditorOperationResult stopPlaying();

    // Runtime simulation step for the active Play session (authored motion).
    // No-op when not playing; never an EditorCommand, never touches the document.
    void advanceRuntime(float dt);

    // Input-driven runtime step (TopDownController). Same guarantees as
    // advanceRuntime; no-op when not playing.
    void updateRuntime(const RuntimeInputSnapshot& input, float dt);

    // Per-frame step of the Sprite Animation Editor clip preview. Workspace
    // state only (previewElapsed/previewFrameIndex), mirroring advanceRuntime:
    // never a Command, never the document, never revision/dirty/history. No-op
    // unless the editor is open with a playing preview on a non-empty clip.
    void advanceSpriteAnimationPreview(float dt);

    // ---- intent path (workspace/editor state) -------------------------------
    EditorOperationResult apply(const SelectEntityIntent& intent);
    EditorOperationResult apply(const SelectSceneIntent& intent);
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
    EditorOperationResult apply(const SetHierarchyFilterIntent& intent);
    EditorOperationResult apply(const SetConsoleFilterIntent& intent);
    EditorOperationResult apply(const SetConsoleShowInfoIntent& intent);
    EditorOperationResult apply(const SetConsoleShowWarningIntent& intent);
    EditorOperationResult apply(const SetConsoleShowErrorIntent& intent);
    EditorOperationResult apply(const SetActiveLayerIntent& intent);
    EditorOperationResult apply(const ToggleLayerEditorVisibilityIntent& intent);
    EditorOperationResult apply(const SetActiveToolIntent& intent);
    EditorOperationResult apply(const ToggleConsoleIntent& intent);
    EditorOperationResult apply(const ResizePanelIntent& intent);

    // ---- console -------------------------------------------------------------
    void logInfo(std::string text);
    void logWarning(std::string text);
    void logError(std::string text);
    // Diagnostic output only: not authoring, not undoable. Same category as
    // logInfo/logWarning/logError above, so clearing it is a direct mutator
    // rather than a Command or Intent.
    void clearConsole();

    // ---- frame ---------------------------------------------------------------
    /** Returns the accumulated invalidation and clears it (once per frame). */
    EditorInvalidation consumeInvalidations();
    EditorInvalidation pendingInvalidations() const { return pending_; }

private:
    EditorOperationResult executeOwned(std::unique_ptr<EditorCommand> command);
    // Every apply(SomeIntent) overload funnels its result through here before
    // returning, exactly as executeOwned() does for commands: a rejected
    // intent is still a real error and must be visible (contract: "ogni
    // errore deve essere... non silenzioso"), not just a return value the
    // caller happens to discard. Warning, not Error - an intent is workspace
    // -only, never a rejected authoring mutation like a failed command.
    EditorOperationResult finishIntent(EditorOperationResult result);
    void accumulate(EditorInvalidation invalidation) { pending_ |= invalidation; }
    void appendConsole(ConsoleMessage::Level level, std::string text);

    // After a structural command (or its undo) mutates the document, the
    // workspace may reference a scene or entity that no longer exists. This
    // brings EditorState back to a valid state in the same operation — it
    // normalizes the active scene, clears a dangling selection and prunes
    // per-scene view state — and returns the extra invalidation that change
    // implies. It restores the document's validity in the workspace, never the
    // UI history: an undone delete does not re-select what it brings back.
    EditorInvalidation reconcileWorkspace();

    ProjectDocument                                  document_;
    EditorState                                      state_;
    EditorUiState                                    uiState_;
    EditorSceneViewState                             defaultSceneView_{};
    CommandStack                                     history_;
    std::vector<ConsoleMessage>                      console_;
    std::optional<PlaySession>                       playSession_;
    EditorInvalidation                               pending_ = EditorInvalidation::None;
};

} // namespace ArtCade::EditorNative
