#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// An animation asset is an empty character container; clips (each with their own
// sheet) are added afterwards via AddAnimationClipCommand.
class AddSpriteAnimationAssetCommand final : public EditorCommand {
public:
    AddSpriteAnimationAssetCommand(AssetId assetId, std::string name);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSpriteAnimationAsset"; }

private:
    AssetId assetId_;
    std::string name_;
};

class RemoveSpriteAnimationAssetCommand final : public EditorCommand {
public:
    explicit RemoveSpriteAnimationAssetCommand(AssetId assetId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSpriteAnimationAsset"; }

private:
    // A renderer (and its optional animator) that referenced this animation;
    // cleared on remove, restored verbatim on undo, so delete leaves nothing
    // dangling on the entity.
    struct ClearedRef {
        SceneId  sceneId;
        EntityId entityId;
        bool     hadAnimator = false;
        SpriteAnimatorComponent animator{};
    };
    AssetId assetId_;
    SpriteAnimationAssetDef removed_{};
    bool captured_ = false;
    std::vector<ClearedRef> clearedRefs_;
    bool refsCaptured_ = false;
};

// Adds a clip carrying its own sheet (imageId). The first clip of an asset also
// becomes its defaultClipId (handled by the document verb).
class AddAnimationClipCommand final : public EditorCommand {
public:
    AddAnimationClipCommand(AssetId assetId, std::string clipId, std::string name,
                            AssetId imageId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddAnimationClip"; }

private:
    AssetId assetId_;
    std::string clipId_;
    std::string name_;
    AssetId imageId_;
};

class RenameAnimationClipCommand final : public EditorCommand {
public:
    RenameAnimationClipCommand(AssetId assetId, std::string clipId, std::string name);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameAnimationClip"; }

private:
    AssetId assetId_;
    std::string clipId_;
    std::string next_;
    std::string previous_;
    bool captured_ = false;
};

class RemoveAnimationClipCommand final : public EditorCommand {
public:
    RemoveAnimationClipCommand(AssetId assetId, std::string clipId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveAnimationClip"; }

private:
    AssetId assetId_;
    std::string clipId_;
    SpriteAnimationClipDef removed_{};
    bool captured_ = false;
};

class SetAnimationClipFramesCommand final : public EditorCommand {
public:
    SetAnimationClipFramesCommand(AssetId assetId, std::string clipId,
                                  std::vector<SpriteAnimationFrameDef> frames);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetAnimationClipFrames"; }

private:
    AssetId assetId_;
    std::string clipId_;
    std::vector<SpriteAnimationFrameDef> next_;
    std::vector<SpriteAnimationFrameDef> previous_;
    bool captured_ = false;
};

class SetAnimationClipFrameRateCommand final : public EditorCommand {
public:
    SetAnimationClipFrameRateCommand(AssetId assetId, std::string clipId, float fps);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetAnimationClipFrameRate"; }

private:
    AssetId assetId_;
    std::string clipId_;
    float next_ = 8.f;
    float previous_ = 8.f;
    bool captured_ = false;
};

class SetAnimationClipPlaybackModeCommand final : public EditorCommand {
public:
    SetAnimationClipPlaybackModeCommand(AssetId assetId, std::string clipId,
                                        AnimationPlaybackMode mode);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetAnimationClipPlaybackMode"; }

private:
    AssetId assetId_;
    std::string clipId_;
    AnimationPlaybackMode next_ = AnimationPlaybackMode::Loop;
    AnimationPlaybackMode previous_ = AnimationPlaybackMode::Loop;
    bool captured_ = false;
};

class AddSpriteAnimatorCommand final : public EditorCommand {
public:
    AddSpriteAnimatorCommand(SceneId sceneId, EntityId id, SpriteAnimatorComponent component);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSpriteAnimator"; }

private:
    SceneId sceneId_;
    EntityId id_;
    SpriteAnimatorComponent component_{};
    // Gates the layer-lock check to the first apply() only - a later redo
    // reuses this same command and must not be blocked by the layer's lock
    // state at redo time.
    bool captured_ = false;
};

class RemoveSpriteAnimatorCommand final : public EditorCommand {
public:
    RemoveSpriteAnimatorCommand(SceneId sceneId, EntityId id);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSpriteAnimator"; }

private:
    SceneId sceneId_;
    EntityId id_;
    SpriteAnimatorComponent removed_{};
    bool captured_ = false;
};

class SetSpriteAnimatorInitialClipCommand final : public EditorCommand {
public:
    SetSpriteAnimatorInitialClipCommand(SceneId sceneId, EntityId id, std::string clipId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteAnimatorInitialClip"; }

private:
    SceneId sceneId_;
    EntityId id_;
    std::string next_;
    std::string previous_;
    bool captured_ = false;
};

class SetSpriteAnimatorPlaybackSpeedCommand final : public EditorCommand {
public:
    SetSpriteAnimatorPlaybackSpeedCommand(SceneId sceneId, EntityId id, float speed);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteAnimatorPlaybackSpeed"; }

private:
    SceneId sceneId_;
    EntityId id_;
    float next_ = 1.f;
    float previous_ = 1.f;
    bool captured_ = false;
};

class SetSpriteAnimatorAutoPlayCommand final : public EditorCommand {
public:
    SetSpriteAnimatorAutoPlayCommand(SceneId sceneId, EntityId id, bool autoPlay);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteAnimatorAutoPlay"; }

private:
    SceneId sceneId_;
    EntityId id_;
    bool next_ = true;
    bool previous_ = true;
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
