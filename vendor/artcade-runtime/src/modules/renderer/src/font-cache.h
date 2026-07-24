#pragma once

#include <raylib.h>
#include <string>
#include <unordered_map>

/** Path-keyed TTF/OTF cache for DrawTextEx (editor memory upload + native lazy load). */
class FontCache {
public:
    bool registerFromMemory(const std::string& path,
                            const unsigned char* data, int len,
                            const std::string& ext,
                            int baseSize);

    void invalidate(const std::string& path);
    void unloadAll();

    /** Returns nullptr if unknown; tries LoadFont from disk when not in cache. */
    const Font* get(const std::string& path);

private:
    struct Entry {
        Font font{};
        bool loaded = false;
    };

    std::unordered_map<std::string, Entry> byPath_;
};
