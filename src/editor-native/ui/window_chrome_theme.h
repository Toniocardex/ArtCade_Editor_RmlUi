#pragma once

namespace ArtCade::EditorNative::WindowChromeTheme {

// Canonical RGB mirrors of machine-readable theme.rcss roles. The Windows
// adapter converts these to COLORREF/BGR at the call site.
// ds-role-mirror(surface-chrome)
inline constexpr unsigned int kCaptionBackgroundRgb = 0x18181b;

// ds-role-mirror(text-primary)
inline constexpr unsigned int kCaptionTextRgb = 0xd4d4d8;

constexpr unsigned long toColorRef(unsigned int rgb) noexcept {
    const unsigned long red = (rgb >> 16u) & 0xffu;
    const unsigned long green = (rgb >> 8u) & 0xffu;
    const unsigned long blue = rgb & 0xffu;
    return red | (green << 8u) | (blue << 16u);
}

} // namespace ArtCade::EditorNative::WindowChromeTheme
