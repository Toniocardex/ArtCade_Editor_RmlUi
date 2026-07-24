#pragma once

#include "artcade/sfx/result.hpp"
#include "artcade/sfx/types.hpp"

#include <filesystem>

namespace artcade::sfx {

struct WavEncodeSettings {
    bool normalize = false;
    float normalizePeak = 0.98f;
    bool applyTpdfDither = true;
    std::uint32_t ditherSeed = 0x51F15EEDu;
};

class WavEncoder final {
public:
    [[nodiscard]] Result<bool> encode(
        const FloatAudioBuffer& audio,
        const std::filesystem::path& destination,
        const WavEncodeSettings& settings = {}
    ) const;
};

} // namespace artcade::sfx
