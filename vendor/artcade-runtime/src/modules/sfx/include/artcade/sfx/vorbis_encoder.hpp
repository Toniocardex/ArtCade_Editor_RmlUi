#pragma once

#include "artcade/sfx/result.hpp"
#include "artcade/sfx/types.hpp"

#include <filesystem>
#include <string>

namespace artcade::sfx {

struct VorbisEncodeSettings {
    float quality = 0.55f; // libvorbis VBR quality range: approximately -0.1 .. 1.0
    std::string vendorTag = "ArtCade SFX Generator";
};

class VorbisEncoder final {
public:
    [[nodiscard]] Result<bool> encode(
        const FloatAudioBuffer& audio,
        const std::filesystem::path& destination,
        const VorbisEncodeSettings& settings = {}
    ) const;
};

} // namespace artcade::sfx
