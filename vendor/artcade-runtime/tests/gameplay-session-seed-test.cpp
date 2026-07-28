// ADR-0037: GameplaySessionSeed::make() must be testable independently of
// GameplaySession/LogicRuntime — this is the only consumer that verifies it.
#include "app/src/gameplay_session_seed.h"

#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <vector>

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; \
    std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

using ArtCade::GameplaySessionSeed::make;

/**
 * Guaranteed by construction (process seed + counter + bijective
 * permutation, see gameplay_session_seed.cpp) — not a probabilistic
 * assertion about the clock or std::random_device.
 */
static void testConsecutiveCallsDiffer() {
    const uint32_t first = make();
    const uint32_t second = make();
    CHECK(first != second);
}

static void testManyConsecutiveCallsAreAllDistinct() {
    std::unordered_set<uint32_t> seen;
    for (int i = 0; i < 1000; ++i) {
        CHECK(seen.insert(make()).second);
    }
}

int main() {
    testConsecutiveCallsDiffer();
    testManyConsecutiveCallsAreAllDistinct();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
