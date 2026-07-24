#include "artcade/sfx/synthesizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace artcade::sfx {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

template <typename T>
T clampValue(T value, T minimum, T maximum) {
    return std::max(minimum, std::min(value, maximum));
}

bool finite(float value) {
    return std::isfinite(value);
}

float semitoneRatio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

float interpolatePitch(const PitchParams& pitch, float normalizedTime, float timeSeconds) {
    const float curveTime = std::pow(clampValue(normalizedTime, 0.0f, 1.0f), pitch.sweepCurve);

    float frequency = 0.0f;
    if (pitch.sweepMode == PitchSweepMode::Exponential) {
        frequency = pitch.startHz * std::pow(pitch.endHz / pitch.startHz, curveTime);
    } else {
        frequency = pitch.startHz + (pitch.endHz - pitch.startHz) * curveTime;
    }

    if (pitch.vibratoDepthSemitones != 0.0f && pitch.vibratoRateHz > 0.0f) {
        const float vibrato = std::sin(kTwoPi * pitch.vibratoRateHz * timeSeconds);
        frequency *= semitoneRatio(pitch.vibratoDepthSemitones * vibrato);
    }

    if (pitch.arpeggioSemitones != 0.0f && pitch.arpeggioRateHz > 0.0f) {
        const auto step = static_cast<std::uint64_t>(
            std::floor(timeSeconds * pitch.arpeggioRateHz)
        );
        if ((step & 1u) != 0u) {
            frequency *= semitoneRatio(pitch.arpeggioSemitones);
        }
    }

    return frequency;
}

float envelopeAt(float timeSeconds, float durationSeconds, const Envelope& envelope) {
    const float attackEnd = envelope.attackSeconds;
    const float decayEnd = attackEnd + envelope.decaySeconds;
    const float releaseStart = durationSeconds - envelope.releaseSeconds;

    if (envelope.attackSeconds > 0.0f && timeSeconds < attackEnd) {
        return clampValue(timeSeconds / envelope.attackSeconds, 0.0f, 1.0f);
    }

    if (envelope.decaySeconds > 0.0f && timeSeconds < decayEnd) {
        const float t = (timeSeconds - attackEnd) / envelope.decaySeconds;
        return 1.0f + (envelope.sustainLevel - 1.0f) * clampValue(t, 0.0f, 1.0f);
    }

    if (envelope.releaseSeconds > 0.0f && timeSeconds >= releaseStart) {
        const float t = (timeSeconds - releaseStart) / envelope.releaseSeconds;
        return envelope.sustainLevel * (1.0f - clampValue(t, 0.0f, 1.0f));
    }

    return envelope.sustainLevel;
}

float polyBlep(float phase, float phaseIncrement) {
    if (phaseIncrement <= 0.0f) {
        return 0.0f;
    }

    if (phase < phaseIncrement) {
        const float t = phase / phaseIncrement;
        return t + t - t * t - 1.0f;
    }

    if (phase > 1.0f - phaseIncrement) {
        const float t = (phase - 1.0f) / phaseIncrement;
        return t * t + t + t + 1.0f;
    }

    return 0.0f;
}

float oscillatorSample(
    Waveform waveform,
    OscillatorQuality quality,
    float phase,
    float phaseIncrement,
    float duty
) {
    phase -= std::floor(phase);
    duty = clampValue(duty, 0.05f, 0.95f);

    switch (waveform) {
        case Waveform::Square: {
            constexpr float squareDuty = 0.50f;
            float sample = phase < squareDuty ? 1.0f : -1.0f;
            if (quality == OscillatorQuality::BandLimited) {
                sample += polyBlep(phase, phaseIncrement);
                float fallingPhase = phase - squareDuty;
                if (fallingPhase < 0.0f) {
                    fallingPhase += 1.0f;
                }
                sample -= polyBlep(fallingPhase, phaseIncrement);
            }
            return sample;
        }

        case Waveform::Pulse: {
            float sample = phase < duty ? 1.0f : -1.0f;
            if (quality == OscillatorQuality::BandLimited) {
                sample += polyBlep(phase, phaseIncrement);
                float fallingPhase = phase - duty;
                if (fallingPhase < 0.0f) {
                    fallingPhase += 1.0f;
                }
                sample -= polyBlep(fallingPhase, phaseIncrement);
            }
            return sample;
        }

        case Waveform::Triangle:
            // Triangle is continuous and aliases substantially less than raw square/saw.
            return 1.0f - 4.0f * std::fabs(phase - 0.5f);

        case Waveform::Saw: {
            float sample = 2.0f * phase - 1.0f;
            if (quality == OscillatorQuality::BandLimited) {
                sample -= polyBlep(phase, phaseIncrement);
            }
            return sample;
        }
    }

    return 0.0f;
}

std::uint32_t xorshift32(std::uint32_t& state) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float randomBipolar(std::uint32_t& state) {
    const std::uint32_t value = xorshift32(state);
    constexpr float denominator = static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return (static_cast<float>(value) / denominator) * 2.0f - 1.0f;
}

float quantizeAmplitude(float sample, int bits) {
    const int clampedBits = clampValue(bits, 2, 16);
    const float levels = static_cast<float>((1u << (clampedBits - 1)) - 1u);
    return std::round(clampValue(sample, -1.0f, 1.0f) * levels) / levels;
}

Result<bool> validatePitch(const PitchParams& pitch, float nyquist, const char* label) {
    if (!finite(pitch.startHz) || !finite(pitch.endHz) ||
        pitch.startHz <= 0.0f || pitch.endHz <= 0.0f ||
        pitch.startHz >= nyquist || pitch.endHz >= nyquist) {
        return Result<bool>::failure(
            ErrorCode::InvalidFrequency,
            std::string(label) + ": startHz/endHz devono essere finiti, positivi e inferiori a Nyquist."
        );
    }

    if (!finite(pitch.sweepCurve) || pitch.sweepCurve <= 0.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidFrequency,
            std::string(label) + ": sweepCurve deve essere maggiore di zero."
        );
    }

    if (!finite(pitch.vibratoDepthSemitones) || !finite(pitch.vibratoRateHz) ||
        pitch.vibratoRateHz < 0.0f ||
        !finite(pitch.arpeggioSemitones) || !finite(pitch.arpeggioRateHz) ||
        pitch.arpeggioRateHz < 0.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidFrequency,
            std::string(label) + ": parametri di vibrato/arpeggio non validi."
        );
    }

    return Result<bool>::success(true);
}

Result<bool> validateVoice(const VoiceParams& voice, float nyquist, const char* label) {
    if (!voice.enabled) {
        return Result<bool>::success(true);
    }

    if (!finite(voice.gain) || voice.gain < 0.0f || voice.gain > 1.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidGain,
            std::string(label) + ": gain deve essere compreso tra 0 e 1."
        );
    }

    if (voice.waveform == Waveform::Pulse &&
        (!finite(voice.dutyStart) || !finite(voice.dutyEnd) ||
         voice.dutyStart < 0.05f || voice.dutyStart > 0.95f ||
         voice.dutyEnd < 0.05f || voice.dutyEnd > 0.95f)) {
        return Result<bool>::failure(
            ErrorCode::InvalidDutyCycle,
            std::string(label) + ": dutyStart/dutyEnd devono essere compresi tra 0.05 e 0.95."
        );
    }

    if (!finite(voice.detuneSemitones)) {
        return Result<bool>::failure(
            ErrorCode::InvalidFrequency,
            std::string(label) + ": detuneSemitones non valido."
        );
    }

    return validatePitch(voice.pitch, nyquist, label);
}

} // namespace

Result<bool> SfxSynthesizer::validate(
    const SfxRecipe& recipe,
    const RenderSettings& settings
) {
    if (recipe.schemaVersion != kRecipeSchemaVersion ||
        recipe.generatorVersion != kGeneratorVersion) {
        return Result<bool>::failure(
            ErrorCode::InvalidRecipe,
            "Versione della recipe o del generatore non supportata."
        );
    }

    if (settings.sampleRate < 8000u || settings.sampleRate > 192000u) {
        return Result<bool>::failure(
            ErrorCode::InvalidSampleRate,
            "Il sample rate deve essere compreso tra 8000 e 192000 Hz."
        );
    }

    if (!finite(recipe.durationSeconds) || recipe.durationSeconds <= 0.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidDuration,
            "La durata deve essere finita e maggiore di zero."
        );
    }

    const double exactFrames = std::ceil(
        static_cast<double>(recipe.durationSeconds) * static_cast<double>(settings.sampleRate)
    );
    if (exactFrames < 1.0 || exactFrames > static_cast<double>(settings.maximumFrames)) {
        return Result<bool>::failure(
            ErrorCode::TooManyFrames,
            "La durata richiesta supera il limite massimo di frame configurato."
        );
    }

    if (!finite(recipe.masterGain) || recipe.masterGain < 0.0f || recipe.masterGain > 1.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidGain,
            "masterGain deve essere compreso tra 0 e 1."
        );
    }

    const Envelope& envelope = recipe.amplitude;
    if (!finite(envelope.attackSeconds) || !finite(envelope.decaySeconds) ||
        !finite(envelope.releaseSeconds) || !finite(envelope.sustainLevel) ||
        envelope.attackSeconds < 0.0f || envelope.decaySeconds < 0.0f ||
        envelope.releaseSeconds < 0.0f || envelope.sustainLevel < 0.0f ||
        envelope.sustainLevel > 1.0f) {
        return Result<bool>::failure(
            ErrorCode::InvalidEnvelope,
            "I parametri ADSR devono essere finiti, non negativi e sustainLevel deve essere tra 0 e 1."
        );
    }

    const float occupiedEnvelopeTime =
        envelope.attackSeconds + envelope.decaySeconds + envelope.releaseSeconds;
    if (occupiedEnvelopeTime > recipe.durationSeconds + 1.0e-6f) {
        return Result<bool>::failure(
            ErrorCode::InvalidEnvelope,
            "Attack + decay + release non possono superare la durata totale."
        );
    }

    if (!recipe.primaryVoice.enabled && !recipe.secondaryVoice.enabled && !recipe.noise.enabled) {
        return Result<bool>::failure(
            ErrorCode::InvalidRecipe,
            "La recipe deve contenere almeno una voce o un layer noise abilitato."
        );
    }

    const float nyquist = static_cast<float>(settings.sampleRate) * 0.5f;
    if (auto result = validateVoice(recipe.primaryVoice, nyquist, "primaryVoice"); !result) {
        return result;
    }
    if (auto result = validateVoice(recipe.secondaryVoice, nyquist, "secondaryVoice"); !result) {
        return result;
    }

    if (recipe.noise.enabled) {
        if (!finite(recipe.noise.gain) || recipe.noise.gain < 0.0f || recipe.noise.gain > 1.0f) {
            return Result<bool>::failure(
                ErrorCode::InvalidGain,
                "noise.gain deve essere compreso tra 0 e 1."
            );
        }
        if (auto result = validatePitch(recipe.noise.clock, nyquist, "noise.clock"); !result) {
            return result;
        }
    }

    if (recipe.bitCrusher.enabled) {
        if (recipe.bitCrusher.quantizationBits < 2 || recipe.bitCrusher.quantizationBits > 16 ||
            !finite(recipe.bitCrusher.reductionRateHz) || recipe.bitCrusher.reductionRateHz < 0.0f ||
            recipe.bitCrusher.reductionRateHz > static_cast<float>(settings.sampleRate)) {
            return Result<bool>::failure(
                ErrorCode::InvalidBitCrusher,
                "Bit crusher non valido: bit 2..16 e reductionRateHz 0..sampleRate."
            );
        }
    }

    if (!finite(recipe.filter.lowPassHz) || recipe.filter.lowPassHz < 0.0f ||
        recipe.filter.lowPassHz >= nyquist ||
        !finite(recipe.filter.dcBlockCutoffHz) || recipe.filter.dcBlockCutoffHz <= 0.0f ||
        recipe.filter.dcBlockCutoffHz >= nyquist) {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "Parametri del filtro non validi."
        );
    }

    if (settings.applyDeclick &&
        (!finite(settings.declickSeconds) || settings.declickSeconds < 0.0f)) {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "declickSeconds deve essere finito e non negativo."
        );
    }

    return Result<bool>::success(true);
}

Result<FloatAudioBuffer> SfxSynthesizer::render(
    const SfxRecipe& recipe,
    const RenderSettings& settings
) const {
    if (auto validation = validate(recipe, settings); !validation) {
        return Result<FloatAudioBuffer>::failure(
            validation.error().code,
            validation.error().message
        );
    }

    const std::size_t frameCount = static_cast<std::size_t>(std::ceil(
        static_cast<double>(recipe.durationSeconds) * static_cast<double>(settings.sampleRate)
    ));

    FloatAudioBuffer output;
    output.sampleRate = settings.sampleRate;
    output.samples.resize(frameCount, 0.0f);

    float primaryPhase = 0.0f;
    float secondaryPhase = 0.0f;

    std::uint32_t randomState = recipe.randomSeed != 0u ? recipe.randomSeed : 1u;
    float noisePhase = 0.0f;
    float noiseValue = randomBipolar(randomState);

    float heldSample = 0.0f;
    float crusherAccumulator = 0.0f;

    float lowPassState = 0.0f;
    const float lowPassAlpha = recipe.filter.lowPassHz > 0.0f
        ? 1.0f - std::exp(-kTwoPi * recipe.filter.lowPassHz / static_cast<float>(settings.sampleRate))
        : 1.0f;

    float dcPreviousInput = 0.0f;
    float dcPreviousOutput = 0.0f;
    const float dcCoefficient = std::exp(
        -kTwoPi * recipe.filter.dcBlockCutoffHz / static_cast<float>(settings.sampleRate)
    );

    const std::size_t declickFrames = settings.applyDeclick
        ? static_cast<std::size_t>(std::ceil(
            settings.declickSeconds * static_cast<float>(settings.sampleRate)
        ))
        : 0u;

    const auto renderVoice = [sampleRate = settings.sampleRate](
        const VoiceParams& voice,
        float normalizedTime,
        float timeSeconds,
        float& phase
    ) -> float {
        if (!voice.enabled || voice.gain <= 0.0f) {
            return 0.0f;
        }

        float frequency = interpolatePitch(voice.pitch, normalizedTime, timeSeconds);
        frequency *= semitoneRatio(voice.detuneSemitones);
        frequency = clampValue(
            frequency,
            1.0f,
            static_cast<float>(sampleRate) * 0.499f
        );

        const float phaseIncrement = frequency / static_cast<float>(sampleRate);
        const float duty = voice.dutyStart +
            (voice.dutyEnd - voice.dutyStart) * normalizedTime;

        const float sample = oscillatorSample(
            voice.waveform,
            voice.quality,
            phase,
            phaseIncrement,
            duty
        );

        phase += phaseIncrement;
        phase -= std::floor(phase);
        return sample * voice.gain;
    };

    for (std::size_t index = 0; index < frameCount; ++index) {
        const float timeSeconds =
            static_cast<float>(index) / static_cast<float>(settings.sampleRate);
        const float normalizedTime = frameCount > 1u
            ? static_cast<float>(index) / static_cast<float>(frameCount - 1u)
            : 0.0f;

        float mixed = 0.0f;
        mixed += renderVoice(
            recipe.primaryVoice,
            normalizedTime,
            timeSeconds,
            primaryPhase
        );
        mixed += renderVoice(
            recipe.secondaryVoice,
            normalizedTime,
            timeSeconds,
            secondaryPhase
        );

        if (recipe.noise.enabled && recipe.noise.gain > 0.0f) {
            float noiseFrequency = interpolatePitch(
                recipe.noise.clock,
                normalizedTime,
                timeSeconds
            );
            noiseFrequency = clampValue(
                noiseFrequency,
                1.0f,
                static_cast<float>(settings.sampleRate) * 0.499f
            );

            noisePhase += noiseFrequency / static_cast<float>(settings.sampleRate);
            while (noisePhase >= 1.0f) {
                noisePhase -= 1.0f;
                noiseValue = randomBipolar(randomState);
            }
            mixed += noiseValue * recipe.noise.gain;
        }

        const float amplitude = envelopeAt(
            timeSeconds,
            recipe.durationSeconds,
            recipe.amplitude
        );
        float sample = mixed * amplitude * recipe.masterGain;

        if (recipe.bitCrusher.enabled) {
            const float refreshRate = recipe.bitCrusher.reductionRateHz;
            if (refreshRate <= 0.0f || refreshRate >= static_cast<float>(settings.sampleRate)) {
                heldSample = sample;
            } else if (index == 0u) {
                heldSample = sample;
            } else {
                crusherAccumulator += refreshRate / static_cast<float>(settings.sampleRate);
                if (crusherAccumulator >= 1.0f) {
                    crusherAccumulator -= std::floor(crusherAccumulator);
                    heldSample = sample;
                }
            }
            sample = quantizeAmplitude(heldSample, recipe.bitCrusher.quantizationBits);
        }

        if (recipe.filter.lowPassHz > 0.0f) {
            lowPassState += lowPassAlpha * (sample - lowPassState);
            sample = lowPassState;
        }

        if (recipe.filter.dcBlockEnabled) {
            const float blocked = sample - dcPreviousInput + dcCoefficient * dcPreviousOutput;
            dcPreviousInput = sample;
            dcPreviousOutput = blocked;
            sample = blocked;
        }

        if (declickFrames > 0u && frameCount > 1u) {
            const float fadeIn = clampValue(
                static_cast<float>(index) / static_cast<float>(declickFrames),
                0.0f,
                1.0f
            );
            const float fadeOut = clampValue(
                static_cast<float>(frameCount - 1u - index) /
                    static_cast<float>(declickFrames),
                0.0f,
                1.0f
            );
            sample *= std::min(fadeIn, fadeOut);
        }

        if (!finite(sample)) {
            return Result<FloatAudioBuffer>::failure(
                ErrorCode::EncoderFailure,
                "Il renderer ha prodotto un valore non finito."
            );
        }

        output.samples[index] = clampValue(sample, -1.0f, 1.0f);
    }

    if (!output.samples.empty() && settings.applyDeclick) {
        output.samples.front() = 0.0f;
        output.samples.back() = 0.0f;
    }

    return Result<FloatAudioBuffer>::success(std::move(output));
}

} // namespace artcade::sfx
