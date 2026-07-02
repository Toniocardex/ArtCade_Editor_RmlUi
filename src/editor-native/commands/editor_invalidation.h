#pragma once

#include <cstdint>
#include <type_traits>

namespace ArtCade::EditorNative {

// =============================================================================
// EditorInvalidation — explicit, small, typed set of "what to refresh".
//
// A command or intent returns the panels its change touched; the coordinator
// OR-accumulates them and the UI refreshes only those next frame. There is no
// string event ("scene.changed"), no dynamic hierarchy, no per-frame full
// rebuild. The flags stay few and comprehensible on purpose (prompt §6).
// =============================================================================
enum class EditorInvalidation : uint32_t {
    None      = 0,
    Hierarchy = 1u << 0,
    Inspector = 1u << 1,
    Viewport  = 1u << 2,
    Assets    = 1u << 3,
    Console   = 1u << 4,
    Toolbar   = 1u << 5,
    Project   = 1u << 6,
    Layout    = 1u << 7,
};

constexpr EditorInvalidation operator|(EditorInvalidation a, EditorInvalidation b) {
    using U = std::underlying_type_t<EditorInvalidation>;
    return static_cast<EditorInvalidation>(static_cast<U>(a) | static_cast<U>(b));
}

constexpr EditorInvalidation operator&(EditorInvalidation a, EditorInvalidation b) {
    using U = std::underlying_type_t<EditorInvalidation>;
    return static_cast<EditorInvalidation>(static_cast<U>(a) & static_cast<U>(b));
}

constexpr EditorInvalidation& operator|=(EditorInvalidation& a, EditorInvalidation b) {
    a = a | b;
    return a;
}

/** True when @p flags contains @p which. */
constexpr bool has(EditorInvalidation flags, EditorInvalidation which) {
    return (flags & which) == which;
}

} // namespace ArtCade::EditorNative
