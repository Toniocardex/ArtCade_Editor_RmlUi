// =============================================================================
// Smoke Test 1 — "Il Cuore che Batte" (Rendering puro)
//
// Obiettivo: React riesce a istanziare il modulo WASM compilato da C++ e
//            Raylib riesce a renderizzare all'interno della pagina.
//
// Risultato atteso: quadrato rosso fisso al centro dello schermo, nessun lag.
// =============================================================================

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#   include <emscripten.h>
#endif

static void UpdateDrawFrame() {
    BeginDrawing();
        ClearBackground({ 10, 17, 33, 255 });     // ArtCade Slate Night

        // The test subject: red square in the centre
        DrawRectangle(350, 250, 100, 100, RED);

        // Status overlay
        DrawText("SMOKE TEST 1: RAYLIB OK",       10, 10, 20, GREEN);
        DrawText("C++ + Raylib rendering in WASM", 10, 36, 14, YELLOW);
        DrawFPS(10, 60);
    EndDrawing();
}

int main() {
    InitWindow(800, 600, "ArtCade — Smoke Test 1: Render");
    SetTargetFPS(60);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
    CloseWindow();
#endif

    return 0;
}
