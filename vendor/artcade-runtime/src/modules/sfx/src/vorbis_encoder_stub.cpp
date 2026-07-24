#include "artcade/sfx/vorbis_encoder.hpp"

namespace artcade::sfx {

Result<bool> VorbisEncoder::encode(
    const FloatAudioBuffer&,
    const std::filesystem::path&,
    const VorbisEncodeSettings&
) const {
    return Result<bool>::failure(
        ErrorCode::EncoderUnavailable,
        "Supporto Ogg Vorbis non compilato. Abilita ARTCADE_SFX_ENABLE_VORBIS."
    );
}

} // namespace artcade::sfx
