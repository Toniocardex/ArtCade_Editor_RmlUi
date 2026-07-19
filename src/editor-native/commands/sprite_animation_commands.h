#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Creates an animation asset with a source sheet and one empty clip as one
// atomic authoring operation and one Undo step.
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

// An animation asset is an empty character container; clips are added afterwards
// via AddAnimationClipCommand.
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
    // An animator that referenced this animation; cleared on remove, restored
    // verbatim on undo, so delete leaves nothing dangling on the entity.
    struct ClearedTypeRef {
        ObjectTypeId objectTypeId;
        SpriteAnimatorComponent animator{};
    };
    struct ClearedOverrideRef {
        SceneId  sceneId;
        EntityId entityId;
        std::optional<SpriteAnimatorOverride> animator;
        bool clearExplicitAnimation = false;
    };
    struct ClearedLogicRef {
        ObjectTypeId objectTypeId;
        std::string ruleId;
        std::size_t actionIndex = 0;
        AssetId previousAnimationAssetId;
        std::string previousClipId;
    };
    AssetId assetId_;
    SpriteAnimationAssetDef removed_{};
    std::size_t assetIndex_ = 0;
    bool captured_ = false;
    std::vector<ClearedTypeRef> clearedTypeRefs_;
    std::vector<ClearedOverrideRef> clearedOverrideRefs_;
    std::vector<ClearedLogicRef> clearedLogicRefs_;
};

class AddAnimationClipCommand final : public EditorCommand {
public:
    AddAnimationClipCommand(AssetId assetId, std::string clipId, std::string name);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddAnimationClip"; }

private:
    AssetId assetId_;
    std::string clipId_;
    std::string name_;
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

// Replaces the asset frame pool and clears every clip's frameIds. Undo restores
// the previous frames and each clip's previous frameId sequence.
class ReplaceAnimationFramesCommand final : public EditorCommand {
public:
    ReplaceAnimationFramesCommand(AssetId assetId, std::vector<SpriteFrameDef> frames);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ReplaceAnimationFrames"; }

private:
    struct ClipFrameIds {
        AnimationClipId clipId;
        std::vector<SpriteFrameId> frameIds;
    };
    AssetId assetId_;
    std::vector<SpriteFrameDef> next_;
    std::vector<SpriteFrameDef> previousFrames_;
    std::vector<ClipFrameIds> previousSequences_;
    bool captured_ = false;
};

// Atomic source-image change: new sheet + empty frame pool + empty sequences.
class ReplaceAnimationSourceImageCommand final : public EditorCommand {
public:
    ReplaceAnimationSourceImageCommand(AssetId assetId, AssetId imageId);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ReplaceAnimationSourceImage"; }

private:
    struct ClipFrameIds {
        AnimationClipId clipId;
        std::vector<SpriteFrameId> frameIds;
    };
    AssetId assetId_;
    AssetId nextImageId_;
    AssetId previousImageId_;
    std::vector<SpriteFrameDef> previousFrames_;
    std::vector<ClipFrameIds> previousSequences_;
    bool captured_ = false;
};

// Deep-copies an animation asset with remapped frame/clip ids; same source sheet.
class DuplicateSpriteAnimationAssetCommand final : public EditorCommand {
public:
    DuplicateSpriteAnimationAssetCommand(AssetId sourceAssetId, AssetId newAssetId,
                                         std::string newName);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "DuplicateSpriteAnimationAsset"; }

private:
    AssetId sourceAssetId_;
    AssetId newAssetId_;
    std::string newName_;
};

// Copies a clip inside the same asset; frameIds stay pool-shared.
class DuplicateAnimationClipCommand final : public EditorCommand {
public:
    DuplicateAnimationClipCommand(AssetId assetId, std::string sourceClipId,
                                  std::string newClipId, std::string newName);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "DuplicateAnimationClip"; }

private:
    AssetId assetId_;
    std::string sourceClipId_;
    std::string newClipId_;
    std::string newName_;
};

class SetAnimationClipFrameIdsCommand final : public EditorCommand {
public:
    SetAnimationClipFrameIdsCommand(AssetId assetId, std::string clipId,
                                    std::vector<SpriteFrameId> frameIds);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetAnimationClipFrameIds"; }

private:
    AssetId assetId_;
    std::string clipId_;
    std::vector<SpriteFrameId> next_;
    std::vector<SpriteFrameId> previous_;
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
