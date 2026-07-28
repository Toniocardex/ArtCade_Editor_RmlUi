#include "gameplay_session_seed.h"

#include <atomic>
#include <chrono>
#include <random>

namespace ArtCade::GameplaySessionSeed {
namespace {

/**
 * MurmurHash3 finalizer — a bijection on uint32_t, so distinct inputs are
 * guaranteed to stay distinct after mixing.
 */
uint32_t permuteSeed(uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

/**
 * Best-effort process differentiator, computed once. std::random_device
 * construction can throw on some platforms/builds; the steady_clock
 * reading alone is still enough to tell separate process runs apart.
 */
uint32_t makeProcessSeedMaterial() noexcept {
    uint64_t material = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    try {
        std::random_device device;
        material ^= static_cast<uint64_t>(device()) << 32;
        material ^= static_cast<uint64_t>(device());
    } catch (...) {
        // The clock remains the best-effort process differentiator.
    }

    return static_cast<uint32_t>(material ^ (material >> 32));
}

}  // namespace

uint32_t make() noexcept {
    static const uint32_t processSeed = makeProcessSeedMaterial();
    static std::atomic<uint32_t> sequence{0};
    // processSeed differentiates separate process runs; ordinal
    // differentiates sessions within this process and cannot collide with
    // itself before a uint32_t wraps. permuteSeed is a bijection, so it
    // cannot introduce a new collision between distinct ordinals.
    const uint32_t ordinal = sequence.fetch_add(1, std::memory_order_relaxed);
    return permuteSeed(processSeed + ordinal);
}

}  // namespace ArtCade::GameplaySessionSeed
