// presentation-input-builder-test.cpp — scene authority for presentation geometry (PR7).

#include "../src/modules/presentation/include/presentation_input_builder.h"

#include <cstdio>
#include <cstdlib>

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

int main() {
    ArtCade::SceneDef scene{};
    scene.worldSize = { 2048.f, 320.f };
    scene.viewportSize = { 800.f, 600.f };

    ArtCade::Presentation::PresentationSimulationInputs sim{};

    const auto inputs = ArtCade::Presentation::presentation_build_inputs(
        &scene, sim);
    expect(inputs.worldWidth == 2048., "scene world width authoritative");
    expect(inputs.logicalWidth == 800., "scene viewport authoritative");

    scene.worldSize.x = 128.f;
    expect(inputs.worldWidth == 2048., "built inputs immutable after scene mutation");

    const auto fallback = ArtCade::Presentation::presentation_build_inputs(
        nullptr, sim);
    expect(fallback.worldWidth == 512., "null scene uses project defaults");

    std::puts("presentation_input_builder_test: all passed");
    return 0;
}

