#pragma once

#include "editor-native/app/shortcuts/shortcut_types.h"

namespace ArtCade::EditorNative {

// Consumes a primary shortcut key until its release frame (ADR-0024).
class KeyboardConsumptionTracker {
public:
    void consumeUntilRelease(ShortcutKey key);
    // Call once at the start of each frame with the fresh snapshot.
    void beginFrame(const KeyboardFrameSnapshot& keyboard);

    bool suppressesPress(ShortcutKey key) const;
    bool suppressesHeld(ShortcutKey key) const;
    bool suppressesRelease(ShortcutKey key) const;
    bool suppressesAny(ShortcutKey key) const {
        return suppressesPress(key) || suppressesHeld(key) || suppressesRelease(key);
    }

private:
    ShortcutKey active_ = ShortcutKey::None;
    ShortcutKey releasing_ = ShortcutKey::None;
};

} // namespace ArtCade::EditorNative
