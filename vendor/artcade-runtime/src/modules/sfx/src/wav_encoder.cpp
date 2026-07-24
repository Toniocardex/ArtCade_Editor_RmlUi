#include "artcade/sfx/wav_encoder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace artcade::sfx {
namespace {

void writeU16Le(std::ofstream& stream, std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32Le(std::ofstream& stream, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::uint32_t xorshift32(std::uint32_t& state) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float randomUnit(std::uint32_t& state) {
    return static_cast<float>(xorshift32(state)) /
        static_cast<float>(std::numeric_limits<std::uint32_t>::max());
}

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

Result<bool> validateAudio(const FloatAudioBuffer& audio) {
    if (audio.sampleRate < 8000u || audio.sampleRate > 192000u || audio.samples.empty()) {
        return Result<bool>::failure(
            ErrorCode::EmptyAudio,
            "Il buffer audio è vuoto o ha un sample rate non valido."
        );
    }

    if (audio.samples.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 2u)) {
        return Result<bool>::failure(
            ErrorCode::TooManyFrames,
            "Il buffer è troppo grande per un file WAV PCM16 RIFF standard."
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

    return Result<bool>::success(true);
}

} // namespace

Result<bool> WavEncoder::encode(
    const FloatAudioBuffer& audio,
    const std::filesystem::path& destination,
    const WavEncodeSettings& settings
) const {
    if (auto validation = validateAudio(audio); !validation) {
        return validation;
    }

    if (lowerExtension(destination) != ".wav") {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "La destinazione WAV deve avere estensione .wav."
        );
    }

    if (!std::isfinite(settings.normalizePeak) ||
        settings.normalizePeak <= 0.0f || settings.normalizePeak > 1.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "normalizePeak deve essere compreso tra 0 e 1."
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
            "Impossibile aprire il file WAV temporaneo in scrittura."
        );
    }

    float conversionGain = 1.0f;
    if (settings.normalize) {
        float peak = 0.0f;
        for (float sample : audio.samples) {
            peak = std::max(peak, std::fabs(sample));
        }
        if (peak > 0.0f) {
            conversionGain = settings.normalizePeak / peak;
        }
    }

    constexpr std::uint16_t channels = 1u;
    constexpr std::uint16_t bitsPerSample = 16u;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(
        audio.samples.size() * sizeof(std::int16_t)
    );
    const std::uint32_t byteRate =
        audio.sampleRate * channels * bitsPerSample / 8u;
    const std::uint16_t blockAlign = channels * bitsPerSample / 8u;

    stream.write("RIFF", 4);
    writeU32Le(stream, 36u + dataSize);
    stream.write("WAVE", 4);

    stream.write("fmt ", 4);
    writeU32Le(stream, 16u);
    writeU16Le(stream, 1u); // PCM integer
    writeU16Le(stream, channels);
    writeU32Le(stream, audio.sampleRate);
    writeU32Le(stream, byteRate);
    writeU16Le(stream, blockAlign);
    writeU16Le(stream, bitsPerSample);

    stream.write("data", 4);
    writeU32Le(stream, dataSize);

    std::uint32_t ditherState = settings.ditherSeed != 0u
        ? settings.ditherSeed
        : 1u;

    for (float input : audio.samples) {
        float sample = input * conversionGain;
        if (settings.applyTpdfDither) {
            const float tpdf = randomUnit(ditherState) - randomUnit(ditherState);
            sample += tpdf / 32768.0f;
        }

        sample = std::max(-1.0f, std::min(sample, 1.0f));
        const auto pcm = static_cast<std::int16_t>(std::lround(
            sample * static_cast<float>(std::numeric_limits<std::int16_t>::max())
        ));
        writeU16Le(stream, static_cast<std::uint16_t>(pcm));
    }

    stream.flush();
    if (!stream.good()) {
        stream.close();
        std::filesystem::remove(temporary, filesystemError);
        return Result<bool>::failure(
            ErrorCode::FileWriteFailed,
            "Errore durante la scrittura del file WAV."
        );
    }
    stream.close();

    // Same-directory atomic replacement. Never expose a partially written WAV.
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        filesystemError = std::error_code(
            static_cast<int>(GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(temporary, destination, filesystemError);
#endif

    if (filesystemError) {
        const std::string finalizeError = filesystemError.message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return Result<bool>::failure(
            ErrorCode::FileWriteFailed,
            "Impossibile finalizzare il file WAV: " + finalizeError
        );
    }

    return Result<bool>::success(true);
}

} // namespace artcade::sfx
