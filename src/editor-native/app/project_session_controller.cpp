#include "editor-native/app/project_session_controller.h"

#include "editor-native/app/confirm_dialog.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/file_dialog.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_load.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/app/script_asset_workflow.h"
#include "editor-native/app/script_syntax_validator.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
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

bool copyConfinedProjectTree(const std::filesystem::path& previousRoot,
                             const std::filesystem::path& nextRoot,
                             const std::filesystem::path& relativeTree,
                             std::string& error) {
    const PathConfinementResult sourceResult =
        resolvePathInsideRoot(previousRoot, relativeTree);
    if (!sourceResult.ok) {
        error = "Could not resolve existing project folder: " + sourceResult.error;
        return false;
    }
    const std::filesystem::path& source = sourceResult.value;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) return true;
    if (ec) {
        error = "Could not inspect existing project folder: " + ec.message();
        return false;
    }

    std::filesystem::recursive_directory_iterator it{
        source, std::filesystem::directory_options::none, ec};
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        error = "Could not inspect existing project tree: " + ec.message();
        return false;
    }
    for (; it != end;) {
        const std::filesystem::path relative = it->path().lexically_relative(previousRoot);
        const PathConfinementResult entry = resolvePathInsideRoot(previousRoot, relative);
        if (!entry.ok) {
            error = "Project tree contains an unsafe path: " + entry.error;
            return false;
        }
        it.increment(ec);
        if (ec) {
            error = "Could not inspect existing project tree: " + ec.message();
            return false;
        }
    }

    std::filesystem::create_directories(nextRoot, ec);
    if (ec) {
        error = "Could not create project folder: " + ec.message();
        return false;
    }
    const PathConfinementResult destinationResult =
        resolvePathInsideRoot(nextRoot, relativeTree);
    if (!destinationResult.ok) {
        error = "Could not resolve destination project folder: " + destinationResult.error;
        return false;
    }
    std::filesystem::copy(
        source, destinationResult.value,
        std::filesystem::copy_options::recursive
            | std::filesystem::copy_options::skip_symlinks
            | std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        error = "Could not copy project folder: " + ec.message();
        return false;
    }
    return true;
}

bool copyProjectFilesForSaveAs(const std::filesystem::path& previousRoot,
                               const std::filesystem::path& nextRoot,
                               std::string& error) {
    if (previousRoot.empty() || previousRoot == nextRoot) return true;
    return copyConfinedProjectTree(previousRoot, nextRoot, "assets", error)
        && copyConfinedProjectTree(previousRoot, nextRoot, "scripts", error);
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
    ui_.setPlayHandlers(
        [this]() { requestPlayProject(); },
        [this]() { requestPlayCurrentScene(); });
    ui_.setImportHandler([this](AssetKind kind) { importAssetOfKind(kind); });
    ui_.setCreateScriptHandler([this]() { createScript(); });
    ui_.setScriptEditorHandlers(
        [this](const AssetId& id) { openScript(id); },
        [this](const AssetId& id) { saveScript(id); },
        [this]() { saveAllScripts(); },
        [this](const AssetId& id) { return closeScript(id); });
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
    titleShowsDirty_ = coordinator_.document().isDirty()
        || coordinator_.state().scriptEditor.anyDirty();
    SetWindowTitle(("ArtCade Studio - " + name
                    + (titleShowsDirty_ ? " \xe2\x80\xa2" : "")).c_str());
}

void ProjectSessionController::refreshWindowTitleIfNeeded() {
    const bool dirty = coordinator_.document().isDirty()
        || coordinator_.state().scriptEditor.anyDirty();
    if (dirty != titleShowsDirty_) refreshWindowTitle();
}

bool ProjectSessionController::saveTo(const std::filesystem::path& path) {
    // Source buffers belong to the existing project root. Persist them there
    // before Save As copies the confined scripts tree to its new root.
    if (!currentProjectPath_.empty() && !saveAllScripts()) return false;
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
    if (!copyProjectFilesForSaveAs(previousRoot, destination.parent_path(), copyError)) {
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
    const bool projectDirty = coordinator_.document().isDirty();
    const bool scriptsDirty = coordinator_.state().scriptEditor.anyDirty();
    if (!projectDirty && !scriptsDirty) return true;
    std::string detail;
    if (projectDirty) detail = "Project";
    for (const ScriptEditorBuffer& buffer : coordinator_.state().scriptEditor.buffers) {
        if (!buffer.dirty()) continue;
        if (!detail.empty()) detail += "\n";
        const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(buffer.scriptAssetId);
        detail += asset && !asset->name.empty() ? asset->name : buffer.scriptAssetId;
    }
    const UnsavedChoice choice = confirmUnsavedChanges(detail);
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
        case AssetKind::Script: picked = openScriptFileDialog(); break;
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
    if (kind == AssetKind::Script) openScript(result.assetId);
    return result.assetId;
}

std::optional<AssetId> ProjectSessionController::createScript() {
    bool savedForCreate = false;
    if (currentProjectPath_.empty()) {
        coordinator_.logInfo(
            "Scripts live inside the project folder - choose where to save the project");
        if (!saveCurrent()) {
            coordinator_.logWarning("Script creation cancelled: the project was not saved");
            return std::nullopt;
        }
        savedForCreate = true;
    }
    const ScriptAssetWorkflowResult result = createScriptAsset(
        coordinator_, currentProjectPath_.parent_path());
    if (!result.ok) {
        coordinator_.logError(result.error);
        return std::nullopt;
    }
    coordinator_.logInfo("Created " + result.assetId + ".lua");
    if (savedForCreate && !saveCurrent()) {
        coordinator_.logWarning("Created script is not saved in the project file yet");
    }
    openScript(result.assetId);
    return result.assetId;
}

bool ProjectSessionController::openScript(const AssetId& assetId) {
    const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
    if (!asset) {
        coordinator_.logError("Open script failed: unknown script asset");
        return false;
    }
    if (const ScriptEditorBuffer* open = coordinator_.state().scriptEditor.find(assetId)) {
        (void)open;
        return coordinator_.apply(ActivateScriptBufferIntent{assetId}).ok;
    }
    if (currentProjectPath_.empty()) {
        coordinator_.logError("Open script failed: save the project first");
        return false;
    }
    ProjectScriptFileService files(currentProjectPath_.parent_path());
    const ScriptFileResult<std::string> source = files.readScript(asset->sourcePath);
    if (!source.ok) {
        coordinator_.logError("Open script failed: " + source.error);
        return false;
    }
    const bool ok = coordinator_.apply(OpenScriptBufferIntent{assetId, source.value}).ok;
    if (ok) coordinator_.logInfo("Opened script " + asset->name);
    return ok;
}

bool ProjectSessionController::saveScript(const AssetId& assetId) {
    const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.find(assetId);
    if (!asset || !buffer) {
        coordinator_.logError("Save script failed: script is not open");
        return false;
    }
    if (currentProjectPath_.empty()) {
        coordinator_.logError("Save script failed: save the project first");
        return false;
    }
    const std::string source = buffer->text;
    ProjectScriptFileService files(currentProjectPath_.parent_path());
    const ScriptFileResult<std::filesystem::path> written =
        files.writeScriptAtomically(asset->sourcePath, source);
    if (!written.ok) {
        coordinator_.logError("Save script failed: " + written.error);
        return false;
    }
    const ScriptFileResult<std::string> persisted = files.readScript(asset->sourcePath);
    if (!persisted.ok) {
        coordinator_.logError("Save script verification failed: " + persisted.error);
        return false;
    }
    coordinator_.apply(MarkScriptBufferSavedIntent{assetId, persisted.value});
    coordinator_.logInfo("Saved script " + asset->name);
    refreshWindowTitleIfNeeded();
    return true;
}

bool ProjectSessionController::saveAllScripts() {
    std::vector<AssetId> dirty;
    for (const ScriptEditorBuffer& buffer : coordinator_.state().scriptEditor.buffers)
        if (buffer.dirty()) dirty.push_back(buffer.scriptAssetId);
    for (const AssetId& id : dirty) if (!saveScript(id)) return false;
    return true;
}

bool ProjectSessionController::closeScript(const AssetId& assetId) {
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.find(assetId);
    if (!buffer) return true;
    if (buffer->dirty()) {
        const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
        const std::string name = asset && !asset->name.empty() ? asset->name : assetId;
        const UnsavedChoice choice = confirmUnsavedChanges(name);
        if (choice == UnsavedChoice::Cancel) return false;
        if (choice == UnsavedChoice::Save && !saveScript(assetId)) return false;
    }
    const bool ok = coordinator_.apply(CloseScriptBufferIntent{assetId}).ok;
    refreshWindowTitleIfNeeded();
    return ok;
}

bool ProjectSessionController::validateSavedScriptsForPlay() {
    const std::vector<AssetId> referenced =
        coordinator_.document().referencedScriptAssetIds(true);
    if (referenced.empty()) return true;
    if (currentProjectPath_.empty()) {
        coordinator_.logError("Play blocked: save the project before running attached scripts");
        return false;
    }

    const ProjectScriptFileService files(currentProjectPath_.parent_path());
    const std::vector<ScriptDiagnostic> diagnostics = validateReferencedScriptSyntax(
        coordinator_.document(), files, referenced);
    for (const AssetId& assetId : referenced) {
        std::vector<ScriptDiagnostic> perAsset;
        std::copy_if(diagnostics.begin(), diagnostics.end(), std::back_inserter(perAsset),
            [&](const ScriptDiagnostic& diagnostic) {
                return diagnostic.scriptAssetId == assetId;
            });
        coordinator_.reportScriptDiagnostics(assetId, perAsset);
    }
    if (!diagnostics.empty()) {
        coordinator_.logError("Play blocked: attached scripts contain errors");
        return false;
    }
    return true;
}

bool ProjectSessionController::requestPlayProject() {
    if (!validateSavedScriptsForPlay()) return false;
    return coordinator_.playProject().ok;
}

bool ProjectSessionController::requestPlayCurrentScene() {
    if (!validateSavedScriptsForPlay()) return false;
    return coordinator_.playCurrentScene().ok;
}

} // namespace ArtCade::EditorNative
