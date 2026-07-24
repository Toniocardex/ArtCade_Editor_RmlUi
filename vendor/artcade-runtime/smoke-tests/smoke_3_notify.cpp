// =============================================================================
// Smoke Test 3 — "Notifica C++ → React (Event Driven)"
//
// Obiettivo: quando l'utente rilascia il mouse (drop del quadrato),
//            C++ notifica React tramite EM_ASM → window.onObjectUpdated.
//            React aggiorna un <input> testuale nel pannello proprietà.
//
// Risultato atteso:
//   - Durante il trascinamento: nessuna notifica verso React.
//   - Al rilascio: il campo "Posizione" nella UI HTML si aggiorna istantaneamente.
// =============================================================================

#include "raylib.h"
#include <cstdio>

#ifdef __EMSCRIPTEN__
#   include <emscripten.h>
#   include <emscripten/html5.h>
#endif

static float squareX = 350.f, squareY = 250.f;
static bool  isDragging = false;
static char  statusBuf[128] = "Drag the square, release to notify React";

// ── C++ → React notification ─────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
static void notifyReact(float x, float y) {
    // Smoke Test 3: EM_ASM calls the window.onObjectUpdated global set by the HTML shell.
    // This is the ONLY moment React is informed — NOT during drag.
    EM_ASM({
        if (typeof window.onObjectUpdated === 'function')
            window.onObjectUpdated($0, $1);
    }, x, y);
}

static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent* e, void*) {
    if (isDragging) {
        // C++ moves the square every frame — React has NO idea yet.
        squareX = static_cast<float>(e->targetX) - 50.f;
        squareY = static_cast<float>(e->targetY) - 50.f;
    }
    return EM_TRUE;
}

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent*, void*) {
    isDragging = true;
    return EM_TRUE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent* e, void*) {
    isDragging = false;
    squareX = static_cast<float>(e->targetX) - 50.f;
    squareY = static_cast<float>(e->targetY) - 50.f;

    // Single Source of Truth: notify React only when drag ends
    notifyReact(squareX, squareY);
    snprintf(statusBuf, sizeof(statusBuf),
             "Notified React: X=%.0f  Y=%.0f", squareX, squareY);
    return EM_TRUE;
}
#endif // __EMSCRIPTEN__

// ── Render ───────────────────────────────────────────────────────────────────
static void UpdateDrawFrame() {
#ifndef __EMSCRIPTEN__
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        squareX = m.x - 50.f;
        squareY = m.y - 50.f;
        isDragging = true;
    } else if (isDragging) {
        isDragging = false;
        snprintf(statusBuf, sizeof(statusBuf),
                 "Released at X=%.0f Y=%.0f  (native — no EM_ASM)", squareX, squareY);
    }
#endif

    BeginDrawing();
        ClearBackground({ 10, 17, 33, 255 });
        DrawRectangle(static_cast<int>(squareX),
                      static_cast<int>(squareY), 100, 100, RED);
        DrawText("SMOKE TEST 3: C++ → React  (EM_ASM)",  10, 10, 20, GREEN);
        DrawText(statusBuf,                               10, 36, 13, YELLOW);
        DrawText("React UI updates ONLY on mouse-up",     10, 56, 12, { 0, 255, 255, 255 });
        DrawFPS(10, 80);
    EndDrawing();
}

int main() {
    InitWindow(800, 600, "ArtCade — Smoke Test 3: C++→React Notify");
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
