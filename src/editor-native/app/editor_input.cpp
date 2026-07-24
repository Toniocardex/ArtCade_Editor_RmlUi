#include "editor-native/app/editor_input.h"

#include "editor-native/app/shortcuts/shortcut_types.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>

#include <raylib.h>

namespace ArtCade::EditorNative {

namespace {

int modifiersFromSnapshot(const KeyboardFrameSnapshot& keyboard) {
    int mod = 0;
    if (any(keyboard.modifiers & ShortcutModifier::Primary)) mod |= Rml::Input::KM_CTRL;
    if (any(keyboard.modifiers & ShortcutModifier::Shift)) mod |= Rml::Input::KM_SHIFT;
    if (any(keyboard.modifiers & ShortcutModifier::Alt)) mod |= Rml::Input::KM_ALT;
    return mod;
}

struct KeyPair { ShortcutKey logical; Rml::Input::KeyIdentifier rml; };
constexpr KeyPair kKeys[] = {
    {ShortcutKey::Backspace, Rml::Input::KI_BACK},
    {ShortcutKey::Delete, Rml::Input::KI_DELETE},
    {ShortcutKey::Enter, Rml::Input::KI_RETURN},
    {ShortcutKey::Tab, Rml::Input::KI_TAB},
    {ShortcutKey::Escape, Rml::Input::KI_ESCAPE},
    {ShortcutKey::Left, Rml::Input::KI_LEFT},
    {ShortcutKey::Right, Rml::Input::KI_RIGHT},
    {ShortcutKey::Up, Rml::Input::KI_UP},
    {ShortcutKey::Down, Rml::Input::KI_DOWN},
    {ShortcutKey::Home, Rml::Input::KI_HOME},
    {ShortcutKey::End, Rml::Input::KI_END},
};

bool focusIsTextField(Rml::Context* context) {
    Rml::Element* focus = context->GetFocusElement();
    if (!focus) return false;
    const Rml::String tag = focus->GetTagName();
    return tag == "input" || tag == "textarea";
}

} // namespace

RmlInputResult pumpRmlInput(Rml::Context* context,
                            const KeyboardFrameSnapshot& keyboard,
                            const RmlInputSuppression& suppression) {
    RmlInputResult result;
    if (!context) return result;

    const int mod = modifiersFromSnapshot(keyboard);

    // RmlUi's context is sized in physical framebuffer pixels, but raylib reports
    // the mouse in logical pixels (it applies SetMouseScale under HIGHDPI). Scale
    // the cursor into physical space so hover/click/drag land on the right element
    // on scaled displays. The factor is 1.0 at 100% scaling.
    const int   sw = GetScreenWidth();
    const int   sh = GetScreenHeight();
    const float sx = sw > 0 ? static_cast<float>(GetRenderWidth())  / static_cast<float>(sw) : 1.f;
    const float sy = sh > 0 ? static_cast<float>(GetRenderHeight()) / static_cast<float>(sh) : 1.f;
    const bool propagated = context->ProcessMouseMove(
        static_cast<int>(GetMouseX() * sx), static_cast<int>(GetMouseY() * sy), mod);
    result.mouseOverUi = !propagated;

    const int buttons[] = {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE};
    for (int i = 0; i < 3; ++i) {
        if (IsMouseButtonPressed(buttons[i]))  context->ProcessMouseButtonDown(i, mod);
        if (IsMouseButtonReleased(buttons[i])) context->ProcessMouseButtonUp(i, mod);
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && !suppression.mouseWheel) context->ProcessMouseWheel(-wheel, mod);

    for (const KeyPair& key : kKeys) {
        if (keyboard.pressed.test(key.logical) || keyboard.repeated.test(key.logical))
            context->ProcessKeyDown(key.rml, mod);
        if (keyboard.released.test(key.logical))
            context->ProcessKeyUp(key.rml, mod);
    }

    for (char32_t c : keyboard.textInput)
        context->ProcessTextInput(static_cast<Rml::Character>(c));

    result.textFocus = focusIsTextField(context);
    return result;
}

} // namespace ArtCade::EditorNative
