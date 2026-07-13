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
    bool handleAction(const std::string& action, const std::string& arg,
                      const std::string& value,
                      const WorkspaceSwitchPreparation& prepareWorkspaceSwitch);

private:
    EditorCoordinator&    coordinator_;
    Rml::ElementDocument* document_ = nullptr;
    LogicBoardPanel       panel_;
};

} // namespace ArtCade::EditorNative
