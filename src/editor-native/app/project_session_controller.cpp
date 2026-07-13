#include "editor-native/app/project_session_controller.h"

#include "editor-native/app/confirm_dialog.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/file_dialog.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_load.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

std::filesystem::path normalizeProjectSavePath(std::filesystem::path destination) {
    if (destination.extension().empty()) destination.replace_extension(".artcade-project");
    const std::filesystem::path fileName = destination.filename();
    const std::string stem = destination.stem().string();
    if (stem.empty() || destination.parent_path().filename() == stem) return destination;
    return destination.parent_path() / stem / fileName;
}

std::filesystem::path suggestedProjectSavePath(const ProjectDoc& doc) {
    std::string stem = doc.projectName.empty() ? std::string("Untitled") : doc.projectName;
    for (char& c : stem) {
        switch (c) {
            case '<': case '>': case ':': case '"': case '/': case '\\':
            case '|': case '?': case '*': c = '_'; break;
            default: break;
        }
    }
    if (stem.empty()) stem = "Untitled";
    return std::filesystem::path(stem + ".artcade-project");
}

bool copyAssetsForSaveAs(const std::filesystem::path& previousRoot,
                         const std::filesystem::path& nextRoot,
                         std::string& error) {
    if (previousRoot.empty() || previousRoot == nextRoot) return true;

    const PathConfinementResult sourceResult = resolvePathInsideRoot(previousRoot, "assets");
    if (!sourceResult.ok) {
        error = "Could not resolve existing assets folder: " + sourceResult.error;
        return false;
    }
    const std::filesystem::path& source = sourceResult.value;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) return true;
    if (ec) {
        error = "Could not inspect existing assets folder: " + ec.message();
        return false;
    }

    std::filesystem::recursive_directory_iterator it{
        source, std::filesystem::directory_options::none, ec};
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        error = "Could not inspect existing assets tree: " + ec.message();
        return false;
    }
    for (; it != end;) {
        const std::filesystem::path relative = it->path().lexically_relative(previousRoot);
        const PathConfinementResult entry = resolvePathInsideRoot(previousRoot, relative);
        if (!entry.ok) {
            error = "Assets tree contains an unsafe path: " + entry.error;
            return false;
        }
        it.increment(ec);
        if (ec) {
            error = "Could not inspect existing assets tree: " + ec.message();
            return false;
        }
    }

    std::filesystem::create_directories(nextRoot, ec);
    if (ec) {
        error = "Could not create project folder: " + ec.message();
        return false;
    }
    const PathConfinementResult destinationResult = resolvePathInsideRoot(nextRoot, "assets");
    if (!destinationResult.ok) {
        error = "Could not resolve destination assets folder: " + destinationResult.error;
        return false;
    }
    std::filesystem::copy(
        source, destinationResult.value,
        std::filesystem::copy_options::recursive
            | std::filesystem::copy_options::skip_symlinks
            | std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        error = "Could not copy assets folder: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

ProjectSessionController::ProjectSessionController(
    EditorCoordinator& coordinator, EditorUi& ui, TextureCache& textureCache)
    : coordinator_(coordinator), ui_(ui), textureCache_(textureCache) {}

void ProjectSessionController::bindUi() {
    ui_.setProjectFileHandlers(
        [this]() { requestNewProject(); },
        [this]() { requestOpenProject(); },
        [this]() { saveCurrent(); },
        [this]() { requestSaveAs(); });
    ui_.setImportHandler([this](AssetKind kind) { importAssetOfKind(kind); });
    ui_.setImportImageForAnimationHandler(
        [this]() { return importAssetOfKind(AssetKind::Image); });
}

std::filesystem::path ProjectSessionController::assetRoot(
    const std::filesystem::path& fallback) const {
    return currentProjectPath_.empty() ? fallback : currentProjectPath_.parent_path();
}

void ProjectSessionController::setCurrentProjectPath(std::filesystem::path path) {
    currentProjectPath_ = std::move(path);
    refreshWindowTitle();
}

void ProjectSessionController::refreshWindowTitle() {
    const std::string name = currentProjectPath_.empty()
        ? std::string("Untitled") : currentProjectPath_.stem().string();
    titleShowsDirty_ = coordinator_.document().isDirty();
    SetWindowTitle(("ArtCade Studio - " + name
                    + (titleShowsDirty_ ? " \xe2\x80\xa2" : "")).c_str());
}

void ProjectSessionController::refreshWindowTitleIfNeeded() {
    if (coordinator_.document().isDirty() != titleShowsDirty_) refreshWindowTitle();
}

bool ProjectSessionController::saveTo(const std::filesystem::path& path) {
    const std::filesystem::path destination = normalizeProjectSavePath(path);
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        coordinator_.logError("Save failed: could not create project folder: " + ec.message());
        return false;
    }
    const std::filesystem::path previousRoot = currentProjectPath_.empty()
        ? std::filesystem::path{} : currentProjectPath_.parent_path();
    std::string copyError;
    if (!copyAssetsForSaveAs(previousRoot, destination.parent_path(), copyError)) {
        coordinator_.logError("Save failed: " + copyError);
        return false;
    }
    const ProjectSaveResult result = saveProjectToFile(coordinator_, destination);
    if (!result.ok) {
        coordinator_.logError("Save failed: " + result.error.message);
        return false;
    }
    currentProjectPath_ = destination;
    refreshWindowTitle();
    coordinator_.logInfo("Saved " + destination.string());
    return true;
}

bool ProjectSessionController::saveCurrent() {
    if (currentProjectPath_.empty()) {
        const auto picked = saveProjectFileDialog(
            suggestedProjectSavePath(coordinator_.document().data()));
        return picked ? saveTo(*picked) : false;
    }
    return saveTo(currentProjectPath_);
}

bool ProjectSessionController::resolveUnsavedChanges() {
    if (!ui_.resolvePendingEdits().resolved()) return false;
    if (!coordinator_.document().isDirty()) return true;
    const UnsavedChoice choice = confirmUnsavedChanges();
    const bool saveOk = choice == UnsavedChoice::Save ? saveCurrent() : false;
    return resolveUnsavedGuard(true, choice, saveOk) == GuardOutcome::Proceed;
}

void ProjectSessionController::requestNewProject() {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before creating a new project");
        return;
    }
    if (!resolveUnsavedChanges()) return;
    const auto picked = saveProjectFileDialog(
        suggestedProjectSavePath(coordinator_.document().data()));
    const std::optional<std::filesystem::path> destination = picked
        ? std::optional<std::filesystem::path>{normalizeProjectSavePath(*picked)}
        : std::nullopt;
    ProjectDoc fresh;
    if (destination) fresh.projectName = destination->stem().string();
    const NewProjectResult created = createNewProjectTransaction(
        coordinator_, ProjectDocument{std::move(fresh)}, destination);
    if (created.cancelled) return;
    if (!created.ok) {
        coordinator_.logError("New project failed: " + created.error.message);
        return;
    }
    textureCache_.clear();
    setCurrentProjectPath(created.destination);
    coordinator_.logInfo("New project");
}

void ProjectSessionController::requestOpenProject() {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before opening another project");
        return;
    }
    if (!resolveUnsavedChanges()) return;
    const std::optional<std::filesystem::path> picked = openProjectFileDialog();
    if (!picked) return;
    const ProjectLoadResult result = loadProjectFromFile(coordinator_, *picked);
    if (!result.ok) {
        coordinator_.logError("Open failed: " + result.error.message);
        return;
    }
    textureCache_.clear();
    setCurrentProjectPath(*picked);
    coordinator_.logInfo("Opened " + picked->filename().string());
}

void ProjectSessionController::requestSaveAs() {
    const std::filesystem::path suggested = currentProjectPath_.empty()
        ? suggestedProjectSavePath(coordinator_.document().data()) : currentProjectPath_;
    if (const auto picked = saveProjectFileDialog(suggested)) saveTo(*picked);
}

std::optional<AssetId> ProjectSessionController::importAssetOfKind(AssetKind kind) {
    std::optional<std::filesystem::path> picked;
    switch (kind) {
        case AssetKind::Image: picked = openImageFileDialog(); break;
        case AssetKind::Audio: picked = openAudioFileDialog(); break;
        case AssetKind::Font:  picked = openFontFileDialog(); break;
    }
    if (!picked) return std::nullopt;
    bool savedForImport = false;
    if (currentProjectPath_.empty()) {
        coordinator_.logInfo(
            "Assets live inside the project folder - choose where to save the project");
        if (!saveCurrent()) {
            coordinator_.logWarning("Import cancelled: the project was not saved");
            return std::nullopt;
        }
        savedForImport = true;
    }
    ImportAssetRequest request;
    request.kind = kind;
    request.sourcePath = *picked;
    const ImportAssetResult result =
        importAsset(coordinator_, currentProjectPath_.parent_path(), request);
    if (!result.ok) {
        coordinator_.logError(result.error);
        return std::nullopt;
    }
    coordinator_.logInfo("Imported " + result.assetId);
    if (savedForImport && !saveCurrent()) {
        coordinator_.logWarning("Imported asset is not saved in the project file yet");
    }
    return result.assetId;
}

} // namespace ArtCade::EditorNative
