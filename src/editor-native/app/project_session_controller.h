#pragma once

#include "editor-native/app/asset_import.h"
#include "editor-native/app/export/export_types.h"
#include "editor-native/app/project_session_id.h"
#include "editor-native/app/recent_projects_store.h"
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

    // ADR-0030: optional MRU store. Null store disables touch/persist.
    void setRecentProjectsStore(RecentProjectsStore* store);
    RecentProjectsStore* recentProjectsStore() const { return recentProjects_; }
    // Fired after a successful touch or explicit remove (UI may refresh hub).
    void setRecentProjectsChangedHandler(std::function<void()> onChanged);

    const std::filesystem::path& currentProjectPath() const { return currentProjectPath_; }
    ProjectSessionId projectSessionId() const { return sessionIdentity_.current(); }
    std::filesystem::path assetRoot(const std::filesystem::path& fallback) const;
    void setCurrentProjectPath(std::filesystem::path path);
    void setProjectRelocationAvailabilityQuery(std::function<bool()> query) {
        projectRelocationAvailable_ = std::move(query);
    }

    bool saveTo(const std::filesystem::path& path);
    void resolveUnsavedChanges(std::function<void(bool proceed)> done);
    std::optional<AssetId> importAssetOfKind(AssetKind kind);
    std::optional<AssetId> createScript();
    void removeScript(const AssetId& assetId);
    bool openScript(const AssetId& assetId);
    bool saveScript(const AssetId& assetId);
    bool saveAllScripts();
    void closeScript(const AssetId& assetId,
                     std::function<void(bool closed)> done = {});
    bool requestPlayProject();
    bool requestPlayCurrentScene();
    bool restartAndApplyScripts();
    void requestExportWindows();

    // ADR-0024: public session workflows (keyboard dispatcher + menu).
    void requestNewProject();
    void requestOpenProject();
    // ADR-0030: open a known path (recent list). Same guards as Open Project.
    void requestOpenProjectAt(const std::filesystem::path& path);
    void removeRecentProject(const std::filesystem::path& path);
    // Touch MRU after an external successful load (e.g. CLI/screenshot bootstrap).
    void noteSuccessfulProjectPath(const std::filesystem::path& path);
    bool requestSave();
    void requestSaveAs();

    void refreshWindowTitle();
    void refreshWindowTitleIfNeeded();

    void setProjectFilesStableQuery(std::function<bool()> query) {
        projectFilesStable_ = std::move(query);
    }
    void setExportTemplatesRoot(std::filesystem::path root) {
        exportTemplatesRoot_ = std::move(root);
    }
    void setPendingExportRunner(std::function<void(ExportRequest, ExportContext)> runner) {
        pendingExportRunner_ = std::move(runner);
    }

private:
    bool saveCurrent();
    void beginExportWindowsAfterSaved();
    std::optional<std::vector<Scripts::ScriptProgram>> snapshotSavedScriptsForPlay();
    void touchRecentProject(const std::filesystem::path& path);
    void persistRecentProjectsBestEffort();
    void openProjectPathGuarded(const std::filesystem::path& path);

    EditorCoordinator&      coordinator_;
    EditorUi&               ui_;
    TextureCache&           textureCache_;
    std::filesystem::path   currentProjectPath_;
    ProjectSessionIdentity  sessionIdentity_;
    std::function<bool()>   projectRelocationAvailable_;
    std::function<bool()>   projectFilesStable_;
    std::filesystem::path   exportTemplatesRoot_;
    std::function<void(ExportRequest, ExportContext)> pendingExportRunner_;
    bool                    titleShowsDirty_ = false;
    RecentProjectsStore*    recentProjects_ = nullptr;
    std::function<void()>   recentProjectsChanged_;
};

} // namespace ArtCade::EditorNative
