#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace artcade::sfx {

inline constexpr std::uint32_t kRecipeSchemaVersion = 1;
inline constexpr std::uint32_t kGeneratorVersion = 2;

enum class Waveform {
    Square,
    Pulse,
    Triangle,
    Saw
};

enum class OscillatorQuality {
    Raw,
    BandLimited
};

enum class PitchSweepMode {
    LinearHz,
    Exponential
};

struct Envelope {
    float attackSeconds = 0.005f;
    float decaySeconds = 0.050f;
    float sustainLevel = 0.65f;
    float releaseSeconds = 0.080f;
};

struct PitchParams {
    float startHz = 440.0f;
    float endHz = 440.0f;
    float sweepCurve = 1.0f;
    PitchSweepMode sweepMode = PitchSweepMode::Exponential;

    float vibratoDepthSemitones = 0.0f;
    float vibratoRateHz = 0.0f;

    float arpeggioSemitones = 0.0f;
    float arpeggioRateHz = 0.0f;
};

struct VoiceParams {
    bool enabled = true;
    Waveform waveform = Waveform::Square;
    OscillatorQuality quality = OscillatorQuality::BandLimited;
    float gain = 1.0f;
    float detuneSemitones = 0.0f;
    float dutyStart = 0.50f;
    float dutyEnd = 0.50f;
    PitchParams pitch{};
};

struct NoiseLayerParams {
    bool enabled = false;
    float gain = 0.0f;
    PitchParams clock{};
};

struct BitCrusherParams {
    bool enabled = true;
    int quantizationBits = 8;

    // Output refresh rate of the sample-and-hold stage. Unlike an integer
    // frame count, this remains stable when the project sample rate changes.
    // Set to 0 to disable temporal reduction.
    float reductionRateHz = 11025.0f;
};

struct FilterParams {
    float lowPassHz = 0.0f; // 0 disables the low-pass filter.
    bool dcBlockEnabled = true;
    float dcBlockCutoffHz = 18.0f;
};

struct SfxRecipe {
    std::uint32_t schemaVersion = kRecipeSchemaVersion;
    std::uint32_t generatorVersion = kGeneratorVersion;

    float durationSeconds = 0.25f;
    float masterGain = 0.75f;
    Envelope amplitude{};

    VoiceParams primaryVoice{};
    VoiceParams secondaryVoice{false};
    NoiseLayerParams noise{};

    BitCrusherParams bitCrusher{};
    FilterParams filter{};

    std::uint32_t randomSeed = 0x00C0FFEEu;
};

struct RenderSettings {
    std::uint32_t sampleRate = 44100;
    std::uint64_t maximumFrames = 44100u * 60u;

    // A short edge fade is applied in addition to the ADSR envelope. It
    // prevents clicks when zero-length attack/release stages are requested.
    bool applyDeclick = true;
    float declickSeconds = 0.0015f;
};

struct FloatAudioBuffer {
    std::uint32_t sampleRate = 44100;
    std::vector<float> samples; // Mono, normalized nominal range [-1, +1].
};

struct GeneratedSfxDef {
    std::uint32_t schemaVersion = 1;
    std::string id;
    std::string name;
    SfxRecipe recipe{};
    std::string outputAssetId;
    std::string outputPath;
    /** Compact fingerprint of the recipe used to produce the current WAV.
     *  Empty means legacy / never registered under the fingerprint model. */
    std::string generatedRecipeFingerprint;
};

} // namespace artcade::sfx
