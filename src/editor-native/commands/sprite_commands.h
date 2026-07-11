#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <optional>

namespace ArtCade::EditorNative {

// =============================================================================
// Sprite-renderer component commands — operate on one SceneInstanceDef in an
// explicit scene. Each invalidates Inspector | Viewport. Typed and specific:
// no generic setComponentProperty(entity, "Sprite", "visible", Variant).
// =============================================================================

/** Add a default sprite renderer. Adding when one already exists is a no-op. */
class AddSpriteRendererCommand final : public EditorCommand {
public:
    AddSpriteRendererCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSpriteRenderer"; }

private:
    SceneId  sceneId_;
    EntityId id_;
    // Gates the layer-lock check to the first apply() only - a later redo
    // reuses this same command and must not be blocked by the layer's lock
    // state at redo time.
    bool     captured_ = false;
};

/** Remove the sprite renderer; undo restores the exact captured component. */
class RemoveSpriteRendererCommand final : public EditorCommand {
public:
    RemoveSpriteRendererCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSpriteRenderer"; }

private:
    SceneId                 sceneId_;
    EntityId                id_;
    SpriteRendererComponent removed_{};
    bool                    captured_ = false;
};

/** Set the sprite renderer's visibility. Setting the same value is a no-op. */
class SetSpriteRendererVisibleCommand final : public EditorCommand {
public:
    SetSpriteRendererVisibleCommand(SceneId sceneId, EntityId id, bool visible);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteRendererVisible"; }

private:
    SceneId  sceneId_;
    EntityId id_;
    bool     next_;
    bool     previous_ = false;
    bool     captured_ = false;
};

/** Set the referenced image asset. Empty clears it; a missing asset fails. */
class SetSpriteRendererAssetCommand final : public EditorCommand {
public:
    SetSpriteRendererAssetCommand(SceneId sceneId, EntityId id, AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteRendererAsset"; }

private:
    SceneId  sceneId_;
    EntityId id_;
    AssetId  next_;
    SpriteRendererComponent previous_{};
    std::optional<SpriteAnimatorComponent> previousAnimator_;
    bool     captured_ = false;
};

/** Set the referenced animation asset. Empty returns the renderer to no source. */
class SetSpriteRendererAnimationCommand final : public EditorCommand {
public:
    SetSpriteRendererAnimationCommand(SceneId sceneId, EntityId id, AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSpriteRendererAnimation"; }

private:
    SceneId  sceneId_;
    EntityId id_;
    AssetId  next_;
    SpriteRendererComponent previous_{};
    std::optional<SpriteAnimatorComponent> previousAnimator_;
    bool     captured_ = false;
};

} // namespace ArtCade::EditorNative
