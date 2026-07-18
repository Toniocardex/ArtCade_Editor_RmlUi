#pragma once

#include "editor-native/app/sfx_batch.h"
#include "editor-native/app/generated_sfx_status_projection.h"
#include "editor-native/commands/editor_operation_result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ArtCade::EditorNative {

class EditorCoordinator;
struct RemoveGeneratedSfxIntent;

enum class GeneratedSfxEditorAction : std::uint8_t {
    CommitBrowserSearch,
    RegenerateAllStale,
    CancelBatch,
    DismissBatchSummary,
    ToggleCreateMenu,
    ToggleMoreMenu,
    OpenCreateFromCurrent,
    CancelCreateFromCurrent,
    ConfirmCreateFromCurrent,
    CreateFromPreset,
    Open,
    Close,
    Duplicate,
    FocusRename,
    Remove,
    Preview,
    StopPreview,
    Generate,
    DismissGenerationError,
    CommitName,
    ApplyPreset,
    Randomize,
    ToggleMode,
    ToggleSection,
    CopyPrimaryToSecondary,
    DragMacro,
    EditCreateFromCurrentName,
    CommitMacro,
    CommitField,
    ToggleField,
    CycleField,
    Count,
};

struct GeneratedSfxEditorActionDescriptor {
    GeneratedSfxEditorAction action;
    std::string_view id;
    bool requiresPendingEditResolution = false;
    bool allowedDuringPlay = false;
    bool mutatesAuthoring = false;
};

const GeneratedSfxEditorActionDescriptor* findGeneratedSfxEditorAction(
    std::string_view id);
const std::array<GeneratedSfxEditorActionDescriptor,
                 static_cast<std::size_t>(GeneratedSfxEditorAction::Count)>&
generatedSfxEditorActionCatalog();
const GeneratedSfxEditorActionDescriptor& generatedSfxEditorActionDescriptor(
    GeneratedSfxEditorAction action);

struct GeneratedSfxGenerationAvailability {
    bool allowed = true;
    std::string reason;
};

struct GeneratedSfxEditorViewModel {
    std::optional<std::string> selectedId;
    bool workspaceOpen = false;
    bool visible = false;
    std::array<bool, static_cast<std::size_t>(GeneratedSfxEditorAction::Count)>
        actionEnabled{};
    bool advancedMode = false;
    std::string browserFilter;
    bool createMenuOpen = false;
    bool moreMenuOpen = false;
    bool createFromCurrentOpen = false;
    std::string createFromCurrentName;
    std::string createFromCurrentError;
    std::string createFromCurrentSourceId;
    bool focusCreateFromCurrentName = false;
    bool focusNameField = false;
    std::unordered_set<std::string> collapsedSections;
    std::string justGeneratedId;
    SfxBatchState batch;

    bool allows(GeneratedSfxEditorAction action) const {
        return actionEnabled[static_cast<std::size_t>(action)];
    }
};

struct GeneratedSfxEditorUpdate {
    bool handled = false;
    bool refresh = false;
    bool deferRefresh = false;
};

struct GeneratedSfxMacroChange {
    bool handled = false;
    bool committed = false;
    float displayValue = 0.f;
};

class GeneratedSfxEditorController {
public:
    using GeneratedSfxRequest = std::function<void(const std::string&)>;
    using WorkspaceRequest = std::function<void()>;
    using ProjectSavedQuery = std::function<bool()>;
    using GenerationAvailabilityQuery =
        std::function<GeneratedSfxGenerationAvailability(const std::string&)>;
    using StatusQuery =
        std::function<GeneratedSfxStatusProjection(const std::string&)>;
    using CreateFromCurrentRequest = std::function<EditorOperationResult(
        const std::string&, const std::string&, const std::string&)>;
    using DeleteRequest = std::function<EditorOperationResult(
        const RemoveGeneratedSfxIntent&)>;

    explicit GeneratedSfxEditorController(EditorCoordinator& coordinator);

    void setGenerationHandlers(GeneratedSfxRequest preview,
                               WorkspaceRequest stopPreview,
                               GeneratedSfxRequest generate);
    void setDiagnosticHandler(GeneratedSfxRequest dismissDiagnostic);
    void setCreateFromCurrentHandler(CreateFromCurrentRequest request);
    void setDeleteHandler(DeleteRequest request);
    void setBatchHandlers(WorkspaceRequest regenerateAllStale,
                          WorkspaceRequest cancelBatch,
                          WorkspaceRequest dismissSummary);
    bool setBatchState(SfxBatchState state);
    void setProjectSavedQuery(ProjectSavedQuery query);
    void setGenerationAvailabilityQuery(GenerationAvailabilityQuery query);
    void setStatusQuery(StatusQuery query);
    void detach();

    GeneratedSfxEditorUpdate dispatch(std::string_view actionId,
                                      const std::string& arg,
                                      const std::string& value);
    GeneratedSfxEditorUpdate dispatch(GeneratedSfxEditorAction action,
                                      const std::string& arg,
                                      const std::string& value);

    void validateCreateFromCurrentName(const std::string& value);
    GeneratedSfxEditorUpdate confirmCreateFromCurrent(const std::string& value);
    GeneratedSfxEditorUpdate closeCreateFromCurrentDialog();

    void beginMacroDrag(const std::string& macroId);
    GeneratedSfxMacroChange changeMacro(const std::string& macroId, float value);
    bool commitMacroDrag();

    void reconcileDocument();
    GeneratedSfxEditorViewModel viewModel() const;
    bool consumeFocusCreateFromCurrentName();
    bool consumeFocusNameField();
    void notifyOutputReady(const std::string& id);
    void clearOneShotStatus();

    bool sectionCollapsed(const std::string& id) const;
    bool projectSaved() const;
    GeneratedSfxGenerationAvailability generationAvailability(
        const std::string& id) const;
    GeneratedSfxStatusProjection status(const std::string& id) const;

private:
    struct MacroDragSession {
        std::string assetId;
        std::string macroId;
        float baselineValue = 0.f;
        float liveValue = 0.f;
    };

    void open(const std::string& id);
    std::string selectedOr(const std::string& explicitId) const;
    void closeWorkspace();
    bool actionEnabled(const GeneratedSfxEditorActionDescriptor& descriptor) const;

    EditorCoordinator& coordinator_;
    GeneratedSfxRequest previewRequest_;
    WorkspaceRequest stopPreviewRequest_;
    GeneratedSfxRequest generateRequest_;
    GeneratedSfxRequest dismissDiagnosticRequest_;
    CreateFromCurrentRequest createFromCurrentRequest_;
    DeleteRequest deleteRequest_;
    WorkspaceRequest regenerateAllStaleRequest_;
    WorkspaceRequest cancelBatchRequest_;
    WorkspaceRequest dismissBatchSummaryRequest_;
    ProjectSavedQuery projectSavedQuery_;
    GenerationAvailabilityQuery generationAvailabilityQuery_;
    StatusQuery statusQuery_;

    std::optional<std::string> selectedId_;
    bool workspaceOpen_ = false;
    bool advancedMode_ = false;
    std::string browserFilter_;
    bool createMenuOpen_ = false;
    bool moreMenuOpen_ = false;
    bool createFromCurrentOpen_ = false;
    std::string createFromCurrentName_;
    std::string createFromCurrentError_;
    std::string createFromCurrentSourceId_;
    bool focusCreateFromCurrentName_ = false;
    bool focusNameField_ = false;
    std::unordered_set<std::string> collapsedSections_{
        "secondary-voice", "noise-layer"};
    std::optional<MacroDragSession> macroDrag_;
    std::string justGeneratedId_;
    SfxBatchState batchState_;
    std::mt19937 random_{std::random_device{}()};
};

} // namespace ArtCade::EditorNative
