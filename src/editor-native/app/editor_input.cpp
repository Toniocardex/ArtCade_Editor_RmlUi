#include "editor-native/app/editor_input.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>

#include <raylib.h>

namespace ArtCade::EditorNative {

namespace {

int currentModifiers() {
    int mod = 0;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mod |= Rml::Input::KM_CTRL;
    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   mod |= Rml::Input::KM_SHIFT;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     mod |= Rml::Input::KM_ALT;
    return mod;
}

// Editing keys that an RML text field needs. Printable characters arrive
// separately via ProcessTextInput (GetCharPressed).
struct KeyPair { int raylib; Rml::Input::KeyIdentifier rml; };
constexpr KeyPair kKeys[] = {
    {KEY_BACKSPACE, Rml::Input::KI_BACK},   {KEY_DELETE, Rml::Input::KI_DELETE},
    {KEY_ENTER,     Rml::Input::KI_RETURN}, {KEY_KP_ENTER, Rml::Input::KI_RETURN},
    {KEY_TAB,       Rml::Input::KI_TAB},    {KEY_ESCAPE,   Rml::Input::KI_ESCAPE},
    {KEY_LEFT,      Rml::Input::KI_LEFT},   {KEY_RIGHT, Rml::Input::KI_RIGHT},
    {KEY_UP,        Rml::Input::KI_UP},     {KEY_DOWN,  Rml::Input::KI_DOWN},
    {KEY_HOME,      Rml::Input::KI_HOME},   {KEY_END,   Rml::Input::KI_END},
};

bool focusIsTextField(Rml::Context* context) {
    Rml::Element* focus = context->GetFocusElement();
    if (!focus) return false;
    const Rml::String tag = focus->GetTagName();
    return tag == "input" || tag == "textarea";
}

} // namespace

RmlInputResult pumpRmlInput(Rml::Context* context,
                            const RmlInputSuppression& suppression) {
    RmlInputResult result;
    if (!context) return result;

    const int mod = currentModifiers();

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
        if (IsKeyPressed(key.raylib) || IsKeyPressedRepeat(key.raylib))
            context->ProcessKeyDown(key.rml, mod);
        if (IsKeyReleased(key.raylib))
            context->ProcessKeyUp(key.rml, mod);
    }

    for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        context->ProcessTextInput(static_cast<Rml::Character>(c));

    result.textFocus = focusIsTextField(context);
    return result;
}

} // namespace ArtCade::EditorNative
