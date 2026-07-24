#pragma once

#include "artcade/sfx/result.hpp"
#include "artcade/sfx/types.hpp"

namespace artcade::sfx {

class SfxSynthesizer final {
public:
    [[nodiscard]] Result<FloatAudioBuffer> render(
        const SfxRecipe& recipe,
        const RenderSettings& settings = {}
    ) const;

    [[nodiscard]] static Result<bool> validate(
        const SfxRecipe& recipe,
        const RenderSettings& settings = {}
    );
};

} // namespace artcade::sfx
