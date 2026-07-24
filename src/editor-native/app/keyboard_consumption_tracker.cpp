#include "editor-native/app/keyboard_consumption_tracker.h"

namespace ArtCade::EditorNative {

void KeyboardConsumptionTracker::consumeUntilRelease(ShortcutKey key) {
    if (key == ShortcutKey::None) return;
    active_ = key;
}

void KeyboardConsumptionTracker::beginFrame(const KeyboardFrameSnapshot& keyboard) {
    releasing_ = ShortcutKey::None;
    if (active_ == ShortcutKey::None) return;

    if (keyboard.released.test(active_)) {
        releasing_ = active_;
        active_ = ShortcutKey::None;
        return;
    }
    if (!keyboard.held.test(active_) && !keyboard.pressed.test(active_)) {
        // Focus loss / missed release — drop silently.
        active_ = ShortcutKey::None;
    }
}

bool KeyboardConsumptionTracker::suppressesPress(ShortcutKey key) const {
    return active_ != ShortcutKey::None && active_ == key;
}

bool KeyboardConsumptionTracker::suppressesHeld(ShortcutKey key) const {
    return active_ != ShortcutKey::None && active_ == key;
}

bool KeyboardConsumptionTracker::suppressesRelease(ShortcutKey key) const {
    return releasing_ != ShortcutKey::None && releasing_ == key;
}

} // namespace ArtCade::EditorNative
