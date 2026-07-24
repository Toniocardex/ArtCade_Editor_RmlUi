#pragma once

#include "editor-native/app/shortcuts/editor_action.h"
#include "editor-native/app/shortcuts/editor_action_state.h"
#include "editor-native/app/shortcuts/escape_routing.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ArtCade::EditorNative {

class EditorCoordinator;
class EditorUi;
class ProjectSessionController;

enum class EditorActionStatus : std::uint8_t {
    Executed,
    StartedWorkflow,
    NoOp,
    Disabled,
    Failed,
};

struct EditorActionResult {
    EditorActionStatus status = EditorActionStatus::NoOp;
    std::string message;

    static EditorActionResult executed() {
        return {EditorActionStatus::Executed, {}};
    }
    static EditorActionResult startedWorkflow() {
        return {EditorActionStatus::StartedWorkflow, {}};
    }
    static EditorActionResult noOp() { return {EditorActionStatus::NoOp, {}}; }
    static EditorActionResult disabled(std::string reason) {
        return {EditorActionStatus::Disabled, std::move(reason)};
    }
    static EditorActionResult failed(std::string reason) {
        return {EditorActionStatus::Failed, std::move(reason)};
    }
};

struct EditorActionRequest {
    EditorActionId action = EditorActionId::None;
    ActionInvocationSource source = ActionInvocationSource::Shortcut;
};

class EditorActionDispatcher {
public:
    using FocusSelectionFn = std::function<bool()>;
    using CloseOverlayFn = std::function<void()>;

    EditorActionDispatcher(EditorCoordinator& coordinator,
                           EditorUi& ui,
                           ProjectSessionController& session);

    void setFocusSelectionHandler(FocusSelectionFn fn) {
        focusSelection_ = std::move(fn);
    }
    void setCloseLegacyOverlayHandler(CloseOverlayFn fn) {
        closeLegacyOverlay_ = std::move(fn);
    }

    void setEscapeContext(EscapeContext context) { escapeContext_ = context; }
    void setClearViewportDragHandler(std::function<void()> fn) {
        clearViewportDrag_ = std::move(fn);
    }
    void setScriptHistoryHandlers(std::function<bool()> tryUndo,
                                  std::function<bool()> tryRedo) {
        tryScriptUndo_ = std::move(tryUndo);
        tryScriptRedo_ = std::move(tryRedo);
    }

    EditorActionResult execute(const EditorActionRequest& request,
                               const EditorActionContext& context);
    EditorActionResult invoke(const EditorActionRequest& request,
                              const EditorActionContext& context);

private:
    EditorActionResult executeEnabled(EditorActionId action,
                                      const EditorActionContext& context);

    EditorCoordinator& coordinator_;
    EditorUi& ui_;
    ProjectSessionController& session_;
    FocusSelectionFn focusSelection_;
    CloseOverlayFn closeLegacyOverlay_;
    std::function<void()> clearViewportDrag_;
    std::function<bool()> tryScriptUndo_;
    std::function<bool()> tryScriptRedo_;
    EscapeContext escapeContext_{};
};

} // namespace ArtCade::EditorNative
