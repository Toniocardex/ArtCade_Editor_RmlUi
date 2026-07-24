#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "artcade/sfx/wav_encoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace artcade::sfx;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SfxRecipe constantTone(Waveform waveform) {
    SfxRecipe recipe;
    recipe.durationSeconds = 0.10f;
    recipe.masterGain = 0.50f;
    recipe.amplitude = {0.0f, 0.0f, 1.0f, 0.0f};

    recipe.primaryVoice.enabled = true;
    recipe.primaryVoice.waveform = waveform;
    recipe.primaryVoice.quality = OscillatorQuality::Raw;
    recipe.primaryVoice.gain = 1.0f;
    recipe.primaryVoice.pitch.startHz = 440.0f;
    recipe.primaryVoice.pitch.endHz = 440.0f;

    recipe.secondaryVoice.enabled = false;
    recipe.noise.enabled = false;
    recipe.bitCrusher.enabled = false;
    recipe.filter.lowPassHz = 0.0f;
    recipe.filter.dcBlockEnabled = false;
    return recipe;
}

RenderSettings noDeclick(std::uint32_t sampleRate = 44100u) {
    RenderSettings settings;
    settings.sampleRate = sampleRate;
    settings.applyDeclick = false;
    return settings;
}

float meanFrom(const std::vector<float>& samples, std::size_t begin) {
    double sum = 0.0;
    for (std::size_t index = begin; index < samples.size(); ++index) {
        sum += samples[index];
    }
    return static_cast<float>(sum / static_cast<double>(samples.size() - begin));
}

std::size_t transitionCount(const std::vector<float>& samples) {
    std::size_t transitions = 0u;
    for (std::size_t index = 1u; index < samples.size(); ++index) {
        if (samples[index] != samples[index - 1u]) {
            ++transitions;
        }
    }
    return transitions;
}

void deterministicWithSameSeed() {
    SfxSynthesizer synth;
    SfxRecipe recipe = presets::explosion();

    auto first = synth.render(recipe);
    auto second = synth.render(recipe);

    expect(first.ok() && second.ok(), "Il preset explosion deve essere renderizzabile.");
    expect(first.value().samples == second.value().samples,
           "Stessa recipe e stesso seed devono produrre PCM identico.");
}

void differentSeedChangesNoise() {
    SfxSynthesizer synth;
    SfxRecipe firstRecipe = presets::explosion();
    SfxRecipe secondRecipe = firstRecipe;
    secondRecipe.randomSeed += 1u;

    auto first = synth.render(firstRecipe);
    auto second = synth.render(secondRecipe);

    expect(first.ok() && second.ok(), "Entrambe le recipe devono essere valide.");
    expect(first.value().samples != second.value().samples,
           "Seed differenti devono modificare il layer noise.");
}

void envelopeClosesBuffer() {
    SfxSynthesizer synth;
    auto result = synth.render(presets::jump());

    expect(result.ok(), "Il preset jump deve essere renderizzabile.");
    expect(!result.value().samples.empty(), "Il buffer non deve essere vuoto.");
    expect(result.value().samples.front() == 0.0f, "Il primo campione deve essere zero.");
    expect(result.value().samples.back() == 0.0f, "L'ultimo campione deve essere zero.");
}

void invalidEnvelopeIsRejected() {
    SfxRecipe recipe = presets::jump();
    recipe.durationSeconds = 0.10f;
    recipe.amplitude = {0.08f, 0.08f, 0.70f, 0.08f};

    auto validation = SfxSynthesizer::validate(recipe);
    expect(!validation.ok(), "ADSR più lungo della durata deve essere rifiutato.");
    expect(validation.error().code == ErrorCode::InvalidEnvelope,
           "Il codice errore deve indicare InvalidEnvelope.");
}

void squareIgnoresDutyCycle() {
    SfxSynthesizer synth;
    SfxRecipe firstRecipe = constantTone(Waveform::Square);
    SfxRecipe secondRecipe = firstRecipe;
    firstRecipe.primaryVoice.dutyStart = 0.20f;
    firstRecipe.primaryVoice.dutyEnd = 0.80f;
    secondRecipe.primaryVoice.dutyStart = 0.50f;
    secondRecipe.primaryVoice.dutyEnd = 0.50f;

    auto first = synth.render(firstRecipe, noDeclick());
    auto second = synth.render(secondRecipe, noDeclick());

    expect(first.ok() && second.ok(), "Le square recipe devono essere valide.");
    expect(first.value().samples == second.value().samples,
           "Square deve restare sempre al 50% indipendentemente dal duty configurato.");
}

void pulseUsesDutyCycle() {
    SfxSynthesizer synth;
    SfxRecipe firstRecipe = constantTone(Waveform::Pulse);
    SfxRecipe secondRecipe = firstRecipe;
    firstRecipe.primaryVoice.dutyStart = 0.20f;
    firstRecipe.primaryVoice.dutyEnd = 0.20f;
    secondRecipe.primaryVoice.dutyStart = 0.80f;
    secondRecipe.primaryVoice.dutyEnd = 0.80f;

    auto first = synth.render(firstRecipe, noDeclick());
    auto second = synth.render(secondRecipe, noDeclick());

    expect(first.ok() && second.ok(), "Le pulse recipe devono essere valide.");
    expect(first.value().samples != second.value().samples,
           "Pulse deve cambiare forma al variare del duty cycle.");
}

void dcBlockerRemovesPulseOffset() {
    SfxSynthesizer synth;
    SfxRecipe rawRecipe = constantTone(Waveform::Pulse);
    rawRecipe.durationSeconds = 1.0f;
    rawRecipe.primaryVoice.dutyStart = 0.80f;
    rawRecipe.primaryVoice.dutyEnd = 0.80f;
    rawRecipe.primaryVoice.pitch.startHz = 120.0f;
    rawRecipe.primaryVoice.pitch.endHz = 120.0f;

    SfxRecipe blockedRecipe = rawRecipe;
    blockedRecipe.filter.dcBlockEnabled = true;

    auto raw = synth.render(rawRecipe, noDeclick());
    auto blocked = synth.render(blockedRecipe, noDeclick());

    expect(raw.ok() && blocked.ok(), "Entrambe le pulse recipe devono essere valide.");

    const std::size_t analysisStart = raw.value().samples.size() / 2u;
    const float rawMean = std::fabs(meanFrom(raw.value().samples, analysisStart));
    const float blockedMean = std::fabs(meanFrom(blocked.value().samples, analysisStart));

    expect(rawMean > 0.10f, "La pulse asimmetrica raw deve avere offset DC misurabile.");
    expect(blockedMean < rawMean * 0.20f,
           "Il DC blocker deve ridurre sensibilmente l'offset medio.");
}

void noiseClockControlsRefreshRate() {
    SfxSynthesizer synth;
    SfxRecipe lowClock = constantTone(Waveform::Square);
    lowClock.primaryVoice.enabled = false;
    lowClock.noise.enabled = true;
    lowClock.noise.gain = 0.8f;
    lowClock.noise.clock.startHz = 120.0f;
    lowClock.noise.clock.endHz = 120.0f;

    SfxRecipe highClock = lowClock;
    highClock.noise.clock.startHz = 6000.0f;
    highClock.noise.clock.endHz = 6000.0f;

    auto low = synth.render(lowClock, noDeclick());
    auto high = synth.render(highClock, noDeclick());

    expect(low.ok() && high.ok(), "Entrambe le noise recipe devono essere valide.");
    expect(transitionCount(high.value().samples) > transitionCount(low.value().samples) * 10u,
           "Un clock noise più alto deve produrre più aggiornamenti casuali.");
}

void bitCrusherRateIsSampleRateIndependent() {
    SfxSynthesizer synth;
    SfxRecipe recipe = constantTone(Waveform::Saw);
    recipe.durationSeconds = 0.25f;
    recipe.primaryVoice.pitch.startHz = 3000.0f;
    recipe.primaryVoice.pitch.endHz = 3000.0f;
    recipe.bitCrusher.enabled = true;
    recipe.bitCrusher.quantizationBits = 16;
    recipe.bitCrusher.reductionRateHz = 8000.0f;

    auto at44100 = synth.render(recipe, noDeclick(44100u));
    auto at48000 = synth.render(recipe, noDeclick(48000u));

    expect(at44100.ok() && at48000.ok(), "Entrambi i sample rate devono essere supportati.");

    const float rate44100 = static_cast<float>(transitionCount(at44100.value().samples)) /
        recipe.durationSeconds;
    const float rate48000 = static_cast<float>(transitionCount(at48000.value().samples)) /
        recipe.durationSeconds;
    const float relativeDifference = std::fabs(rate44100 - rate48000) /
        std::max(rate44100, rate48000);

    expect(relativeDifference < 0.10f,
           "La reductionRateHz deve mantenere un comportamento simile tra sample rate diversi.");
}

void renderProducesOnlyFiniteSamples() {
    SfxSynthesizer synth;
    const std::vector<SfxRecipe> recipes{
        presets::coin(),
        presets::jump(),
        presets::laser(),
        presets::explosion(),
        presets::hit()
    };

    for (const SfxRecipe& recipe : recipes) {
        auto result = synth.render(recipe);
        expect(result.ok(), "Tutti i preset devono essere validi.");
        for (float sample : result.value().samples) {
            expect(std::isfinite(sample), "Il renderer non deve produrre NaN o infinito.");
            expect(sample >= -1.0f && sample <= 1.0f,
                   "I campioni devono restare nel range normalizzato.");
        }
    }
}

void maximumFrameLimitIsEnforced() {
    SfxRecipe recipe = presets::coin();
    recipe.durationSeconds = 2.0f;
    RenderSettings settings;
    settings.maximumFrames = 100u;

    auto validation = SfxSynthesizer::validate(recipe, settings);
    expect(!validation.ok(), "Il limite massimo di frame deve essere applicato.");
    expect(validation.error().code == ErrorCode::TooManyFrames,
           "Il codice errore deve essere TooManyFrames.");
}

void wavEncoderWritesRiffHeader() {
    SfxSynthesizer synth;
    auto rendered = synth.render(presets::coin());
    expect(rendered.ok(), "Il preset coin deve essere renderizzabile.");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "artcade_sfx_v2_test.wav";
    WavEncoder encoder;
    auto encoded = encoder.encode(rendered.value(), path);
    expect(encoded.ok(), "Il WAV deve essere scritto correttamente.");

    std::ifstream stream(path, std::ios::binary);
    char header[12]{};
    stream.read(header, 12);
    expect(stream.gcount() == 12, "Il WAV deve contenere un header completo.");
    expect(std::string(header, header + 4) == "RIFF", "Header RIFF mancante.");
    expect(std::string(header + 8, header + 12) == "WAVE", "Header WAVE mancante.");

    std::error_code error;
    std::filesystem::remove(path, error);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"deterministicWithSameSeed", deterministicWithSameSeed},
        {"differentSeedChangesNoise", differentSeedChangesNoise},
        {"envelopeClosesBuffer", envelopeClosesBuffer},
        {"invalidEnvelopeIsRejected", invalidEnvelopeIsRejected},
        {"squareIgnoresDutyCycle", squareIgnoresDutyCycle},
        {"pulseUsesDutyCycle", pulseUsesDutyCycle},
        {"dcBlockerRemovesPulseOffset", dcBlockerRemovesPulseOffset},
        {"noiseClockControlsRefreshRate", noiseClockControlsRefreshRate},
        {"bitCrusherRateIsSampleRateIndependent", bitCrusherRateIsSampleRateIndependent},
        {"renderProducesOnlyFiniteSamples", renderProducesOnlyFiniteSamples},
        {"maximumFrameLimitIsEnforced", maximumFrameLimitIsEnforced},
        {"wavEncoderWritesRiffHeader", wavEncoderWritesRiffHeader}
    };

    std::size_t passed = 0u;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << test.first << ": " << exception.what() << '\n';
            return 1;
        }
    }

    std::cout << passed << "/" << tests.size() << " test superati.\n";
    return 0;
}
