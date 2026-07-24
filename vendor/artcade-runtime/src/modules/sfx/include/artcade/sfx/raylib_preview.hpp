#pragma once

#include "artcade/sfx/result.hpp"
#include "artcade/sfx/types.hpp"

namespace artcade::sfx {

// Requires an initialized Raylib audio device. Use from the same thread that
// owns Raylib audio resources.
class RaylibPreview final {
public:
    RaylibPreview() = default;
    ~RaylibPreview();

    RaylibPreview(const RaylibPreview&) = delete;
    RaylibPreview& operator=(const RaylibPreview&) = delete;

    RaylibPreview(RaylibPreview&& other) noexcept;
    RaylibPreview& operator=(RaylibPreview&& other) noexcept;

    [[nodiscard]] Result<bool> load(const FloatAudioBuffer& audio);
    void play();
    void stop();
    void unload();

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool playing() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace artcade::sfx
