#include "artcade/sfx/recipe_json.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace artcade::sfx {
namespace {

using Json = nlohmann::json;

const char* toString(Waveform value) {
    switch (value) {
        case Waveform::Square: return "square";
        case Waveform::Pulse: return "pulse";
        case Waveform::Triangle: return "triangle";
        case Waveform::Saw: return "saw";
    }
    return "square";
}

const char* toString(OscillatorQuality value) {
    switch (value) {
        case OscillatorQuality::Raw: return "raw";
        case OscillatorQuality::BandLimited: return "bandLimited";
    }
    return "bandLimited";
}

const char* toString(PitchSweepMode value) {
    switch (value) {
        case PitchSweepMode::LinearHz: return "linearHz";
        case PitchSweepMode::Exponential: return "exponential";
    }
    return "exponential";
}

Waveform waveformFromString(const std::string& value) {
    if (value == "square") return Waveform::Square;
    if (value == "pulse") return Waveform::Pulse;
    if (value == "triangle") return Waveform::Triangle;
    if (value == "saw") return Waveform::Saw;
    throw std::invalid_argument("Waveform non riconosciuta: " + value);
}

OscillatorQuality qualityFromString(const std::string& value) {
    if (value == "raw") return OscillatorQuality::Raw;
    if (value == "bandLimited") return OscillatorQuality::BandLimited;
    throw std::invalid_argument("OscillatorQuality non riconosciuta: " + value);
}

PitchSweepMode sweepModeFromString(const std::string& value) {
    if (value == "linearHz") return PitchSweepMode::LinearHz;
    if (value == "exponential") return PitchSweepMode::Exponential;
    throw std::invalid_argument("PitchSweepMode non riconosciuta: " + value);
}

Json pitchToJson(const PitchParams& pitch) {
    return Json{
        {"startHz", pitch.startHz},
        {"endHz", pitch.endHz},
        {"sweepCurve", pitch.sweepCurve},
        {"sweepMode", toString(pitch.sweepMode)},
        {"vibratoDepthSemitones", pitch.vibratoDepthSemitones},
        {"vibratoRateHz", pitch.vibratoRateHz},
        {"arpeggioSemitones", pitch.arpeggioSemitones},
        {"arpeggioRateHz", pitch.arpeggioRateHz}
    };
}

PitchParams pitchFromJson(const Json& json) {
    PitchParams pitch;
    pitch.startHz = json.at("startHz").get<float>();
    pitch.endHz = json.at("endHz").get<float>();
    pitch.sweepCurve = json.value("sweepCurve", 1.0f);
    pitch.sweepMode = sweepModeFromString(json.value("sweepMode", "exponential"));
    pitch.vibratoDepthSemitones = json.value("vibratoDepthSemitones", 0.0f);
    pitch.vibratoRateHz = json.value("vibratoRateHz", 0.0f);
    pitch.arpeggioSemitones = json.value("arpeggioSemitones", 0.0f);
    pitch.arpeggioRateHz = json.value("arpeggioRateHz", 0.0f);
    return pitch;
}

Json voiceToJson(const VoiceParams& voice) {
    return Json{
        {"enabled", voice.enabled},
        {"waveform", toString(voice.waveform)},
        {"quality", toString(voice.quality)},
        {"gain", voice.gain},
        {"detuneSemitones", voice.detuneSemitones},
        {"dutyStart", voice.dutyStart},
        {"dutyEnd", voice.dutyEnd},
        {"pitch", pitchToJson(voice.pitch)}
    };
}

VoiceParams voiceFromJson(const Json& json) {
    VoiceParams voice;
    voice.enabled = json.value("enabled", true);
    voice.waveform = waveformFromString(json.value("waveform", "square"));
    voice.quality = qualityFromString(json.value("quality", "bandLimited"));
    voice.gain = json.value("gain", 1.0f);
    voice.detuneSemitones = json.value("detuneSemitones", 0.0f);
    voice.dutyStart = json.value("dutyStart", 0.5f);
    voice.dutyEnd = json.value("dutyEnd", 0.5f);
    voice.pitch = pitchFromJson(json.at("pitch"));
    return voice;
}

Json recipeToJson(const SfxRecipe& recipe) {
    return Json{
        {"schemaVersion", recipe.schemaVersion},
        {"generatorVersion", recipe.generatorVersion},
        {"durationSeconds", recipe.durationSeconds},
        {"masterGain", recipe.masterGain},
        {"amplitude", {
            {"attackSeconds", recipe.amplitude.attackSeconds},
            {"decaySeconds", recipe.amplitude.decaySeconds},
            {"sustainLevel", recipe.amplitude.sustainLevel},
            {"releaseSeconds", recipe.amplitude.releaseSeconds}
        }},
        {"primaryVoice", voiceToJson(recipe.primaryVoice)},
        {"secondaryVoice", voiceToJson(recipe.secondaryVoice)},
        {"noise", {
            {"enabled", recipe.noise.enabled},
            {"gain", recipe.noise.gain},
            {"clock", pitchToJson(recipe.noise.clock)}
        }},
        {"bitCrusher", {
            {"enabled", recipe.bitCrusher.enabled},
            {"quantizationBits", recipe.bitCrusher.quantizationBits},
            {"reductionRateHz", recipe.bitCrusher.reductionRateHz}
        }},
        {"filter", {
            {"lowPassHz", recipe.filter.lowPassHz},
            {"dcBlockEnabled", recipe.filter.dcBlockEnabled},
            {"dcBlockCutoffHz", recipe.filter.dcBlockCutoffHz}
        }},
        {"randomSeed", recipe.randomSeed}
    };
}

SfxRecipe recipeFromJson(const Json& json) {
    SfxRecipe recipe;
    recipe.schemaVersion = json.at("schemaVersion").get<std::uint32_t>();
    recipe.generatorVersion = json.at("generatorVersion").get<std::uint32_t>();
    recipe.durationSeconds = json.at("durationSeconds").get<float>();
    recipe.masterGain = json.value("masterGain", 0.75f);

    const Json& amplitude = json.at("amplitude");
    recipe.amplitude.attackSeconds = amplitude.value("attackSeconds", 0.005f);
    recipe.amplitude.decaySeconds = amplitude.value("decaySeconds", 0.050f);
    recipe.amplitude.sustainLevel = amplitude.value("sustainLevel", 0.65f);
    recipe.amplitude.releaseSeconds = amplitude.value("releaseSeconds", 0.080f);

    recipe.primaryVoice = voiceFromJson(json.at("primaryVoice"));
    recipe.secondaryVoice = voiceFromJson(json.at("secondaryVoice"));

    const Json& noise = json.at("noise");
    recipe.noise.enabled = noise.value("enabled", false);
    recipe.noise.gain = noise.value("gain", 0.0f);
    recipe.noise.clock = pitchFromJson(noise.at("clock"));

    const Json& bitCrusher = json.at("bitCrusher");
    recipe.bitCrusher.enabled = bitCrusher.value("enabled", true);
    recipe.bitCrusher.quantizationBits = bitCrusher.value("quantizationBits", 8);
    recipe.bitCrusher.reductionRateHz = bitCrusher.value("reductionRateHz", 11025.0f);

    const Json& filter = json.at("filter");
    recipe.filter.lowPassHz = filter.value("lowPassHz", 0.0f);
    recipe.filter.dcBlockEnabled = filter.value("dcBlockEnabled", true);
    recipe.filter.dcBlockCutoffHz = filter.value("dcBlockCutoffHz", 18.0f);

    recipe.randomSeed = json.value("randomSeed", 0x00C0FFEEu);
    return recipe;
}

} // namespace

Result<std::string> serializeRecipeJson(
    const GeneratedSfxDef& definition,
    int indentation
) {
    try {
        Json json{
            {"schemaVersion", definition.schemaVersion},
            {"id", definition.id},
            {"name", definition.name},
            {"recipe", recipeToJson(definition.recipe)},
            {"output", {
                {"assetId", definition.outputAssetId},
                {"path", definition.outputPath}
            }},
            {"generatedRecipeFingerprint", definition.generatedRecipeFingerprint}
        };
        return Result<std::string>::success(json.dump(indentation));
    } catch (const std::exception& exception) {
        return Result<std::string>::failure(
            ErrorCode::InvalidRecipe,
            std::string("Serializzazione JSON fallita: ") + exception.what()
        );
    }
}

Result<GeneratedSfxDef> deserializeRecipeJson(std::string_view jsonText) {
    try {
        const Json json = Json::parse(jsonText.begin(), jsonText.end());

        GeneratedSfxDef definition;
        definition.schemaVersion = json.at("schemaVersion").get<std::uint32_t>();
        definition.id = json.at("id").get<std::string>();
        definition.name = json.at("name").get<std::string>();
        definition.recipe = recipeFromJson(json.at("recipe"));

        const Json& output = json.at("output");
        definition.outputAssetId = output.at("assetId").get<std::string>();
        definition.outputPath = output.at("path").get<std::string>();
        definition.generatedRecipeFingerprint =
            json.value("generatedRecipeFingerprint", std::string{});
        // Legacy projects: linked output without fingerprint → assume in sync.
        if (!definition.outputAssetId.empty()
            && definition.generatedRecipeFingerprint.empty()) {
            definition.generatedRecipeFingerprint = recipeFingerprint(definition.recipe);
        }

        return Result<GeneratedSfxDef>::success(std::move(definition));
    } catch (const std::exception& exception) {
        return Result<GeneratedSfxDef>::failure(
            ErrorCode::InvalidRecipe,
            std::string("Deserializzazione JSON fallita: ") + exception.what()
        );
    }
}

std::string recipeFingerprint(const SfxRecipe& recipe) {
    try {
        return recipeToJson(recipe).dump(-1);
    } catch (...) {
        return {};
    }
}

} // namespace artcade::sfx
