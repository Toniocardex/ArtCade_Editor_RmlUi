#pragma once

#include "editor-native/app/shortcuts/editor_action.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ArtCade::EditorNative {

// Logical keys independent of Raylib. Mapped by the native adapter.
enum class ShortcutKey : std::uint16_t {
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Escape, Enter, Tab, Space, Backspace, Delete,
    Left, Right, Up, Down, Home, End,
    Grave, // `
    Count
};

enum class ShortcutModifier : std::uint8_t {
    None    = 0,
    Primary = 1 << 0, // Ctrl on Windows
    Shift   = 1 << 1,
    Alt     = 1 << 2,
};

inline constexpr ShortcutModifier operator|(ShortcutModifier a, ShortcutModifier b) {
    return static_cast<ShortcutModifier>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline constexpr ShortcutModifier operator&(ShortcutModifier a, ShortcutModifier b) {
    return static_cast<ShortcutModifier>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
inline constexpr bool any(ShortcutModifier m) {
    return static_cast<std::uint8_t>(m) != 0;
}

enum class ShortcutTrigger : std::uint8_t {
    PressOnce,
    Repeat,
};

enum class WorkspaceMask : std::uint8_t {
    None        = 0,
    Scene       = 1 << 0,
    LogicBoard  = 1 << 1,
    ScriptEditor = 1 << 2,
    Any = Scene | LogicBoard | ScriptEditor,
};

enum class FocusDomainMask : std::uint16_t {
    None            = 0,
    SceneViewport   = 1 << 0,
    Hierarchy       = 1 << 1,
    Assets          = 1 << 2,
    Inspector       = 1 << 3,
    TilePalette     = 1 << 4,
    LogicBoard      = 1 << 5,
    ScriptTextEditor = 1 << 6,
    Console         = 1 << 7,
    ContextMenu     = 1 << 8,
    Modal           = 1 << 9,
    TextControl     = 1 << 10,
    AnySceneAuthoring = SceneViewport | Hierarchy | Inspector,
    Any = 0xFFFF,
};

enum class OverlayMask : std::uint8_t {
    None                 = 0,
    NoneOnly             = 1 << 0, // binding requires no overlay
    SpriteAnimationEditor = 1 << 1,
    TilesetEditor        = 1 << 2,
    GeneratedSfxEditor   = 1 << 3,
    Any = 0xFF,
};

enum class EditorModeMask : std::uint8_t {
    None = 0,
    Edit = 1 << 0,
    Play = 1 << 1,
    Any  = Edit | Play,
};

inline constexpr WorkspaceMask operator|(WorkspaceMask a, WorkspaceMask b) {
    return static_cast<WorkspaceMask>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline constexpr WorkspaceMask operator&(WorkspaceMask a, WorkspaceMask b) {
    return static_cast<WorkspaceMask>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
inline constexpr FocusDomainMask operator|(FocusDomainMask a, FocusDomainMask b) {
    return static_cast<FocusDomainMask>(
        static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}
inline constexpr FocusDomainMask operator&(FocusDomainMask a, FocusDomainMask b) {
    return static_cast<FocusDomainMask>(
        static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}
inline constexpr OverlayMask operator|(OverlayMask a, OverlayMask b) {
    return static_cast<OverlayMask>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline constexpr OverlayMask operator&(OverlayMask a, OverlayMask b) {
    return static_cast<OverlayMask>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
inline constexpr EditorModeMask operator|(EditorModeMask a, EditorModeMask b) {
    return static_cast<EditorModeMask>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline constexpr EditorModeMask operator&(EditorModeMask a, EditorModeMask b) {
    return static_cast<EditorModeMask>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

struct ShortcutGesture {
    ShortcutKey key = ShortcutKey::None;
    ShortcutModifier modifiers = ShortcutModifier::None;

    bool operator==(const ShortcutGesture& o) const {
        return key == o.key && modifiers == o.modifiers;
    }
};

struct ShortcutScope {
    WorkspaceMask workspaces = WorkspaceMask::Any;
    FocusDomainMask focusDomains = FocusDomainMask::Any;
    OverlayMask overlays = OverlayMask::NoneOnly;
    EditorModeMask modes = EditorModeMask::Edit;
    bool allowTextEditing = false;
    bool requireTilemapContext = false;
};

using ShortcutBindingId = std::uint16_t;

struct ShortcutBinding {
    ShortcutBindingId id = 0;
    EditorActionId action = EditorActionId::None;
    ShortcutGesture gesture{};
    ShortcutScope scope{};
    ShortcutTrigger trigger = ShortcutTrigger::PressOnce;
};

// Dense bitset indexed by ShortcutKey.
struct ShortcutKeySet {
    static constexpr std::size_t kBits =
        static_cast<std::size_t>(ShortcutKey::Count);
    std::uint64_t words[(kBits + 63) / 64] = {};

    void set(ShortcutKey key) {
        const auto i = static_cast<std::size_t>(key);
        if (i == 0 || i >= kBits) return;
        words[i / 64] |= (std::uint64_t{1} << (i % 64));
    }
    bool test(ShortcutKey key) const {
        const auto i = static_cast<std::size_t>(key);
        if (i == 0 || i >= kBits) return false;
        return (words[i / 64] & (std::uint64_t{1} << (i % 64))) != 0;
    }
    void clear() {
        for (auto& w : words) w = 0;
    }
    bool empty() const {
        for (auto w : words) if (w) return false;
        return true;
    }
};

struct KeyboardFrameSnapshot {
    ShortcutKeySet pressed;
    ShortcutKeySet repeated;
    ShortcutKeySet released;
    ShortcutKeySet held;
    ShortcutModifier modifiers = ShortcutModifier::None;
    std::vector<char32_t> textInput;
    bool windowFocused = true;
};

} // namespace ArtCade::EditorNative
