#include "artcade/sfx/raylib_preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <raylib.h>

namespace artcade::sfx {

struct RaylibPreview::Impl {
    Sound sound{};
    bool loaded = false;
};

RaylibPreview::~RaylibPreview() {
    unload();
    delete impl_;
}

RaylibPreview::RaylibPreview(RaylibPreview&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}

RaylibPreview& RaylibPreview::operator=(RaylibPreview&& other) noexcept {
    if (this != &other) {
        unload();
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

Result<bool> RaylibPreview::load(const FloatAudioBuffer& audio) {
    if (!IsAudioDeviceReady()) {
        return Result<bool>::failure(
            ErrorCode::InvalidArgument,
            "Il dispositivo audio Raylib non è inizializzato."
        );
    }
    if (audio.samples.empty() || audio.sampleRate < 8000u || audio.sampleRate > 192000u ||
        audio.samples.size() > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        return Result<bool>::failure(
            ErrorCode::EmptyAudio,
            "Il buffer audio è vuoto o non valido."
        );
    }

    unload();
    if (impl_ == nullptr) {
        impl_ = new Impl{};
    }

    std::vector<std::int16_t> pcm(audio.samples.size());
    for (std::size_t index = 0; index < audio.samples.size(); ++index) {
        const float sample = audio.samples[index];
        if (!std::isfinite(sample)) {
            return Result<bool>::failure(
                ErrorCode::InvalidArgument,
                "Il buffer contiene campioni non finiti."
            );
        }
        const float clamped = std::max(-1.0f, std::min(sample, 1.0f));
        pcm[index] = static_cast<std::int16_t>(std::lround(
            clamped * static_cast<float>(std::numeric_limits<std::int16_t>::max())
        ));
    }

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(pcm.size());
    wave.sampleRate = audio.sampleRate;
    wave.sampleSize = 16u;
    wave.channels = 1u;
    wave.data = pcm.data();

    impl_->sound = LoadSoundFromWave(wave);
    // ArtCade currently vendors Raylib 4.x, where this API is named
    // IsSoundReady (Raylib 5 renamed it to IsSoundValid).
    impl_->loaded = IsSoundReady(impl_->sound);
    if (!impl_->loaded) {
        return Result<bool>::failure(
            ErrorCode::EncoderFailure,
            "Raylib non ha creato la risorsa Sound per la preview."
        );
    }

    return Result<bool>::success(true);
}

void RaylibPreview::play() {
    if (loaded()) {
        PlaySound(impl_->sound);
    }
}

void RaylibPreview::stop() {
    if (loaded()) {
        StopSound(impl_->sound);
    }
}

void RaylibPreview::unload() {
    if (impl_ != nullptr && impl_->loaded) {
        StopSound(impl_->sound);
        UnloadSound(impl_->sound);
        impl_->sound = {};
        impl_->loaded = false;
    }
}

bool RaylibPreview::loaded() const noexcept {
    return impl_ != nullptr && impl_->loaded;
}

bool RaylibPreview::playing() const noexcept {
    return loaded() && IsSoundPlaying(impl_->sound);
}

} // namespace artcade::sfx
