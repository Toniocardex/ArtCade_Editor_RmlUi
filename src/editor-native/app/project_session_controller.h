#pragma once

#include "core/types.h"
#include "editor-native/app/asset_import.h"

#include <filesystem>
#include <optional>

namespace ArtCade::EditorNative {

class EditorCoordinator;
class EditorUi;
class TextureCache;

// Owns application-session state for the currently opened project and binds
// the canonical New/Open/Save/Import workflows to EditorUi. It is not a second
// project authority: authoring data remains exclusively in ProjectDocument.
class ProjectSessionController {
public:
    ProjectSessionController(EditorCoordinator& coordinator, EditorUi& ui,
                             TextureCache& textureCache);

    void bindUi();

    const std::filesystem::path& currentProjectPath() const { return currentProjectPath_; }
    std::filesystem::path assetRoot(const std::filesystem::path& fallback) const;
    void setCurrentProjectPath(std::filesystem::path path);

    bool saveTo(const std::filesystem::path& path);
    bool resolveUnsavedChanges();
    std::optional<AssetId> importAssetOfKind(AssetKind kind);
    std::optional<AssetId> createScript();
    bool openScript(const AssetId& assetId);
    bool saveScript(const AssetId& assetId);
    bool saveAllScripts();
    bool closeScript(const AssetId& assetId);

    void refreshWindowTitle();
    void refreshWindowTitleIfNeeded();

private:
    bool saveCurrent();
    void requestNewProject();
    void requestOpenProject();
    void requestSaveAs();

    EditorCoordinator&      coordinator_;
    EditorUi&               ui_;
    TextureCache&           textureCache_;
    std::filesystem::path   currentProjectPath_;
    bool                    titleShowsDirty_ = false;
};

} // namespace ArtCade::EditorNative
