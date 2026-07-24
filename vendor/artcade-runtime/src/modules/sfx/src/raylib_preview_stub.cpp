#include "artcade/sfx/raylib_preview.hpp"

#include <utility>

namespace artcade::sfx {

struct RaylibPreview::Impl {};

RaylibPreview::~RaylibPreview() {
    delete impl_;
}

RaylibPreview::RaylibPreview(RaylibPreview&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}

RaylibPreview& RaylibPreview::operator=(RaylibPreview&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

Result<bool> RaylibPreview::load(const FloatAudioBuffer&) {
    return Result<bool>::failure(
        ErrorCode::EncoderUnavailable,
        "Preview Raylib non compilata. Abilita ARTCADE_SFX_ENABLE_RAYLIB."
    );
}

void RaylibPreview::play() {}
void RaylibPreview::stop() {}
void RaylibPreview::unload() {}
bool RaylibPreview::loaded() const noexcept { return false; }
bool RaylibPreview::playing() const noexcept { return false; }

} // namespace artcade::sfx
