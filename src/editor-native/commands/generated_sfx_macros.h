#pragma once

#include "artcade/sfx/types.hpp"

#include <array>
#include <random>
#include <string>

namespace ArtCade::EditorNative {

// Simple-mode macro layer for the Generated SFX editor: seven sliders standing
// in for the ~40 raw SfxRecipe fields. Every setter below is invariant-
// preserving on its own — it never has to be paired with a follow-up clamp by
// the caller, and the result always passes artcade::sfx::SfxSynthesizer::
// validate() for the default RenderSettings (44100 Hz -> 22050 Hz Nyquist).
struct SfxMacro {
    const char* id;
    const char* label;
    const char* unit;
    float sliderMin;
    float sliderMax;
    float step;
    int   decimals; // for formatting the companion numeric readout
};

// Slider-space domains:
//  - duration/attack/release: seconds, linear.
//  - pitch: [0,1] normalized log position, see sfxPitchMacroHz()/sfxHzToPitchMacro().
//  - pitchSweep: semitones, linear, signed.
//  - noise/crunch: percent, linear.
inline constexpr std::array<SfxMacro, 7> kSfxMacros{{
    {"duration",   "Duration", "s",  0.02f, 2.0f,  0.01f,  2},
    {"pitch",      "Pitch",    "Hz", 0.0f,  1.0f,  0.001f, 0},
    {"pitchSweep", "Sweep",    "st", -24.f, 24.f,  1.f,    0},
    {"attack",     "Attack",   "s",  0.0f,  0.5f,  0.001f, 3},
    {"release",    "Release",  "s",  0.0f,  1.0f,  0.01f,  2},
    {"noise",      "Noise",    "%",  0.0f,  100.f, 1.f,    0},
    {"crunch",     "Crunch",   "%",  0.0f,  100.f, 1.f,    0},
}};

inline constexpr float kSfxPitchMinHz = 40.f;
inline constexpr float kSfxPitchMaxHz = 4000.f;
// Generous headroom under any reasonable project Nyquist (44.1kHz sample
// rate -> 22050Hz): Pitch tops out at 4000Hz, Sweep at +24 semitones is a
// further x4, worst case 16000Hz. This is a no-op in range today and only
// guards a future change to the macro ranges above.
inline constexpr float kSfxEndHzSafetyCeiling = 16000.f;

const SfxMacro* findSfxMacro(const std::string& macroId);

// value = kSfxPitchMinHz * pow(kSfxPitchMaxHz / kSfxPitchMinHz, normalized)
float sfxPitchMacroToHz(float normalized);
float sfxHzToPitchMacro(float hz);

// Reverse mapping: current recipe -> slider-space value for one macro. This
// is what drives the native <input type="range">'s own value (Pitch: [0,1]
// log position; the other six: already in their display unit).
float sfxMacroValue(const artcade::sfx::SfxRecipe& recipe, const std::string& macroId);

// Forward mapping: apply one macro's new slider-space value onto a recipe
// clone. Returns false for an unknown macroId (recipe left untouched).
bool setSfxMacroField(artcade::sfx::SfxRecipe& recipe, const std::string& macroId,
                     float sliderValue);

// Real-world display units for the companion numeric input: Hz for Pitch,
// identical to sfxMacroValue() for the other six macros (slider space and
// display space are the same unit for those). Reading straight from the
// recipe (rather than round-tripping sfxMacroValue -> sfxPitchMacroToHz)
// avoids compounding floating-point error on every refresh.
float sfxMacroDisplayValue(const artcade::sfx::SfxRecipe& recipe, const std::string& macroId);

// Applies a real-world-unit edit (e.g. a typed "880" for Pitch, in Hz) --
// converts to slider space first where the two differ (Pitch only), then
// delegates to setSfxMacroField. Use this for the companion text input's
// commit path; use setSfxMacroField directly for the range input/drag path,
// which already reports its own value in slider space.
bool setSfxMacroFieldFromDisplay(artcade::sfx::SfxRecipe& recipe, const std::string& macroId,
                                 float displayValue);

// Randomizes only the 7 macro-mapped fields using biased (not uniform)
// distributions so the result is usually a playable sound rather than noise
// at the extremes of every range. Every intermediate write goes through
// setSfxMacroField, so the output always passes SfxSynthesizer::validate().
void randomizeSfxMacros(artcade::sfx::SfxRecipe& recipe, std::mt19937& rng);

} // namespace ArtCade::EditorNative
