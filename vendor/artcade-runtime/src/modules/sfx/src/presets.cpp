#include "artcade/sfx/presets.hpp"

namespace artcade::sfx::presets {
namespace {

SfxRecipe baseRecipe(float duration) {
    SfxRecipe recipe;
    recipe.durationSeconds = duration;
    recipe.masterGain = 0.72f;
    recipe.amplitude = {0.002f, 0.030f, 0.70f, 0.060f};
    recipe.primaryVoice.enabled = true;
    recipe.secondaryVoice.enabled = false;
    recipe.noise.enabled = false;
    recipe.bitCrusher.enabled = true;
    recipe.bitCrusher.quantizationBits = 8;
    recipe.bitCrusher.reductionRateHz = 11025.0f;
    recipe.filter.lowPassHz = 0.0f;
    recipe.filter.dcBlockEnabled = true;
    recipe.filter.dcBlockCutoffHz = 18.0f;
    return recipe;
}

} // namespace

SfxRecipe coin() {
    SfxRecipe recipe = baseRecipe(0.18f);
    recipe.masterGain = 0.62f;
    recipe.amplitude = {0.001f, 0.024f, 0.78f, 0.050f};

    recipe.primaryVoice.waveform = Waveform::Square;
    recipe.primaryVoice.quality = OscillatorQuality::BandLimited;
    recipe.primaryVoice.gain = 0.72f;
    recipe.primaryVoice.pitch.startHz = 880.0f;
    recipe.primaryVoice.pitch.endHz = 1320.0f;
    recipe.primaryVoice.pitch.sweepCurve = 0.65f;
    recipe.primaryVoice.pitch.arpeggioSemitones = 7.0f;
    recipe.primaryVoice.pitch.arpeggioRateHz = 24.0f;

    recipe.secondaryVoice.enabled = true;
    recipe.secondaryVoice.waveform = Waveform::Square;
    recipe.secondaryVoice.quality = OscillatorQuality::BandLimited;
    recipe.secondaryVoice.gain = 0.28f;
    recipe.secondaryVoice.detuneSemitones = 12.0f;
    recipe.secondaryVoice.pitch = recipe.primaryVoice.pitch;

    recipe.bitCrusher.quantizationBits = 8;
    recipe.bitCrusher.reductionRateHz = 16000.0f;
    return recipe;
}

SfxRecipe jump() {
    SfxRecipe recipe = baseRecipe(0.30f);
    recipe.masterGain = 0.68f;
    recipe.amplitude = {0.002f, 0.040f, 0.72f, 0.080f};

    recipe.primaryVoice.waveform = Waveform::Pulse;
    recipe.primaryVoice.quality = OscillatorQuality::BandLimited;
    recipe.primaryVoice.gain = 1.0f;
    recipe.primaryVoice.dutyStart = 0.38f;
    recipe.primaryVoice.dutyEnd = 0.62f;
    recipe.primaryVoice.pitch.startHz = 165.0f;
    recipe.primaryVoice.pitch.endHz = 720.0f;
    recipe.primaryVoice.pitch.sweepCurve = 1.55f;
    recipe.primaryVoice.pitch.sweepMode = PitchSweepMode::Exponential;

    recipe.bitCrusher.quantizationBits = 8;
    recipe.bitCrusher.reductionRateHz = 12000.0f;
    return recipe;
}

SfxRecipe laser() {
    SfxRecipe recipe = baseRecipe(0.38f);
    recipe.masterGain = 0.58f;
    recipe.amplitude = {0.001f, 0.018f, 0.82f, 0.120f};

    recipe.primaryVoice.waveform = Waveform::Saw;
    recipe.primaryVoice.quality = OscillatorQuality::BandLimited;
    recipe.primaryVoice.gain = 0.78f;
    recipe.primaryVoice.pitch.startHz = 1600.0f;
    recipe.primaryVoice.pitch.endHz = 85.0f;
    recipe.primaryVoice.pitch.sweepCurve = 0.82f;
    recipe.primaryVoice.pitch.vibratoDepthSemitones = 0.45f;
    recipe.primaryVoice.pitch.vibratoRateHz = 31.0f;

    recipe.secondaryVoice.enabled = true;
    recipe.secondaryVoice.waveform = Waveform::Pulse;
    recipe.secondaryVoice.quality = OscillatorQuality::Raw;
    recipe.secondaryVoice.gain = 0.22f;
    recipe.secondaryVoice.dutyStart = 0.28f;
    recipe.secondaryVoice.dutyEnd = 0.52f;
    recipe.secondaryVoice.detuneSemitones = -12.0f;
    recipe.secondaryVoice.pitch = recipe.primaryVoice.pitch;

    recipe.bitCrusher.quantizationBits = 7;
    recipe.bitCrusher.reductionRateHz = 12000.0f;
    recipe.filter.lowPassHz = 7600.0f;
    return recipe;
}

SfxRecipe explosion() {
    SfxRecipe recipe = baseRecipe(0.68f);
    recipe.masterGain = 0.78f;
    recipe.amplitude = {0.001f, 0.080f, 0.66f, 0.420f};

    recipe.primaryVoice.waveform = Waveform::Triangle;
    recipe.primaryVoice.quality = OscillatorQuality::Raw;
    recipe.primaryVoice.gain = 0.30f;
    recipe.primaryVoice.pitch.startHz = 135.0f;
    recipe.primaryVoice.pitch.endHz = 42.0f;
    recipe.primaryVoice.pitch.sweepCurve = 0.72f;

    recipe.noise.enabled = true;
    recipe.noise.gain = 0.92f;
    recipe.noise.clock.startHz = 10500.0f;
    recipe.noise.clock.endHz = 620.0f;
    recipe.noise.clock.sweepCurve = 0.72f;
    recipe.noise.clock.sweepMode = PitchSweepMode::Exponential;

    recipe.bitCrusher.quantizationBits = 6;
    recipe.bitCrusher.reductionRateHz = 9000.0f;
    recipe.filter.lowPassHz = 2800.0f;
    recipe.randomSeed = 0x0A17CADEu;
    return recipe;
}

SfxRecipe hit() {
    SfxRecipe recipe = baseRecipe(0.14f);
    recipe.masterGain = 0.74f;
    recipe.amplitude = {0.001f, 0.016f, 0.42f, 0.070f};

    recipe.primaryVoice.waveform = Waveform::Triangle;
    recipe.primaryVoice.quality = OscillatorQuality::Raw;
    recipe.primaryVoice.gain = 0.42f;
    recipe.primaryVoice.pitch.startHz = 190.0f;
    recipe.primaryVoice.pitch.endHz = 72.0f;
    recipe.primaryVoice.pitch.sweepCurve = 0.65f;

    recipe.noise.enabled = true;
    recipe.noise.gain = 0.80f;
    recipe.noise.clock.startHz = 9000.0f;
    recipe.noise.clock.endHz = 1450.0f;
    recipe.noise.clock.sweepCurve = 0.75f;

    recipe.bitCrusher.quantizationBits = 7;
    recipe.bitCrusher.reductionRateHz = 11025.0f;
    recipe.filter.lowPassHz = 4200.0f;
    recipe.randomSeed = 0x0BADC0DEu;
    return recipe;
}

} // namespace artcade::sfx::presets
