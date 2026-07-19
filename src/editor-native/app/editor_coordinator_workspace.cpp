#include "editor-native/app/editor_coordinator.h"

#include "editor-native/model/numeric_validation.h"
#include "editor-native/view/scene_grid.h"
#include "sprite-animation-core.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kSelectionInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport | EditorInvalidation::Toolbar;

constexpr EditorInvalidation kSceneChangeInvalidation =
    kSelectionInvalidation | EditorInvalidation::Toolbar;
} // namespace

EditorOperationResult EditorCoordinator::apply(const SelectEntityIntent& intent) {
    if (intent.entityId == INVALID_ENTITY) {
        // A pending gesture belongs to whatever *was* selected - it must not
        // survive the selection changing out from under it, regardless of
        // whether the new selection (here, none) would itself support one.
        cancelPendingTilemapGesture();
        state_.selection.primaryEntity = INVALID_ENTITY;
        reconcileTilemapEditingContext();
        accumulate(kSelectionInvalidation);
        return EditorOperationResult::success(kSelectionInvalidation);
    }
    const SceneInstanceDef* inst =
        document_.findInstanceInScene(state_.activeSceneId, intent.entityId);
    if (!inst) {
        return finishIntent(EditorOperationResult::failure("Unknown entity id in active scene"));
    }
    cancelPendingTilemapGesture();
    state_.selection.primaryEntity = intent.entityId;
    // A layer is a real authoring scope: selecting an entity — from Hierarchy
    // or Scene View — always makes its own layer the active one, so
    // Brush/Delete/Inspector never keep targeting a stale layer after the
    // selection itself has moved on.
    state_.sceneViews[state_.activeSceneId].activeLayerId =
        document_.effectiveLayerId(state_.activeSceneId, *inst);
    // If the newly selected entity doesn't support the active tilemap tool,
    // this falls the tool back to Select; if it does (e.g. switching between
    // two tilemaps), the tool is deliberately left alone and only the tile
    // selection is reconciled against the new target's tileset.
    reconcileTilemapEditingContext();
    // Slice 5: selecting a Tilemap always reveals the Tile Palette dock
    // (session layout only — height/visibility stay in EditorUiState).
    EditorInvalidation inv = kSelectionInvalidation;
    if (inst->tilemap.has_value()) {
        uiState_.tilePaletteDockVisible = true;
        inv = inv | EditorInvalidation::Layout;
    }
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SelectSceneIntent& intent) {
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene id"));
    }
    // A pending gesture belonged to the scene just left, not the one becoming
    // active - and the scene's Inspector has no selection to support a
    // tilemap tool either way, so it always falls back to Select.
    cancelPendingTilemapGesture();
    // Editorial focus only — workspace state, not ProjectDocument.
    state_.activeSceneId = intent.sceneId;
    state_.selection.clear();
    // Ensure a per-scene view state exists (restored on return to this scene).
    state_.sceneViews.try_emplace(intent.sceneId);
    reconcileTilemapEditingContext();
    accumulate(kSceneChangeInvalidation);
    return EditorOperationResult::success(kSceneChangeInvalidation);
}

EditorOperationResult EditorCoordinator::apply(const SwitchCenterWorkspaceIntent& intent) {
    // A user-initiated workspace selection during Play overrides the automatic
    // Logic -> Scene preview route. Do this before the no-op fast path so an
    // explicit click on the already-visible Scene tab is still respected.
    if (isPlaying() && playNavigation_ && playNavigation_->returnToOriginArmed)
        playNavigation_->returnToOriginArmed = false;
    if (state_.centerWorkspaceMode == intent.mode)
        return EditorOperationResult::success(EditorInvalidation::None);
    // A scene gesture cannot survive after its surface has been hidden. This
    // clears only workspace preview state; no Command, revision or dirty state.
    cancelPendingTilemapGesture();
    state_.centerWorkspaceMode = intent.mode;
    if (intent.mode == CenterWorkspaceMode::Logic) {
        LogicBoardEditorState& logicState = state_.logicBoardEditor;
        // Preserve the explicitly opened board. A deterministic first type is
        // only a bootstrap for entering Logic before any board was ever opened;
        // the current scene selection is deliberately not consulted here.
        if (!logicState.objectTypeId || !document_.hasObjectType(*logicState.objectTypeId)) {
            std::vector<ObjectTypeId> ids;
            ids.reserve(document_.data().objectTypes.size());
            for (const auto& [id, unused] : document_.data().objectTypes) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            logicState.objectTypeId = ids.empty() ? std::optional<ObjectTypeId>{}
                                                  : std::optional<ObjectTypeId>{ids.front()};
        }
    }

    const EditorInvalidation inv = EditorInvalidation::LogicBoard
                                 | EditorInvalidation::ScriptEditor
                                 | EditorInvalidation::Viewport
                                 | EditorInvalidation::Toolbar
                                 | EditorInvalidation::Layout;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const OpenLogicBoardIntent& intent) {
    if (!document_.hasObjectType(intent.objectTypeId))
        return finishIntent(EditorOperationResult::failure("Unknown Object Type"));
    if (isPlaying() && playNavigation_ && playNavigation_->returnToOriginArmed)
        playNavigation_->returnToOriginArmed = false;
    LogicBoardEditorState& logicState = state_.logicBoardEditor;
    if (state_.centerWorkspaceMode == CenterWorkspaceMode::Logic
        && logicState.objectTypeId == intent.objectTypeId)
        return EditorOperationResult::success(EditorInvalidation::None);
    const bool enteringLogic = state_.centerWorkspaceMode != CenterWorkspaceMode::Logic;
    cancelPendingTilemapGesture();
    logicState.objectTypeId = intent.objectTypeId;
    state_.centerWorkspaceMode = CenterWorkspaceMode::Logic;
    EditorInvalidation inv = EditorInvalidation::LogicBoard | EditorInvalidation::Toolbar;
    if (enteringLogic) inv |= EditorInvalidation::Viewport | EditorInvalidation::Layout;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetLogicBoardTabIntent& intent) {
    if (state_.logicBoardEditor.tab == intent.tab)
        return EditorOperationResult::success(EditorInvalidation::None);
    state_.logicBoardEditor.tab = intent.tab;
    const EditorInvalidation inv = EditorInvalidation::LogicBoard | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetLogicBoardSearchIntent& intent) {
    if (state_.logicBoardEditor.search == intent.search)
        return EditorOperationResult::success(EditorInvalidation::None);
    state_.logicBoardEditor.search = intent.search;
    accumulate(EditorInvalidation::LogicBoard);
    return EditorOperationResult::success(EditorInvalidation::LogicBoard);
}

EditorOperationResult EditorCoordinator::apply(const OpenScriptBufferIntent& intent) {
    if (!document_.hasScriptAsset(intent.scriptAssetId)) {
        return finishIntent(EditorOperationResult::failure("Unknown script asset"));
    }
    if (isPlaying() && playNavigation_ && playNavigation_->returnToOriginArmed)
        playNavigation_->returnToOriginArmed = false;
    const bool enteringScript = state_.centerWorkspaceMode != CenterWorkspaceMode::Script;
    state_.scriptEditor.open(intent.scriptAssetId, intent.savedText);
    state_.centerWorkspaceMode = CenterWorkspaceMode::Script;
    EditorInvalidation invalidation =
        EditorInvalidation::ScriptEditor | EditorInvalidation::Toolbar;
    if (enteringScript) {
        cancelPendingTilemapGesture();
        invalidation |= EditorInvalidation::Viewport | EditorInvalidation::Layout;
    }
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const ActivateScriptBufferIntent& intent) {
    if (!state_.scriptEditor.find(intent.scriptAssetId)) {
        return finishIntent(EditorOperationResult::failure("Script buffer is not open"));
    }
    if (isPlaying() && playNavigation_ && playNavigation_->returnToOriginArmed)
        playNavigation_->returnToOriginArmed = false;
    if (state_.scriptEditor.activeAssetId == intent.scriptAssetId
        && state_.centerWorkspaceMode == CenterWorkspaceMode::Script) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    state_.scriptEditor.activeAssetId = intent.scriptAssetId;
    state_.centerWorkspaceMode = CenterWorkspaceMode::Script;
    const EditorInvalidation invalidation =
        EditorInvalidation::ScriptEditor | EditorInvalidation::Toolbar
        | EditorInvalidation::Viewport | EditorInvalidation::Layout;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const EditScriptBufferIntent& intent) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.find(intent.scriptAssetId);
    if (!buffer) return finishIntent(EditorOperationResult::failure("Script buffer is not open"));
    const bool hadDiagnostics = !buffer->diagnostics.empty();
    const bool changed = buffer->edit(intent.text, intent.cursorOffset);
    if (!changed) return EditorOperationResult::success(EditorInvalidation::None);
    // The static textarea already owns the native editing gesture and the
    // controller updates only line numbers/status. Do not invalidate and
    // rebuild the Script panel for every character; Toolbar is enough to
    // refresh derived dirty/history affordances and the global status bar.
    if (hadDiagnostics) replaceScriptDiagnostics(intent.scriptAssetId, {});
    const EditorInvalidation invalidation = EditorInvalidation::Toolbar
        | (hadDiagnostics ? EditorInvalidation::Console : EditorInvalidation::None);
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const SetScriptCursorIntent& intent) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.find(intent.scriptAssetId);
    if (!buffer) return finishIntent(EditorOperationResult::failure("Script buffer is not open"));
    buffer->cursorOffset = clampScriptCursorOffset(buffer->text, intent.cursorOffset);
    buffer->scrollTop = std::max(0.f, intent.scrollTop);
    return EditorOperationResult::success(EditorInvalidation::None);
}

EditorOperationResult EditorCoordinator::apply(const SetScriptEditorFocusIntent& intent) {
    state_.scriptEditor.editorFocused = intent.focused;
    return EditorOperationResult::success(EditorInvalidation::None);
}

EditorOperationResult EditorCoordinator::apply(const MarkScriptBufferSavedIntent& intent) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.find(intent.scriptAssetId);
    if (!buffer) return finishIntent(EditorOperationResult::failure("Script buffer is not open"));
    const bool hadDiagnostics = !buffer->diagnostics.empty();
    const std::uint64_t previousRevision = buffer->revision;
    buffer->markSaved(intent.persistedText);
    if (isPlaying()) {
        const auto applied = appliedPlayScriptSources_.find(intent.scriptAssetId);
        if (applied != appliedPlayScriptSources_.end()) {
            if (applied->second == scriptSourceStamp(intent.persistedText))
                outdatedPlayScriptAssets_.erase(intent.scriptAssetId);
            else
                outdatedPlayScriptAssets_.insert(intent.scriptAssetId);
        }
    }
    const bool invalidatedDiagnostics = hadDiagnostics && buffer->revision != previousRevision;
    if (invalidatedDiagnostics) replaceScriptDiagnostics(intent.scriptAssetId, {});
    const EditorInvalidation invalidation = EditorInvalidation::ScriptEditor
        | EditorInvalidation::Toolbar
        | (invalidatedDiagnostics ? EditorInvalidation::Console : EditorInvalidation::None);
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const CloseScriptBufferIntent& intent) {
    if (!state_.scriptEditor.close(intent.scriptAssetId)) {
        return finishIntent(EditorOperationResult::failure("Script buffer is not open"));
    }
    const EditorInvalidation invalidation =
        EditorInvalidation::ScriptEditor | EditorInvalidation::Toolbar;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const UndoScriptBufferIntent&) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.active();
    const bool hadDiagnostics = buffer && !buffer->diagnostics.empty();
    if (!buffer || !buffer->undo()) return EditorOperationResult::success(EditorInvalidation::None);
    if (hadDiagnostics) replaceScriptDiagnostics(buffer->scriptAssetId, {});
    const EditorInvalidation invalidation = EditorInvalidation::ScriptEditor
        | EditorInvalidation::Toolbar
        | (hadDiagnostics ? EditorInvalidation::Console : EditorInvalidation::None);
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const RedoScriptBufferIntent&) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.active();
    const bool hadDiagnostics = buffer && !buffer->diagnostics.empty();
    if (!buffer || !buffer->redo()) return EditorOperationResult::success(EditorInvalidation::None);
    if (hadDiagnostics) replaceScriptDiagnostics(buffer->scriptAssetId, {});
    const EditorInvalidation invalidation = EditorInvalidation::ScriptEditor
        | EditorInvalidation::Toolbar
        | (hadDiagnostics ? EditorInvalidation::Console : EditorInvalidation::None);
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const SetScriptSearchIntent& intent) {
    if (state_.scriptEditor.search == intent.search)
        return EditorOperationResult::success(EditorInvalidation::None);
    state_.scriptEditor.search = intent.search;
    accumulate(EditorInvalidation::ScriptEditor);
    return EditorOperationResult::success(EditorInvalidation::ScriptEditor);
}

EditorOperationResult EditorCoordinator::apply(const SetScriptDiagnosticsIntent& intent) {
    ScriptEditorBuffer* buffer = state_.scriptEditor.find(intent.scriptAssetId);
    if (!buffer) return EditorOperationResult::success(EditorInvalidation::None);
    // Debounced work is allowed to finish after a newer edit. A stale result is
    // expected control flow, not a warning and never replaces current state.
    if (buffer->revision != intent.sourceRevision)
        return EditorOperationResult::success(EditorInvalidation::None);
    const ScriptAssetDef* asset = document_.findScriptAsset(intent.scriptAssetId);
    if (!asset) return finishIntent(EditorOperationResult::failure("Unknown script asset"));
    for (const ScriptDiagnostic& diagnostic : intent.diagnostics) {
        if (diagnostic.scriptAssetId != intent.scriptAssetId
            || diagnostic.path != asset->sourcePath) {
            return finishIntent(EditorOperationResult::failure(
                "Script diagnostic source does not match its asset"));
        }
    }

    const bool changed = buffer->diagnostics != intent.diagnostics
        || buffer->validatedRevision != intent.sourceRevision
        || buffer->validationPending;
    buffer->diagnostics = intent.diagnostics;
    buffer->validatedRevision = intent.sourceRevision;
    buffer->validationPending = false;
    if (!changed) return EditorOperationResult::success(EditorInvalidation::None);

    replaceScriptDiagnostics(intent.scriptAssetId, buffer->diagnostics);
    const EditorInvalidation invalidation =
        EditorInvalidation::ScriptEditor | EditorInvalidation::Console;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::apply(const SetViewportZoomIntent& intent) {
    // Same guard as the grid intents below: with no real scene behind
    // intent.sceneId, this would otherwise silently create workspace view
    // state for a scene that doesn't exist (e.g. mouse wheel over the empty
    // "No scene open" viewport, which the UI disabled state alone can't stop
    // — a click/scroll still reaches the coordinator regardless of how a
    // control is styled).
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene"));
    }
    if (!NumericValidation::isFinite(intent.zoom) || intent.zoom <= 0.f) {
        return finishIntent(EditorOperationResult::failure("Viewport zoom must be positive"));
    }
    state_.sceneViews[intent.sceneId].zoom = clampZoom(intent.zoom);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const PanViewportIntent& intent) {
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene"));
    }
    EditorSceneViewState& view = state_.sceneViews[intent.sceneId];
    const Vec2 next{view.pan.x + intent.delta.x, view.pan.y + intent.delta.y};
    if (!NumericValidation::isFinite(intent.delta) || !NumericValidation::isFinite(next)) {
        return finishIntent(EditorOperationResult::failure("Viewport pan must be finite"));
    }
    view.pan = next;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridVisibilityIntent& intent) {
    // Guard against the UI's disabled state being the only thing standing
    // between this and a nonexistent scene: without it, `sceneViews[id]`
    // would silently create workspace state for a scene that isn't real.
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene"));
    }
    state_.sceneViews[intent.sceneId].gridVisible = intent.visible;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridSnapEnabledIntent& intent) {
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene"));
    }
    state_.sceneViews[intent.sceneId].gridSnapEnabled = intent.enabled;
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetSceneGridCellSizeIntent& intent) {
    if (!document_.hasScene(intent.sceneId)) {
        return finishIntent(EditorOperationResult::failure("Unknown scene"));
    }
    if (!std::isfinite(intent.cellSize) || intent.cellSize <= 0.0f) {
        return finishIntent(
            EditorOperationResult::failure("Grid cell size must be a positive number"));
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
        return finishIntent(EditorOperationResult::failure("Unknown sprite animation asset"));
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
            if (clip.id != *editor.selectedClipId || clip.frameIds.empty()) continue;
            editor.sliceColumns = static_cast<int>(clip.frameIds.size());
            editor.sliceRows = 1;
            break;
        }
    }
    editor.selectedSheetFrames.clear();
    editor.selectedTimelineIndices.clear();
    editor.pendingResliceConfirm = false;
    editor.pendingSourceImageId.reset();
    editor.previewPlaying = false;
    editor.previewElapsed = 0.f;
    editor.previewSpeed = 1.f;
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
        return finishIntent(EditorOperationResult::failure("Unknown sprite animation asset"));
    }
    for (const SpriteAnimationClipDef& clip : asset->clips) {
        if (clip.id == intent.clipId) {
            SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
            editor.openAssetId = intent.assetId;
            editor.selectedClipId = intent.clipId;
            editor.selectedSheetFrames.clear();
            editor.selectedTimelineIndices.clear();
            editor.previewElapsed = 0.f;
            editor.previewFrameIndex = 0;
            accumulate(EditorInvalidation::Viewport);
            return EditorOperationResult::success(EditorInvalidation::Viewport);
        }
    }
    return finishIntent(EditorOperationResult::failure("Unknown animation clip"));
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
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    if (!NumericValidation::isFinite(intent.zoom) || intent.zoom <= 0.f) {
        return finishIntent(EditorOperationResult::failure("Sprite sheet zoom must be positive"));
    }
    state_.spriteAnimationEditor.sheetZoom = clampSheetZoom(intent.zoom);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const PanSpriteSheetIntent& intent) {
    if (!state_.spriteAnimationEditor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    const Vec2 next{editor.sheetPan.x + intent.delta.x,
                    editor.sheetPan.y + intent.delta.y};
    if (!NumericValidation::isFinite(intent.delta) || !NumericValidation::isFinite(next)) {
        return finishIntent(EditorOperationResult::failure("Sprite sheet pan must be finite"));
    }
    editor.sheetPan = next;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetAnimationPreviewPlayingIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.selectedClipId) {
        return finishIntent(EditorOperationResult::failure("No animation clip selected"));
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
        if (clip && editor.previewFrameIndex + 1 >= clip->frameIds.size()) {
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
        return finishIntent(EditorOperationResult::failure("No animation clip selected"));
    }
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(*editor.openAssetId);
    const SpriteAnimationClipDef* clip = nullptr;
    if (asset) {
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
        }
    }
    if (!clip || clip->frameIds.empty()) {
        return finishIntent(EditorOperationResult::failure("Clip has no frames to scrub"));
    }
    editor.previewPlaying = false;
    editor.previewFrameIndex = std::min(intent.frameIndex, clip->frameIds.size() - 1);
    editor.previewElapsed = 0.f;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const StepAnimationPreviewIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.selectedClipId) {
        return finishIntent(EditorOperationResult::failure("No animation clip selected"));
    }
    const SpriteAnimationAssetDef* asset =
        document_.findSpriteAnimationAsset(*editor.openAssetId);
    const SpriteAnimationClipDef* clip = nullptr;
    if (asset) {
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *editor.selectedClipId) { clip = &candidate; break; }
        }
    }
    if (!clip || clip->frameIds.empty()) {
        return finishIntent(EditorOperationResult::failure("Clip has no frames to step"));
    }
    const std::size_t count = clip->frameIds.size();
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

EditorOperationResult EditorCoordinator::apply(const SetAnimationPreviewSpeedIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    if (!std::isfinite(intent.speed) || intent.speed <= 0.f) {
        return finishIntent(EditorOperationResult::failure("Preview speed must be positive"));
    }
    editor.previewSpeed = intent.speed;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(
    const ToggleAnimationSheetFrameSelectionIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || intent.frameId.empty()) {
        return finishIntent(EditorOperationResult::failure("No animation sheet selection"));
    }
    auto& frames = editor.selectedSheetFrames;
    const auto it = std::find(frames.begin(), frames.end(), intent.frameId);
    if (it == frames.end()) frames.push_back(intent.frameId);
    else frames.erase(it);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetAnimationTimelineSelectionIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    editor.selectedTimelineIndices = intent.indices;
    if (!intent.indices.empty()) {
        editor.previewPlaying = false;
        editor.previewFrameIndex = intent.indices.back();
        editor.previewElapsed = 0.f;
    }
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const RequestAnimationResliceConfirmIntent&) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    editor.pendingResliceConfirm = true;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const ConfirmAnimationResliceIntent&) {
    state_.spriteAnimationEditor.pendingResliceConfirm = false;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const CancelAnimationResliceIntent&) {
    state_.spriteAnimationEditor.pendingResliceConfirm = false;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const RequestAnimationSourceImageIntent& intent) {
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Sprite Animation Editor is not open"));
    }
    if (!document_.hasImageAsset(intent.imageId)) {
        return finishIntent(EditorOperationResult::failure("Unknown source image"));
    }
    editor.pendingSourceImageId = intent.imageId;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const ConfirmAnimationSourceImageIntent&) {
    // Workspace-only: confirm clears the pending prompt. The UI must execute
    // ReplaceAnimationSourceImageCommand itself (same pattern as reslice).
    SpriteAnimationEditorState& editor = state_.spriteAnimationEditor;
    if (!editor.openAssetId || !editor.pendingSourceImageId) {
        return finishIntent(EditorOperationResult::failure("No pending source image change"));
    }
    editor.pendingSourceImageId.reset();
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const CancelAnimationSourceImageIntent&) {
    state_.spriteAnimationEditor.pendingSourceImageId.reset();
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
    if (!clip || clip->frameIds.empty() || !Animation::isValidAnimationFps(clip->framesPerSecond)) {
        editor.previewPlaying = false;
        accumulate(EditorInvalidation::Viewport);
        return;
    }
    if (!NumericValidation::isFinite(dt) || dt <= 0.f) return;
    if (editor.previewFrameIndex >= clip->frameIds.size()) {
        editor.previewFrameIndex = 0;   // frames edited under a stale playhead
        editor.previewElapsed = 0.f;
    }
    Animation::AnimationPlaybackCursor cursor;
    cursor.frameIndex = editor.previewFrameIndex;
    cursor.elapsedSeconds = editor.previewElapsed;
    cursor.playbackSpeed = editor.previewSpeed > 0.f ? editor.previewSpeed : 1.f;
    cursor.playing = true;
    cursor.completed = false;
    const Animation::AnimationAdvanceResult advanced = Animation::advanceAnimation(
        clip->frameIds.size(), clip->framesPerSecond, clip->playbackMode, cursor, dt);
    editor.previewFrameIndex = advanced.cursor.frameIndex;
    editor.previewElapsed = advanced.cursor.elapsedSeconds;
    if (!advanced.cursor.playing || advanced.completedThisStep) {
        // Once: hold the last frame and flip the Play affordance back.
        editor.previewPlaying = false;
        editor.previewElapsed = 0.f;
        accumulate(EditorInvalidation::Viewport);
    }
}

EditorOperationResult EditorCoordinator::apply(const OpenTilesetEditorIntent& intent) {
    const TilesetAsset* asset = document_.findTilesetAsset(intent.assetId);
    if (!asset) {
        return finishIntent(EditorOperationResult::failure("Unknown tileset asset"));
    }
    TilesetEditorState& editor = state_.tilesetEditor;
    editor.openAssetId = intent.assetId;
    editor.pendingSlicing = asset->slicing;
    editor.selectedTileId.reset();
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const CloseTilesetEditorIntent&) {
    state_.tilesetEditor = TilesetEditorState{};
    const EditorInvalidation inv = EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetPendingTilesetSlicingIntent& intent) {
    if (!state_.tilesetEditor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Tileset Editor is not open"));
    }
    TilesetSlicing clamped = intent.slicing;
    clamped.tileWidth  = std::clamp(clamped.tileWidth, 1, 4096);
    clamped.tileHeight = std::clamp(clamped.tileHeight, 1, 4096);
    clamped.marginX    = std::clamp(clamped.marginX, 0, 4096);
    clamped.marginY    = std::clamp(clamped.marginY, 0, 4096);
    clamped.spacingX   = std::clamp(clamped.spacingX, 0, 4096);
    clamped.spacingY   = std::clamp(clamped.spacingY, 0, 4096);
    state_.tilesetEditor.pendingSlicing = clamped;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetTilesetEditorZoomIntent& intent) {
    if (!state_.tilesetEditor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Tileset Editor is not open"));
    }
    if (!NumericValidation::isFinite(intent.zoom) || intent.zoom <= 0.f) {
        return finishIntent(EditorOperationResult::failure("Tileset editor zoom must be positive"));
    }
    state_.tilesetEditor.zoom = clampTilesetEditorZoom(intent.zoom);
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const PanTilesetEditorIntent& intent) {
    if (!state_.tilesetEditor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Tileset Editor is not open"));
    }
    TilesetEditorState& editor = state_.tilesetEditor;
    const Vec2 next{editor.pan.x + intent.delta.x, editor.pan.y + intent.delta.y};
    if (!NumericValidation::isFinite(intent.delta) || !NumericValidation::isFinite(next)) {
        return finishIntent(EditorOperationResult::failure("Tileset editor pan must be finite"));
    }
    editor.pan = next;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SelectTilesetTileIntent& intent) {
    if (!state_.tilesetEditor.openAssetId) {
        return finishIntent(EditorOperationResult::failure("Tileset Editor is not open"));
    }
    state_.tilesetEditor.selectedTileId = intent.tileId;
    accumulate(EditorInvalidation::Viewport);
    return EditorOperationResult::success(EditorInvalidation::Viewport);
}

EditorOperationResult EditorCoordinator::apply(const SetHierarchyFilterIntent& intent) {
    uiState_.hierarchyFilter = intent.filter;
    accumulate(EditorInvalidation::Hierarchy);
    return EditorOperationResult::success(EditorInvalidation::Hierarchy);
}

EditorOperationResult EditorCoordinator::apply(const SetAssetFilterIntent& intent) {
    if (uiState_.assetFilter == intent.filter)
        return EditorOperationResult::success(EditorInvalidation::None);
    uiState_.assetFilter = intent.filter;
    accumulate(EditorInvalidation::Assets);
    return EditorOperationResult::success(EditorInvalidation::Assets);
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
        return finishIntent(EditorOperationResult::failure("Unknown layer id"));
    }
    state_.sceneViews[intent.sceneId].activeLayerId = intent.layerId;
    EditorInvalidation inv = EditorInvalidation::Inspector;

    if (intent.sceneId == state_.activeSceneId) {
        // A layer switch always ends any in-progress paint gesture - it
        // belonged to whichever layer/entity was being edited a moment ago,
        // not to the layer that's active now.
        if (cancelPendingTilemapGesture()) inv |= EditorInvalidation::Viewport;
        // The selection is an authoring scope, not just a UI highlight: an
        // entity from the layer just left must not stay the operative target
        // for Delete/Inspector/tilemap tools once the active layer moves on.
        if (state_.selection.hasEntity()) {
            const SceneInstanceDef* inst =
                document_.findInstanceInScene(intent.sceneId, state_.selection.primaryEntity);
            if (!inst || document_.effectiveLayerId(intent.sceneId, *inst) != intent.layerId) {
                state_.selection.clear();
                inv |= EditorInvalidation::Hierarchy | EditorInvalidation::Viewport;
            }
        }
        // Whether the selection just got cleared above, or survived because
        // it's already on the new active layer, bring the tool back in line:
        // a tilemap tool with nothing left to (or newly unable to) operate on
        // falls back to Select.
        reconcileTilemapEditingContext();
    }

    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const ToggleLayerEditorVisibilityIntent& intent) {
    if (!document_.hasLayer(intent.sceneId, intent.layerId)) {
        return finishIntent(EditorOperationResult::failure("Unknown layer id"));
    }
    auto& hidden = state_.sceneViews[intent.sceneId].hiddenLayerIds;
    if (hidden.erase(intent.layerId) == 0) hidden.insert(intent.layerId);
    const EditorInvalidation inv = EditorInvalidation::Inspector
        | EditorInvalidation::Hierarchy | EditorInvalidation::Viewport;
    accumulate(inv);
    return EditorOperationResult::success(inv);
}

EditorOperationResult EditorCoordinator::apply(const SetActiveToolIntent& intent) {
    if (intent.tool != state_.activeTool) {
        // A pending stroke/rectangle/override belongs to the tool that
        // started it - switching tools must never leave one dangling (not
        // just relying on the keyboard-shortcut guards at the input-routing
        // call sites).
        cancelPendingTilemapGesture();
    }
    state_.activeTool = intent.tool;
    // Defence in depth: nothing in this codebase currently offers a tilemap
    // tool button without a selection that supports it (the Tile Palette dock
    // only renders tools for a selected tilemap), but the B/E/I/R/F keyboard
    // shortcuts (editor_app.cpp) reach this with no such guard - so if the
    // requested tool doesn't fit the current selection, this immediately falls
    // it back to Select rather than sticking.
    reconcileTilemapEditingContext();
    // The active tool is rendered as button state on the Scene toolbar
    // (Select/Pan always; Brush/Eraser/… when a Tilemap is selected). Both
    // read EditorState::activeTool / effectiveTilemapTool(), so Inspector
    // (dock stamp readout) and Toolbar invalidations fire together.
    accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    return EditorOperationResult::success(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
}

EditorOperationResult EditorCoordinator::apply(const BeginTemporaryToolOverrideIntent& intent) {
    state_.tilemapEditor.temporaryToolOverride = intent.tool;
    accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    return EditorOperationResult::success(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
}

EditorOperationResult EditorCoordinator::apply(const EndTemporaryToolOverrideIntent&) {
    if (!state_.tilemapEditor.temporaryToolOverride) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    state_.tilemapEditor.temporaryToolOverride.reset();
    accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    return EditorOperationResult::success(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
}

} // namespace ArtCade::EditorNative
