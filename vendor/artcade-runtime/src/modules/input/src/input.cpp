#include "../include/input.h"
#include "../include/pointer-coords.h"
#include <raylib.h>
#include <cstring>

// Forward declaration of helper defined in keymap.cpp
int keyCodeFromString(const std::string& code);

// All known Raylib key codes we poll every frame (avoids iterating 512 slots blindly)
static const int ALL_KEYS[] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_ZERO, KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR,
    KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE,
    KEY_SPACE, KEY_ENTER, KEY_ESCAPE, KEY_TAB, KEY_BACKSPACE,
    KEY_DELETE, KEY_INSERT, KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT,
    KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL,
    KEY_LEFT_ALT, KEY_RIGHT_ALT,
    KEY_LEFT_SUPER, KEY_RIGHT_SUPER,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
    KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
    KEY_KP_ADD, KEY_KP_SUBTRACT, KEY_KP_MULTIPLY, KEY_KP_DIVIDE,
    KEY_KP_DECIMAL, KEY_KP_ENTER,
    KEY_MINUS, KEY_EQUAL, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET,
    KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
    KEY_COMMA, KEY_PERIOD, KEY_SLASH,
};

namespace ArtCade::Modules {

bool Input::init()     { return true; }
void Input::shutdown() {}

void Input::poll() {
    // Clear frame-edge flags
    std::memset(state_.keysPressedThisFrame,  0, sizeof(state_.keysPressedThisFrame));
    std::memset(state_.keysReleasedThisFrame, 0, sizeof(state_.keysReleasedThisFrame));

    for (int key : ALL_KEYS) {
        if (key <= 0 || key >= 512) continue;
        state_.keysDown[key]             = IsKeyDown(key);
        state_.keysPressedThisFrame[key] = IsKeyPressed(key);
        state_.keysReleasedThisFrame[key]= IsKeyReleased(key);
    }

    // Mouse — framebuffer pixels (CSS→buffer scale on Emscripten).
    const Vector2 mp = GetMousePosition();
    state_.mousePosition =
        pointerCoordsNormalizeToFramebuffer(mp.x, mp.y);

    state_.mouseButtonDown[0] = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    state_.mouseButtonDown[1] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    state_.mouseButtonDown[2] = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
}

void Input::resetFrameState() {
    std::memset(state_.keysPressedThisFrame,  0, sizeof(state_.keysPressedThisFrame));
    std::memset(state_.keysReleasedThisFrame, 0, sizeof(state_.keysReleasedThisFrame));
}

bool Input::isKeyDown(const std::string& code) const {
    int k = stringToRaylibKey(code);
    return (k > 0 && k < 512) ? state_.keysDown[k] : false;
}

bool Input::wasKeyPressed(const std::string& code) const {
    int k = stringToRaylibKey(code);
    return (k > 0 && k < 512) ? state_.keysPressedThisFrame[k] : false;
}

bool Input::wasKeyReleased(const std::string& code) const {
    int k = stringToRaylibKey(code);
    return (k > 0 && k < 512) ? state_.keysReleasedThisFrame[k] : false;
}

Vec2 Input::mousePosition() const {
    return state_.mousePosition;
}

bool Input::isMouseButtonDown(int btn) const {
    return (btn >= 0 && btn < 3) ? state_.mouseButtonDown[btn] : false;
}

bool Input::wasMouseButtonPressed(int btn) const {
    if (btn < 0 || btn >= 3) return false;
    static const int RL_BTN[] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
    return IsMouseButtonPressed(RL_BTN[btn]);
}

int Input::stringToRaylibKey(const std::string& code) const {
    return keyCodeFromString(code);
}

} // namespace ArtCade::Modules
