#include "editor-native/ui/generated_sfx_editor_controller.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/model/generated_sfx_policy.h"
#include "editor-native/model/generated_sfx_preset_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr std::array<GeneratedSfxEditorActionDescriptor,
                     static_cast<std::size_t>(GeneratedSfxEditorAction::Count)>
    kActions{{
    {GeneratedSfxEditorAction::CommitBrowserSearch, "commit-sfx-browser-search", false, true, false},
    {GeneratedSfxEditorAction::RegenerateAllStale, "regenerate-all-stale-sfx", true, false, true},
    {GeneratedSfxEditorAction::CancelBatch, "cancel-sfx-batch", false, true, false},
    {GeneratedSfxEditorAction::DismissBatchSummary, "dismiss-sfx-batch-summary", false, true, false},
    {GeneratedSfxEditorAction::ToggleCreateMenu, "toggle-sfx-create-menu", true, false, false},
    {GeneratedSfxEditorAction::ToggleMoreMenu, "toggle-sfx-more-menu", true, false, false},
    {GeneratedSfxEditorAction::OpenCreateFromCurrent, "open-sfx-create-from-current", true, false, false},
    {GeneratedSfxEditorAction::CancelCreateFromCurrent, "cancel-sfx-create-from-current", false, true, false},
    {GeneratedSfxEditorAction::ConfirmCreateFromCurrent, "confirm-sfx-create-from-current", true, false, true},
    {GeneratedSfxEditorAction::CreateFromPreset, "create-generated-sfx", true, false, true},
    {GeneratedSfxEditorAction::Open, "open-generated-sfx", true, false, false},
    {GeneratedSfxEditorAction::Close, "close-generated-sfx", true, true, false},
    {GeneratedSfxEditorAction::Duplicate, "duplicate-generated-sfx", true, false, true},
    {GeneratedSfxEditorAction::FocusRename, "focus-sfx-rename", true, false, false},
    {GeneratedSfxEditorAction::Remove, "remove-generated-sfx", true, false, true},
    {GeneratedSfxEditorAction::Preview, "preview-generated-sfx", false, false, false},
    {GeneratedSfxEditorAction::StopPreview, "stop-generated-sfx-preview", false, true, false},
    {GeneratedSfxEditorAction::Generate, "generate-sfx-output", true, false, true},
    {GeneratedSfxEditorAction::DismissGenerationError, "dismiss-sfx-generation-error", false, true, false},
    {GeneratedSfxEditorAction::CommitName, "commit-sfx-name", false, false, true},
    {GeneratedSfxEditorAction::ApplyPreset, "apply-sfx-preset", true, false, true},
    {GeneratedSfxEditorAction::Randomize, "randomize-sfx", true, false, true},
    {GeneratedSfxEditorAction::ToggleMode, "toggle-sfx-mode", true, false, false},
    {GeneratedSfxEditorAction::ToggleSection, "toggle-sfx-section", true, false, false},
    {GeneratedSfxEditorAction::CopyPrimaryToSecondary, "copy-primary-to-secondary", true, false, true},
    {GeneratedSfxEditorAction::DragMacro, "drag-sfx-macro", false, false, true},
    {GeneratedSfxEditorAction::EditCreateFromCurrentName, "sfx-create-from-current-name", false, false, false},
    {GeneratedSfxEditorAction::CommitMacro, "commit-sfx-macro", false, false, true},
    {GeneratedSfxEditorAction::CommitField, "commit-sfx-field", false, false, true},
    {GeneratedSfxEditorAction::ToggleField, "toggle-sfx-field", true, false, true},
    {GeneratedSfxEditorAction::CycleField, "cycle-sfx-field", true, false, true},
}};

bool setNumericField(artcade::sfx::SfxRecipe& recipe,
                     const std::string& field,
                     const std::string& text) {
    const auto parsed = parseNumberField(text);
    if (!parsed) return false;
    const float value = *parsed;
    if (field == "duration") recipe.durationSeconds = value;
    else if (field == "masterGain") recipe.masterGain = value;
    else if (field == "attack") recipe.amplitude.attackSeconds = value;
    else if (field == "decay") recipe.amplitude.decaySeconds = value;
    else if (field == "sustain") recipe.amplitude.sustainLevel = value;
    else if (field == "release") recipe.amplitude.releaseSeconds = value;
    else if (field == "crusher.bits") {
        if (std::floor(value) != value) return false;
        recipe.bitCrusher.quantizationBits = static_cast<int>(value);
    } else if (field == "crusher.rate") recipe.bitCrusher.reductionRateHz = value;
    else if (field == "filter.lowPass") recipe.filter.lowPassHz = value;
    else if (field == "filter.dcCutoff") recipe.filter.dcBlockCutoffHz = value;
    else if (field == "noise.gain") recipe.noise.gain = value;
    else {
        artcade::sfx::VoiceParams* voice = nullptr;
        std::string suffix;
        if (field.rfind("primary.", 0) == 0) {
            voice = &recipe.primaryVoice;
            suffix = field.substr(8);
        } else if (field.rfind("secondary.", 0) == 0) {
            voice = &recipe.secondaryVoice;
            suffix = field.substr(10);
        }
        if (voice) {
            if (suffix == "gain") voice->gain = value;
            else if (suffix == "detune") voice->detuneSemitones = value;
            else if (suffix == "dutyStart") voice->dutyStart = value;
            else if (suffix == "dutyEnd") voice->dutyEnd = value;
            else {
                auto& pitch = voice->pitch;
                if (suffix == "pitch.startHz") pitch.startHz = value;
                else if (suffix == "pitch.endHz") pitch.endHz = value;
                else if (suffix == "pitch.curve") pitch.sweepCurve = value;
                else if (suffix == "pitch.vibratoDepth") pitch.vibratoDepthSemitones = value;
                else if (suffix == "pitch.vibratoRate") pitch.vibratoRateHz = value;
                else if (suffix == "pitch.arpSemitones") pitch.arpeggioSemitones = value;
                else if (suffix == "pitch.arpRate") pitch.arpeggioRateHz = value;
                else return false;
            }
        } else if (field.rfind("noise.pitch.", 0) == 0) {
            auto& pitch = recipe.noise.clock;
            suffix = field.substr(12);
            if (suffix == "startHz") pitch.startHz = value;
            else if (suffix == "endHz") pitch.endHz = value;
            else if (suffix == "curve") pitch.sweepCurve = value;
            else if (suffix == "vibratoDepth") pitch.vibratoDepthSemitones = value;
            else if (suffix == "vibratoRate") pitch.vibratoRateHz = value;
            else if (suffix == "arpSemitones") pitch.arpeggioSemitones = value;
            else if (suffix == "arpRate") pitch.arpeggioRateHz = value;
            else return false;
        } else return false;
    }
    return true;
}

bool editToggleField(artcade::sfx::SfxRecipe& recipe, const std::string& field) {
    if (field == "primary.enabled") recipe.primaryVoice.enabled = !recipe.primaryVoice.enabled;
    else if (field == "secondary.enabled") recipe.secondaryVoice.enabled = !recipe.secondaryVoice.enabled;
    else if (field == "noise.enabled") recipe.noise.enabled = !recipe.noise.enabled;
    else if (field == "crusher.enabled") recipe.bitCrusher.enabled = !recipe.bitCrusher.enabled;
    else if (field == "filter.dcEnabled") recipe.filter.dcBlockEnabled = !recipe.filter.dcBlockEnabled;
    else return false;
    return true;
}

bool editCycleField(artcade::sfx::SfxRecipe& recipe, const std::string& field) {
    artcade::sfx::VoiceParams* voice = nullptr;
    if (field.rfind("primary.", 0) == 0) voice = &recipe.primaryVoice;
    else if (field.rfind("secondary.", 0) == 0) voice = &recipe.secondaryVoice;
    if (voice && field.find(".waveform") != std::string::npos) {
        using artcade::sfx::Waveform;
        voice->waveform = static_cast<Waveform>(
            (static_cast<int>(voice->waveform) + 1) % 4);
        return true;
    }
    if (voice && field.find(".quality") != std::string::npos) {
        using artcade::sfx::OscillatorQuality;
        voice->quality = voice->quality == OscillatorQuality::Raw
            ? OscillatorQuality::BandLimited : OscillatorQuality::Raw;
        return true;
    }
    if (field.find(".sweep") == std::string::npos) return false;
    artcade::sfx::PitchParams* pitch = nullptr;
    if (field.rfind("primary.", 0) == 0) pitch = &recipe.primaryVoice.pitch;
    else if (field.rfind("secondary.", 0) == 0) pitch = &recipe.secondaryVoice.pitch;
    else if (field.rfind("noise.", 0) == 0) pitch = &recipe.noise.clock;
    if (!pitch) return false;
    using artcade::sfx::PitchSweepMode;
    pitch->sweepMode = pitch->sweepMode == PitchSweepMode::LinearHz
        ? PitchSweepMode::Exponential : PitchSweepMode::LinearHz;
    return true;
}

} // namespace

const GeneratedSfxEditorActionDescriptor* findGeneratedSfxEditorAction(
    std::string_view id) {
    for (const auto& action : kActions)
        if (action.id == id) return &action;
    return nullptr;
}

const std::array<GeneratedSfxEditorActionDescriptor,
                 static_cast<std::size_t>(GeneratedSfxEditorAction::Count)>&
generatedSfxEditorActionCatalog() {
    return kActions;
}

const GeneratedSfxEditorActionDescriptor& generatedSfxEditorActionDescriptor(
    GeneratedSfxEditorAction action) {
    for (const auto& candidate : kActions)
        if (candidate.action == action) return candidate;
    return kActions[0];
}

GeneratedSfxEditorController::GeneratedSfxEditorController(
    EditorCoordinator& coordinator) : coordinator_(coordinator) {}

void GeneratedSfxEditorController::setGenerationHandlers(
    GeneratedSfxRequest preview, WorkspaceRequest stopPreview,
    GeneratedSfxRequest generate) {
    previewRequest_ = std::move(preview);
    stopPreviewRequest_ = std::move(stopPreview);
    generateRequest_ = std::move(generate);
}

void GeneratedSfxEditorController::setDiagnosticHandler(
    GeneratedSfxRequest dismissDiagnostic) {
    dismissDiagnosticRequest_ = std::move(dismissDiagnostic);
}

void GeneratedSfxEditorController::setCreateFromCurrentHandler(
    CreateFromCurrentRequest request) {
    createFromCurrentRequest_ = std::move(request);
}

void GeneratedSfxEditorController::setDeleteHandler(DeleteRequest request) {
    deleteRequest_ = std::move(request);
}

void GeneratedSfxEditorController::setBatchHandlers(
    WorkspaceRequest regenerateAllStale, WorkspaceRequest cancelBatch,
    WorkspaceRequest dismissSummary) {
    regenerateAllStaleRequest_ = std::move(regenerateAllStale);
    cancelBatchRequest_ = std::move(cancelBatch);
    dismissBatchSummaryRequest_ = std::move(dismissSummary);
}

bool GeneratedSfxEditorController::setBatchState(SfxBatchState state) {
    const bool changed = state.active != batchState_.active
        || state.cancelRequested != batchState_.cancelRequested
        || state.summaryVisible != batchState_.summaryVisible
        || state.currentIndex != batchState_.currentIndex
        || state.items.size() != batchState_.items.size()
        || state.succeeded != batchState_.succeeded
        || state.failed != batchState_.failed
        || state.skipped != batchState_.skipped
        || state.cancelled != batchState_.cancelled;
    batchState_ = std::move(state);
    return changed;
}

void GeneratedSfxEditorController::setProjectSavedQuery(ProjectSavedQuery query) {
    projectSavedQuery_ = std::move(query);
}

void GeneratedSfxEditorController::setGenerationAvailabilityQuery(
    GenerationAvailabilityQuery query) {
    generationAvailabilityQuery_ = std::move(query);
}

void GeneratedSfxEditorController::setStatusQuery(StatusQuery query) {
    statusQuery_ = std::move(query);
}

void GeneratedSfxEditorController::detach() {
    closeWorkspace();
    previewRequest_ = {};
    stopPreviewRequest_ = {};
    generateRequest_ = {};
    dismissDiagnosticRequest_ = {};
    createFromCurrentRequest_ = {};
    deleteRequest_ = {};
    regenerateAllStaleRequest_ = {};
    cancelBatchRequest_ = {};
    dismissBatchSummaryRequest_ = {};
    projectSavedQuery_ = {};
    generationAvailabilityQuery_ = {};
    statusQuery_ = {};
    batchState_ = {};
    macroDrag_.reset();
}

GeneratedSfxEditorUpdate GeneratedSfxEditorController::dispatch(
    std::string_view actionId, const std::string& arg,
    const std::string& value) {
    const auto* descriptor = findGeneratedSfxEditorAction(actionId);
    if (!descriptor) return {};
    return dispatch(descriptor->action, arg, value);
}

std::string GeneratedSfxEditorController::selectedOr(
    const std::string& explicitId) const {
    return explicitId.empty() && selectedId_ ? *selectedId_ : explicitId;
}

void GeneratedSfxEditorController::open(const std::string& id) {
    if (id.empty() || !coordinator_.document().hasGeneratedSfx(id)) return;
    if (selectedId_ && *selectedId_ != id && stopPreviewRequest_) stopPreviewRequest_();
    workspaceOpen_ = true;
    selectedId_ = id;
    createMenuOpen_ = false;
}

void GeneratedSfxEditorController::closeWorkspace() {
    if (stopPreviewRequest_) stopPreviewRequest_();
    workspaceOpen_ = false;
    selectedId_.reset();
    createMenuOpen_ = false;
    moreMenuOpen_ = false;
    createFromCurrentOpen_ = false;
    createFromCurrentName_.clear();
    createFromCurrentError_.clear();
    createFromCurrentSourceId_.clear();
    macroDrag_.reset();
}

bool GeneratedSfxEditorController::actionEnabled(
    const GeneratedSfxEditorActionDescriptor& descriptor) const {
    return !coordinator_.isPlaying() || descriptor.allowedDuringPlay;
}

GeneratedSfxEditorUpdate GeneratedSfxEditorController::dispatch(
    GeneratedSfxEditorAction action, const std::string& arg,
    const std::string& value) {
    clearOneShotStatus();
    const auto& descriptor = generatedSfxEditorActionDescriptor(action);
    if (!actionEnabled(descriptor)) {
        coordinator_.logWarning("Stop Play before using the Generated SFX editor");
        return {true, false, false};
    }

    switch (action) {
    case GeneratedSfxEditorAction::CommitBrowserSearch:
        browserFilter_ = value;
        return {true, true, false};
    case GeneratedSfxEditorAction::RegenerateAllStale:
        if (regenerateAllStaleRequest_) regenerateAllStaleRequest_();
        return {true, false, false};
    case GeneratedSfxEditorAction::CancelBatch:
        if (cancelBatchRequest_) cancelBatchRequest_();
        return {true, false, false};
    case GeneratedSfxEditorAction::DismissBatchSummary:
        if (dismissBatchSummaryRequest_) dismissBatchSummaryRequest_();
        return {true, false, false};
    case GeneratedSfxEditorAction::ToggleCreateMenu:
        createMenuOpen_ = !createMenuOpen_;
        moreMenuOpen_ = false;
        return {true, true, false};
    case GeneratedSfxEditorAction::ToggleMoreMenu:
        if (!selectedId_) return {true, false, false};
        moreMenuOpen_ = !moreMenuOpen_;
        createMenuOpen_ = false;
        return {true, true, false};
    case GeneratedSfxEditorAction::OpenCreateFromCurrent: {
        const std::string sourceId = selectedOr(arg);
        const auto* source = coordinator_.document().findGeneratedSfx(sourceId);
        if (!source) return {true, false, false};
        moreMenuOpen_ = false;
        createFromCurrentOpen_ = true;
        createFromCurrentSourceId_ = sourceId;
        createFromCurrentName_ = uniqueGeneratedSfxName(
            coordinator_.document(), source->name);
        createFromCurrentError_.clear();
        if (audioDisplayNameExists(
                coordinator_.document().data(), createFromCurrentName_))
            createFromCurrentError_ = "Audio name already exists";
        focusCreateFromCurrentName_ = true;
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::CancelCreateFromCurrent:
        return closeCreateFromCurrentDialog();
    case GeneratedSfxEditorAction::ConfirmCreateFromCurrent:
        return confirmCreateFromCurrent(value);
    case GeneratedSfxEditorAction::CreateFromPreset: {
        const auto* preset = findGeneratedSfxPreset(arg);
        const auto recipe = generatedSfxRecipeFromPreset(arg);
        if (!preset || !recipe) return {true, false, false};
        const std::string id = nextGeneratedSfxId(coordinator_.document());
        const std::string name = uniqueGeneratedSfxName(
            coordinator_.document(), std::string(preset->defaultAssetName));
        const auto result = coordinator_.apply(
            CreateGeneratedSfxIntent{id, name, *recipe});
        if (result.ok) {
            createMenuOpen_ = false;
            focusNameField_ = true;
            open(id);
        }
        return {true, result.ok, false};
    }
    case GeneratedSfxEditorAction::Open:
        open(arg);
        return {true, true, false};
    case GeneratedSfxEditorAction::Close:
        closeWorkspace();
        return {true, true, false};
    case GeneratedSfxEditorAction::Duplicate: {
        const std::string sourceId = selectedOr(arg);
        const auto* source = coordinator_.document().findGeneratedSfx(sourceId);
        if (!source) return {true, false, false};
        const std::string newId = nextGeneratedSfxId(coordinator_.document());
        const std::string newName = uniqueGeneratedSfxName(
            coordinator_.document(), source->name);
        const auto result = coordinator_.apply(
            DuplicateGeneratedSfxIntent{sourceId, newId, newName});
        if (result.ok) {
            focusNameField_ = true;
            open(newId);
        }
        return {true, result.ok, false};
    }
    case GeneratedSfxEditorAction::FocusRename:
        if (coordinator_.document().hasGeneratedSfx(arg)) {
            focusNameField_ = true;
            open(arg);
        }
        return {true, true, false};
    case GeneratedSfxEditorAction::Remove: {
        const std::string id = selectedOr(arg);
        if (id.empty()) return {true, false, false};
        const auto result = deleteRequest_
            ? deleteRequest_(RemoveGeneratedSfxIntent{id})
            : EditorOperationResult::failure(
                "Generated SFX delete application handler is unavailable");
        if (result.ok && selectedId_ == id) {
            if (stopPreviewRequest_) stopPreviewRequest_();
            selectedId_.reset();
            const auto& list = coordinator_.document().data().generatedSfx;
            if (!list.empty()) selectedId_ = list.front().id;
        }
        return {true, result.ok, false};
    }
    case GeneratedSfxEditorAction::Preview:
        if (selectedId_ && previewRequest_) previewRequest_(*selectedId_);
        return {true, false, false};
    case GeneratedSfxEditorAction::StopPreview:
        if (stopPreviewRequest_) stopPreviewRequest_();
        return {true, false, false};
    case GeneratedSfxEditorAction::Generate:
        if (selectedId_ && generateRequest_) generateRequest_(*selectedId_);
        return {true, true, false};
    case GeneratedSfxEditorAction::DismissGenerationError:
        if (selectedId_ && dismissDiagnosticRequest_)
            dismissDiagnosticRequest_(*selectedId_);
        return {true, true, false};
    case GeneratedSfxEditorAction::CommitName: {
        const auto* current = selectedId_
            ? coordinator_.document().findGeneratedSfx(*selectedId_) : nullptr;
        if (current && !value.empty() && value != current->name)
            coordinator_.apply(RenameGeneratedSfxIntent{current->id, value});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::ApplyPreset: {
        const auto recipe = generatedSfxRecipeFromPreset(arg);
        if (!selectedId_ || !recipe) return {true, false, false};
        coordinator_.apply(UpdateGeneratedSfxRecipeIntent{*selectedId_, *recipe});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::Randomize: {
        const auto* current = selectedId_
            ? coordinator_.document().findGeneratedSfx(*selectedId_) : nullptr;
        if (!current) return {true, false, false};
        auto recipe = current->recipe;
        randomizeSfxMacros(recipe, random_);
        coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
            current->id, std::move(recipe)});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::ToggleMode:
        advancedMode_ = !advancedMode_;
        return {true, true, false};
    case GeneratedSfxEditorAction::ToggleSection:
        if (collapsedSections_.count(arg)) collapsedSections_.erase(arg);
        else collapsedSections_.insert(arg);
        return {true, true, false};
    case GeneratedSfxEditorAction::CopyPrimaryToSecondary: {
        const auto* current = selectedId_
            ? coordinator_.document().findGeneratedSfx(*selectedId_) : nullptr;
        if (!current) return {true, false, false};
        auto recipe = current->recipe;
        const bool wasEnabled = recipe.secondaryVoice.enabled;
        recipe.secondaryVoice = recipe.primaryVoice;
        recipe.secondaryVoice.enabled = wasEnabled;
        coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
            current->id, std::move(recipe)});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::DragMacro:
    case GeneratedSfxEditorAction::EditCreateFromCurrentName:
        // Gesture/text events are routed to the controller's dedicated
        // lifecycle methods by the RmlUi listener.
        return {true, false, false};
    case GeneratedSfxEditorAction::CommitMacro: {
        const auto* current = selectedId_
            ? coordinator_.document().findGeneratedSfx(*selectedId_) : nullptr;
        const auto parsed = parseNumberField(value);
        if (!current || !parsed) {
            coordinator_.logError("Generated SFX macro is not a valid number");
            return {true, true, false};
        }
        auto recipe = current->recipe;
        if (!setSfxMacroFieldFromDisplay(recipe, arg, *parsed)) {
            coordinator_.logError("Generated SFX macro is not a valid number");
            return {true, true, false};
        }
        coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
            current->id, std::move(recipe)});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::CommitField:
    case GeneratedSfxEditorAction::ToggleField:
    case GeneratedSfxEditorAction::CycleField: {
        const auto* current = selectedId_
            ? coordinator_.document().findGeneratedSfx(*selectedId_) : nullptr;
        if (!current) return {true, false, false};
        auto recipe = current->recipe;
        bool understood = false;
        if (action == GeneratedSfxEditorAction::CommitField && arg == "seed") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            understood = end && *end == '\0'
                && parsed <= static_cast<unsigned long>(
                    std::numeric_limits<std::uint32_t>::max());
            if (understood) recipe.randomSeed = static_cast<std::uint32_t>(parsed);
        } else if (action == GeneratedSfxEditorAction::CommitField) {
            understood = setNumericField(recipe, arg, value);
        } else if (action == GeneratedSfxEditorAction::ToggleField) {
            understood = editToggleField(recipe, arg);
        } else {
            understood = editCycleField(recipe, arg);
        }
        if (!understood) {
            coordinator_.logError(
                "Generated SFX field is not a valid number or setting");
            return {true, true, false};
        }
        coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
            current->id, std::move(recipe)});
        return {true, true, false};
    }
    case GeneratedSfxEditorAction::Count:
        break;
    }
    return {};
}

void GeneratedSfxEditorController::validateCreateFromCurrentName(
    const std::string& value) {
    if (!createFromCurrentOpen_) return;
    createFromCurrentName_ = normalizeAudioDisplayName(value);
    if (createFromCurrentName_.empty()) {
        createFromCurrentError_ = "Name cannot be empty";
    } else if (audioDisplayNameExists(
                   coordinator_.document().data(), createFromCurrentName_)) {
        createFromCurrentError_ = "Audio name already exists";
    } else {
        createFromCurrentError_.clear();
    }
}

GeneratedSfxEditorUpdate
GeneratedSfxEditorController::confirmCreateFromCurrent(const std::string& value) {
    if (!createFromCurrentOpen_) return {true, false, false};
    if (!actionEnabled(generatedSfxEditorActionDescriptor(
            GeneratedSfxEditorAction::ConfirmCreateFromCurrent))) {
        coordinator_.logWarning("Stop Play before creating Generated SFX");
        return {true, false, false};
    }
    if (!projectSaved()) {
        createFromCurrentError_ =
            "Save the project before creating an audio asset";
        return {true, false, true};
    }
    validateCreateFromCurrentName(
        value.empty() ? createFromCurrentName_ : value);
    if (!createFromCurrentError_.empty()) return {true, false, true};
    if (createFromCurrentSourceId_.empty()
        || !coordinator_.document().hasGeneratedSfx(createFromCurrentSourceId_))
        return closeCreateFromCurrentDialog();

    const std::string newId = nextGeneratedSfxId(coordinator_.document());
    const auto availability = generationAvailability(newId);
    if (!availability.allowed) {
        createFromCurrentError_ = availability.reason.empty()
            ? "Audio output cannot be created" : availability.reason;
        return {true, false, true};
    }
    const auto result = createFromCurrentRequest_
        ? createFromCurrentRequest_(
            createFromCurrentSourceId_, newId, createFromCurrentName_)
        : EditorOperationResult::failure(
            "Create from Current application handler is unavailable");
    if (!result.ok) {
        createFromCurrentError_ = result.error.empty()
            ? "Could not create sound" : result.error;
        return {true, false, true};
    }
    createFromCurrentOpen_ = false;
    createFromCurrentName_.clear();
    createFromCurrentError_.clear();
    createFromCurrentSourceId_.clear();
    moreMenuOpen_ = false;
    open(newId);
    return {true, false, true};
}

GeneratedSfxEditorUpdate
GeneratedSfxEditorController::closeCreateFromCurrentDialog() {
    createFromCurrentOpen_ = false;
    createFromCurrentName_.clear();
    createFromCurrentError_.clear();
    createFromCurrentSourceId_.clear();
    focusCreateFromCurrentName_ = false;
    return {true, false, true};
}

void GeneratedSfxEditorController::beginMacroDrag(const std::string& macroId) {
    if (!actionEnabled(generatedSfxEditorActionDescriptor(
            GeneratedSfxEditorAction::DragMacro))) return;
    if (!selectedId_) return;
    const auto* current = coordinator_.document().findGeneratedSfx(*selectedId_);
    if (!current) return;
    clearOneShotStatus();
    const float baseline = sfxMacroValue(current->recipe, macroId);
    macroDrag_ = MacroDragSession{current->id, macroId, baseline, baseline};
}

GeneratedSfxMacroChange GeneratedSfxEditorController::changeMacro(
    const std::string& macroId, float value) {
    if (!actionEnabled(generatedSfxEditorActionDescriptor(
            GeneratedSfxEditorAction::DragMacro))) return {};
    if (!selectedId_) return {};
    const auto* current = coordinator_.document().findGeneratedSfx(*selectedId_);
    if (!current) return {};
    clearOneShotStatus();
    if (macroDrag_ && macroDrag_->assetId == current->id
        && macroDrag_->macroId == macroId) {
        macroDrag_->liveValue = value;
        auto preview = current->recipe;
        if (!setSfxMacroField(preview, macroId, value)) return {};
        return {true, false, sfxMacroDisplayValue(preview, macroId)};
    }
    auto recipe = current->recipe;
    if (!setSfxMacroField(recipe, macroId, value)) return {};
    const float display = sfxMacroDisplayValue(recipe, macroId);
    coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
        current->id, std::move(recipe)});
    return {true, true, display};
}

bool GeneratedSfxEditorController::commitMacroDrag() {
    if (!actionEnabled(generatedSfxEditorActionDescriptor(
            GeneratedSfxEditorAction::DragMacro))) {
        macroDrag_.reset();
        return false;
    }
    if (!macroDrag_) return false;
    const MacroDragSession session = *macroDrag_;
    macroDrag_.reset();
    if (!selectedId_ || *selectedId_ != session.assetId) return false;
    const auto* current = coordinator_.document().findGeneratedSfx(session.assetId);
    if (!current) return false;
    auto recipe = current->recipe;
    if (!setSfxMacroField(recipe, session.macroId, session.liveValue)) return false;
    const auto result = coordinator_.apply(UpdateGeneratedSfxRecipeIntent{
        current->id, std::move(recipe)});
    return result.ok;
}

void GeneratedSfxEditorController::reconcileDocument() {
    if (selectedId_ && !coordinator_.document().hasGeneratedSfx(*selectedId_)) {
        if (stopPreviewRequest_) stopPreviewRequest_();
        selectedId_.reset();
        moreMenuOpen_ = false;
        createFromCurrentOpen_ = false;
        createFromCurrentName_.clear();
        createFromCurrentError_.clear();
        createFromCurrentSourceId_.clear();
        const auto& list = coordinator_.document().data().generatedSfx;
        if (workspaceOpen_ && !list.empty()) selectedId_ = list.front().id;
    }
    if (macroDrag_ && (!selectedId_ || macroDrag_->assetId != *selectedId_))
        macroDrag_.reset();
}

GeneratedSfxEditorViewModel GeneratedSfxEditorController::viewModel() const {
    GeneratedSfxEditorViewModel result;
    result.selectedId = selectedId_;
    result.workspaceOpen = workspaceOpen_;
    result.visible = workspaceOpen_ && !coordinator_.isPlaying();
    for (const auto& descriptor : kActions) {
        result.actionEnabled[static_cast<std::size_t>(descriptor.action)] =
            actionEnabled(descriptor);
    }
    result.advancedMode = advancedMode_;
    result.browserFilter = browserFilter_;
    result.createMenuOpen = createMenuOpen_;
    result.moreMenuOpen = moreMenuOpen_;
    result.createFromCurrentOpen = createFromCurrentOpen_;
    result.createFromCurrentName = createFromCurrentName_;
    result.createFromCurrentError = createFromCurrentError_;
    result.createFromCurrentSourceId = createFromCurrentSourceId_;
    result.focusCreateFromCurrentName = focusCreateFromCurrentName_;
    result.focusNameField = focusNameField_;
    result.collapsedSections = collapsedSections_;
    result.justGeneratedId = justGeneratedId_;
    result.batch = batchState_;
    return result;
}

bool GeneratedSfxEditorController::consumeFocusCreateFromCurrentName() {
    return std::exchange(focusCreateFromCurrentName_, false);
}

bool GeneratedSfxEditorController::consumeFocusNameField() {
    return std::exchange(focusNameField_, false);
}

void GeneratedSfxEditorController::notifyOutputReady(const std::string& id) {
    justGeneratedId_ = id;
}

void GeneratedSfxEditorController::clearOneShotStatus() {
    justGeneratedId_.clear();
}

bool GeneratedSfxEditorController::sectionCollapsed(const std::string& id) const {
    return collapsedSections_.count(id) != 0;
}

bool GeneratedSfxEditorController::projectSaved() const {
    return !projectSavedQuery_ || projectSavedQuery_();
}

GeneratedSfxGenerationAvailability
GeneratedSfxEditorController::generationAvailability(const std::string& id) const {
    return generationAvailabilityQuery_
        ? generationAvailabilityQuery_(id)
        : GeneratedSfxGenerationAvailability{};
}

GeneratedSfxStatusProjection
GeneratedSfxEditorController::status(const std::string& id) const {
    return statusQuery_ ? statusQuery_(id) : GeneratedSfxStatusProjection{};
}

} // namespace ArtCade::EditorNative
