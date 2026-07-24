#include "artcade/sfx/vorbis_encoder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

namespace artcade::sfx {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return extension;
}

bool writePage(std::ofstream& stream, const ogg_page& page) {
    stream.write(
        reinterpret_cast<const char*>(page.header),
        static_cast<std::streamsize>(page.header_len)
    );
    stream.write(
        reinterpret_cast<const char*>(page.body),
        static_cast<std::streamsize>(page.body_len)
    );
    return stream.good();
}

std::uint32_t serialFor(const FloatAudioBuffer& audio) {
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    const std::uint64_t sampleCount = static_cast<std::uint64_t>(audio.samples.size());
    mix(audio.sampleRate);
    mix(static_cast<std::uint32_t>(sampleCount & 0xFFFFFFFFu));
    mix(static_cast<std::uint32_t>((sampleCount >> 32u) & 0xFFFFFFFFu));
    return hash == 0u ? 1u : hash;
}

} // namespace

Result<bool> VorbisEncoder::encode(
    const FloatAudioBuffer& audio,
    const std::filesystem::path& destination,
    const VorbisEncodeSettings& settings
) const {
    if (audio.samples.empty() || audio.sampleRate < 8000u || audio.sampleRate > 192000u) {
        return Result<bool>::failure(
            ErrorCode::EmptyAudio,
            "Il buffer audio è vuoto o ha un sample rate non valido."
        );
    }
    for (float sample : audio.samples) {
        if (!std::isfinite(sample)) {
            return Result<bool>::failure(
                ErrorCode::InvalidArgument,
                "Il buffer contiene campioni non finiti."
            );
        }
    }

    if (lowerExtension(destination) != ".ogg") {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "La destinazione Vorbis deve avere estensione .ogg."
        );
    }

    if (!std::isfinite(settings.quality) || settings.quality < -0.1f || settings.quality > 1.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "La qualità Vorbis deve essere compresa approssimativamente tra -0.1 e 1.0."
        );
    }

    std::error_code filesystemError;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), filesystemError);
        if (filesystemError) {
            return Result<bool>::failure(
                ErrorCode::FileOpenFailed,
                "Impossibile creare la cartella di destinazione: " + filesystemError.message()
            );
        }
    }

    const std::filesystem::path temporary = destination.string() + ".artcade_tmp";
    std::filesystem::remove(temporary, filesystemError);
    filesystemError.clear();

    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Result<bool>::failure(
            ErrorCode::FileOpenFailed,
            "Impossibile aprire il file OGG temporaneo in scrittura."
        );
    }

    vorbis_info info;
    vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(
            &info,
            1,
            static_cast<long>(audio.sampleRate),
            settings.quality
        ) != 0) {
        vorbis_info_clear(&info);
        stream.close();
        std::filesystem::remove(temporary, filesystemError);
        return Result<bool>::failure(
            ErrorCode::EncoderFailure,
            "libvorbis non ha accettato i parametri di codifica."
        );
    }

    vorbis_comment comment;
    vorbis_comment_init(&comment);
    vorbis_comment_add_tag(
        &comment,
        const_cast<char*>("ENCODER"),
        const_cast<char*>(settings.vendorTag.c_str())
    );

    vorbis_dsp_state dsp;
    vorbis_block block;
    ogg_stream_state oggStream;

    const bool dspReady = vorbis_analysis_init(&dsp, &info) == 0;
    const bool blockReady = dspReady && vorbis_block_init(&dsp, &block) == 0;
    const bool streamReady = blockReady &&
        ogg_stream_init(&oggStream, static_cast<int>(serialFor(audio))) == 0;

    if (!streamReady) {
        if (blockReady) {
            vorbis_block_clear(&block);
        }
        if (dspReady) {
            vorbis_dsp_clear(&dsp);
        }
        vorbis_comment_clear(&comment);
        vorbis_info_clear(&info);
        stream.close();
        std::filesystem::remove(temporary, filesystemError);
        return Result<bool>::failure(
            ErrorCode::EncoderFailure,
            "Inizializzazione del codificatore Ogg Vorbis fallita."
        );
    }

    bool successful = true;

    ogg_packet identificationHeader;
    ogg_packet commentHeader;
    ogg_packet setupHeader;
    if (vorbis_analysis_headerout(
            &dsp,
            &comment,
            &identificationHeader,
            &commentHeader,
            &setupHeader
        ) != 0) {
        successful = false;
    }

    if (successful) {
        ogg_stream_packetin(&oggStream, &identificationHeader);
        ogg_stream_packetin(&oggStream, &commentHeader);
        ogg_stream_packetin(&oggStream, &setupHeader);

        ogg_page page;
        while (ogg_stream_flush(&oggStream, &page) != 0) {
            if (!writePage(stream, page)) {
                successful = false;
                break;
            }
        }
    }

    const auto drainEncoder = [&]() -> bool {
        ogg_packet packet;
        ogg_page page;

        while (vorbis_analysis_blockout(&dsp, &block) == 1) {
            vorbis_analysis(&block, nullptr);
            vorbis_bitrate_addblock(&block);

            while (vorbis_bitrate_flushpacket(&dsp, &packet) != 0) {
                ogg_stream_packetin(&oggStream, &packet);

                while (ogg_stream_pageout(&oggStream, &page) != 0) {
                    if (!writePage(stream, page)) {
                        return false;
                    }
                }
            }
        }
        return true;
    };

    constexpr std::size_t chunkFrames = 1024u;
    std::size_t cursor = 0u;
    while (successful && cursor < audio.samples.size()) {
        const std::size_t remaining = audio.samples.size() - cursor;
        const std::size_t count = std::min(chunkFrames, remaining);

        float** analysisBuffer = vorbis_analysis_buffer(
            &dsp,
            static_cast<int>(count)
        );
        if (analysisBuffer == nullptr || analysisBuffer[0] == nullptr) {
            successful = false;
            break;
        }

        for (std::size_t index = 0; index < count; ++index) {
            analysisBuffer[0][index] = std::max(
                -1.0f,
                std::min(audio.samples[cursor + index], 1.0f)
            );
        }

        vorbis_analysis_wrote(&dsp, static_cast<int>(count));
        cursor += count;
        successful = drainEncoder();
    }

    if (successful) {
        vorbis_analysis_wrote(&dsp, 0);
        successful = drainEncoder();
    }

    stream.flush();
    successful = successful && stream.good();

    ogg_stream_clear(&oggStream);
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);
    stream.close();

    if (!successful) {
        std::filesystem::remove(temporary, filesystemError);
        return Result<bool>::failure(
            ErrorCode::EncoderFailure,
            "Errore durante la codifica del file Ogg Vorbis."
        );
    }

    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError && std::filesystem::exists(destination)) {
        filesystemError.clear();
        std::filesystem::remove(destination, filesystemError);
        if (!filesystemError) {
            std::filesystem::rename(temporary, destination, filesystemError);
        }
    }

    if (filesystemError) {
        const std::string finalizeError = filesystemError.message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return Result<bool>::failure(
            ErrorCode::FileWriteFailed,
            "Impossibile finalizzare il file OGG: " + finalizeError
        );
    }

    return Result<bool>::success(true);
}

} // namespace artcade::sfx
