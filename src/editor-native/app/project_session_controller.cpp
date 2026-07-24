#include "editor-native/app/project_session_controller.h"

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
    ui_.setExportWindowsHandler([this]() { requestExportWindows(); });
    ui_.setPlayHandlers(
        [this]() { requestPlayProject(); },
        [this]() { requestPlayCurrentScene(); });
    ui_.setImportHandler([this](AssetKind kind) { importAssetOfKind(kind); });
    ui_.setCreateScriptHandler([this]() { createScript(); });
    ui_.setRemoveScriptHandler([this](const AssetId& id) { removeScript(id); });
    ui_.setScriptEditorHandlers(
        [this](const AssetId& id) { openScript(id); },
        [this](const AssetId& id) { saveScript(id); },
        [this]() { saveAllScripts(); },
        [this](const AssetId& id) { closeScript(id); },
        [this]() { restartAndApplyScripts(); });
    ui_.setImportImageForAnimationHandler(
        [this]() { return importAssetOfKind(AssetKind::Image); });
}

std::filesystem::path ProjectSessionController::assetRoot(
    const std::filesystem::path& fallback) const {
    return currentProjectPath_.empty() ? fallback : currentProjectPath_.parent_path();
}

void ProjectSessionController::setCurrentProjectPath(std::filesystem::path path) {
    // Every successful New/Open/Replace/Save-As boundary starts a distinct
    // session even when the resulting path is textually identical.
    sessionIdentity_.advance();
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
    const std::filesystem::path destination = normalizeProjectSavePath(path);
    if (currentProjectPath_ != destination && projectRelocationAvailable_
        && !projectRelocationAvailable_()) {
        coordinator_.logWarning(
            "Wait for the Generated SFX operation to finish before Save As");
        return false;
    }
    // Source buffers belong to the existing project root. Persist them there
    // before Save As copies the confined scripts tree to its new root.
    if (!currentProjectPath_.empty() && !saveAllScripts()) return false;
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
    const bool relocating = !previousRoot.empty()
        && previousRoot != destination.parent_path();
    if (relocating) {
        const auto historyReady = coordinator_.validateCommandSideEffectRebase(
            previousRoot, destination.parent_path());
        if (!historyReady.ok) return false;
    }
    const ProjectSaveResult result = saveProjectToFile(coordinator_, destination);
    if (!result.ok) {
        coordinator_.logError("Save failed: " + result.error.message);
        return false;
    }
    if (relocating)
        coordinator_.rebaseCommandSideEffects(
            previousRoot, destination.parent_path());
    if (currentProjectPath_ != destination) setCurrentProjectPath(destination);
    else refreshWindowTitle();
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

void ProjectSessionController::resolveUnsavedChanges(std::function<void(bool proceed)> done) {
    if (!done) return;
    if (!ui_.resolvePendingEdits().resolved()) {
        done(false);
        return;
    }
    const bool projectDirty = coordinator_.document().isDirty();
    const bool scriptsDirty = coordinator_.state().scriptEditor.anyDirty();
    if (!projectDirty && !scriptsDirty) {
        done(true);
        return;
    }
    std::string detail;
    if (projectDirty) detail = "Project";
    for (const ScriptEditorBuffer& buffer : coordinator_.state().scriptEditor.buffers) {
        if (!buffer.dirty()) continue;
        if (!detail.empty()) detail += "\n";
        const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(buffer.scriptAssetId);
        detail += asset && !asset->name.empty() ? asset->name : buffer.scriptAssetId;
    }
    if (!ui_.promptSaveDiscardCancel(
            "Unsaved changes",
            "There are unsaved changes:\n\n" + detail,
            "Save them before continuing?",
            [this, done = std::move(done)](UnsavedChoice choice) {
                const bool saveOk = choice == UnsavedChoice::Save ? saveCurrent() : false;
                done(resolveUnsavedGuard(true, choice, saveOk) == GuardOutcome::Proceed);
            })) {
        done(false);
    }
}

void ProjectSessionController::requestNewProject() {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before creating a new project");
        return;
    }
    resolveUnsavedChanges([this](bool proceed) {
        if (!proceed) return;
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
    });
}

void ProjectSessionController::requestOpenProject() {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before opening another project");
        return;
    }
    resolveUnsavedChanges([this](bool proceed) {
        if (!proceed) return;
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
    });
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

void ProjectSessionController::removeScript(const AssetId& assetId) {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before deleting scripts");
        return;
    }
    if (currentProjectPath_.empty()) {
        coordinator_.logError("Delete script failed: save the project first");
        return;
    }
    const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
    if (!asset) {
        coordinator_.logError("Delete script failed: unknown script asset");
        return;
    }
    const std::vector<AssetId> referenced =
        coordinator_.document().referencedScriptAssetIds(false);
    if (std::find(referenced.begin(), referenced.end(), assetId) != referenced.end()) {
        coordinator_.logError(
            "Cannot remove Script Asset while it is attached to an Object Type");
        return;
    }
    const std::string name = !asset->name.empty() ? asset->name : assetId;
    const std::string sourcePath = asset->sourcePath;
    if (!ui_.promptDangerConfirm(
            "Delete Script?",
            "Delete Script \"" + name + "\" and permanently remove:\n\n" + sourcePath,
            "Undo can restore the file contents recorded at deletion, but this "
            "cannot recover unsaved editor drafts discarded first.",
            "Delete Script and file",
            [this, assetId, name](bool confirmed) {
                if (!confirmed) return;
                closeScript(assetId, [this, assetId, name](bool closed) {
                    if (!closed) return;
                    const ScriptAssetDef* asset =
                        coordinator_.document().findScriptAsset(assetId);
                    if (!asset) {
                        coordinator_.logError(
                            "Delete script failed: script was removed while confirming");
                        return;
                    }
                    const std::vector<AssetId> referencedAgain =
                        coordinator_.document().referencedScriptAssetIds(false);
                    if (std::find(referencedAgain.begin(), referencedAgain.end(), assetId)
                        != referencedAgain.end()) {
                        coordinator_.logError(
                            "Cannot remove Script Asset while it is attached to an Object Type");
                        return;
                    }
                    const ScriptDirtyBufferQuery dirtyQuery =
                        [this](const AssetId& id) {
                            const ScriptEditorBuffer* buffer =
                                coordinator_.state().scriptEditor.find(id);
                            return buffer && buffer->dirty();
                        };
                    const ScriptAssetWorkflowResult result = removeScriptAsset(
                        coordinator_, currentProjectPath_.parent_path(), assetId, dirtyQuery);
                    if (!result.ok) {
                        coordinator_.logError(result.error);
                        return;
                    }
                    coordinator_.logInfo("Deleted Script " + name);
                    refreshWindowTitleIfNeeded();
                });
            })) {
        return;
    }
}

void ProjectSessionController::closeScript(const AssetId& assetId,
                                           std::function<void(bool closed)> done) {
    const auto finish = [done = std::move(done)](bool closed) {
        if (done) done(closed);
    };
    const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.find(assetId);
    if (!buffer) {
        finish(true);
        return;
    }
    if (!buffer->dirty()) {
        const bool ok = coordinator_.apply(CloseScriptBufferIntent{assetId}).ok;
        refreshWindowTitleIfNeeded();
        finish(ok);
        return;
    }
    const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(assetId);
    const std::string name = asset && !asset->name.empty() ? asset->name : assetId;
    if (!ui_.promptSaveDiscardCancel(
            "Unsaved script",
            "\"" + name + "\" has unsaved changes.",
            "Save them before closing?",
            [this, assetId, finish = std::move(finish)](UnsavedChoice choice) {
                if (choice == UnsavedChoice::Cancel) {
                    finish(false);
                    return;
                }
                if (choice == UnsavedChoice::Save && !saveScript(assetId)) {
                    finish(false);
                    return;
                }
                const bool ok = coordinator_.apply(CloseScriptBufferIntent{assetId}).ok;
                refreshWindowTitleIfNeeded();
                finish(ok);
            })) {
        finish(false);
    }
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

std::optional<std::vector<Scripts::ScriptProgram>>
ProjectSessionController::snapshotSavedScriptsForPlay() {
    const std::vector<AssetId> referenced =
        coordinator_.document().referencedScriptAssetIds(true);
    if (referenced.empty()) return std::vector<Scripts::ScriptProgram>{};
    if (currentProjectPath_.empty()) {
        coordinator_.logError("Play blocked: save the project before running attached scripts");
        return std::nullopt;
    }

    const ProjectScriptFileService files(currentProjectPath_.parent_path());
    SavedScriptSnapshotResult snapshot = snapshotReferencedScripts(
        coordinator_.document(), files, referenced);
    for (const AssetId& assetId : referenced) {
        std::vector<ScriptDiagnostic> perAsset;
        std::copy_if(snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
            std::back_inserter(perAsset),
            [&](const ScriptDiagnostic& diagnostic) {
                return diagnostic.scriptAssetId == assetId;
            });
        coordinator_.reportScriptDiagnostics(assetId, perAsset);
    }
    if (!snapshot.ok()) {
        coordinator_.logError("Play blocked: attached scripts contain errors");
        return std::nullopt;
    }
    Scripts::ScriptRuntime runtimeValidator;
    bool contractFailed = false;
    for (const Scripts::ScriptProgram& program : snapshot.programs) {
        std::string runtimeError;
        if (runtimeValidator.validateProgram(program, &runtimeError)) continue;
        const int line = Scripts::scriptDiagnosticLine(runtimeError);
        coordinator_.reportScriptDiagnostics(program.assetId, {
            ScriptDiagnostic{DiagnosticSeverity::Error, "SCRIPT_CONTRACT",
                             program.assetId, program.sourcePath, line,
                             line > 0 ? 1 : 0,
                             std::nullopt, {}, std::move(runtimeError)}});
        contractFailed = true;
    }
    if (contractFailed) {
        coordinator_.logError("Play blocked: attached scripts violate the runtime contract");
        return std::nullopt;
    }
    return std::move(snapshot.programs);
}

bool ProjectSessionController::requestPlayProject() {
    std::optional<std::vector<Scripts::ScriptProgram>> scripts =
        snapshotSavedScriptsForPlay();
    if (!scripts) return false;
    return coordinator_.playProject(*scripts, assetRoot({})).ok;
}

bool ProjectSessionController::requestPlayCurrentScene() {
    std::optional<std::vector<Scripts::ScriptProgram>> scripts =
        snapshotSavedScriptsForPlay();
    if (!scripts) return false;
    return coordinator_.playCurrentScene(*scripts, assetRoot({})).ok;
}

bool ProjectSessionController::restartAndApplyScripts() {
    if (!coordinator_.scriptRestartRequired()) {
        coordinator_.logWarning("No saved Script changes require restart");
        return false;
    }
    // Snapshot and contract validation happen while the current PlaySession is
    // still alive. A failure therefore leaves the running game untouched.
    std::optional<std::vector<Scripts::ScriptProgram>> scripts =
        snapshotSavedScriptsForPlay();
    if (!scripts) return false;
    return coordinator_.restartPlaying(*scripts, assetRoot({})).ok;
}

void ProjectSessionController::requestExportWindows() {
    ui_.resolvePendingEdits();
    if (coordinator_.isPlaying()) {
        coordinator_.logError("EXP001 — Export is unavailable during Play");
        return;
    }
    if (projectFilesStable_ && !projectFilesStable_()) {
        coordinator_.logError(
            "EXP008 — Project files are being generated. Wait before exporting.");
        return;
    }

    const auto proceed = [this]() { beginExportWindowsAfterSaved(); };

    if (currentProjectPath_.empty()) {
        coordinator_.logWarning("Save the project before Export");
        const auto picked = saveProjectFileDialog(
            suggestedProjectSavePath(coordinator_.document().data()));
        if (!picked) return;
        if (!saveTo(*picked)) return;
        proceed();
        return;
    }

    const bool dirty = coordinator_.document().isDirty()
        || coordinator_.state().scriptEditor.anyDirty();
    if (dirty) {
        ui_.openConfirm(
            "Save and Export",
            "The project has unsaved changes. Save before exporting?",
            "Discard and Export is not available — the packer reads the saved project.",
            "",
            "Save and Export",
            "primary",
            [this, proceed](EditorUi::ConfirmChoice choice) {
                if (choice != EditorUi::ConfirmChoice::Primary) return;
                if (!saveCurrent()) return;
                proceed();
            });
        return;
    }

    proceed();
}

void ProjectSessionController::beginExportWindowsAfterSaved() {
    if (currentProjectPath_.empty()) {
        coordinator_.logError("EXP002 — Project must be saved before Export");
        return;
    }
    if (projectFilesStable_ && !projectFilesStable_()) {
        coordinator_.logError(
            "EXP008 — Project files are being generated. Wait before exporting.");
        return;
    }

    const auto destinationParent = pickExportDestinationFolder(
        currentProjectPath_.parent_path().parent_path());
    if (!destinationParent) return;

    std::string productName = coordinator_.document().data().projectName;
    if (productName.empty()) productName = currentProjectPath_.stem().string();
    bool normalized = false;
    productName = normalizeProductFileName(std::move(productName), &normalized);

    const std::filesystem::path finalDir = *destinationParent / productName;
    std::error_code ec;
    const bool exists = std::filesystem::exists(finalDir, ec);
    const auto enqueue = [this, destinationParent, productName, exists]() {
        ExportRequest request;
        request.target = ExportTarget::WindowsX64;
        request.destinationDirectory = *destinationParent;
        request.productName = productName;
        request.replaceExisting = exists;

        ExportContext context;
        context.projectFile = currentProjectPath_;
        context.projectRoot = currentProjectPath_.parent_path();
        context.projectId = coordinator_.document().data().projectId;
        context.projectName = coordinator_.document().data().projectName;
        context.projectRevision = coordinator_.document().revision();
        context.projectFormatVersion = coordinator_.document().data().formatVersion;

        if (pendingExportRunner_) {
            pendingExportRunner_(std::move(request), std::move(context));
        } else {
            coordinator_.logError("Export runner is not configured");
        }
    };

    if (exists) {
        ui_.openConfirm(
            "Replace existing export?",
            "A folder named \"" + productName + "\" already exists in the destination.",
            "The previous export will be replaced transactionally.",
            "",
            "Replace and Export",
            "danger",
            [enqueue](EditorUi::ConfirmChoice choice) {
                if (choice == EditorUi::ConfirmChoice::Primary) enqueue();
            });
        return;
    }
    enqueue();
}

} // namespace ArtCade::EditorNative
