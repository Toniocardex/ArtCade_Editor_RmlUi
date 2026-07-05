#include "editor-native/app/editor_coordinator.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// Selecting an entity refreshes the tree highlight, the inspector contents and
// the viewport gizmo — nothing else.
constexpr EditorInvalidation kSelectionInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;

// A scene change additionally refreshes the toolbar (scene name / play state).
constexpr EditorInvalidation kSceneChangeInvalidation =
    kSelectionInvalidation | EditorInvalidation::Toolbar;

constexpr EditorInvalidation kProjectReplaceInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport | EditorInvalidation::Assets
    | EditorInvalidation::Toolbar | EditorInvalidation::Project;

// Start/Stop Play re-renders every panel whose controls must freeze (disabled)
// while Play runs and re-enable on Stop: Toolbar (play/undo/redo), Inspector,
// Hierarchy (create/delete) and Assets (import/remove). Viewport switches between
// the authoring projection and the Play snapshot.
constexpr EditorInvalidation kPlayToggleInvalidation =
    EditorInvalidation::Toolbar | EditorInvalidation::Viewport
    | EditorInvalidation::Inspector | EditorInvalidation::Hierarchy
    | EditorInvalidation::Assets;

SceneId normalizedSceneId(const ProjectDocument& document) {
    if (!document.startSceneId().empty() && document.hasScene(document.startSceneId())) {
        return document.startSceneId();
    }
    if (!document.data().scenes.empty()) {
        return document.data().scenes.begin()->first;
    }
    return {};
}
} // namespace

EditorCoordinator::EditorCoordinator(ProjectDoc doc)
    : document_(std::move(doc)) {
    state_.activeSceneId = document_.startSceneId();
    if (state_.activeSceneId.empty() && !document_.data().scenes.empty()) {
        state_.activeSceneId = document_.data().scenes.begin()->first;
    }
}

const EditorSceneViewState& EditorCoordinator::sceneView(const SceneId& id) const {
    const auto it = state_.sceneViews.find(id);
    return it == state_.sceneViews.end() ? defaultSceneView_ : it->second;
}

void EditorCoordinator::markSceneViewInitialized(const SceneId& id) {
    state_.sceneViews[id].initialized = true;   // workspace only; no dirty/invalidation
}

// ----------------------------------------------------------------------------
// Command path
// ----------------------------------------------------------------------------
EditorOperationResult EditorCoordinator::executeOwned(
    std::unique_ptr<EditorCommand> command) {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Stop Play before editing the authoring document");
        return EditorOperationResult::failure("Cannot edit project while Play is running");
    }

    // The document revision is the authoritative mutation signal: a command
    // changed the project iff the revision moved. The asserts pin the command
    // contract in debug builds; the revision comparison drives behaviour in all.
    const uint64_t revisionBefore = document_.revision();
    EditorOperationResult result = command->apply(document_);
    const uint64_t revisionAfter = document_.revision();

    if (!result.ok) {
        // A failed command must not mutate the document.
        assert(revisionAfter == revisionBefore && "failed command mutated the document");
        appendConsole(ConsoleMessage::Level::Error, result.error);
        return result;
    }

    if (revisionAfter == revisionBefore) {
        // A no-op must declare neither a change nor an invalidation, and is not
        // recorded (so it cannot be undone).
        assert(result.change.isNone() && "no-op command reported a DomainChange");
        assert(result.invalidation == EditorInvalidation::None
               && "no-op command reported an invalidation");
        return result;
    }

    // A real authoring mutation must be described and invalidated.
    assert(!result.change.isNone() && "mutating command reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None
           && "mutating command reported no invalidation");

    accumulate(result.invalidation);
    accumulate(reconcileWorkspace());   // keep EditorState valid in the same op
    accumulate(EditorInvalidation::Toolbar);   // undo became available
    history_.record(std::move(command), revisionBefore, revisionAfter);
    return result;
}

EditorOperationResult EditorCoordinator::undo() {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Stop Play before undoing authoring changes");
        return EditorOperationResult::failure("Cannot undo while Play is running");
    }

    if (!history_.canUndo()) {
        return EditorOperationResult::failure("Nothing to undo");
    }
    // Same revision-based contract as executeOwned, applied to the inverse: a
    // failed undo must not mutate; a successful one must mutate and declare a
    // change + invalidation. (asserts compile out in release.)
    CommandEntry entry = history_.takeUndo();
    const uint64_t before = document_.revision();
    EditorOperationResult result = entry.command->undo(document_);
    const uint64_t after = document_.revision();
    (void)before;
    (void)after;
    if (!result.ok) {
        assert(after == before && "failed undo mutated the document");
        appendConsole(ConsoleMessage::Level::Error, result.error);
        history_.pushUndo(std::move(entry));               // unchanged: keep it undoable
        return result;
    }
    assert(after != before && "undo succeeded without mutating the document");
    assert(!result.change.isNone() && "undo reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None && "undo reported no invalidation");
    document_.restoreRevision(entry.revisionBefore);   // dirty reflects state A
    accumulate(result.invalidation);
    accumulate(reconcileWorkspace());
    accumulate(EditorInvalidation::Toolbar);           // undo/redo availability changed
    history_.pushRedo(std::move(entry));               // now redoable
    return result;
}

EditorOperationResult EditorCoordinator::redo() {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Stop Play before redoing authoring changes");
        return EditorOperationResult::failure("Cannot redo while Play is running");
    }

    if (!history_.canRedo()) {
        return EditorOperationResult::failure("Nothing to redo");
    }
    // Redo re-applies the same command with its already-captured values; it does
    // not build an inverse or re-read the UI. restoreRevision returns to the
    // command's recorded post-state, so a redo back to the saved revision is clean.
    CommandEntry entry = history_.takeRedo();
    const uint64_t before = document_.revision();
    EditorOperationResult result = entry.command->apply(document_);
    const uint64_t after = document_.revision();
    (void)before;
    (void)after;
    if (!result.ok) {
        assert(after == before && "failed redo mutated the document");
        appendConsole(ConsoleMessage::Level::Error, result.error);
        history_.pushRedo(std::move(entry));               // unchanged: keep it redoable
        return result;
    }
    assert(after != before && "redo succeeded without mutating the document");
    assert(!result.change.isNone() && "redo reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None && "redo reported no invalidation");
    document_.restoreRevision(entry.revisionAfter);    // dirty reflects state B
    accumulate(result.invalidation);
    accumulate(reconcileWorkspace());
    accumulate(EditorInvalidation::Toolbar);
    history_.pushUndo(std::move(entry));               // undoable again
    return result;
}

EditorInvalidation EditorCoordinator::reconcileWorkspace() {
    EditorInvalidation extra = EditorInvalidation::None;

    // 1. The active scene must reference a scene that still exists. If it was
    //    removed, normalize to the start scene (or the first scene, or none) and
    //    drop the selection — it belonged to a scene that is gone.
    if (!state_.activeSceneId.empty() && !document_.hasScene(state_.activeSceneId)) {
        state_.sceneViews.erase(state_.activeSceneId);
        state_.activeSceneId = normalizedSceneId(document_);
        state_.selection.clear();
        if (!state_.activeSceneId.empty()) {
            state_.sceneViews.try_emplace(state_.activeSceneId);
        }
        extra |= EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
               | EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    }

    // 2. No per-scene view state may outlive its scene.
    for (auto it = state_.sceneViews.begin(); it != state_.sceneViews.end();) {
        if (!document_.hasScene(it->first)) it = state_.sceneViews.erase(it);
        else ++it;
    }

    // 3. The selection must reference an instance that still exists in the active
    //    scene. Deleting the selected entity empties the Inspector; deleting any
    //    other entity leaves the selection untouched.
    if (state_.selection.hasEntity()
        && !document_.findInstanceInScene(state_.activeSceneId,
                                          state_.selection.primaryEntity)) {
        state_.selection.clear();
        extra |= EditorInvalidation::Inspector;
    }

    // 4. Per-scene layer workspace state must reference existing layers: an active
    //    layer that vanished falls back to the scene default; a hidden id that
    //    vanished is dropped. (After a layer remove / project replace / scene undo.)
    for (auto& [sceneId, view] : state_.sceneViews) {
        const SceneDef* scene = document_.findScene(sceneId);
        if (!scene) continue;
        if (!view.activeLayerId.empty() && !document_.hasLayer(sceneId, view.activeLayerId)) {
            view.activeLayerId = scene->defaultLayerId;
            extra |= EditorInvalidation::Inspector;
        }
        for (auto it = view.hiddenLayerIds.begin(); it != view.hiddenLayerIds.end();) {
            if (!document_.hasLayer(sceneId, *it)) it = view.hiddenLayerIds.erase(it);
            else ++it;
        }
    }

    if (state_.spriteAnimationEditor.openAssetId
        && !document_.hasSpriteAnimationAsset(*state_.spriteAnimationEditor.openAssetId)) {
        state_.spriteAnimationEditor = SpriteAnimationEditorState{};
        extra |= EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    }

    return extra;
}

EditorOperationResult EditorCoordinator::replaceProject(ProjectDocument replacement) {
    if (isPlaying()) {
        return EditorOperationResult::failure("Cannot replace project while Play is running");
    }

    document_.replaceClean(std::move(replacement));

    state_.activeSceneId = normalizedSceneId(document_);
    state_.selection.clear();
    state_.sceneViews.clear();
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    if (!state_.activeSceneId.empty()) {
        state_.sceneViews.try_emplace(state_.activeSceneId);
    }
    history_.clear();

    accumulate(kProjectReplaceInvalidation);
    return EditorOperationResult::success(
        kProjectReplaceInvalidation, DomainChange::projectReplaced());
}

EditorOperationResult EditorCoordinator::markProjectSaved() {
    document_.markSaved();
    accumulate(EditorInvalidation::Toolbar);
    return EditorOperationResult::success(EditorInvalidation::Toolbar);
}

// ----------------------------------------------------------------------------
// Play / Stop
// ----------------------------------------------------------------------------
bool EditorCoordinator::canPlayProject() const {
    const SceneId& sceneId = document_.startSceneId();
    return !sceneId.empty() && document_.hasScene(sceneId);
}

bool EditorCoordinator::canPlayCurrentScene() const {
    const SceneId& sceneId = state_.activeSceneId;
    return !sceneId.empty() && document_.hasScene(sceneId);
}

EditorOperationResult EditorCoordinator::playProject() {
    if (isPlaying()) {
        return EditorOperationResult::failure("Already playing");
    }
    if (!canPlayProject()) {
        return EditorOperationResult::failure("Cannot play project: no valid start scene");
    }
    std::string error;
    std::optional<PlaySession> session = PlaySession::startProject(document_, &error);
    if (!session.has_value()) {
        return EditorOperationResult::failure(error.empty() ? "Cannot start Play" : error);
    }
    playSession_.emplace(std::move(*session));
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    logInfo("Play project started (document untouched)");
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    accumulate(kPlayToggleInvalidation);
    return EditorOperationResult::success(kPlayToggleInvalidation);
}

EditorOperationResult EditorCoordinator::playCurrentScene() {
    if (isPlaying()) {
        return EditorOperationResult::failure("Already playing");
    }
    if (!canPlayCurrentScene()) {
        return EditorOperationResult::failure("Cannot play current scene: no active scene");
    }
    std::string error;
    std::optional<PlaySession> session =
        PlaySession::startActiveScene(document_, state_.activeSceneId, &error);
    if (!session.has_value()) {
        return EditorOperationResult::failure(error.empty() ? "Cannot start Play" : error);
    }
    playSession_.emplace(std::move(*session));
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    logInfo("Play current scene started (document untouched)");
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    accumulate(kPlayToggleInvalidation);
    return EditorOperationResult::success(kPlayToggleInvalidation);
}

EditorOperationResult EditorCoordinator::stopPlaying() {
    if (!isPlaying()) {
        return EditorOperationResult::failure("Not playing");
    }
    playSession_.reset();   // RAII: back to the untouched authoring document
    logInfo("Stopped - back to authoring document");
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    accumulate(kPlayToggleInvalidation);
    return EditorOperationResult::success(kPlayToggleInvalidation);
}

void EditorCoordinator::advanceRuntime(float dt) {
    if (playSession_) playSession_->advance(dt);
}

void EditorCoordinator::updateRuntime(const RuntimeInputSnapshot& input, float dt) {
    if (playSession_) playSession_->update(input, dt);
}

// ----------------------------------------------------------------------------
// Intent path — workspace state only; never the ProjectDocument, never undo.
// ----------------------------------------------------------------------------
EditorOperationResult EditorCoordinator::apply(const SelectEntityIntent& intent) {
    if (intent.entityId != INVALID_ENTITY
        && !document_.findInstanceInScene(state_.activeSceneId, intent.entityId)) {
        return EditorOperationResult::failure("Unknown entity id in active scene");
    }
    state_.selection.primaryEntity = intent.entityId;
    accumulate(kSelectionInvalidation);
    return EditorOperationResult::success(kSelectionInvalidation);
}

EditorOperationResult EditorCoordinator::apply(const SelectSceneIntent& intent) {
    if (!document_.hasScene(intent.sceneId)) {
        return EditorOperationResult::failure("Unknown scene id");
    }
    // Editorial focus only — workspace state, not ProjectDocument.
    state_.activeSceneId = intent.sceneId;
    state_.selection.clear();
    // Ensure a per-scene view state exists (restored on return to this scene).
    state_.sceneViews.try_emplace(intent.sceneId);
    accumulate(kSceneChangeInvalidation);
    return EditorOperationResult::success(kSceneChangeInvalidation);
}

EditorOperationResult EditorCoordinator::apply(const SetViewportZoomIntent& intent) {
    state_.sceneViews[intent.sceneId].zoom = clampZoom(intent.zoom);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const PanViewportIntent& intent) {
    EditorSceneViewState& view = state_.sceneViews[intent.sceneId];
    view.pan.x += intent.delta.x;
    view.pan.y += intent.delta.y;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridVisibilityIntent& intent) {
    state_.sceneViews[intent.sceneId].gridVisible = intent.visible;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridSnapEnabledIntent& intent) {
    state_.sceneViews[intent.sceneId].gridSnapEnabled = intent.enabled;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridCellSizeIntent& intent) {
    if (!std::isfinite(intent.cellSize) || intent.cellSize <= 0.0f) {
        return EditorOperationResult::failure("Grid cell size must be a positive number");
    }
    EditorSceneViewState& view = state_.sceneViews[intent.sceneId];
    if (view.gridCellSize == intent.cellSize) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    view.gridCellSize = intent.cellSize;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const OpenSpriteAnimationEditorIntent& intent) {
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(intent.assetId);
    if (!asset) {
        return EditorOperationResult::failure("Unknown sprite animation asset");
    }
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    editor.openAssetId = intent.assetId;
    if (asset->clips.empty()) {
        editor.selectedClipId.reset();
    } else if (!editor.selectedClipId
               || std::none_of(asset->clips.begin(), asset->clips.end(),
                               [&](const SpriteAnimationClipDef& clip) {
                                   return clip.id == *editor.selectedClipId;
                               })) {
        editor.selectedClipId = asset->clips.front().id;
    }
    // Adopt the selected clip's frame count so the grid overlay matches an
    // already-sliced clip on open (a single row of N frames is the common case).
    if (editor.selectedClipId) {
        for (const SpriteAnimationClipDef& clip : asset->clips) {
            if (clip.id != *editor.selectedClipId || clip.frames.empty()) continue;
            editor.sliceColumns = static_cast<int>(clip.frames.size());
            editor.sliceRows = 1;
            break;
        }
    }
    editor.selectedFrameIndex = 0;
    editor.previewPlaying = false;
    editor.previewElapsed = 0.f;
    editor.previewFrameIndex = 0;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const CloseSpriteAnimationEditorIntent&) {
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SelectAnimationClipIntent& intent) {
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(intent.assetId);
    if (!asset) {
        return EditorOperationResult::failure("Unknown sprite animation asset");
    }
    for (const SpriteAnimationClipDef& clip : asset->clips) {
        if (clip.id == intent.clipId) {
            SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
            editor.openAssetId = intent.assetId;
            editor.selectedClipId = intent.clipId;
            editor.selectedFrameIndex = 0;
            editor.previewElapsed = 0.f;
            editor.previewFrameIndex = 0;
            accumulate(EditorInvalidation::Viewport);
            return EditorOperationResult::success(EditorInvalidation::Viewport);
        }
    }
    return EditorOperationResult::failure("Unknown animation clip");
}

EditorOperationResult EditorCoordinator::apply(
    const SetAnimationSliceGridIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    const EditorInvalidation inv = EditorInvalidation::Viewport;
    const int columns = std::clamp(intent.columns, 1, 4096);
    const int rows = std::clamp(intent.rows, 1, 4096);
    const int margin = std::clamp(intent.margin, 0, 4096);
    const int spacing = std::clamp(intent.spacing, 0, 4096);
    if (editor.sliceColumns == columns
        && editor.sliceRows == rows
        && editor.sliceMargin == margin
        && editor.sliceSpacing == spacing) {
        accumulate(inv);
        return EditorOperationResult::success(inv);
    }
    editor.sliceColumns = columns;
    editor.sliceRows = rows;
    editor.sliceMargin = margin;
    editor.sliceSpacing = spacing;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetSpriteSheetZoomIntent& intent) {
    if (!state_.spriteAnimationEditor.openAssetId) {
        return EditorOperationResult::failure("Sprite Animation Editor is not open");
    }
    state_.spriteAnimationEditor.sheetZoom = clampSheetZoom(intent.zoom);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const PanSpriteSheetIntent& intent) {
    if (!state_.spriteAnimationEditor.openAssetId) {
        return EditorOperationResult::failure("Sprite Animation Editor is not open");
    }
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    editor.sheetPan.x += intent.delta.x;
    editor.sheetPan.y += intent.delta.y;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetAnimationPreviewPlayingIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.selectedClipId) {
        return EditorOperationResult::failure("No animation clip selected");
    }
    editor.previewPlaying = intent.playing;
    if (intent.playing) {
        // Restarting a finished Once preview replays from the first frame.
        const SpriteAnimationAssetDef* asset =
            document_.findSpriteAnimationAsset(*editor.openAssetId);
        const SpriteAnimationClipDef* clip = nullptr;
        if (asset) {
            for (const SpriteAnimationClipDef& candidate : asset->clips) {
                if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
            }
        }
        if (clip && editor.previewFrameIndex + 1 >= clip->frames.size()) {
            editor.previewFrameIndex = 0;
            editor.previewElapsed = 0.f;
        }
    }
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetAnimationPreviewFrameIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.selectedClipId) {
        return EditorOperationResult::failure("No animation clip selected");
    }
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(*editor.openAssetId);
    const SpriteAnimationClipDef* clip = nullptr;
    if (asset) {
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
        }
    }
    if (!clip || clip->frames.empty()) {
        return EditorOperationResult::failure("Clip has no frames to scrub");
    }
    editor.previewPlaying = false;
    editor.previewFrameIndex = std::min(intent.frameIndex, clip->frames.size() - 1);
    editor.previewElapsed = 0.f;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const StepAnimationPreviewIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.selectedClipId) {
        return EditorOperationResult::failure("No animation clip selected");
    }
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(*editor.openAssetId);
    const SpriteAnimationClipDef* clip = nullptr;
    if (asset) {
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
        }
    }
    if (!clip || clip->frames.empty()) {
        return EditorOperationResult::failure("Clip has no frames to step");
    }
    const std::size_t count = clip->frames.size();
    const std::size_t current = std::min(editor.previewFrameIndex, count - 1);
    // Euclidean wrap keeps any negative delta inside [0, count).
    const long long stepped =
        (static_cast<long long>(current) + intent.delta) % static_cast<long long>(count);
    editor.previewPlaying = false;
    editor.previewFrameIndex =
        static_cast<std::size_t>(stepped < 0 ? stepped + static_cast<long long>(count)
                                             : stepped);
    editor.previewElapsed = 0.f;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

void EditorCoordinator::advanceSpriteAnimationPreview(float dt) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.previewPlaying || !editor.openAssetId || !editor.selectedClipId) return;
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(*editor.openAssetId);
    const SpriteAnimationClipDef* clip = nullptr;
    if (asset) {
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
        }
    }
    if (!clip || clip->frames.empty() || clip->framesPerSecond <= 0.f
        || !std::isfinite(clip->framesPerSecond)) {
        editor.previewPlaying = false;
        accumulate(EditorInvalidation::Viewport);
        return;
    }
    if (!(dt > 0.f)) return;
    if (editor.previewFrameIndex >= clip->frames.size()) {
        editor.previewFrameIndex = 0;   // frames edited under a stale playhead
        editor.previewElapsed = 0.f;
    }
    const float frameDuration = 1.f / clip->framesPerSecond;
    editor.previewElapsed += dt;
    while (editor.previewElapsed >= frameDuration) {
        editor.previewElapsed -= frameDuration;
        if (editor.previewFrameIndex + 1 < clip->frames.size()) {
            ++editor.previewFrameIndex;
        } else if (clip->playbackMode == AnimationPlaybackMode::Loop) {
            editor.previewFrameIndex = 0;
        } else {
            // Once: hold the last frame and flip the Play affordance back.
            editor.previewPlaying = false;
            editor.previewElapsed = 0.f;
            accumulate(EditorInvalidation::Viewport);
            return;
        }
    }
}

EditorOperationResult EditorCoordinator::apply(const SetHierarchyFilterIntent& intent) {
    uiState_.hierarchyFilter = intent.filter;
    accumulate(EditorInvalidation::Hierarchy);
    return EditorOperationResult::success(EditorInvalidation::Hierarchy);
}

EditorOperationResult EditorCoordinator::apply(const SetConsoleFilterIntent& intent) {
    uiState_.consoleFilter = intent.filter;
    accumulate(EditorInvalidation::Console);
    return EditorOperationResult::success(EditorInvalidation::Console);
}

EditorOperationResult EditorCoordinator::apply(const SetConsoleShowInfoIntent& intent) {
    uiState_.consoleShowInfo = intent.visible;
    accumulate(EditorInvalidation::Console);
    return EditorOperationResult::success(EditorInvalidation::Console);
}

EditorOperationResult EditorCoordinator::apply(const SetConsoleShowWarningIntent& intent) {
    uiState_.consoleShowWarning = intent.visible;
    accumulate(EditorInvalidation::Console);
    return EditorOperationResult::success(EditorInvalidation::Console);
}

EditorOperationResult EditorCoordinator::apply(const SetConsoleShowErrorIntent& intent) {
    uiState_.consoleShowError = intent.visible;
    accumulate(EditorInvalidation::Console);
    return EditorOperationResult::success(EditorInvalidation::Console);
}

EditorOperationResult EditorCoordinator::apply(const SetActiveLayerIntent& intent) {
    if (!document_.hasLayer(intent.sceneId, intent.layerId)) {
        return EditorOperationResult::failure("Unknown layer id");
    }
    state_.sceneViews[intent.sceneId].activeLayerId = intent.layerId;
    accumulate(EditorInvalidation::Inspector);
    return EditorOperationResult::success(EditorInvalidation::Inspector);
}

EditorOperationResult EditorCoordinator::apply(const ToggleLayerEditorVisibilityIntent& intent) {
    if (!document_.hasLayer(intent.sceneId, intent.layerId)) {
        return EditorOperationResult::failure("Unknown layer id");
    }
    auto& hidden = state_.sceneViews[intent.sceneId].hiddenLayerIds;
    if (hidden.erase(intent.layerId) == 0) hidden.insert(intent.layerId);
    const EditorInvalidation inv = EditorInvalidation::Inspector
        | EditorInvalidation::Hierarchy | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetActiveToolIntent& intent) {
    state_.activeTool = intent.tool;
    return EditorOperationResult::success(EditorInvalidation::Toolbar);
}

EditorOperationResult EditorCoordinator::apply(const ToggleConsoleIntent&) {
    uiState_.consoleVisible = !uiState_.consoleVisible;
    const EditorInvalidation inv = EditorInvalidation::Layout | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const ResizePanelIntent& intent) {
    switch (intent.panel) {
        case ResizePanelIntent::Panel::Left:
            uiState_.leftPanelWidth = clampLeftPanel(intent.size);
            break;
        case ResizePanelIntent::Panel::Right:
            uiState_.rightPanelWidth = clampRightPanel(intent.size);
            break;
        case ResizePanelIntent::Panel::Console:
            uiState_.consoleHeight = clampConsole(intent.size);
            break;
    }
    // A splitter drag relays out the shell but refreshes no panel content.
    const EditorInvalidation inv = EditorInvalidation::Layout | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

// ----------------------------------------------------------------------------
// Console
// ----------------------------------------------------------------------------
const ConsoleMessage* EditorCoordinator::consoleMessage(
    std::optional<std::size_t> index) const {
    if (!index || *index >= console_.size()) return nullptr;
    return &console_[*index];
}

std::string formatConsoleMessageForClipboard(const ConsoleMessage& message) {
    const char* label = "Info";
    switch (message.level) {
        case ConsoleMessage::Level::Warning: label = "Warning"; break;
        case ConsoleMessage::Level::Error:   label = "Error";   break;
        case ConsoleMessage::Level::Info:    label = "Info";    break;
    }
    return std::string("[") + label + "] " + message.text;
}

void EditorCoordinator::appendConsole(ConsoleMessage::Level level, std::string text) {
    console_.push_back(ConsoleMessage{level, std::move(text)});
    accumulate(EditorInvalidation::Console);
}

void EditorCoordinator::logInfo(std::string text) {
    appendConsole(ConsoleMessage::Level::Info, std::move(text));
}
void EditorCoordinator::logWarning(std::string text) {
    appendConsole(ConsoleMessage::Level::Warning, std::move(text));
}
void EditorCoordinator::logError(std::string text) {
    appendConsole(ConsoleMessage::Level::Error, std::move(text));
}

void EditorCoordinator::clearConsole() {
    console_.clear();
    accumulate(EditorInvalidation::Console);
}

// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------
EditorInvalidation EditorCoordinator::consumeInvalidations() {
    const EditorInvalidation snapshot = pending_;
    pending_ = EditorInvalidation::None;
    return snapshot;
}

} // namespace ArtCade::EditorNative
