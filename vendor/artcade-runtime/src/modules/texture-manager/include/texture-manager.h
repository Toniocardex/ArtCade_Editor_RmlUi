#pragma once

#include "../../../core/module.h"
#include <string>
#include <cstdint>

namespace ArtCade::Modules {

// Raylib-agnostic texture metadata (no raylib.h needed here)
struct TextureInfo {
    unsigned int gpuId   = 0;
    int          width   = 0;
    int          height  = 0;
};

/**
 * TextureManager — GPU texture cache with reference counting.
 *
 * All Raylib types are hidden in the .cpp via Pimpl — no raylib.h leaks
 * into translation units that only include this header.
 *
 * load()    : loads from disk (or returns cached handle), bumps ref count.
 * release() : decrements ref count; unloads GPU texture when it hits 0.
 * getInfo() : fills a TextureInfo with gpu id + dimensions (returns false
 *             if the handle is invalid).
 *
 * Missing files return a 1×1 magenta placeholder and never crash.
 */
class TextureManager final : public IModule {
public:
    TextureManager();
    ~TextureManager();

    bool init()     override;
    void shutdown() override;

    // Load (or ref-count bump); returns a numeric handle (0 = invalid)
    uint32_t load(const std::string& path);

    // Decrement ref count; unloads GPU texture when count hits 0
    void release(uint32_t handle);
    void release(const std::string& path);

    // Query texture metadata; returns false if handle is invalid
    bool getInfo(uint32_t handle, TextureInfo& out) const;

    // Path → handle (0 if not loaded)
    uint32_t    handleOf(const std::string& path) const;

    // Total distinct textures currently in GPU memory
    std::size_t loadedCount() const;

    // Force-unload everything (called by shutdown)
    void unloadAll();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace ArtCade::Modules
