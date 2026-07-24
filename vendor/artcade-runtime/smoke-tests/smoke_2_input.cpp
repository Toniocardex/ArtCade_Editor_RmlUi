// =============================================================================
// Smoke Test 2 — "Input Nativo (Il Bypass JS)"
//
// Obiettivo: il C++ legge il mouse NATIVAMENTE tramite emscripten/html5.h,
//            senza passare per props/state di React.
//
// Risultato atteso: il quadrato segue il cursore in tempo reale.
//                  Il tab "Performance" del browser NON deve mostrare
//                  attività JS causata dal movimento del mouse.
// =============================================================================

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#   include <emscripten.h>
#   include <emscripten/html5.h>
#endif

static float squareX = 350.f, squareY = 250.f;
static bool  isDragging = false;

// ── Native input callbacks (WASM-only) ───────────────────────────────────────
#ifdef __EMSCRIPTEN__

static EM_BOOL onMouseMove(int /*type*/,
                           const EmscriptenMouseEvent* e,
                           void* /*ud*/)
{
    if (isDragging) {
        squareX = static_cast<float>(e->targetX) - 50.f;
        squareY = static_cast<float>(e->targetY) - 50.f;
    }
    return EM_TRUE;  // consumed — no JS event fires
}

static EM_BOOL onMouseDown(int /*type*/,
                           const EmscriptenMouseEvent* /*e*/,
                           void* /*ud*/)
{
    isDragging = true;
    return EM_TRUE;
}

static EM_BOOL onMouseUp(int /*type*/,
                         const EmscriptenMouseEvent* /*e*/,
                         void* /*ud*/)
{
    isDragging = false;
    return EM_TRUE;
}

#endif // __EMSCRIPTEN__

// ── Render ───────────────────────────────────────────────────────────────────
static void UpdateDrawFrame() {
#ifndef __EMSCRIPTEN__
    // Native fallback: use Raylib input API
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        squareX = m.x - 50.f;
        squareY = m.y - 50.f;
    }
#endif

    BeginDrawing();
        ClearBackground({ 10, 17, 33, 255 });
        DrawRectangle(static_cast<int>(squareX),
                      static_cast<int>(squareY), 100, 100, RED);
        DrawText("SMOKE TEST 2: NATIVE INPUT",               10, 10, 20, GREEN);
        DrawText("Drag the square — zero JS mousemove events!", 10, 36, 14, YELLOW);
        DrawFPS(10, 60);
    EndDrawing();
}

int main() {
    InitWindow(800, 600, "ArtCade — Smoke Test 2: Native Input");
    SetTargetFPS(60);

#ifdef __EMSCRIPTEN__
    // Register callbacks BEFORE entering the main loop
    emscripten_set_mousemove_callback("#artcade-canvas", nullptr, 1, onMouseMove);
    emscripten_set_mousedown_callback("#artcade-canvas", nullptr, 1, onMouseDown);
    emscripten_set_mouseup_callback  ("#artcade-canvas", nullptr, 1, onMouseUp  );
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
    CloseWindow();
#endif

    return 0;
}
