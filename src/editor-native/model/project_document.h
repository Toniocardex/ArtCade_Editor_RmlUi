#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ArtCade::EditorNative {

class AddBoxColliderCommand;
class AddSpriteRendererCommand;
class CreateEntityCommand;
class CreateSceneCommand;
class DeleteEntityCommand;
class DeleteSceneCommand;
class EditorCoordinator;
class AddLinearMoverCommand;
class RemoveBoxColliderCommand;
class RemoveLinearMoverCommand;
class RemoveSpriteRendererCommand;
class RenameEntityCommand;
class SetEntityPositionCommand;
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
class AddPlatformerControllerCommand;
class RemovePlatformerControllerCommand;
class SetPlatformerValueCommand;
class AddImageAssetCommand;
class RemoveImageAssetCommand;
class AddAudioAssetCommand;
class RemoveAudioAssetCommand;
class AddFontAssetCommand;
class RemoveFontAssetCommand;
class SetBoxColliderOffsetCommand;
class SetBoxColliderSizeCommand;
class SetBoxColliderModeCommand;
class RenameSceneCommand;
class SetSceneSizeCommand;
class SetSceneBackgroundCommand;
class SetSpriteRendererAssetCommand;
class SetSpriteRendererAnimationCommand;
class SetSpriteRendererVisibleCommand;
class AddSpriteAnimationAssetCommand;
class RemoveSpriteAnimationAssetCommand;
class AddAnimationClipCommand;
class RenameAnimationClipCommand;
class RemoveAnimationClipCommand;
class SetAnimationClipFramesCommand;
class SetAnimationClipFrameRateCommand;
class SetAnimationClipPlaybackModeCommand;
class AddSpriteAnimatorCommand;
class RemoveSpriteAnimatorCommand;
class SetSpriteAnimatorInitialClipCommand;
class SetSpriteAnimatorPlaybackSpeedCommand;
class SetSpriteAnimatorAutoPlayCommand;
class SetStartSceneCommand;
class RenameProjectCommand;

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
    const EntityDef*         findObjectType(const std::string& id) const;
    /** True if @p id is a known image asset (ProjectDoc.imageAssets). */
    bool                     hasImageAsset(const AssetId& id) const;
    const ImageAssetDef*     findImageAsset(const AssetId& id) const;
    bool                     hasSpriteAnimationAsset(const AssetId& id) const;
    const SpriteAnimationAssetDef* findSpriteAnimationAsset(const AssetId& id) const;
    bool                     hasAudioAsset(const AssetId& id) const;
    const AudioAssetDef*     findAudioAsset(const AssetId& id) const;
    bool                     hasFontAsset(const AssetId& id) const;
    const FontAssetDef*      findFontAsset(const AssetId& id) const;

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
    friend class AddSpriteRendererCommand;
    friend class CloneInstanceCommand;
    friend class CreateEntityCommand;
    friend class CreateEntityWithDefaultTypeCommand;
    friend class CreateSceneCommand;
    friend class DeleteEntityCommand;
    friend class DeleteSceneCommand;
    friend class EditorCoordinator;
    friend class RemoveBoxColliderCommand;
    friend class RemoveLinearMoverCommand;
    friend class RemoveSpriteRendererCommand;
    friend class RenameEntityCommand;
    friend class SetEntityPositionCommand;
    friend class SetBoxColliderEnabledCommand;
    friend class SetBoxColliderOffsetCommand;
    friend class SetBoxColliderSizeCommand;
    friend class SetBoxColliderModeCommand;
    friend class SetLinearMoverDirectionCommand;
    friend class SetLinearMoverSpeedCommand;
    friend class AddTopDownControllerCommand;
    friend class RemoveTopDownControllerCommand;
    friend class SetTopDownControllerSpeedCommand;
    friend class AddSceneLayerCommand;
    friend class RenameSceneLayerCommand;
    friend class MoveSceneLayerCommand;
    friend class RemoveSceneLayerCommand;
    friend class SetEntityLayerCommand;
    friend class AddPlatformerControllerCommand;
    friend class RemovePlatformerControllerCommand;
    friend class SetPlatformerValueCommand;
    friend class AddImageAssetCommand;
    friend class RemoveImageAssetCommand;
    friend class AddAudioAssetCommand;
    friend class RemoveAudioAssetCommand;
    friend class AddFontAssetCommand;
    friend class RemoveFontAssetCommand;
    friend class RenameSceneCommand;
    friend class SetSceneSizeCommand;
    friend class SetSceneBackgroundCommand;
    friend class SetSpriteRendererAssetCommand;
    friend class SetSpriteRendererAnimationCommand;
    friend class SetSpriteRendererVisibleCommand;
    friend class AddSpriteAnimationAssetCommand;
    friend class RemoveSpriteAnimationAssetCommand;
    friend class AddAnimationClipCommand;
    friend class RenameAnimationClipCommand;
    friend class RemoveAnimationClipCommand;
    friend class SetAnimationClipFramesCommand;
    friend class SetAnimationClipFrameRateCommand;
    friend class SetAnimationClipPlaybackModeCommand;
    friend class AddSpriteAnimatorCommand;
    friend class RemoveSpriteAnimatorCommand;
    friend class SetSpriteAnimatorInitialClipCommand;
    friend class SetSpriteAnimatorPlaybackSpeedCommand;
    friend class SetSpriteAnimatorAutoPlayCommand;
    friend class SetStartSceneCommand;
    friend class RenameProjectCommand;
    friend class RenameObjectTypeCommand;

    // ---- Patch (authoring mutations; called by commands) --------------------
    bool setProjectName(std::string name);
    bool setInstancePosition(const SceneId& sceneId, EntityId id, Vec2 position);
    bool setInstanceName(const SceneId& sceneId, EntityId id, std::string name);
    bool setSceneName(const SceneId& sceneId, std::string name);
    // The scene world size (Dimensions). Resizing never moves instances — an
    // entity left outside the new bounds keeps its coordinates (Outside Scene UX
    // flags it); only rendering/clipping/spawn-centre derive from this.
    bool setSceneSize(const SceneId& sceneId, Vec2 size);
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
    bool createInstance(const SceneId& sceneId, SceneInstanceDef instance);
    bool insertInstance(const SceneId& sceneId, std::size_t index, SceneInstanceDef instance);
    bool deleteInstance(const SceneId& sceneId, EntityId id);
    // Sprite-renderer component patch verbs (explicit, no property bag).
    bool addSpriteRenderer(const SceneId& sceneId, EntityId id, SpriteRendererComponent component);
    bool removeSpriteRenderer(const SceneId& sceneId, EntityId id);
    bool setSpriteRendererVisible(const SceneId& sceneId, EntityId id, bool visible);
    bool setSpriteRendererAsset(const SceneId& sceneId, EntityId id, AssetId assetId);
    bool setSpriteRendererAnimation(const SceneId& sceneId, EntityId id, AssetId animationAssetId);
    bool addSpriteAnimator(const SceneId& sceneId, EntityId id, SpriteAnimatorComponent component);
    bool removeSpriteAnimator(const SceneId& sceneId, EntityId id);
    bool setSpriteAnimatorInitialClip(const SceneId& sceneId, EntityId id, std::string clipId);
    bool setSpriteAnimatorPlaybackSpeed(const SceneId& sceneId, EntityId id, float speed);
    bool setSpriteAnimatorAutoPlay(const SceneId& sceneId, EntityId id, bool autoPlay);
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
    // TopDownController is authored on the object type only. This slice edits the
    // movement speed (canonical maxSpeed); the other canonical fields persist
    // untouched.
    bool addTopDownController(const std::string& objectTypeId, TopDownControllerComponent component);
    bool removeTopDownController(const std::string& objectTypeId);
    bool setTopDownControllerSpeed(const std::string& objectTypeId, float speed);
    // PlatformerController is authored on the object type only. `field` selects the
    // canonical scalar (0 = maxSpeed, 1 = jumpForce, 2 = customGravity), matching
    // commands/PlatformerField so this header stays free of the command enum.
    bool addPlatformerController(const std::string& objectTypeId, PlatformerControllerComponent component);
    bool removePlatformerController(const std::string& objectTypeId);
    bool setPlatformerValue(const std::string& objectTypeId, int field, float value);
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
    bool setAnimationClipFrames(const AssetId& assetId, const std::string& clipId,
                                std::vector<SpriteAnimationFrameDef> frames);
    bool setAnimationClipFrameRate(const AssetId& assetId, const std::string& clipId, float fps);
    bool setAnimationClipPlaybackMode(const AssetId& assetId, const std::string& clipId,
                                      AnimationPlaybackMode mode);
    bool addAudioAsset(AudioAssetDef asset);
    bool removeAudioAsset(const AssetId& assetId);
    bool addFontAsset(FontAssetDef asset);
    bool removeFontAsset(const AssetId& assetId);
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

} // namespace ArtCade::EditorNative
