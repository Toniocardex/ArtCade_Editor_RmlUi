#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "artcade/sfx/vorbis_encoder.hpp"
#include "artcade/sfx/wav_encoder.hpp"

#include <iostream>

int main() {
    using namespace artcade::sfx;

    SfxRecipe recipe = presets::laser();
    recipe.randomSeed = 123456u;

    SfxSynthesizer synthesizer;
    auto rendered = synthesizer.render(recipe);
    if (!rendered) {
        std::cerr << "Render fallito: " << rendered.error().message << '\n';
        return 1;
    }

    WavEncoder wavEncoder;
    auto wavResult = wavEncoder.encode(rendered.value(), "laser.wav");
    if (!wavResult) {
        std::cerr << "WAV fallito: " << wavResult.error().message << '\n';
        return 1;
    }

    VorbisEncoder vorbisEncoder;
    auto oggResult = vorbisEncoder.encode(rendered.value(), "laser.ogg");
    if (!oggResult) {
        std::cout << "OGG non generato: " << oggResult.error().message << '\n';
    }

    std::cout << "Creato laser.wav con "
              << rendered.value().samples.size()
              << " campioni.\n";
    return 0;
}
