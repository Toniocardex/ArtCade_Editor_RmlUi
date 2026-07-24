#pragma once

#include <raylib.h>
#include <string>
#include <unordered_map>
#include <cstdint>

// Internal texture cache for the Renderer.
// Not part of the public API — only included by renderer.cpp and texture-cache.cpp.
class TextureCache {
public:
    // Load from disk; returns handle (>0). Missing files get a magenta placeholder.
    uint32_t load(const std::string& path);

    // Register/replace a texture decoded from an in-memory image buffer
    // (e.g. a tileset uploaded from the editor that is not in the VFS).
    // `ext` is the raylib file-type hint, e.g. ".png". Returns handle (>0),
    // or 0 if the buffer could not be decoded.
    uint32_t registerFromMemory(const std::string& path,
                                const unsigned char* data, int len,
                                const std::string& ext);

    // Release a single texture by handle or path.
    void unload(uint32_t handle);
    void unloadByPath(const std::string& path);
    void unloadAll();

    // Non-owning access; returns nullptr if handle/path is unknown.
    const Texture2D* get(uint32_t handle)         const;
    const Texture2D* getByPath(const std::string& path) const;

    bool     isLoaded (const std::string& path)   const;
    uint32_t handleOf (const std::string& path)   const;

private:
    struct Entry { Texture2D tex{}; std::string path; };

    std::unordered_map<uint32_t, Entry>       byHandle_;
    std::unordered_map<std::string, uint32_t> pathToHandle_;
    uint32_t next_ = 1;
};
