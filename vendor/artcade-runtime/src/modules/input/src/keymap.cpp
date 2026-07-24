// keymap.cpp — string (KeyboardEvent.code) → Raylib key constant
// Included by input.cpp via direct .cpp include trick to avoid a header for one function.
// Called only from Input::stringToRaylibKey().

#include <raylib.h>
#include <string>
#include <unordered_map>

static const std::unordered_map<std::string, int>& keyMap() {
    static const std::unordered_map<std::string, int> m = {
        // Letters
        {"KeyA",KEY_A}, {"KeyB",KEY_B}, {"KeyC",KEY_C}, {"KeyD",KEY_D},
        {"KeyE",KEY_E}, {"KeyF",KEY_F}, {"KeyG",KEY_G}, {"KeyH",KEY_H},
        {"KeyI",KEY_I}, {"KeyJ",KEY_J}, {"KeyK",KEY_K}, {"KeyL",KEY_L},
        {"KeyM",KEY_M}, {"KeyN",KEY_N}, {"KeyO",KEY_O}, {"KeyP",KEY_P},
        {"KeyQ",KEY_Q}, {"KeyR",KEY_R}, {"KeyS",KEY_S}, {"KeyT",KEY_T},
        {"KeyU",KEY_U}, {"KeyV",KEY_V}, {"KeyW",KEY_W}, {"KeyX",KEY_X},
        {"KeyY",KEY_Y}, {"KeyZ",KEY_Z},
        // Digits (top row)
        {"Digit0",KEY_ZERO},  {"Digit1",KEY_ONE},   {"Digit2",KEY_TWO},
        {"Digit3",KEY_THREE}, {"Digit4",KEY_FOUR},  {"Digit5",KEY_FIVE},
        {"Digit6",KEY_SIX},   {"Digit7",KEY_SEVEN}, {"Digit8",KEY_EIGHT},
        {"Digit9",KEY_NINE},
        // Common special keys
        {"Space",KEY_SPACE}, {"Enter",KEY_ENTER}, {"Escape",KEY_ESCAPE},
        {"Tab",KEY_TAB},     {"Backspace",KEY_BACKSPACE},
        {"Delete",KEY_DELETE}, {"Insert",KEY_INSERT},
        {"Home",KEY_HOME},   {"End",KEY_END},
        {"PageUp",KEY_PAGE_UP}, {"PageDown",KEY_PAGE_DOWN},
        // Arrow keys
        {"ArrowLeft",KEY_LEFT},  {"ArrowRight",KEY_RIGHT},
        {"ArrowUp",KEY_UP},      {"ArrowDown",KEY_DOWN},
        // Modifiers
        {"ShiftLeft",KEY_LEFT_SHIFT},    {"ShiftRight",KEY_RIGHT_SHIFT},
        {"ControlLeft",KEY_LEFT_CONTROL},{"ControlRight",KEY_RIGHT_CONTROL},
        {"AltLeft",KEY_LEFT_ALT},        {"AltRight",KEY_RIGHT_ALT},
        {"MetaLeft",KEY_LEFT_SUPER},     {"MetaRight",KEY_RIGHT_SUPER},
        // Function keys
        {"F1",KEY_F1},  {"F2",KEY_F2},  {"F3",KEY_F3},  {"F4",KEY_F4},
        {"F5",KEY_F5},  {"F6",KEY_F6},  {"F7",KEY_F7},  {"F8",KEY_F8},
        {"F9",KEY_F9},  {"F10",KEY_F10},{"F11",KEY_F11},{"F12",KEY_F12},
        // Numpad
        {"Numpad0",KEY_KP_0}, {"Numpad1",KEY_KP_1}, {"Numpad2",KEY_KP_2},
        {"Numpad3",KEY_KP_3}, {"Numpad4",KEY_KP_4}, {"Numpad5",KEY_KP_5},
        {"Numpad6",KEY_KP_6}, {"Numpad7",KEY_KP_7}, {"Numpad8",KEY_KP_8},
        {"Numpad9",KEY_KP_9},
        {"NumpadAdd",KEY_KP_ADD}, {"NumpadSubtract",KEY_KP_SUBTRACT},
        {"NumpadMultiply",KEY_KP_MULTIPLY}, {"NumpadDivide",KEY_KP_DIVIDE},
        {"NumpadDecimal",KEY_KP_DECIMAL},   {"NumpadEnter",KEY_KP_ENTER},
        // Symbols (US layout)
        {"Minus",KEY_MINUS},      {"Equal",KEY_EQUAL},
        {"BracketLeft",KEY_LEFT_BRACKET}, {"BracketRight",KEY_RIGHT_BRACKET},
        {"Backslash",KEY_BACKSLASH}, {"Semicolon",KEY_SEMICOLON},
        {"Quote",KEY_APOSTROPHE},   {"Backquote",KEY_GRAVE},
        {"Comma",KEY_COMMA},        {"Period",KEY_PERIOD},
        {"Slash",KEY_SLASH},
    };
    return m;
}

// Called from Input::stringToRaylibKey()
int keyCodeFromString(const std::string& code) {
    auto it = keyMap().find(code);
    return (it != keyMap().end()) ? it->second : 0;
}
