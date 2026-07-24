#include "editor-native/app/shortcuts/shortcut_format.h"

#include "editor-native/app/shortcuts/editor_action_catalog.h"

namespace ArtCade::EditorNative {

namespace {

const char* keyLabel(ShortcutKey key) {
    switch (key) {
    case ShortcutKey::A: return "A"; case ShortcutKey::B: return "B";
    case ShortcutKey::C: return "C"; case ShortcutKey::D: return "D";
    case ShortcutKey::E: return "E"; case ShortcutKey::F: return "F";
    case ShortcutKey::G: return "G"; case ShortcutKey::H: return "H";
    case ShortcutKey::I: return "I"; case ShortcutKey::J: return "J";
    case ShortcutKey::K: return "K"; case ShortcutKey::L: return "L";
    case ShortcutKey::M: return "M"; case ShortcutKey::N: return "N";
    case ShortcutKey::O: return "O"; case ShortcutKey::P: return "P";
    case ShortcutKey::Q: return "Q"; case ShortcutKey::R: return "R";
    case ShortcutKey::S: return "S"; case ShortcutKey::T: return "T";
    case ShortcutKey::U: return "U"; case ShortcutKey::V: return "V";
    case ShortcutKey::W: return "W"; case ShortcutKey::X: return "X";
    case ShortcutKey::Y: return "Y"; case ShortcutKey::Z: return "Z";
    case ShortcutKey::Digit0: return "0"; case ShortcutKey::Digit1: return "1";
    case ShortcutKey::Digit2: return "2"; case ShortcutKey::Digit3: return "3";
    case ShortcutKey::Digit4: return "4"; case ShortcutKey::Digit5: return "5";
    case ShortcutKey::Digit6: return "6"; case ShortcutKey::Digit7: return "7";
    case ShortcutKey::Digit8: return "8"; case ShortcutKey::Digit9: return "9";
    case ShortcutKey::F1: return "F1"; case ShortcutKey::F2: return "F2";
    case ShortcutKey::F3: return "F3"; case ShortcutKey::F4: return "F4";
    case ShortcutKey::F5: return "F5"; case ShortcutKey::F6: return "F6";
    case ShortcutKey::F7: return "F7"; case ShortcutKey::F8: return "F8";
    case ShortcutKey::F9: return "F9"; case ShortcutKey::F10: return "F10";
    case ShortcutKey::F11: return "F11"; case ShortcutKey::F12: return "F12";
    case ShortcutKey::Escape: return "Esc";
    case ShortcutKey::Enter: return "Enter";
    case ShortcutKey::Tab: return "Tab";
    case ShortcutKey::Space: return "Space";
    case ShortcutKey::Backspace: return "Backspace";
    case ShortcutKey::Delete: return "Delete";
    case ShortcutKey::Left: return "Left";
    case ShortcutKey::Right: return "Right";
    case ShortcutKey::Up: return "Up";
    case ShortcutKey::Down: return "Down";
    case ShortcutKey::Home: return "Home";
    case ShortcutKey::End: return "End";
    case ShortcutKey::Grave: return "`";
    default: return "?";
    }
}

} // namespace

std::string formatShortcutGesture(const ShortcutGesture& gesture) {
    if (gesture.key == ShortcutKey::None) return {};
    std::string out;
    const auto m = gesture.modifiers;
    if (any(m & ShortcutModifier::Primary)) out += "Ctrl+";
    if (any(m & ShortcutModifier::Shift)) out += "Shift+";
    if (any(m & ShortcutModifier::Alt)) out += "Alt+";
    out += keyLabel(gesture.key);
    return out;
}

std::string formatPrimaryShortcut(EditorActionId action) {
    const auto* bindings = shortcutBindings();
    const std::size_t count = shortcutBindingCount();
    for (std::size_t i = 0; i < count; ++i) {
        if (bindings[i].action == action)
            return formatShortcutGesture(bindings[i].gesture);
    }
    return {};
}

std::vector<std::string> formatAllShortcuts(EditorActionId action) {
    std::vector<std::string> out;
    const auto* bindings = shortcutBindings();
    const std::size_t count = shortcutBindingCount();
    for (std::size_t i = 0; i < count; ++i) {
        if (bindings[i].action != action) continue;
        std::string formatted = formatShortcutGesture(bindings[i].gesture);
        if (!formatted.empty()) out.push_back(std::move(formatted));
    }
    return out;
}

} // namespace ArtCade::EditorNative
