#include "color-hex.h"

#include <cstdio>

namespace ArtCade {
namespace {

int hexDigitValue(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int channelToByte(float channel) {
    if (!(channel >= 0.f)) return 0;
    if (channel >= 1.f) return 255;
    return static_cast<int>(channel * 255.f + 0.5f);
}

} // namespace

std::optional<Vec4> parseColorHexRgb(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 6) return std::nullopt;

    int r = 0, g = 0, b = 0;
    if (text.size() == 3) {
        const int rd = hexDigitValue(static_cast<unsigned char>(text[0]));
        const int gd = hexDigitValue(static_cast<unsigned char>(text[1]));
        const int bd = hexDigitValue(static_cast<unsigned char>(text[2]));
        if (rd < 0 || gd < 0 || bd < 0) return std::nullopt;
        r = (rd << 4) | rd;
        g = (gd << 4) | gd;
        b = (bd << 4) | bd;
    } else {
        const auto readByte = [&](std::size_t i) -> std::optional<int> {
            const int hi = hexDigitValue(static_cast<unsigned char>(text[i]));
            const int lo = hexDigitValue(static_cast<unsigned char>(text[i + 1]));
            if (hi < 0 || lo < 0) return std::nullopt;
            return (hi << 4) | lo;
        };
        const auto rb = readByte(0);
        const auto gb = readByte(2);
        const auto bb = readByte(4);
        if (!rb || !gb || !bb) return std::nullopt;
        r = *rb;
        g = *gb;
        b = *bb;
    }
    return Vec4{
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        1.f,
    };
}

std::string formatColorHexRgb(Vec4 color) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  channelToByte(color.r), channelToByte(color.g), channelToByte(color.b));
    return buf;
}

} // namespace ArtCade
