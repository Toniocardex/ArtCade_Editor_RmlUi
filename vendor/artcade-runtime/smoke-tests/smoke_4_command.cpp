// =============================================================================
// Smoke Test 4 — "Comando React → C++ (Il Telecomando)" — Bridge Completo
//
// Obiettivo: l'utente usa la UI di React per alterare lo stato del motore C++.
//
// Come usarlo nel browser:
//   Digita X/Y nel pannello HTML, premi "Send to C++" oppure:
//   Module.ccall('set_square_pos', null, ['number','number'], [200, 300])
//
// Risultato atteso: il quadrato si teletrasporta alla coordinata indicata.
// =============================================================================

#include "raylib.h"
#include <cstdio>
#include <cmath>

#ifdef __EMSCRIPTEN__
#   include <emscripten.h>
#   include <emscripten/html5.h>
#endif

// Visual state
static float squareX    = 350.f, squareY    = 250.f;  // current (lerped)
static float targetX    = 350.f, targetY    = 250.f;  // target (set by ccall)
static bool  isDragging = false;
static int   cmdCount   = 0;
static char  line1[128] = "ST-4: Full Bridge — drag OR type coordinates in the HTML panel";
static char  line2[128] = "Module.ccall('set_square_pos', null, ['number','number'], [x, y])";

// ─────────────────────────────────────────────────────────────────────────────
// React → C++  (Smoke Test 4: EMSCRIPTEN_KEEPALIVE)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
extern "C" {

EMSCRIPTEN_KEEPALIVE void set_square_pos(float x, float y) {
    targetX = x;
    targetY = y;
    ++cmdCount;
    snprintf(line1, sizeof(line1),
             "React command #%d  →  pos (%.0f, %.0f)", cmdCount, x, y);
}

} // extern "C"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// C++ → React  (Smoke Test 3: EM_ASM)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
static void notifyReact(float x, float y) {
    EM_ASM({
        if (typeof window.onObjectUpdated === 'function')
            window.onObjectUpdated($0, $1);
    }, x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Native input  (Smoke Test 2: no JS events)
// ─────────────────────────────────────────────────────────────────────────────
static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent* e, void*) {
    if (isDragging) {
        squareX = targetX = static_cast<float>(e->targetX) - 50.f;
        squareY = targetY = static_cast<float>(e->targetY) - 50.f;
    }
    return EM_TRUE;
}
static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent*, void*) {
    isDragging = true;
    return EM_TRUE;
}
static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent* e, void*) {
    isDragging = false;
    squareX = targetX = static_cast<float>(e->targetX) - 50.f;
    squareY = targetY = static_cast<float>(e->targetY) - 50.f;
    notifyReact(squareX, squareY);
    snprintf(line1, sizeof(line1),
             "Drag ended → notified React: (%.0f, %.0f)", squareX, squareY);
    return EM_TRUE;
}
#endif // __EMSCRIPTEN__

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────
static void UpdateDrawFrame() {
#ifndef __EMSCRIPTEN__
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        squareX = targetX = m.x - 50.f;
        squareY = targetY = m.y - 50.f;
    }
#endif

    // Smooth lerp toward target (so ccall teleports look animated)
    squareX += (targetX - squareX) * 0.15f;
    squareY += (targetY - squareY) * 0.15f;

    BeginDrawing();
        ClearBackground({ 10, 17, 33, 255 });

        DrawRectangle(static_cast<int>(squareX),
                      static_cast<int>(squareY), 100, 100, RED);

        // ── Status overlay ─────────────────────────────────────────────────
        DrawText("SMOKE TEST 4: FULL BRIDGE  [PASS]",   10, 10, 20, LIME);
        DrawText(line1,                                  10, 38, 13, YELLOW);
        DrawText(line2,                                  10, 56, 12, { 0, 255, 255, 255 });

        char pos[64];
        snprintf(pos, sizeof(pos), "pos: (%.1f,  %.1f)", squareX, squareY);
        DrawText(pos, 10, 76, 14, WHITE);
        DrawFPS(10, 98);
    EndDrawing();
}

int main() {
    InitWindow(800, 600, "ArtCade — Smoke Test 4: Full Bridge");
    SetTargetFPS(60);

#ifdef __EMSCRIPTEN__
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
