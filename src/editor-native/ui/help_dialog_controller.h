#pragma once

#include "editor-native/app/editor_build_info.h"
#include "editor-native/app/shortcuts/keyboard_shortcut_projection.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

enum class HelpDialogPage : std::uint8_t {
    None,
    KeyboardShortcuts,
    About,
};

class HelpDialogController {
public:
    explicit HelpDialogController(Rml::ElementDocument* document);

    bool isOpen() const { return page_ != HelpDialogPage::None; }
    HelpDialogPage page() const { return page_; }

    void openKeyboardShortcuts(const KeyboardShortcutProjectionInput& input);
    void openAbout(const AboutArtCadeProjection& projection);
    void close();

    void setShortcutFilter(KeyboardShortcutsFilter filter);
    void setShortcutSearch(std::string search);
    void refreshKeyboardShortcuts(const KeyboardShortcutProjectionInput& input);

    // Tab / Shift+Tab stay inside the open Help dialog.
    bool handleTabKey(bool shift);

private:
    void renderKeyboardShortcuts();
    void renderShortcutsList();
    void renderAbout();
    void showHost();
    void hideHost();
    void captureFocus();
    void restoreFocus();
    void focusInitialControl();
    std::vector<Rml::Element*> focusableElements() const;

    Rml::ElementDocument* document_ = nullptr;
    HelpDialogPage page_ = HelpDialogPage::None;
    KeyboardShortcutsFilter filter_ = KeyboardShortcutsFilter::All;
    std::string search_;
    EditorActionContext availabilityContext_{};
    AboutArtCadeProjection about_{};
    Rml::Element* previousFocus_ = nullptr;
};

} // namespace ArtCade::EditorNative
