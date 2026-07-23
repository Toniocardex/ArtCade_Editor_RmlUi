#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class AddBoxColliderCommand;
class CreateEntityCommand;
class CreateSceneCommand;
class DeleteEntityCommand;
class DeleteSceneCommand;
class EditorCoordinator;
class AddLinearMoverCommand;
class RemoveBoxColliderCommand;
class RemoveLinearMoverCommand;
class RenameEntityCommand;
class SetEntityTransformCommand;
struct AuthoredTransformPatch;
class SetBoxColliderEnabledCommand;
class SetLinearMoverDirectionCommand;
class SetLinearMoverSpeedCommand;
class AddTopDownControllerCommand;
class RemoveTopDownControllerCommand;
class SetTopDownControllerSpeedCommand;
class AddSceneLayerCommand;
class RenameSceneLayerCommand;
class MoveSceneLayerCommand;
class RemoveSceneLayerCommand;
class SetEntityLayerCommand;
class SetLayerLockedCommand;
class AddPlatformerControllerCommand;
class RemovePlatformerControllerCommand;
class SetPlatformerValueCommand;
class AddImageAssetCommand;
class RemoveImageAssetCommand;
class AddAudioAssetCommand;
class RenameAudioAssetCommand;
class RemoveAudioAssetCommand;
class CreateGeneratedSfxCommand;
class RenameGeneratedSfxCommand;
class UpdateGeneratedSfxRecipeCommand;
class RemoveGeneratedSfxCommand;
class RegisterGeneratedSfxOutputCommand;
class AddFontAssetCommand;
class RemoveFontAssetCommand;
class AddScriptAssetCommand;
class RemoveScriptAssetCommand;
class RenameScriptAssetCommand;
class AddScriptAttachmentCommand;
class RemoveScriptAttachmentCommand;
class MoveScriptAttachmentCommand;
class SetScriptAttachmentEnabledCommand;
class SetBoxColliderOffsetCommand;
class SetBoxColliderSizeCommand;
class SetBoxColliderModeCommand;
class RenameSceneCommand;
class SetSceneSizeCommand;
class SetSceneViewportSizeCommand;
class SetSceneBackgroundCommand;
class AddSpriteAnimationAssetCommand;
class RemoveSpriteAnimationAssetCommand;
class AddAnimationClipCommand;
class RenameAnimationClipCommand;
class RemoveAnimationClipCommand;
class ReplaceAnimationFramesCommand;
class SetAnimationClipFrameIdsCommand;
class SetAnimationClipFrameRateCommand;
class SetAnimationClipPlaybackModeCommand;
class AddTilemapComponentCommand;
class RemoveTilemapComponentCommand;
class SetTilemapTilesetCommand;
class SetTilemapCellSizeCommand;
class PaintTilemapCellsCommand;
class SetStartSceneCommand;
class RenameProjectCommand;
class ChangeLogicConditionTypeCommand;
class SetLogicAnimationClipCommand;
class RepairIncompatibleLogicCommand;
class AddSpriteRendererToObjectTypeCommand;
class RemoveSpriteRendererFromObjectTypeCommand;
class SetObjectTypeSpriteSourceCommand;
class AddSpriteAnimatorToObjectTypeCommand;
class RemoveSpriteAnimatorFromObjectTypeCommand;
class SetObjectTypeInitialClipCommand;
class SetObjectTypeAutoPlayCommand;
class SetObjectTypePlaybackSpeedCommand;
class SetInstanceSpriteOverrideCommand;
class SetInstanceAnimatorOverrideCommand;
class ClearInstanceSpriteOverrideCommand;
class ClearInstanceAnimatorOverrideCommand;
class AddGlobalVariableCommand;
class RemoveGlobalVariableCommand;
class RenameGlobalVariableCommand;
class SetGlobalVariableTypeCommand;
class SetGlobalVariableInitialValueCommand;
class SetGlobalVariableDescriptionCommand;
class SetLogicConditionJoinCommand;
class SetLogicConditionNegatedCommand;
class SetInstanceVisibleCommand;
class SetTopDownControllerAccelerationCommand;
class SetTopDownControllerFrictionCommand;
class SetTopDownControllerFourDirectionsCommand;
class AddAutoDestroyCommand;
class RemoveAutoDestroyCommand;
class SetAutoDestroyLifespanCommand;

// =============================================================================
// ProjectDocument — the single authoring authority of the native editor.
//
// It OWNS the canonical ProjectDoc (runtime-cpp/src/core/types.h). There is no
// parallel UiProjectModel / InspectorProjectModel / RuntimeProjectCopy: panels
// query this object and mutate it only through commands (prompt §3).
//
// Three structural verbs map to the runtime projection (prompt §7):
//   replace()        — Replace: open / recovery / import / full swap
//   setInstance*()   — Patch  : local mutation of one instance in an explicit scene
//
// `replaceCount()` and `revision()` are observable spies so tests can prove a
// selection or editor scene change performs neither a Replace nor a serialization.
// =============================================================================
class ProjectDocument {
public:
    ProjectDocument() = default;
    explicit ProjectDocument(ProjectDoc doc);

    // ---- queries (read-only) -------------------------------------------------
    const ProjectDoc&        data() const { return doc_; }
    const SceneId&           startSceneId() const { return doc_.activeSceneId; }
    const SceneDef*          findScene(const SceneId& id) const;
    bool                     hasScene(const SceneId& id) const;
    const SceneInstanceDef*  findInstanceInScene(const SceneId& sceneId, EntityId id) const;
    /** True if @p id is a known object type (ProjectDoc.objectTypes). */
    bool                     hasObjectType(const std::string& id) const;
    /** True if @p layerId is a render layer of scene @p sceneId. */
    bool                     hasLayer(const SceneId& sceneId, const std::string& layerId) const;
    /** True if @p layerId exists in @p sceneId and is editor-locked (pick
     *  gating). False if the scene or layer doesn't resolve. */
    bool                     isLayerLocked(const SceneId& sceneId, const std::string& layerId) const;
    /** Convenience over effectiveLayerId + isLayerLocked: true if @p instance's
     *  effective layer in @p sceneId is locked. This is the gate every
     *  instance-owned command (addressed by SceneId + EntityId) checks before
     *  mutating. Components owned by the shared object type (BoxCollider2D,
     *  LinearMover, TopDownController, PlatformerController - addressed by
     *  objectTypeId only, potentially shared by instances on other layers) are
     *  deliberately never gated by this. */
    bool                     isInstanceLayerLocked(const SceneId& sceneId,
                                                    const SceneInstanceDef& instance) const;
    const EntityDef*         findObjectType(const std::string& id) const;
    /** True if @p id is a known image asset (ProjectDoc.imageAssets). */
    bool                     hasImageAsset(const AssetId& id) const;
    const ImageAssetDef*     findImageAsset(const AssetId& id) const;
    bool                     hasSpriteAnimationAsset(const AssetId& id) const;
    const SpriteAnimationAssetDef* findSpriteAnimationAsset(const AssetId& id) const;
    bool                     hasAudioAsset(const AssetId& id) const;
    const AudioAssetDef*     findAudioAsset(const AssetId& id) const;
    bool                     hasGeneratedSfx(const std::string& id) const;
    const artcade::sfx::GeneratedSfxDef* findGeneratedSfx(const std::string& id) const;
    /** Recipe that currently owns @p outputAssetId, or nullptr if independent. */
    const artcade::sfx::GeneratedSfxDef* findGeneratedSfxByOutputAssetId(
        const AssetId& outputAssetId) const;
    bool                     hasFontAsset(const AssetId& id) const;
    const FontAssetDef*      findFontAsset(const AssetId& id) const;
    bool                     hasScriptAsset(const AssetId& id) const;
    const ScriptAssetDef*    findScriptAsset(const AssetId& id) const;
    /** Deterministic unique Script Asset ids referenced by Object Types.
     *  Disabled attachments are omitted when @p enabledOnly is true. */
    std::vector<AssetId>     referencedScriptAssetIds(bool enabledOnly) const;
    bool                     hasTilesetAsset(const AssetId& id) const;
    const TilesetAsset*      findTilesetAsset(const AssetId& id) const;
    /** @p instance's effective layer: its layerId if it names a real layer of
     *  @p sceneId, otherwise the scene's defaultLayerId ("" / legacy /
     *  dangling -> default). Empty string if the scene doesn't exist. */
    std::string effectiveLayerId(const SceneId& sceneId, const SceneInstanceDef& instance) const;
    /** Every instance of @p sceneId in back-to-front RENDER order: layers[0]
     *  (background) first, the last layer (foreground) last; a legacy scene
     *  with no layers keeps SceneDef::instances' own order. This is a
     *  visual/render order only - it must never be used to reorder a runtime
     *  simulation container (PlaySession keeps RuntimeScene::entities in
     *  structural order; see RuntimeScene::renderOrder for its own separate
     *  render-order index list). Empty if the scene does not exist. */
    std::vector<const SceneInstanceDef*> instancesInRenderOrder(const SceneId& sceneId) const;

    bool      isDirty()      const { return revision_ != savedRevision_; }
    uint64_t  revision()     const { return revision_; }
    uint64_t  savedRevision() const { return savedRevision_; }
    uint32_t  replaceCount() const { return replaceCount_; }

    // ---- Replace (structural) -----------------------------------------------
    /** Full document swap. The only place replaceCount is bumped. */
    void replace(ProjectDoc doc);

private:
    friend class AddBoxColliderCommand;
    friend class AddLinearMoverCommand;
    friend class AddSpriteRendererToObjectTypeCommand;
    friend class SetObjectTypeSpritePresentationCommand;
    friend class SetInstanceSpritePresentationOverrideCommand;
    friend class RemoveSpriteRendererFromObjectTypeCommand;
    friend class SetObjectTypeSpriteSourceCommand;
    friend class AddSpriteAnimatorToObjectTypeCommand;
    friend class RemoveSpriteAnimatorFromObjectTypeCommand;
    friend class SetObjectTypeInitialClipCommand;
    friend class SetObjectTypeAutoPlayCommand;
    friend class SetObjectTypePlaybackSpeedCommand;
    friend class SetInstanceSpriteOverrideCommand;
    friend class SetInstanceAnimatorOverrideCommand;
    friend class ClearInstanceSpriteOverrideCommand;
    friend class ClearInstanceAnimatorOverrideCommand;
    friend class CloneInstanceCommand;
    friend class CreateEntityCommand;
    friend class CreateEntityWithDefaultTypeCommand;
    friend class CreateSceneCommand;
    friend class DeleteEntityCommand;
    friend class DeleteSceneCommand;
    friend class EditorCoordinator;
    friend class RemoveBoxColliderCommand;
    friend class RemoveLinearMoverCommand;
    friend class RenameEntityCommand;
    friend class SetEntityTransformCommand;
    friend class SetInstanceVisibleCommand;
    friend class SetBoxColliderEnabledCommand;
    friend class SetBoxColliderOffsetCommand;
    friend class SetBoxColliderSizeCommand;
    friend class SetBoxColliderModeCommand;
    friend class SetLinearMoverDirectionCommand;
    friend class SetLinearMoverSpeedCommand;
    friend class AddTopDownControllerCommand;
    friend class RemoveTopDownControllerCommand;
    friend class SetTopDownControllerSpeedCommand;
    friend class SetTopDownControllerAccelerationCommand;
    friend class SetTopDownControllerFrictionCommand;
    friend class SetTopDownControllerFourDirectionsCommand;
    friend class AddAutoDestroyCommand;
    friend class RemoveAutoDestroyCommand;
    friend class SetAutoDestroyLifespanCommand;
    friend class AddCameraTargetCommand;
    friend class RemoveCameraTargetCommand;
    friend class SetCameraTargetOffsetCommand;
    friend class SetCameraTargetFollowSpeedCommand;
    friend class AddSceneLayerCommand;
    friend class RenameSceneLayerCommand;
    friend class MoveSceneLayerCommand;
    friend class RemoveSceneLayerCommand;
    friend class SetEntityLayerCommand;
    friend class SetLayerLockedCommand;
    friend class AddPlatformerControllerCommand;
    friend class RemovePlatformerControllerCommand;
    friend class SetPlatformerValueCommand;
    friend class AddImageAssetCommand;
    friend class RemoveImageAssetCommand;
    friend class AddAudioAssetCommand;
    friend class RenameAudioAssetCommand;
    friend class RemoveAudioAssetCommand;
    friend class CreateGeneratedSfxCommand;
    friend class RenameGeneratedSfxCommand;
    friend class UpdateGeneratedSfxRecipeCommand;
    friend class RemoveGeneratedSfxCommand;
    friend class DuplicateGeneratedSfxCommand;
    friend class RegisterGeneratedSfxOutputCommand;
    friend class AddFontAssetCommand;
    friend class RemoveFontAssetCommand;
    friend class AddScriptAssetCommand;
    friend class RemoveScriptAssetCommand;
    friend class RenameScriptAssetCommand;
    friend class AddScriptAttachmentCommand;
    friend class RemoveScriptAttachmentCommand;
    friend class MoveScriptAttachmentCommand;
    friend class SetScriptAttachmentEnabledCommand;
    friend class RenameSceneCommand;
    friend class SetSceneSizeCommand;
    friend class SetSceneViewportSizeCommand;
    friend class SetSceneBackgroundCommand;
    friend class CreateSpriteAnimationAssetCommand;
    friend class AddSpriteAnimationAssetCommand;
    friend class RemoveSpriteAnimationAssetCommand;
    friend class AddAnimationClipCommand;
    friend class RenameAnimationClipCommand;
    friend class RemoveAnimationClipCommand;
    friend class ReplaceAnimationFramesCommand;
    friend class ReplaceAnimationSourceImageCommand;
    friend class DuplicateSpriteAnimationAssetCommand;
    friend class DuplicateAnimationClipCommand;
    friend class SetAnimationClipFrameIdsCommand;
    friend class SetAnimationClipFrameRateCommand;
    friend class SetAnimationClipPlaybackModeCommand;
    friend class CreateTilemapEntityCommand;
    friend class AddTilemapComponentCommand;
    friend class RemoveTilemapComponentCommand;
    friend class SetTilemapTilesetCommand;
    friend class SetTilemapCellSizeCommand;
    friend class PaintTilemapCellsCommand;
    friend class SetStartSceneCommand;
    friend class RenameProjectCommand;
    friend class RenameObjectTypeCommand;
    friend class AddTilesetAssetCommand;
    friend class RemoveTilesetAssetCommand;
    friend class RenameTilesetCommand;
    friend class ChangeTilesetSlicingCommand;
    friend class CreateLogicBoardCommand;
    friend class AddGlobalVariableCommand;
    friend class RemoveGlobalVariableCommand;
    friend class RenameGlobalVariableCommand;
    friend class SetGlobalVariableTypeCommand;
    friend class SetGlobalVariableInitialValueCommand;
    friend class SetGlobalVariableDescriptionCommand;
    friend class RemoveLogicBoardCommand;
    friend class AddLogicRuleCommand;
    friend class DuplicateLogicRuleCommand;
    friend class RemoveLogicRuleCommand;
    friend class MoveLogicRuleCommand;
    friend class SetLogicRuleEnabledCommand;
    friend class SetLogicRuleExecutionModeCommand;
    friend class ReplaceLogicTriggerCommand;
    friend class AddLogicActionCommand;
    friend class RemoveLogicActionCommand;
    friend class MoveLogicActionCommand;
    friend class ChangeLogicActionTypeCommand;
    friend class AddLogicConditionCommand;
    friend class RemoveLogicConditionCommand;
    friend class MoveLogicConditionCommand;
    friend class ChangeLogicConditionTypeCommand;
    friend class SetLogicConditionJoinCommand;
    friend class SetLogicConditionNegatedCommand;
    friend class SetLogicPropertyCommand;
    friend class SetLogicAnimationClipCommand;
    friend class RepairIncompatibleLogicCommand;

    // ---- Patch (authoring mutations; called by commands) --------------------
    bool setProjectName(std::string name);
    // Authored fields only (position / rotation / scale). Never touches velocity.
    bool patchInstanceTransform(const SceneId& sceneId, EntityId id,
                                const AuthoredTransformPatch& patch);
    bool setInstanceName(const SceneId& sceneId, EntityId id, std::string name);
    bool setInstanceVisible(const SceneId& sceneId, EntityId id, bool visible);
    bool setSceneName(const SceneId& sceneId, std::string name);
    // The scene world size (Dimensions). Resizing never moves instances — an
    // entity left outside the new bounds keeps its coordinates (Outside Scene UX
    // flags it); only rendering/clipping/spawn-centre derive from this.
    bool setSceneSize(const SceneId& sceneId, Vec2 size);
    bool setSceneViewportSize(const SceneId& sceneId, Vec2 size);
    bool setSceneBackground(const SceneId& sceneId, Vec4 color);
    // The persisted gameplay start scene. Empty is allowed only when there are
    // no scenes; a non-empty id must reference an existing scene.
    bool setStartSceneId(const SceneId& sceneId);
    bool createScene(const SceneId& id, const std::string& name);
    bool deleteScene(const SceneId& id);
    // Restore a previously deleted scene with its instances and the start-scene
    // id that was active before deletion — the exact inverse of deleteScene.
    bool restoreScene(SceneDef scene, const SceneId& startSceneId);
    // Instance structural verbs. createInstance appends; insertInstance places at
    // a captured index so DeleteEntityCommand undo restores the original order.
    // Object type catalog. The editor creates a real object type for the first
    // entity placed in an empty catalog, so every instance.objectTypeId resolves
    // to a persisted ObjectTypeDef (no "Entity" sentinel id). createObjectType
    // rejects a duplicate id; removeObjectType is its exact inverse.
    bool createObjectType(EntityDef type);
    bool removeObjectType(const std::string& id);
    bool setObjectTypeName(const std::string& id, std::string name);
    // Per-scene render layers. `layers` is the single order authority (index 0 =
    // background). addSceneLayer inserts at an index; moveSceneLayer reorders in
    // place; removeSceneLayer drops a layer (caller enforces the empty/non-default
    // policy); setInstanceLayer reassigns one instance. The default layer is
    // created by createScene and is never removed by these verbs.
    bool addSceneLayer(const SceneId& sceneId, const std::string& layerId,
                       const std::string& name, std::size_t index);
    bool renameSceneLayer(const SceneId& sceneId, const std::string& layerId,
                          const std::string& name);
    bool moveSceneLayer(const SceneId& sceneId, const std::string& layerId, std::size_t index);
    bool removeSceneLayer(const SceneId& sceneId, const std::string& layerId);
    bool setInstanceLayer(const SceneId& sceneId, EntityId id, const std::string& layerId);
    bool setLayerLocked(const SceneId& sceneId, const std::string& layerId, bool locked);
    bool createInstance(const SceneId& sceneId, SceneInstanceDef instance);
    bool insertInstance(const SceneId& sceneId, std::size_t index, SceneInstanceDef instance);
    bool deleteInstance(const SceneId& sceneId, EntityId id);
    bool addTilemapComponent(const SceneId& sceneId, EntityId id, TilemapComponent component);
    bool removeTilemapComponent(const SceneId& sceneId, EntityId id);
    bool setTilemapTileset(const SceneId& sceneId, EntityId id, AssetId tilesetAssetId);
    bool setTilemapCellSize(const SceneId& sceneId, EntityId id, Vec2 cellSize);
    // Wholesale replace - the only mutator PaintTilemapCellsCommand needs,
    // since it builds one complete validated next-state locally (patched
    // cells + created/removed chunks) rather than mutating cell-by-cell.
    bool setTilemapComponent(const SceneId& sceneId, EntityId id, TilemapComponent replacement);
    bool addCameraTarget(const SceneId& sceneId, EntityId id, CameraTargetComponent component);
    bool removeCameraTarget(const SceneId& sceneId, EntityId id);
    bool setCameraTarget(const SceneId& sceneId, EntityId id, CameraTargetComponent component);
    // BoxCollider2D is authored on the object type only; instances never store it.
    bool addBoxCollider(const std::string& objectTypeId, BoxCollider2DComponent component);
    bool removeBoxCollider(const std::string& objectTypeId);
    bool setBoxColliderOffset(const std::string& objectTypeId, Vec2 offset);
    bool setBoxColliderSize(const std::string& objectTypeId, Vec2 size);
    bool setBoxColliderEnabled(const std::string& objectTypeId, bool enabled);
    bool setBoxColliderMode(const std::string& objectTypeId, BoxColliderMode mode);
    // LinearMover is authored on the object type only (canonical gameplay
    // component); instances never store it. Pause is a runtime flag, not edited.
    bool addLinearMover(const std::string& objectTypeId, LinearMoverComponent component);
    bool removeLinearMover(const std::string& objectTypeId);
    bool setLinearMoverDirection(const std::string& objectTypeId, Vec2 direction);
    bool setLinearMoverSpeed(const std::string& objectTypeId, float speed);
    // TopDownController is authored on the object type only.
    bool addTopDownController(const std::string& objectTypeId, TopDownControllerComponent component);
    bool removeTopDownController(const std::string& objectTypeId);
    bool setTopDownControllerSpeed(const std::string& objectTypeId, float speed);
    bool setTopDownControllerAcceleration(const std::string& objectTypeId, float acceleration);
    bool setTopDownControllerFriction(const std::string& objectTypeId, float friction);
    bool setTopDownControllerFourDirections(const std::string& objectTypeId, bool fourDirections);
    // Object-type authored; a materialized runtime instance owns _timeAlive.
    // lifespan == 0 deliberately disables automatic expiry.
    bool addAutoDestroy(const std::string& objectTypeId, AutoDestroyComponent component);
    bool removeAutoDestroy(const std::string& objectTypeId);
    bool setAutoDestroyLifespan(const std::string& objectTypeId, float lifespan);
    // PlatformerController is authored on the object type only. `field` selects the
    // canonical scalar (0 = maxSpeed … 5 = climbSpeed), matching
    // commands/PlatformerField so this header stays free of the command enum.
    bool addPlatformerController(const std::string& objectTypeId, PlatformerControllerComponent component);
    bool removePlatformerController(const std::string& objectTypeId);
    bool setPlatformerValue(const std::string& objectTypeId, int field, float value);
    bool replaceLogicBoard(const std::string& objectTypeId,
                           std::optional<LogicBoardDef> board);
    // Image asset catalog. The application copies the file on disk; the document
    // only records AssetId + portable relative sourcePath. Add rejects a
    // duplicate AssetId. Removal does not touch the file on disk.
    bool addImageAsset(ImageAssetDef asset);
    bool removeImageAsset(const AssetId& assetId);
    bool addSpriteAnimationAsset(SpriteAnimationAssetDef asset);
    bool removeSpriteAnimationAsset(const AssetId& assetId);
    bool addAnimationClip(const AssetId& assetId, SpriteAnimationClipDef clip);
    bool renameAnimationClip(const AssetId& assetId, const std::string& clipId, std::string name);
    bool removeAnimationClip(const AssetId& assetId, const std::string& clipId);
    // Replaces the asset frame pool and clears every clip's frameIds (sequences
    // must be re-authored against the new pool).
    bool replaceAnimationFrames(const AssetId& assetId, std::vector<SpriteFrameDef> frames);
    bool setAnimationClipFrameIds(const AssetId& assetId, const std::string& clipId,
                                  std::vector<SpriteFrameId> frameIds);
    // Rebinds the sheet only while the frame pool and all clip sequences are empty.
    bool setAnimationSourceImage(const AssetId& assetId, AssetId imageId);
    // Atomic source change: new image, empty frame pool, empty clip sequences.
    bool replaceAnimationSourceImage(const AssetId& assetId, AssetId imageId);
    bool setAnimationClipFrameRate(const AssetId& assetId, const std::string& clipId, float fps);
    bool setAnimationClipPlaybackMode(const AssetId& assetId, const std::string& clipId,
                                      AnimationPlaybackMode mode);
    bool addAudioAsset(AudioAssetDef asset);
    bool removeAudioAsset(const AssetId& assetId);
    bool addFontAsset(FontAssetDef asset);
    bool removeFontAsset(const AssetId& assetId);
    bool addScriptAsset(ScriptAssetDef asset);
    bool removeScriptAsset(const AssetId& assetId);
    bool setScriptAssetName(const AssetId& assetId, std::string name);
    bool replaceScriptComponent(const ObjectTypeId& objectTypeId,
                                std::optional<ScriptComponent> scripts);
    // Tileset asset catalog (Slice 1: data model only, no TilemapComponent
    // references it yet). Add rejects a duplicate assetId. setTilesetSlicing
    // takes both the new slicing config and the new tiles together, since a
    // slicing change always invalidates any previously-sliced tiles.
    bool addTilesetAsset(TilesetAsset asset);
    bool removeTilesetAsset(const AssetId& assetId);
    bool setTilesetName(const AssetId& assetId, std::string name);
    bool setTilesetSlicing(const AssetId& assetId, TilesetSlicing slicing,
                           std::vector<TileDefinition> tiles);
    // Commit a fully prepared authoring candidate as one Command mutation. This
    // does not represent Project Replace: replaceCount and savedRevision remain
    // untouched, while exactly one fresh dirty revision is allocated.
    void commitStagedCommand(ProjectDoc staged);
    void replaceClean(ProjectDocument replacement);
    void markSaved();
    // Set the current revision to a previously observed value (undo/redo). Unlike
    // markDirty it does not allocate a new id, so a redo back to the saved state
    // reports clean again. Never moves the monotonic high-water mark.
    void restoreRevision(uint64_t revision) { revision_ = revision; }

    SceneDef*         mutableScene(const SceneId& id);
    SceneInstanceDef* mutableInstanceInScene(const SceneId& sceneId, EntityId id);
    void              markDirty();

    ProjectDoc doc_{};
    uint64_t   revision_     = 0;
    uint64_t   savedRevision_ = 0;
    // Monotonic allocator for revision ids. It never decreases, so a command
    // executed after an undo gets a fresh id that cannot collide with the
    // (now discarded) redo branch — keeping isDirty() correct.
    uint64_t   revisionHighWater_ = 0;
    uint32_t   replaceCount_ = 0;
};

/** Single display-name query for Audio assets.
 *  Linked Generated SFX output → GeneratedSfxDef.name.
 *  Independent/imported audio → AudioAssetDef.name (fallback assetId). */
[[nodiscard]] std::string resolveAudioAssetDisplayName(
    const ProjectDocument& document,
    const AudioAssetDef& audio);

} // namespace ArtCade::EditorNative
