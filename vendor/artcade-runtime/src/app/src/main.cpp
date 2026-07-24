#include "../include/app.h"

#ifdef ARTCADE_WASM
namespace {
ArtCade::Application& wasmApplication() {
    // Emscripten returns from main() after emscripten_set_main_loop; a stack
    // Application would be destroyed while editor ccalls still run.
    static ArtCade::Application app;
    return app;
}
} // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef ARTCADE_WASM
    return wasmApplication().run(argc, argv);
#else
    return ArtCade::Application{}.run(argc, argv);
#endif
}
