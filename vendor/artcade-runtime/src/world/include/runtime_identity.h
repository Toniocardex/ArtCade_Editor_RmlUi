#pragma once

// ADR-0053 / ADR-0052: compile-time identity for Play (in-process) and export
// template parity. ARTCADE_RUNTIME_BUILD_ID is injected by CMake (git short SHA
// or "local").

#ifndef ARTCADE_RUNTIME_BUILD_ID
#define ARTCADE_RUNTIME_BUILD_ID "local"
#endif

namespace ArtCade {

inline constexpr const char* kPlatformerGroundSupportContract = "ADR-0052";

inline const char* runtimeBuildId() {
    return ARTCADE_RUNTIME_BUILD_ID;
}

inline const char* platformerGroundSupportContract() {
    return kPlatformerGroundSupportContract;
}

} // namespace ArtCade
