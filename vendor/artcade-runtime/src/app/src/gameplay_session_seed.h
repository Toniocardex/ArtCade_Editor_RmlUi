#pragma once

#include <cstdint>

// ADR-0037: session-level entropy for LogicRuntime's per-scope RNG seed.
// GameplaySession (the composition root) calls make() once per session and
// passes the result into LogicRuntime's constructor; LogicRuntime itself
// never reads the clock or OS entropy directly.
namespace ArtCade::GameplaySessionSeed {

/**
 * One session seed. Guaranteed distinct from every other call in the same
 * process until the internal 32-bit counter wraps (see .cpp). Not
 * cryptographic — only meant to keep consecutive Play sessions from
 * starting the Logic Board RNG at the same point.
 */
uint32_t make() noexcept;

}  // namespace ArtCade::GameplaySessionSeed
