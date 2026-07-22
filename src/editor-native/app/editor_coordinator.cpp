#include "editor-native/app/editor_coordinator.h"

#include "editor-native/commands/logic_board_commands.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/view/scene_grid.h"
#include "logic-core.h"
#include "script-core.h"

#include <cassert>
#include <algorithm>
#include <optional>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// Selecting an entity refreshes the tree highlight, the inspector contents and
// the viewport gizmo — nothing else.
constexpr EditorInvalidation kProjectReplaceInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport | EditorInvalidation::Assets
    | EditorInvalidation::Toolbar | EditorInvalidation::Project
    | EditorInvalidation::ScriptEditor | EditorInvalidation::Layout;

// Start/Stop Play re-renders every panel whose controls must freeze (disabled)
// while Play runs and re-enable on Stop: Toolbar (play/undo/redo), Inspector,
// Hierarchy (create/delete) and Assets (import/remove). Viewport switches between
// the authoring projection and the Play snapshot.
constexpr EditorInvalidation kPlayToggleInvalidation =
    EditorInvalidation::Toolbar | EditorInvalidation::Viewport
    | EditorInvalidation::Inspector | EditorInvalidation::Hierarchy
    | EditorInvalidation::Assets | EditorInvalidation::LogicBoard
    | EditorInvalidation::ScriptEditor;

constexpr EditorInvalidation kPlayNavigationInvalidation =
    kPlayToggleInvalidation | EditorInvalidation::Layout;

SceneId normalizedSceneId(const ProjectDocument& document) {
    if (!document.startSceneId().empty() && document.hasScene(document.startSceneId())) {
        return document.startSceneId();
    }
    if (!document.data().scenes.empty()) {
        return document.data().scenes.begin()->first;
    }
    return {};
}

const LogicRuleDef* findLogicRule(const ProjectDocument& document,
                                  const ObjectTypeId& objectTypeId,
                                  const LogicRuleId& ruleId) {
    const EntityDef* objectType = document.findObjectType(objectTypeId);
    if (!objectType || !objectType->logicBoard) return nullptr;
    const auto rule = std::find_if(
        objectType->logicBoard->rules.begin(), objectType->logicBoard->rules.end(),
        [&](const LogicRuleDef& candidate) { return candidate.id == ruleId; });
    return rule == objectType->logicBoard->rules.end() ? nullptr : &*rule;
}

std::string nextCopiedRuleName(const LogicBoardDef& board, const std::string& sourceName) {
    const std::string base = sourceName.empty() ? "Rule" : sourceName;
    const auto nameTaken = [&](const std::string& candidate) {
        return std::any_of(board.rules.begin(), board.rules.end(),
            [&](const LogicRuleDef& rule) { return rule.name == candidate; });
    };
    const std::string first = base + " Copy";
    if (!nameTaken(first)) return first;
    for (std::size_t ordinal = 2;; ++ordinal) {
        const std::string candidate = first + " " + std::to_string(ordinal);
        if (!nameTaken(candidate)) return candidate;
    }
}
} // namespace

EditorCoordinator::EditorCoordinator(ProjectDoc doc)
    : document_(std::move(doc)) {
    state_.activeSceneId = document_.startSceneId();
    if (state_.activeSceneId.empty() && !document_.data().scenes.empty()) {
        state_.activeSceneId = document_.data().scenes.begin()->first;
    }
}

EditorOperationResult EditorCoordinator::apply(const ChangeLogicTriggerTypeIntent& intent) {
    return execute(ReplaceLogicTriggerCommand{
        intent.objectTypeId, intent.ruleId,
        Logic::makeDefaultEventBlock(intent.typeId)});
}

EditorOperationResult EditorCoordinator::apply(const DuplicateLogicRuleIntent& intent) {
    const EntityDef* objectType = document_.findObjectType(intent.objectTypeId);
    if (!objectType || !objectType->logicBoard)
        return EditorOperationResult::failure("Object Type has no Logic Board");
    const LogicBoardDef& board = *objectType->logicBoard;
    const auto source = std::find_if(board.rules.begin(), board.rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == intent.sourceRuleId; });
    if (source == board.rules.end())
        return EditorOperationResult::failure("Unknown Logic source rule");
    LogicRuleDef clone = *source;
    clone.id = nextLogicRuleId(board);
    clone.name = nextCopiedRuleName(board, source->name);
    const std::size_t insertionIndex = static_cast<std::size_t>(source - board.rules.begin()) + 1;
    return execute(DuplicateLogicRuleCommand{
        intent.objectTypeId, intent.sourceRuleId, std::move(clone), insertionIndex});
}

EditorOperationResult EditorCoordinator::apply(const AddLogicActionTypeIntent& intent) {
    const LogicRuleDef* rule = findLogicRule(document_, intent.objectTypeId, intent.ruleId);
    const std::size_t insertionIndex = rule ? rule->actions.size() : 0;
    return execute(AddLogicActionCommand{
        intent.objectTypeId, intent.ruleId,
        Logic::makeDefaultBlock(intent.typeId, Logic::BlockKind::Action),
        insertionIndex});
}

EditorOperationResult EditorCoordinator::apply(const ChangeLogicActionTypeIntent& intent) {
    return execute(ChangeLogicActionTypeCommand{
        intent.objectTypeId, intent.ruleId, intent.actionIndex, intent.typeId});
}

EditorOperationResult EditorCoordinator::apply(const AddLogicConditionTypeIntent& intent) {
    const LogicRuleDef* rule = findLogicRule(document_, intent.objectTypeId, intent.ruleId);
    const std::size_t insertionIndex = rule ? rule->conditions.size() : 0;
    return execute(AddLogicConditionCommand{
        intent.objectTypeId, intent.ruleId,
        Logic::makeDefaultBlock(intent.typeId, Logic::BlockKind::Condition),
        insertionIndex});
}

EditorOperationResult EditorCoordinator::apply(const ChangeLogicConditionTypeIntent& intent) {
    return execute(ChangeLogicConditionTypeCommand{
        intent.objectTypeId, intent.ruleId, intent.conditionIndex, intent.typeId});
}

const EditorSceneViewState& EditorCoordinator::sceneView(const SceneId& id) const {
    const auto it = state_.sceneViews.find(id);
    return it == state_.sceneViews.end() ? defaultSceneView_ : it->second;
}

void EditorCoordinator::markSceneViewInitialized(const SceneId& id) {
    state_.sceneViews[id].initialized = true;   // workspace only; no dirty/invalidation
}

std::string EditorCoordinator::activeLayerId(const SceneId& sceneId) const {
    const SceneDef* scene = document_.findScene(sceneId);
    if (!scene) return {};
    std::string active = sceneView(sceneId).activeLayerId;
    if (active.empty() || !document_.hasLayer(sceneId, active)) active = scene->defaultLayerId;
    return active;
}

bool EditorCoordinator::cancelPendingTilemapGesture() {
    bool any = false;
    if (state_.tilemapEditor.pendingStroke) { apply(CancelTilePaintStrokeIntent{}); any = true; }
    if (state_.tilemapEditor.pendingRectangle) { apply(CancelTileRectangleIntent{}); any = true; }
    if (state_.tilemapEditor.temporaryToolOverride) { apply(EndTemporaryToolOverrideIntent{}); any = true; }
    return any;
}

// ----------------------------------------------------------------------------
// Command path
// ----------------------------------------------------------------------------
EditorOperationResult EditorCoordinator::executeOwned(
    std::unique_ptr<EditorCommand> command,
    std::unique_ptr<EditorCommandSideEffect> sideEffect) {
    if (isPlaying()) {
        if (sideEffect) {
            const auto rollback = sideEffect->rollbackInitial();
            if (!rollback.ok) appendConsole(
                ConsoleMessage::Level::Error,
                "Could not roll back external artifact: " + rollback.error);
        }
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
        if (sideEffect) {
            const auto rollback = sideEffect->rollbackInitial();
            if (!rollback.ok)
                result.error += "; external rollback failed: " + rollback.error;
        }
        appendConsole(ConsoleMessage::Level::Error, result.error);
        return result;
    }

    if (revisionAfter == revisionBefore) {
        // A no-op must declare neither a change nor an invalidation, and is not
        // recorded (so it cannot be undone). If a side effect already mutated
        // the filesystem, roll it back — otherwise external artifacts can be
        // deleted with no history entry.
        assert(result.change.isNone() && "no-op command reported a DomainChange");
        assert(result.invalidation == EditorInvalidation::None
               && "no-op command reported an invalidation");
        if (sideEffect) {
            const auto rollback = sideEffect->rollbackInitial();
            if (!rollback.ok) {
                appendConsole(ConsoleMessage::Level::Error,
                    "Could not roll back external artifact after command no-op: "
                        + rollback.error);
                return EditorOperationResult::failure(
                    "Command produced no authoring change and external rollback failed");
            }
        }
        return result;
    }

    // A real authoring mutation must be described and invalidated.
    assert(!result.change.isNone() && "mutating command reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None
           && "mutating command reported no invalidation");

    accumulate(result.invalidation);
    reconcileWorkspaceAndAnnounce();   // keep EditorState valid in the same op
    accumulate(EditorInvalidation::Toolbar);   // undo became available
    history_.record(std::move(command), std::move(sideEffect),
                    revisionBefore, revisionAfter);
    return result;
}

EditorOperationResult EditorCoordinator::undo() {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Stop Play before undoing authoring changes");
        return EditorOperationResult::failure("Cannot undo while Play is running");
    }

    if (!history_.canUndo()) {
        appendConsole(ConsoleMessage::Level::Warning, "Nothing to undo");
        return EditorOperationResult::failure("Nothing to undo");
    }
    // Same revision-based contract as executeOwned, applied to the inverse: a
    // failed undo must not mutate; a successful one must mutate and declare a
    // change + invalidation. (asserts compile out in release.)
    CommandEntry entry = history_.takeUndo();
    if (entry.sideEffect) {
        const auto prepared = entry.sideEffect->prepareUndo();
        if (!prepared.ok) {
            appendConsole(ConsoleMessage::Level::Error, prepared.error);
            history_.pushUndo(std::move(entry));
            return EditorOperationResult::failure(prepared.error);
        }
    }
    const uint64_t before = document_.revision();
    EditorOperationResult result = entry.command->undo(document_);
    const uint64_t after = document_.revision();
    (void)before;
    (void)after;
    if (!result.ok) {
        assert(after == before && "failed undo mutated the document");
        if (entry.sideEffect) {
            const auto rollback = entry.sideEffect->rollbackUndo();
            if (!rollback.ok)
                result.error += "; external rollback failed: " + rollback.error;
        }
        appendConsole(ConsoleMessage::Level::Error, result.error);
        history_.pushUndo(std::move(entry));               // unchanged: keep it undoable
        return result;
    }
    assert(after != before && "undo succeeded without mutating the document");
    assert(!result.change.isNone() && "undo reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None && "undo reported no invalidation");
    document_.restoreRevision(entry.revisionBefore);   // dirty reflects state A
    if (entry.sideEffect) entry.sideEffect->commitUndo();
    accumulate(result.invalidation);
    reconcileWorkspaceAndAnnounce();
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
        appendConsole(ConsoleMessage::Level::Warning, "Nothing to redo");
        return EditorOperationResult::failure("Nothing to redo");
    }
    // Redo re-applies the same command with its already-captured values; it does
    // not build an inverse or re-read the UI. restoreRevision returns to the
    // command's recorded post-state, so a redo back to the saved revision is clean.
    CommandEntry entry = history_.takeRedo();
    if (entry.sideEffect) {
        const auto prepared = entry.sideEffect->prepareRedo();
        if (!prepared.ok) {
            appendConsole(ConsoleMessage::Level::Error, prepared.error);
            history_.pushRedo(std::move(entry));
            return EditorOperationResult::failure(prepared.error);
        }
    }
    const uint64_t before = document_.revision();
    EditorOperationResult result = entry.command->apply(document_);
    const uint64_t after = document_.revision();
    (void)before;
    (void)after;
    if (!result.ok) {
        assert(after == before && "failed redo mutated the document");
        if (entry.sideEffect) {
            const auto rollback = entry.sideEffect->rollbackRedo();
            if (!rollback.ok)
                result.error += "; external rollback failed: " + rollback.error;
        }
        appendConsole(ConsoleMessage::Level::Error, result.error);
        history_.pushRedo(std::move(entry));               // unchanged: keep it redoable
        return result;
    }
    assert(after != before && "redo succeeded without mutating the document");
    assert(!result.change.isNone() && "redo reported no DomainChange");
    assert(result.invalidation != EditorInvalidation::None && "redo reported no invalidation");
    document_.restoreRevision(entry.revisionAfter);    // dirty reflects state B
    if (entry.sideEffect) entry.sideEffect->commitRedo();
    accumulate(result.invalidation);
    reconcileWorkspaceAndAnnounce();
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

    // 5. The active layer must track the selected instance's own layer: the
    //    selection is always the authoring target, so if it changed layer by
    //    some means other than SelectEntityIntent/SetActiveLayerIntent (e.g. a
    //    SetEntityLayerCommand move, or its undo/redo), activeLayerId follows
    //    here instead of leaving a stale mismatch until the next manual pick.
    //    Symmetric with SetActiveLayerIntent, which clears the selection when
    //    the *active layer* changes explicitly instead. Workspace-only: never
    //    touches the document, so no dirty/revision/undo implication.
    if (state_.selection.hasEntity()) {
        if (const SceneInstanceDef* inst = document_.findInstanceInScene(
                state_.activeSceneId, state_.selection.primaryEntity)) {
            const std::string effLayer = document_.effectiveLayerId(state_.activeSceneId, *inst);
            EditorSceneViewState& view = state_.sceneViews[state_.activeSceneId];
            if (view.activeLayerId != effLayer) {
                view.activeLayerId = effLayer;
                extra |= EditorInvalidation::Inspector | EditorInvalidation::Viewport
                       | EditorInvalidation::Hierarchy;
            }
        }
    }

    if (state_.spriteAnimationEditor.openAssetId
        && !document_.hasSpriteAnimationAsset(*state_.spriteAnimationEditor.openAssetId)) {
        state_.spriteAnimationEditor = SpriteAnimationEditorState{};
        extra |= EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    }

    if (state_.tilesetEditor.openAssetId
        && !document_.hasTilesetAsset(*state_.tilesetEditor.openAssetId)) {
        state_.tilesetEditor = TilesetEditorState{};
        extra |= EditorInvalidation::Viewport | EditorInvalidation::Toolbar;
    }

    if (state_.tilemapEditor.pendingStroke) {
        const PendingTileStroke& stroke = *state_.tilemapEditor.pendingStroke;
        const SceneInstanceDef* inst = document_.findInstanceInScene(stroke.sceneId, stroke.entityId);
        if (!inst || !inst->tilemap.has_value()
            || document_.isInstanceLayerLocked(stroke.sceneId, *inst)) {
            // The entity being painted (or its component) vanished mid-stroke,
            // or its layer got locked out from under the gesture
            // (SetLayerLockedCommand, undo/redo included) - a locked layer
            // must reject edits *now*, not just future Begins. Discard only
            // the stroke, not the tile/tool preferences the user already
            // chose (stamp, rectangleOutlineMode, ...). This bypasses
            // routeViewportTilemapPaint's own End/Cancel calls, so a
            // right-click Eraser override in progress is dropped here too -
            // otherwise it would be stuck on Eraser with no stroke left to
            // ever trigger its cleanup.
            state_.tilemapEditor.pendingStroke.reset();
            state_.tilemapEditor.hoveredCell.reset();
            state_.tilemapEditor.temporaryToolOverride.reset();
            extra |= EditorInvalidation::Viewport;
        }
    }
    if (state_.tilemapEditor.pendingRectangle) {
        const PendingTileRectangle& rect = *state_.tilemapEditor.pendingRectangle;
        const SceneInstanceDef* inst = document_.findInstanceInScene(rect.sceneId, rect.entityId);
        if (!inst || !inst->tilemap.has_value()
            || document_.isInstanceLayerLocked(rect.sceneId, *inst)) {
            state_.tilemapEditor.pendingRectangle.reset();
            state_.tilemapEditor.hoveredCell.reset();
            extra |= EditorInvalidation::Viewport;
        }
    }

    if (state_.logicBoardEditor.objectTypeId
        && !document_.hasObjectType(*state_.logicBoardEditor.objectTypeId)) {
        std::vector<ObjectTypeId> ids;
        ids.reserve(document_.data().objectTypes.size());
        for (const auto& [id, unused] : document_.data().objectTypes) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        state_.logicBoardEditor.objectTypeId = ids.empty()
            ? std::optional<ObjectTypeId>{} : std::optional<ObjectTypeId>{ids.front()};
        extra |= EditorInvalidation::LogicBoard;
    }

    // An asset metadata command can remove a script while its workspace buffer
    // is open. Buffers never outlive their authoritative ScriptAssetDef: close
    // those dangling tabs here so execute/undo/redo/project replacement all
    // converge on the same reconciliation path.
    for (std::size_t i = state_.scriptEditor.buffers.size(); i > 0; --i) {
        const AssetId assetId = state_.scriptEditor.buffers[i - 1].scriptAssetId;
        if (!document_.hasScriptAsset(assetId)) {
            state_.scriptEditor.close(assetId);
            extra |= EditorInvalidation::ScriptEditor | EditorInvalidation::Toolbar;
        }
    }
    const auto firstDanglingDiagnostic = std::remove_if(
        console_.begin(), console_.end(), [&](const ConsoleMessage& message) {
            return message.scriptSource
                && !document_.hasScriptAsset(message.scriptSource->scriptAssetId);
        });
    if (firstDanglingDiagnostic != console_.end()) {
        console_.erase(firstDanglingDiagnostic, console_.end());
        extra |= EditorInvalidation::Console;
    }

    // Last: with the selection/scene/layer state above now fully valid, bring
    // the tool/gesture/tile-selection context back in line with it - covers
    // every Command that could change what the selected instance's tilemap
    // editability is (tileset swap, tilemap component removal, layer lock),
    // not just the selection-changing Intents that also call this directly.
    reconcileTilemapEditingContext();

    return extra;
}

void EditorCoordinator::reconcileWorkspaceAndAnnounce() {
    const std::string before = activeLayerId(state_.activeSceneId);
    accumulate(reconcileWorkspace());
    if (!state_.selection.hasEntity()) return;   // not an entity-layer move
    const std::string after = activeLayerId(state_.activeSceneId);
    if (after == before) return;   // edge-triggered: only a genuine change announces
    if (sceneView(state_.activeSceneId).hiddenLayerIds.count(after) == 0) return;
    std::string layerName = after;
    if (const SceneDef* scene = document_.findScene(state_.activeSceneId)) {
        for (const SceneLayerDef& layer : scene->layers) {
            if (layer.id == after) { layerName = layer.name; break; }
        }
    }
    appendConsole(ConsoleMessage::Level::Info,
                  "Entity moved to hidden layer \"" + layerName + "\"");
}

bool EditorCoordinator::selectionSupportsTilemapTool() const {
    return selectionSupportsTilemapEditing(document_, state_, state_.activeSceneId);
}

void EditorCoordinator::reconcileTilemapEditingContext() {
    // Palette views of deleted tilesets are meaningless in every branch; the
    // views of still-existing tilesets survive selection changes (they are
    // per-tileset UX preferences, not per-selection state).
    for (auto it = state_.tilemapEditor.paletteViews.begin();
         it != state_.tilemapEditor.paletteViews.end();) {
        if (!document_.findTilesetAsset(it->first)) {
            it = state_.tilemapEditor.paletteViews.erase(it);
        } else {
            ++it;
        }
    }
    if (selectionSupportsTilemapTool()) {
        reconcileStampAgainstTileset();
        return;
    }
    // The selection can no longer support a tilemap tool (nothing selected,
    // no TilemapComponent, moved off the active layer, or the layer got
    // locked) - a pending gesture belongs to whatever *was* selected, so it
    // must not survive; a stale stamp is meaningless without a target; and a
    // tilemap tool has nothing left to operate on, so it falls back to
    // Select (never Pan, which isn't a tilemap tool either and stays as-is).
    cancelPendingTilemapGesture();
    if (state_.tilemapEditor.stamp) {
        state_.tilemapEditor.stamp.reset();
        accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    }
    if (isTilemapTool(state_.activeTool)) {
        state_.activeTool = EditorTool::Select;
        accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    }
}

void EditorCoordinator::reconcileStampAgainstTileset() {
    if (!state_.tilemapEditor.stamp) return;
    const TilemapTileStamp& stamp = *state_.tilemapEditor.stamp;
    const SceneInstanceDef* inst =
        document_.findInstanceInScene(state_.activeSceneId, state_.selection.primaryEntity);
    // selectionSupportsTilemapTool() already guarantees inst && inst->tilemap
    // when reached via reconcileTilemapEditingContext(), but this stays
    // defensive since it's a distinct, independently callable step.
    bool valid = inst && inst->tilemap.has_value() && stampIsValid(stamp)
        // Provenance, not just id existence: after the tilemap switches to a
        // different tileset, identically-named ids must not keep the stamp
        // alive pointing at tiles the user never selected.
        && stamp.sourceTilesetAssetId == inst->tilemap->tilesetAssetId;
    if (valid) {
        const TilesetAsset* tileset = document_.findTilesetAsset(inst->tilemap->tilesetAssetId);
        valid = tileset != nullptr && std::all_of(
            stamp.tiles.begin(), stamp.tiles.end(),
            [&](const std::optional<TileId>& id) {
                return !id || std::any_of(
                    tileset->tiles.begin(), tileset->tiles.end(),
                    [&](const TileDefinition& tile) { return tile.id == *id; });
            });
    }
    // Keep a still-valid stamp, reset an invalid one WHOLE - never partially
    // repaired and never an implicit substitute, which could make Brush
    // silently paint something the user never chose.
    if (!valid) {
        state_.tilemapEditor.stamp.reset();
        accumulate(EditorInvalidation::Inspector | EditorInvalidation::Toolbar);
    }
}

EditorOperationResult EditorCoordinator::replaceProject(ProjectDocument replacement) {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Cannot replace project while Play is running");
        return EditorOperationResult::failure("Cannot replace project while Play is running");
    }

    document_.replaceClean(std::move(replacement));

    state_.activeSceneId = normalizedSceneId(document_);
    state_.selection.clear();
    state_.activeTool = EditorTool::Select;
    state_.centerWorkspaceMode = CenterWorkspaceMode::Scene;
    state_.sceneViews.clear();
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    state_.tilesetEditor = TilesetEditorState{};
    state_.tilemapEditor = TilemapEditorState{};
    state_.logicBoardEditor = LogicBoardEditorState{};
    state_.scriptEditor.clear();
    uiState_.inspectorRevealRequest.reset();
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

EditorOperationResult EditorCoordinator::apply(
    const CreateGeneratedSfxIntent& intent) {
    return execute(CreateGeneratedSfxCommand{
        intent.id, intent.name, intent.recipe});
}

EditorOperationResult EditorCoordinator::apply(
    const DuplicateGeneratedSfxIntent& intent) {
    return execute(DuplicateGeneratedSfxCommand{
        intent.sourceId, intent.newId, intent.newName});
}

EditorOperationResult EditorCoordinator::apply(
    const RenameGeneratedSfxIntent& intent) {
    return execute(RenameGeneratedSfxCommand{intent.id, intent.name});
}

EditorOperationResult EditorCoordinator::apply(
    const UpdateGeneratedSfxRecipeIntent& intent) {
    return execute(UpdateGeneratedSfxRecipeCommand{intent.id, intent.recipe});
}

EditorCommandSideEffectResult EditorCoordinator::validateCommandSideEffectRebase(
    const std::filesystem::path& previousRoot,
    const std::filesystem::path& nextRoot) {
    const auto result = history_.validateSideEffectRebase(previousRoot, nextRoot);
    if (!result.ok) appendConsole(
        ConsoleMessage::Level::Error,
        "Save As cannot preserve external Undo/Redo history: " + result.error);
    return result;
}

void EditorCoordinator::rebaseCommandSideEffects(
    const std::filesystem::path& previousRoot,
    const std::filesystem::path& nextRoot) {
    history_.rebaseSideEffects(previousRoot, nextRoot);
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
    return playProject({});
}

EditorOperationResult EditorCoordinator::playProject(
    const std::vector<Scripts::ScriptProgram>& scripts,
    const std::filesystem::path& assetRoot) {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning, "Already playing");
        return EditorOperationResult::failure("Already playing");
    }
    if (!canPlayProject()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Cannot play project: no valid start scene");
        return EditorOperationResult::failure("Cannot play project: no valid start scene");
    }
    // Drop uncommitted paint/rect workspace state — Play materializes the
    // document only; a mid-stroke preview must not outlive Start Play.
    cancelPendingTilemapGesture();
    std::string error;
    std::optional<PlaySession> session = PlaySession::startProject(document_, scripts, &error);
    if (!session.has_value()) {
        const std::string message = error.empty() ? "Cannot start Play" : error;
        appendConsole(ConsoleMessage::Level::Error, message);
        return EditorOperationResult::failure(message);
    }
    session->setAssetRoot(assetRoot);
    const CenterWorkspaceMode originWorkspace = state_.centerWorkspaceMode;
    const bool fromAuthoringWorkspace = originWorkspace != CenterWorkspaceMode::Scene;
    PlayNavigationState navigation;
    if (fromAuthoringWorkspace) {
        navigation.originWorkspace = originWorkspace;
        navigation.originObjectTypeId = state_.logicBoardEditor.objectTypeId;
        navigation.originLogicTab = state_.logicBoardEditor.tab;
        navigation.originLogicSearch = state_.logicBoardEditor.search;
        navigation.autoSwitchedToScene = true;
        navigation.returnToOriginArmed = true;
    }
    playSession_.emplace(std::move(*session));
    playLaunch_ = PlayLaunchState{PlayLaunchKind::Project, document_.startSceneId()};
    recordAppliedScriptSources(scripts);
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    if (fromAuthoringWorkspace) {
        state_.centerWorkspaceMode = CenterWorkspaceMode::Scene;
        playNavigation_ = std::move(navigation);
        logInfo(originWorkspace == CenterWorkspaceMode::Logic
            ? "Play project started - Scene runtime opened; Stop returns to Logic Board"
            : "Play project started - Scene runtime opened; Stop returns to Script Editor");
    } else {
        playNavigation_.reset();
        logInfo("Play project started (document untouched)");
    }
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    const EditorInvalidation invalidation = fromAuthoringWorkspace
        ? kPlayNavigationInvalidation : kPlayToggleInvalidation;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::playCurrentScene() {
    return playCurrentScene({});
}

EditorOperationResult EditorCoordinator::playCurrentScene(
    const std::vector<Scripts::ScriptProgram>& scripts,
    const std::filesystem::path& assetRoot) {
    if (isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning, "Already playing");
        return EditorOperationResult::failure("Already playing");
    }
    if (!canPlayCurrentScene()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "Cannot play current scene: no active scene");
        return EditorOperationResult::failure("Cannot play current scene: no active scene");
    }
    cancelPendingTilemapGesture();
    std::string error;
    std::optional<PlaySession> session =
        PlaySession::startActiveScene(document_, state_.activeSceneId, scripts, &error);
    if (!session.has_value()) {
        const std::string message = error.empty() ? "Cannot start Play" : error;
        appendConsole(ConsoleMessage::Level::Error, message);
        return EditorOperationResult::failure(message);
    }
    session->setAssetRoot(assetRoot);
    const CenterWorkspaceMode originWorkspace = state_.centerWorkspaceMode;
    const bool fromAuthoringWorkspace = originWorkspace != CenterWorkspaceMode::Scene;
    PlayNavigationState navigation;
    if (fromAuthoringWorkspace) {
        navigation.originWorkspace = originWorkspace;
        navigation.originObjectTypeId = state_.logicBoardEditor.objectTypeId;
        navigation.originLogicTab = state_.logicBoardEditor.tab;
        navigation.originLogicSearch = state_.logicBoardEditor.search;
        navigation.autoSwitchedToScene = true;
        navigation.returnToOriginArmed = true;
    }
    playSession_.emplace(std::move(*session));
    playLaunch_ = PlayLaunchState{PlayLaunchKind::CurrentScene, state_.activeSceneId};
    recordAppliedScriptSources(scripts);
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    if (fromAuthoringWorkspace) {
        state_.centerWorkspaceMode = CenterWorkspaceMode::Scene;
        playNavigation_ = std::move(navigation);
        logInfo(originWorkspace == CenterWorkspaceMode::Logic
            ? "Play current scene started - Scene runtime opened; Stop returns to Logic Board"
            : "Play current scene started - Scene runtime opened; Stop returns to Script Editor");
    } else {
        playNavigation_.reset();
        logInfo("Play current scene started (document untouched)");
    }
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    const EditorInvalidation invalidation = fromAuthoringWorkspace
        ? kPlayNavigationInvalidation : kPlayToggleInvalidation;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

void EditorCoordinator::recordAppliedScriptSources(
    const std::vector<Scripts::ScriptProgram>& scripts) {
    appliedPlayScriptSources_.clear();
    for (const Scripts::ScriptProgram& program : scripts)
        appliedPlayScriptSources_[program.assetId] = scriptSourceStamp(program.source);
    outdatedPlayScriptAssets_.clear();
}

EditorOperationResult EditorCoordinator::restartPlaying(
    const std::vector<Scripts::ScriptProgram>& scripts,
    const std::filesystem::path& assetRoot) {
    if (!isPlaying() || !playLaunch_) {
        appendConsole(ConsoleMessage::Level::Warning, "Cannot restart: Play is not running");
        return EditorOperationResult::failure("Cannot restart: Play is not running");
    }
    if (!scriptRestartRequired()) {
        appendConsole(ConsoleMessage::Level::Warning,
                      "No saved Script changes require restart");
        return EditorOperationResult::failure("No saved Script changes require restart");
    }

    std::string error;
    std::optional<PlaySession> replacement;
    if (playLaunch_->kind == PlayLaunchKind::Project) {
        replacement = PlaySession::startProject(document_, scripts, &error);
    } else {
        replacement = PlaySession::startActiveScene(
            document_, playLaunch_->sceneId, scripts, &error);
    }
    if (!replacement) {
        const std::string message = error.empty()
            ? "Cannot restart Play" : "Cannot restart Play: " + error;
        appendConsole(ConsoleMessage::Level::Error, message);
        return EditorOperationResult::failure(message);
    }
    replacement->setAssetRoot(assetRoot);

    const CenterWorkspaceMode originWorkspace = state_.centerWorkspaceMode;
    const bool fromAuthoringWorkspace = originWorkspace != CenterWorkspaceMode::Scene;
    PlayNavigationState navigation;
    if (fromAuthoringWorkspace) {
        navigation.originWorkspace = originWorkspace;
        navigation.originObjectTypeId = state_.logicBoardEditor.objectTypeId;
        navigation.originLogicTab = state_.logicBoardEditor.tab;
        navigation.originLogicSearch = state_.logicBoardEditor.search;
        navigation.autoSwitchedToScene = true;
        navigation.returnToOriginArmed = true;
    }

    // Materialization completed before this assignment, so a validation/load
    // failure above leaves the old runtime and navigation fully intact.
    playSession_ = std::move(*replacement);
    recordAppliedScriptSources(scripts);
    state_.spriteAnimationEditor = SpriteAnimationEditorState{};
    if (fromAuthoringWorkspace) {
        state_.centerWorkspaceMode = CenterWorkspaceMode::Scene;
        playNavigation_ = std::move(navigation);
        logInfo(originWorkspace == CenterWorkspaceMode::Script
            ? "Play restarted with saved Script changes; Stop returns to Script Editor"
            : "Play restarted; Stop returns to Logic Board");
    } else {
        logInfo("Play restarted with saved Script changes");
    }

    const EditorInvalidation invalidation = fromAuthoringWorkspace
        ? kPlayNavigationInvalidation : kPlayToggleInvalidation;
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

EditorOperationResult EditorCoordinator::stopPlaying() {
    if (!isPlaying()) {
        appendConsole(ConsoleMessage::Level::Warning, "Not playing");
        return EditorOperationResult::failure("Not playing");
    }
    playSession_.reset();   // RAII: back to the untouched authoring document
    EditorInvalidation invalidation = kPlayToggleInvalidation;
    if (playNavigation_ && playNavigation_->returnToOriginArmed) {
        state_.centerWorkspaceMode = playNavigation_->originWorkspace;
        if (playNavigation_->originWorkspace == CenterWorkspaceMode::Logic) {
            state_.logicBoardEditor.objectTypeId = playNavigation_->originObjectTypeId;
            state_.logicBoardEditor.tab = playNavigation_->originLogicTab;
            state_.logicBoardEditor.search = playNavigation_->originLogicSearch;
        }
        invalidation |= EditorInvalidation::Layout;
        logInfo(playNavigation_->originWorkspace == CenterWorkspaceMode::Logic
            ? "Stopped - returned to Logic Board"
            : "Stopped - returned to Script Editor");
    } else {
        logInfo("Stopped - back to authoring document");
    }
    playNavigation_.reset();
    playLaunch_.reset();
    appliedPlayScriptSources_.clear();
    outdatedPlayScriptAssets_.clear();
    // A Play/Stop toggle re-renders every authoring-affordance panel so their
    // controls switch to the frozen (disabled) state and back: Inspector fields,
    // Hierarchy create/delete buttons and Assets import/remove buttons.
    accumulate(invalidation);
    return EditorOperationResult::success(invalidation);
}

void EditorCoordinator::tickRuntime(const RuntimeInputSnapshot& input, float dt) {
    if (!playSession_) return;
    playSession_->tick(input, dt);
    const std::vector<Scripts::ScriptRuntimeDiagnostic> diagnostics =
        playSession_->drainScriptDiagnostics();
    for (const Scripts::ScriptRuntimeDiagnostic& diagnostic : diagnostics) {
        console_.push_back(ConsoleMessage{
            ConsoleMessage::Level::Error,
            diagnostic.sourcePath + " [SCRIPT_RUNTIME/" + diagnostic.callback + "] entity "
                + std::to_string(diagnostic.owner) + ": " + diagnostic.message,
            ConsoleMessage::ScriptSource{diagnostic.scriptAssetId,
                                         diagnostic.sourcePath,
                                         diagnostic.line, diagnostic.column}});
    }
    if (!diagnostics.empty()) accumulate(EditorInvalidation::Console);
}

// ----------------------------------------------------------------------------
// Intent path — workspace state only; never the ProjectDocument, never undo.
// ----------------------------------------------------------------------------
EditorOperationResult EditorCoordinator::finishIntent(EditorOperationResult result) {
    if (!result.ok) appendConsole(ConsoleMessage::Level::Warning, result.error);
    return result;
}
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
    console_.push_back(ConsoleMessage{level, std::move(text), std::nullopt});
    accumulate(EditorInvalidation::Console);
}

void EditorCoordinator::replaceScriptDiagnostics(
    const AssetId& scriptAssetId,
    const std::vector<ScriptDiagnostic>& diagnostics) {
    console_.erase(std::remove_if(console_.begin(), console_.end(),
        [&](const ConsoleMessage& message) {
            return message.scriptSource
                && message.scriptSource->scriptAssetId == scriptAssetId;
        }), console_.end());
    for (const ScriptDiagnostic& diagnostic : diagnostics) {
        ConsoleMessage::Level level = ConsoleMessage::Level::Error;
        if (diagnostic.severity == DiagnosticSeverity::Info)
            level = ConsoleMessage::Level::Info;
        else if (diagnostic.severity == DiagnosticSeverity::Warning)
            level = ConsoleMessage::Level::Warning;
        const std::string location = diagnostic.path
            + (diagnostic.line > 0 ? ":" + std::to_string(diagnostic.line) : std::string{})
            + (diagnostic.column > 0 ? ":" + std::to_string(diagnostic.column) : std::string{});
        console_.push_back(ConsoleMessage{
            level, location + " [" + diagnostic.code + "] " + diagnostic.message,
            ConsoleMessage::ScriptSource{diagnostic.scriptAssetId, diagnostic.path,
                                         diagnostic.line, diagnostic.column}});
    }
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

void EditorCoordinator::reportScriptDiagnostics(
    const AssetId& scriptAssetId,
    const std::vector<ScriptDiagnostic>& diagnostics) {
    replaceScriptDiagnostics(scriptAssetId, diagnostics);
    accumulate(EditorInvalidation::Console);
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
