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
    void restoreAfterLayout();

    /**
     * True while the Logic Board is replacing its own markup. Callers reacting
     * to a blur must ask at *event* time: destroying the focused field emits a
     * blur that is indistinguishable from the author leaving it, and deferring
     * the question to when the action runs answers it after the rebuild has
     * finished — which is always "no".
     */
    bool isRebuilding() const { return panel_.isRebuilding(); }
    void toggleDropdown(const std::string& dropdownId);
    void closeDropdown();
    bool hasOpenDropdown() const { return panel_.hasOpenDropdown(); }
    const std::string& openDropdownId() const { return panel_.openDropdownId(); }
    // ADR-0035: mirrors InspectorPanel's identical keyboard-nav surface.
    void moveDropdownHighlight(int delta);
    std::optional<DropdownNavEntry> dropdownHighlightCommit() const {
        return panel_.dropdownHighlightCommit();
    }
    void discardContextualGlobalVariable();
    bool hasContextualGlobalVariableDraft() const;
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
    /** ADR-0029: parse @p text and either commit it or show why it failed. */
    void commitExpressionField(const std::string& address, const std::string& text);

public:
    /**
     * Commits @p text into the addressed expression parameter. Returns the
     * parser's complaint when it does not parse — the text stays in the field
     * with the diagnostic beside it, so the caller can block instead of
     * discarding what the author wrote (Engineering Gates §15).
     */
    std::string commitExpressionText(const std::string& address, const std::string& text);

private:

    EditorCoordinator&    coordinator_;
    Rml::ElementDocument* document_ = nullptr;
    LogicBoardPanel       panel_;
};

} // namespace ArtCade::EditorNative
