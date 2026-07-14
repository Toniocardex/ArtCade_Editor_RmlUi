#include "editor-native/commands/generated_sfx_macros.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {
namespace {

using artcade::sfx::Envelope;
using artcade::sfx::PitchParams;
using artcade::sfx::SfxRecipe;

// Semitone offset of the current sweep (endHz relative to startHz). Used both
// to report the Sweep macro's display value and to preserve the sweep shape
// when Pitch moves startHz.
float semitoneSweepOf(const PitchParams& pitch) {
    if (pitch.startHz <= 0.f || pitch.endHz <= 0.f) return 0.f;
    return 12.f * std::log2(pitch.endHz / pitch.startHz);
}

// endHz = startHz * 2^(semitones/12) is positive by construction whenever
// startHz is positive, so this can never drive validatePitch's "endHz > 0"
// check negative the way an additive Hz delta could.
float applySweepSemitones(float startHz, float semitones) {
    const float hz = startHz * std::pow(2.f, semitones / 12.f);
    return std::min(hz, kSfxEndHzSafetyCeiling);
}

// Keeps attack+decay+release within durationSeconds (SfxSynthesizer::validate's
// envelope invariant) by scaling all three down proportionally, including
// decaySeconds even though it isn't one of the seven macros — Simple mode
// must never produce a recipe validate() would reject.
void rescaleEnvelopeToFitDuration(Envelope& envelope, float durationSeconds) {
    const float occupied = envelope.attackSeconds + envelope.decaySeconds + envelope.releaseSeconds;
    if (occupied <= durationSeconds || occupied <= 0.f) return;
    const float scale = durationSeconds / occupied;
    envelope.attackSeconds *= scale;
    envelope.decaySeconds *= scale;
    envelope.releaseSeconds *= scale;
}

// Crunch is a 0-100% intensity, not the raw bit count: 0 disengages the Bit
// Crusher entirely (quantizationBits left untouched), >0 both enables it and
// quantizes 16 -> 4 bits as intensity rises. Editing quantizationBits alone
// would silently do nothing while bitCrusher.enabled was already false.
constexpr float kCrunchMaxBits = 16.f;
constexpr float kCrunchMinBits = 4.f;

} // namespace

const SfxMacro* findSfxMacro(const std::string& macroId) {
    for (const SfxMacro& macro : kSfxMacros) {
        if (macroId == macro.id) return &macro;
    }
    return nullptr;
}

float sfxPitchMacroToHz(float normalized) {
    const float clamped = std::clamp(normalized, 0.f, 1.f);
    return kSfxPitchMinHz * std::pow(kSfxPitchMaxHz / kSfxPitchMinHz, clamped);
}

float sfxHzToPitchMacro(float hz) {
    const float clampedHz = std::clamp(hz, kSfxPitchMinHz, kSfxPitchMaxHz);
    return std::log(clampedHz / kSfxPitchMinHz) / std::log(kSfxPitchMaxHz / kSfxPitchMinHz);
}

float sfxMacroValue(const SfxRecipe& recipe, const std::string& macroId) {
    if (macroId == "duration") return recipe.durationSeconds;
    if (macroId == "pitch") return sfxHzToPitchMacro(recipe.primaryVoice.pitch.startHz);
    if (macroId == "pitchSweep") return semitoneSweepOf(recipe.primaryVoice.pitch);
    if (macroId == "attack") return recipe.amplitude.attackSeconds;
    if (macroId == "release") return recipe.amplitude.releaseSeconds;
    if (macroId == "noise") return recipe.noise.gain * 100.f;
    if (macroId == "crunch") {
        if (!recipe.bitCrusher.enabled) return 0.f;
        const float t = (kCrunchMaxBits - static_cast<float>(recipe.bitCrusher.quantizationBits))
                      / (kCrunchMaxBits - kCrunchMinBits);
        return std::clamp(t, 0.f, 1.f) * 100.f;
    }
    return 0.f;
}

bool setSfxMacroField(SfxRecipe& recipe, const std::string& macroId, float sliderValue) {
    const SfxMacro* macro = findSfxMacro(macroId);
    if (!macro) return false;
    const float clamped = std::clamp(sliderValue, macro->sliderMin, macro->sliderMax);

    if (macroId == "duration") {
        recipe.durationSeconds = clamped;
        rescaleEnvelopeToFitDuration(recipe.amplitude, recipe.durationSeconds);
    } else if (macroId == "pitch") {
        PitchParams& pitch = recipe.primaryVoice.pitch;
        const float semitones = semitoneSweepOf(pitch);
        const float hz = sfxPitchMacroToHz(clamped);
        pitch.startHz = hz;
        pitch.endHz = applySweepSemitones(hz, semitones);
    } else if (macroId == "pitchSweep") {
        PitchParams& pitch = recipe.primaryVoice.pitch;
        pitch.endHz = applySweepSemitones(pitch.startHz, clamped);
    } else if (macroId == "attack") {
        recipe.amplitude.attackSeconds = clamped;
        rescaleEnvelopeToFitDuration(recipe.amplitude, recipe.durationSeconds);
    } else if (macroId == "release") {
        recipe.amplitude.releaseSeconds = clamped;
        rescaleEnvelopeToFitDuration(recipe.amplitude, recipe.durationSeconds);
    } else if (macroId == "noise") {
        recipe.noise.gain = clamped / 100.f;
        recipe.noise.enabled = clamped > 0.f;
    } else if (macroId == "crunch") {
        if (clamped <= 0.f) {
            recipe.bitCrusher.enabled = false;
        } else {
            recipe.bitCrusher.enabled = true;
            const float bits = kCrunchMaxBits + (clamped / 100.f) * (kCrunchMinBits - kCrunchMaxBits);
            recipe.bitCrusher.quantizationBits = std::clamp(
                static_cast<int>(std::lround(bits)),
                static_cast<int>(kCrunchMinBits), static_cast<int>(kCrunchMaxBits));
        }
    } else {
        return false;
    }
    return true;
}

float sfxMacroDisplayValue(const SfxRecipe& recipe, const std::string& macroId) {
    if (macroId == "pitch") return recipe.primaryVoice.pitch.startHz;
    return sfxMacroValue(recipe, macroId);
}

bool setSfxMacroFieldFromDisplay(SfxRecipe& recipe, const std::string& macroId, float displayValue) {
    if (macroId == "pitch") return setSfxMacroField(recipe, macroId, sfxHzToPitchMacro(displayValue));
    return setSfxMacroField(recipe, macroId, displayValue);
}

void randomizeSfxMacros(SfxRecipe& recipe, std::mt19937& rng) {
    // Duration first: attack/release below rescale against whatever duration
    // is current at the time they run, so establishing it first keeps the
    // envelope shape closer to what each draw actually asked for.
    std::uniform_real_distribution<float> durationDist(0.05f, 0.7f);
    setSfxMacroField(recipe, "duration", (durationDist(rng) + durationDist(rng)) * 0.5f);

    std::uniform_real_distribution<float> unit(0.f, 1.f);
    setSfxMacroField(recipe, "pitch", unit(rng));

    std::normal_distribution<float> sweepDist(0.f, 6.f);
    setSfxMacroField(recipe, "pitchSweep", std::clamp(sweepDist(rng), -24.f, 24.f));

    std::exponential_distribution<float> attackDist(20.f); // mean 0.05s
    setSfxMacroField(recipe, "attack", std::min(attackDist(rng), 0.5f));

    std::exponential_distribution<float> releaseDist(8.f); // mean 0.125s
    setSfxMacroField(recipe, "release", std::min(releaseDist(rng), 1.0f));

    // Noise/Crunch: explicit off-probability so "usually off, sometimes on" is
    // a visible decision, not an accident of drawing uniformly from [0,100].
    std::bernoulli_distribution noiseOn(0.30);
    float noise = 0.f;
    if (noiseOn(rng)) {
        std::uniform_real_distribution<float> intensity(5.f, 60.f);
        noise = intensity(rng);
    }
    setSfxMacroField(recipe, "noise", noise);

    std::bernoulli_distribution crunchOn(0.30);
    float crunch = 0.f;
    if (crunchOn(rng)) {
        std::uniform_real_distribution<float> intensity(10.f, 80.f);
        crunch = intensity(rng);
    }
    setSfxMacroField(recipe, "crunch", crunch);
}

} // namespace ArtCade::EditorNative
