#include "editor-native/app/native_keyboard_adapter.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

namespace {

struct KeyMap {
    ShortcutKey logical;
    int raylib;
};

constexpr KeyMap kKeys[] = {
    {ShortcutKey::A, KEY_A}, {ShortcutKey::B, KEY_B}, {ShortcutKey::C, KEY_C},
    {ShortcutKey::D, KEY_D}, {ShortcutKey::E, KEY_E}, {ShortcutKey::F, KEY_F},
    {ShortcutKey::G, KEY_G}, {ShortcutKey::H, KEY_H}, {ShortcutKey::I, KEY_I},
    {ShortcutKey::J, KEY_J}, {ShortcutKey::K, KEY_K}, {ShortcutKey::L, KEY_L},
    {ShortcutKey::M, KEY_M}, {ShortcutKey::N, KEY_N}, {ShortcutKey::O, KEY_O},
    {ShortcutKey::P, KEY_P}, {ShortcutKey::Q, KEY_Q}, {ShortcutKey::R, KEY_R},
    {ShortcutKey::S, KEY_S}, {ShortcutKey::T, KEY_T}, {ShortcutKey::U, KEY_U},
    {ShortcutKey::V, KEY_V}, {ShortcutKey::W, KEY_W}, {ShortcutKey::X, KEY_X},
    {ShortcutKey::Y, KEY_Y}, {ShortcutKey::Z, KEY_Z},
    {ShortcutKey::Digit0, KEY_ZERO}, {ShortcutKey::Digit1, KEY_ONE},
    {ShortcutKey::Digit2, KEY_TWO}, {ShortcutKey::Digit3, KEY_THREE},
    {ShortcutKey::Digit4, KEY_FOUR}, {ShortcutKey::Digit5, KEY_FIVE},
    {ShortcutKey::Digit6, KEY_SIX}, {ShortcutKey::Digit7, KEY_SEVEN},
    {ShortcutKey::Digit8, KEY_EIGHT}, {ShortcutKey::Digit9, KEY_NINE},
    {ShortcutKey::F1, KEY_F1}, {ShortcutKey::F2, KEY_F2}, {ShortcutKey::F3, KEY_F3},
    {ShortcutKey::F4, KEY_F4}, {ShortcutKey::F5, KEY_F5}, {ShortcutKey::F6, KEY_F6},
    {ShortcutKey::F7, KEY_F7}, {ShortcutKey::F8, KEY_F8}, {ShortcutKey::F9, KEY_F9},
    {ShortcutKey::F10, KEY_F10}, {ShortcutKey::F11, KEY_F11}, {ShortcutKey::F12, KEY_F12},
    {ShortcutKey::Escape, KEY_ESCAPE}, {ShortcutKey::Enter, KEY_ENTER},
    {ShortcutKey::Tab, KEY_TAB}, {ShortcutKey::Space, KEY_SPACE},
    {ShortcutKey::Backspace, KEY_BACKSPACE}, {ShortcutKey::Delete, KEY_DELETE},
    {ShortcutKey::Left, KEY_LEFT}, {ShortcutKey::Right, KEY_RIGHT},
    {ShortcutKey::Up, KEY_UP}, {ShortcutKey::Down, KEY_DOWN},
    {ShortcutKey::Home, KEY_HOME}, {ShortcutKey::End, KEY_END},
    {ShortcutKey::Grave, KEY_GRAVE},
};

ShortcutModifier readModifiers() {
    ShortcutModifier m = ShortcutModifier::None;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        m = m | ShortcutModifier::Primary;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
        m = m | ShortcutModifier::Shift;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))
        m = m | ShortcutModifier::Alt;
    return m;
}

} // namespace

KeyboardFrameSnapshot captureKeyboardFrame() {
    KeyboardFrameSnapshot snap;
    snap.windowFocused = IsWindowFocused();
    snap.modifiers = readModifiers();

    for (const KeyMap& k : kKeys) {
        if (IsKeyPressed(k.raylib)) snap.pressed.set(k.logical);
        if (IsKeyPressedRepeat(k.raylib) && !IsKeyPressed(k.raylib))
            snap.repeated.set(k.logical);
        if (IsKeyReleased(k.raylib)) snap.released.set(k.logical);
        if (IsKeyDown(k.raylib)) snap.held.set(k.logical);
    }

    // Enter also from keypad.
    if (IsKeyPressed(KEY_KP_ENTER)) snap.pressed.set(ShortcutKey::Enter);
    if (IsKeyPressedRepeat(KEY_KP_ENTER) && !IsKeyPressed(KEY_KP_ENTER))
        snap.repeated.set(ShortcutKey::Enter);
    if (IsKeyReleased(KEY_KP_ENTER)) snap.released.set(ShortcutKey::Enter);
    if (IsKeyDown(KEY_KP_ENTER)) snap.held.set(ShortcutKey::Enter);

    for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        snap.textInput.push_back(static_cast<char32_t>(c));

    return snap;
}

} // namespace ArtCade::EditorNative
