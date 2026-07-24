#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include <string>

namespace ArtCade::Modules {

/**
 * Input — keyboard, mouse, (future) gamepad.
 *
 * Key codes are passed as strings matching KeyboardEvent.code
 * (e.g. "KeyW", "Space", "ArrowLeft") for Lua-friendliness.
 * Internal keymap translation lives in src/keymap.h (private).
 */
class Input final : public IModule {
public:
    Input() = default;

    bool init() override;
    void shutdown() override;

    // Called once per frame before game logic
    void poll();

    // Called at end of frame — clears pressed/released edge flags
    void resetFrameState();

    // Keyboard
    bool isKeyDown(const std::string& code)      const;
    bool wasKeyPressed(const std::string& code)  const;
    bool wasKeyReleased(const std::string& code) const;

    // Mouse
    Vec2 mousePosition()              const;
    bool isMouseButtonDown(int btn)   const;   // 0=LMB 1=RMB 2=MMB
    bool wasMouseButtonPressed(int btn) const;

    const InputState& state() const { return state_; }

private:
    InputState state_;

    int stringToRaylibKey(const std::string& code) const;
};

} // namespace ArtCade::Modules
