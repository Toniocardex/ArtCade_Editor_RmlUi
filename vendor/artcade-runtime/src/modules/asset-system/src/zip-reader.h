#pragma once
// =============================================================================
// zip-reader.h — Minimal ZIP extractor (Phase 17)
//
// Supports STORE (method 0) and DEFLATE (method 8).
// DEFLATE decompression uses sinflate() already compiled inside raylib (rcore.c).
// =============================================================================
#include <string>

namespace ArtCade {

// Extract all files from a ZIP archive to destDir, creating sub-directories.
// Returns true on success, false on any read / decompression error.
bool zipExtractAll(const std::string& zipPath, const std::string& destDir);

} // namespace ArtCade
