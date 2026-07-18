#pragma once

#include "core/types.h"
#include "editor-native/app/asset_import.h"
#include "editor-native/app/project_session_id.h"
#include "script-runtime.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

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
    ProjectSessionId projectSessionId() const { return sessionIdentity_.current(); }
    std::filesystem::path assetRoot(const std::filesystem::path& fallback) const;
    void setCurrentProjectPath(std::filesystem::path path);
    void setProjectRelocationAvailabilityQuery(std::function<bool()> query) {
        projectRelocationAvailable_ = std::move(query);
    }

    bool saveTo(const std::filesystem::path& path);
    bool resolveUnsavedChanges();
    std::optional<AssetId> importAssetOfKind(AssetKind kind);
    std::optional<AssetId> createScript();
    bool openScript(const AssetId& assetId);
    bool saveScript(const AssetId& assetId);
    bool saveAllScripts();
    bool closeScript(const AssetId& assetId);
    bool requestPlayProject();
    bool requestPlayCurrentScene();
    bool restartAndApplyScripts();

    void refreshWindowTitle();
    void refreshWindowTitleIfNeeded();

private:
    bool saveCurrent();
    void requestNewProject();
    void requestOpenProject();
    void requestSaveAs();
    std::optional<std::vector<Scripts::ScriptProgram>> snapshotSavedScriptsForPlay();

    EditorCoordinator&      coordinator_;
    EditorUi&               ui_;
    TextureCache&           textureCache_;
    std::filesystem::path   currentProjectPath_;
    ProjectSessionIdentity  sessionIdentity_;
    std::function<bool()>   projectRelocationAvailable_;
    bool                    titleShowsDirty_ = false;
};

} // namespace ArtCade::EditorNative
