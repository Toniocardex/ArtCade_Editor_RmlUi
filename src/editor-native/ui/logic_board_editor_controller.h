#pragma once

#include "editor-native/ui/logic_board_panel.h"

#include <functional>
#include <string>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Controller for the Object-Type-owned Logic Board editor. It owns the
// presentation projection and translates RmlUi actions into coordinator
// intents/commands; authoring authority remains in ProjectDocument.
class LogicBoardEditorController {
public:
    using WorkspaceSwitchPreparation = std::function<void()>;

    LogicBoardEditorController(EditorCoordinator& coordinator,
                               Rml::ElementDocument* document);

    void detach();
    void refresh();
    void toggleDropdown(const std::string& dropdownId);
    void closeDropdown();
    bool hasKeyCapture() const;
    bool captureKey(LogicKey key);
    bool cancelKeyCapture();
    void toggleVariablesDrawer();
    void toggleRuleCollapsed(const std::string& ruleId);
    void collapseAllRules();
    void expandAllRules();
    // Queried by EditorUi::refreshToolbar() to sync the static Collapse
    // All/Expand All toolbar buttons' disabled state (those buttons live
    // outside the panel's own rebuilt markup as of slice 1c).
    bool canCollapseAllRules() const;
    bool canExpandAllRules() const;
    // Re-syncs the "compact" responsive class without a full rebuild — cheap
    // enough to call from a per-frame splitter drag callback.
    void syncResponsiveClass();
    std::string objectTypeMenuEntries() const;
    bool handleAction(const std::string& action, const std::string& arg,
                      const std::string& value,
                      const WorkspaceSwitchPreparation& prepareWorkspaceSwitch);

private:
    EditorCoordinator&    coordinator_;
    Rml::ElementDocument* document_ = nullptr;
    LogicBoardPanel       panel_;
};

} // namespace ArtCade::EditorNative
