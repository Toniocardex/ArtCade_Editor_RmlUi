#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Creates an animation asset together with its initial sheet-backed clip as
// one atomic authoring operation and one Undo step.
class CreateSpriteAnimationAssetCommand final : public EditorCommand {
public:
    CreateSpriteAnimationAssetCommand(AssetId assetId, std::string name,
                                      std::string clipId, std::string clipName,
                                      AssetId imageId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateSpriteAnimationAsset"; }

private:
    AssetId assetId_;
    std::string name_;
    std::string clipId_;
    std::string clipName_;
    AssetId imageId_;
};

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
    struct ClearedTypeRef {
        ObjectTypeId objectTypeId;
        SpriteRendererComponent renderer{};
        std::optional<SpriteAnimatorComponent> animator;
    };
    struct ClearedOverrideRef {
        SceneId  sceneId;
        EntityId entityId;
        std::optional<SpriteRendererOverride> renderer;
        std::optional<SpriteAnimatorOverride> animator;
        bool clearExplicitAnimation = false;
    };
    AssetId assetId_;
    SpriteAnimationAssetDef removed_{};
    std::size_t assetIndex_ = 0;
    bool captured_ = false;
    std::vector<ClearedTypeRef> clearedTypeRefs_;
    std::vector<ClearedOverrideRef> clearedOverrideRefs_;
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

} // namespace ArtCade::EditorNative
