# RmlUi Native Editor — Architectural Plan

Status: **spike / phase planning**
Scope: isolated, parallel native C++ editor target evaluated against the current
React + WebView + WASM editor. The web editor stays buildable and untouched.

This document is the mandatory pre-implementation audit (prompt §15) plus the
implementation strategy. It records *what the repository actually is*, not what
the prompt assumes it is.

---

## 1. Audit — what the repository actually contains

### 1.1 Native target, main loop, ownership

| Concern | Reality in repo | File |
| --- | --- | --- |
| Native entry point | `int main()` → `Application{}.run()` | `runtime-cpp/src/app/src/main.cpp` |
| Top-level orchestrator | `class ArtCade::Application` owns all modules, wires `EngineContext`, drives the loop | `runtime-cpp/src/app/include/app.h` |
| Main loop | fixed-timestep accumulator; `mainLoop()` native `while`, `emscripten_set_main_loop` on WASM | `runtime-cpp/src/app/src/app_loop.cpp` |
| **Window ownership** | `RaylibSurface` — the *only* caller of `InitWindow` / `CloseWindow` / `BeginDrawing` / `EndDrawing` / `WindowShouldClose` | `runtime-cpp/src/modules/renderer/src/raylib_surface.cpp` |
| Graphics context | Raylib **5.0** (vendored `libs/raylib`), desktop OpenGL via `rlgl`; all raylib types confined behind a Pimpl in `renderer.cpp` | `libs/raylib/src/raylib.h` (`RAYLIB_VERSION "5.0"`) |
| Build (native) | Ninja + MSVC via `VsDevCmd`, **static CRT `/MT`** (`CMAKE_MSVC_RUNTIME_LIBRARY`), output `build-native/src/app/game.exe` | `runtime-cpp/build_native.bat`, `runtime-cpp/CMakeLists.txt` |
| Build (web) | `build_wasm.bat` → `editor/public/runtime/game.{js,wasm}` | `runtime-cpp/build_wasm.bat` |

`InitWindow` is called exactly once, from `RaylibSurface::open`. There is **one
window, one GL context, one render thread, one loop** today. The split the
prompt asks for — *window ownership vs scene rendering* — already exists:
`RaylibSurface` owns the window; `Renderer` owns scene drawing on top of it.

### 1.2 Authoring model (the "ProjectDocument" the prompt asks for)

The root authoring data model **already exists** and is plain data:

- `struct ProjectDoc` — `runtime-cpp/src/core/types.h:501`
  - `objectTypes`, `entities`, `scenes` (`SceneId → SceneDef`), `layers`,
    `tilePalette`, `tilesets`, `imageAssets`, `world` (`WorldSettings`),
    `globalVariables`, `activeSceneId`, …
- `struct SceneDef` (`types.h:405`) — `worldSize`, `viewportSize`, `cameraStart`,
  `backgroundColor`, `instances` (`SceneInstanceDef`), `tilemapLayers`, …
- `struct SceneInstanceDef` (`types.h:395`) — `id`, `objectTypeId`,
  `instanceName`, `transform`, `visible`, `layerId`, overrides.
- `struct EntityDef` (`types.h:301`) — full component bag.

There is **no single authoring authority object** today. On the web side the
authority is the React/TypeScript store; the runtime receives it through the
WASM bridge and projects it into `SceneManager` + `RuntimeEntityGateway`.

**Decision:** the native editor introduces one authority, `ProjectDocument`,
that *owns the existing `ProjectDoc`* (no parallel/duplicate model). Commands
mutate it; panels query it. This satisfies "one authority / no duplicate model"
(prompt §3) while reusing the canonical repo model verbatim.

### 1.3 Runtime projection — Replace / Select / Patch already exist

`RuntimeEntityGateway` (`runtime-cpp/src/modules/runtime-entity-gateway/include/runtime-entity-gateway.h`)
already exposes exactly the three operations the prompt §7 describes, in-process
and without JSON:

- **Replace** → `replaceProject(scenes, entityDefs, …)` / `registerScenes(...)`
- **Select** → switch active scene without `replaceProject` (line ~236), via
  `SceneManager::loadScene` + `syncSceneActivation`
- **Patch** → add/update one entity to a scene without `replaceProject` (line ~245)

`SceneManager` (`scene-system/include/scene-manager.h`) holds the *runtime
projection* of scenes/entityDefs and the active-scene id. This is **not** the
authoring authority — it is the read-model the viewport renders. The native
editor calls these directly; it does **not** route through the string bridge.

### 1.4 Viewport rendering — reusable as-is

- `FrameCoordinator` (`app/render/frame_coordinator.h`) builds an immutable
  `SceneFrameSnapshot` from `(activeScene, renderer, editorViewport, overlay)`.
- `EditorOverlayRenderer` (`app/render/editor-overlay-renderer.h`) draws the
  editor chrome — `drawBackdrop`, `drawGrid`, `drawSceneFrame`, `drawSelection`
  — and **already takes an explicit `EditorOverlayState`** instead of reading
  globals. House rule in that header: *"the renderer never reads `EditorAPI::s_*`
  statics."* This is exactly the discipline the prompt wants.
- Render passes already factored: `grid_pass`, `gizmo_pass`,
  `scene_background_pass`, `scene_entities_pass` under `app/render/passes`.

The native viewport reuses this stack. It needs only to supply an
`EditorOverlayState` and an active `SceneDef`, then draw inside a viewport rect.

### 1.5 Editor ↔ runtime bridge today (what we are replacing)

- `EditorAPI` (`modules/editor-api`) is the WASM bridge:
  `EMSCRIPTEN_KEEPALIVE` exports (`editor_set_transform`, `editor_load_project`,
  `editor_set_active_scene`, …) + `EM_ASM` notifications back to JS.
- The full class is compiled **only under `#ifdef __EMSCRIPTEN__`**
  (`editor-api.h:117`); on native it is a no-op stub. So a native target that
  simply does not call `EditorAPI` is automatically free of the JSON bridge,
  scene-sync service, readiness flags and fingerprints — those live on the
  TypeScript side (`editor/src/utils/runtime-sync-*.ts`,
  `runtime-fingerprint.ts`) and never run in a native process.

### 1.6 Input

`Modules::Input` (`modules/input/include/input.h`) is string-keyed
(`KeyboardEvent.code`-style: `"KeyW"`, `"Space"`). It polls Raylib each frame.
For the native editor, RmlUi must receive raw platform events *first*; the
viewport/`Input` only sees what RmlUi does not consume.

### 1.7 Tests

Home-grown harness: a `CHECK(cond)` macro counting pass/fail in `main()`,
registered with CTest (`tests/CMakeLists.txt`, `artcade_test(...)`). No gtest /
catch2. Raylib **stubs** exist (`tests/stubs/raylib.h`, `raylib-stub.cpp`) so
render-adjacent code can be tested without a GL context. The native-editor core
tests use the same pattern and need **no** stubs (the core has no GL/RmlUi
dependency).

### 1.8 Licensing

- ArtCade `LICENSE` is present at repo root (unchanged by this spike).
- Raylib (zlib/libpng) is already vendored.
- **RmlUi** is MIT. **FreeType** (RmlUi's default font engine) is FTL/GPLv2 dual
  license — FTL is permissible for this use. Both notices go in
  `THIRD_PARTY_NOTICES.md` + `licenses/` when the RmlUi target is enabled.

---

## 2. Target architecture

One authority, two paths (command vs intent), one coordinator, explicit small
invalidation, in-process Replace/Select/Patch. No sync service, no event bus, no
polling, no fingerprint, no JSON between UI and core.

```
RmlUi event
  → Panel controller (thin)
      → EditorCoordinator.execute(EditorCommand)   // mutates ProjectDocument, undoable
      → EditorCoordinator.apply(EditorIntent)      // mutates SelectionState / EditorUiState
  → EditorOperationResult { ok, EditorInvalidation, error }
  → coordinator accumulates invalidation
  → next frame: editor_ui applies invalidation → only affected panels refresh
  → viewport renders active SceneDef next frame
```

### 2.1 Module split (maps to verifiability)

| Library | Depends on | Verifiable here |
| --- | --- | --- |
| `artcade-editor-core` (static) | `artcade-core` only (ProjectDoc/types) — **no Raylib, no RmlUi** | **yes** — compiles + unit-tested via CTest |
| `artcade-editor-native` (exe) | editor-core + Raylib + RmlUi + FreeType | scaffold; GL/RmlUi compile requires the pinned fetch + MSVC build |

This split is not bureaucratic: it is the line between *deterministic authoring
logic* (the thing the prompt §30 says is the real deliverable) and *presentation*
(RmlUi + GL). The headline path — `NumberField Position X → SetEntityPositionCommand
→ ProjectDocument → Inspector|Viewport invalidation` — lives entirely in
`artcade-editor-core` and is unit-tested with spies, with zero presentation code.

### 2.2 Directory layout (prompt §23/§31 naming rules)

Placed under `runtime-cpp/src/editor-native/` so it reuses the existing CMake
module graph (`artcade-core`, `scene-system`, `renderer`, Raylib) without path
gymnastics.

```
runtime-cpp/src/editor-native/
├── CMakeLists.txt
├── model/
│   ├── project_document.{h,cpp}     # class ProjectDocument  (owns ProjectDoc)
│   ├── selection_state.h            # struct SelectionState
│   ├── editor_ui_state.h            # struct EditorUiState, EditorSceneViewState
│   └── play_session.{h,cpp}         # class PlaySession  (Play/Stop, §8)
├── commands/
│   ├── editor_invalidation.h        # enum class EditorInvalidation + bit ops
│   ├── editor_operation_result.h    # struct EditorOperationResult
│   ├── editor_command.h             # interface EditorCommand
│   ├── editor_intent.h              # SelectEntityIntent, SelectSceneIntent, …
│   ├── entity_commands.{h,cpp}      # SetEntityPositionCommand, RenameEntityCommand
│   └── scene_commands.{h,cpp}       # CreateSceneCommand, SetSceneBackgroundCommand
├── app/
│   ├── editor_coordinator.{h,cpp}   # class EditorCoordinator (the only coordinator)
│   ├── command_stack.{h,cpp}        # undo/redo stack (minimal)
│   ├── input_routing.h              # pure predicate: viewport vs UI (§19)
│   ├── editor_app.{h,cpp}           # native window + frame loop          [Raylib]
│   ├── rml_host.{h,cpp}             # RmlUi context + font + load/resize   [RmlUi]
│   ├── rml_renderer.{h,cpp}         # Rml::RenderInterface over rlgl       [RmlUi+GL]
│   └── rml_system.{h,cpp}           # Rml::SystemInterface (time/clipboard)[RmlUi]
├── ui/
│   ├── editor_ui.{h,cpp}            # owns panels, applies invalidation
│   ├── hierarchy_panel.{h,cpp}
│   ├── inspector_panel.{h,cpp}
│   └── console_panel.{h,cpp}
├── view/
│   └── scene_view.{h,cpp}           # viewport: active SceneDef → renderer rect
└── resources/ui/
    ├── editor_shell.rml
    ├── hierarchy.rml inspector.rml console.rml
    └── theme.rcss layout.rcss controls.rcss panels.rcss
```

Naming review against §31: no `manager`, `service`, `helper`, `utils`,
`bridge`, `sync`, or `handler` in any new file. `rml_` prefix only on the three
files that genuinely depend on RmlUi. Panels are toolkit-neutral names.

### 2.3 Invalidation (prompt §6)

```cpp
enum class EditorInvalidation : uint32_t {
    None=0, Hierarchy=1u<<0, Inspector=1u<<1, Viewport=1u<<2,
    Assets=1u<<3, Console=1u<<4, Toolbar=1u<<5, Project=1u<<6,
};
struct EditorOperationResult { bool ok=false; EditorInvalidation invalidation=None; std::string error; };
```

Every command/intent returns an explicit result; the coordinator OR-accumulates
invalidation and `consumeInvalidations()` returns+clears it once per frame. No
string events, no dynamic invalidation hierarchy.

### 2.4 Play / Stop (prompt §8)

`PlaySession` is created *from* `ProjectDocument` (copies the active scene's
instances into a mutable runtime list). The simulation mutates the session only;
the document is `const` to the session. Stop = destroy the session and return to
the untouched document. **No JSON reload, no scene-sync, no readiness wait.**
In the full app this PlaySession drives `RuntimeEntityGateway::replaceProject`
(Replace) once at Play start; editing afterwards is out of scope during Play.

---

## 3. Renderer strategy (prompt §18)

Chosen: **Option 2 → Option 1 hybrid.** Use the RmlUi reference **GL3 render
backend** as the starting `Rml::RenderInterface`, but drive it inside the
existing ArtCade loop and the GL context already created by `RaylibSurface`
(Raylib initialises GL 3.3 on desktop). Per frame:

```
RaylibSurface::begin_drawing()         // BeginDrawing — clears, sets up rlgl
  viewport: Renderer draws active SceneDef into the viewport rect (scissor)
  rlDrawRenderBatchActive()            // flush rlgl before handing GL to RmlUi
  rmlContext->Render()                 // RmlUi draws the shell with its own GL state
  (restore: rlgl re-binds its state on next batch)
RaylibSurface::end_drawing()           // EndDrawing — swap
```

No nested `BeginDrawing`. The viewport draws in the **same** window (Option 1);
a `RenderTexture` (Option 3) is the documented fallback if rlgl/RmlUi GL-state
bleed proves unmanageable, with no per-frame CPU copy. The choice and any state
save/restore are documented in the final report.

The minimal new seam in the renderer: expose "flush current batch" + a scissor
setter so the viewport can be clipped to its rect. `Renderer` already computes
`ScreenClipRect` (`worldScreenClipRect()`), so the math exists.

---

## 4. Input routing (prompt §19)

Single pipeline, pure decision function in `app/input_routing.h`:

```
platform event → RmlUi → consumed? stop
                       → else viewport (only if cursor in rect AND no RML text focus AND no popup)
                       → else PlaySession
```

The predicate `should_viewport_receive_input(cursorInRect, rmlConsumed,
rmlTextFocused, rmlPopupOpen)` is pure and unit-tested in editor-core (covers
prompt §24.16 without needing RmlUi).

---

## 5. Files to add / modify

**Add:** everything under `runtime-cpp/src/editor-native/`,
`runtime-cpp/build_native_editor.bat`, `runtime-cpp/run_native_editor.bat`,
`tests/editor-core-test.cpp`, `THIRD_PARTY_NOTICES.md`, `licenses/RmlUi.txt`,
`docs/RMLUI_NATIVE_EDITOR_REPORT.md`.

**Modify (additive only):** `runtime-cpp/CMakeLists.txt` (add editor-core lib
always; add editor-native exe behind `option(ARTCADE_BUILD_NATIVE_EDITOR)` +
RmlUi `FetchContent`), `tests/CMakeLists.txt` (register editor-core test).

**Untouched:** the entire web editor (`editor/`), the existing `src/app`
runtime, every existing module. The web build path is not modified.

---

## 6. Risks

1. **RmlUi ↔ rlgl GL-state bleed** — both drive the same context. Mitigation:
   flush rlgl before `Render()`; fallback to render-texture viewport (Option 3).
2. **FetchContent needs network at first configure** — pinned tag, no moving
   branch, no runtime download. On an offline machine the editor-native target
   simply stays disabled (`-DARTCADE_BUILD_NATIVE_EDITOR=OFF` is the default);
   editor-core and the web build are unaffected.
3. **Static CRT `/MT`** — RmlUi + FreeType must be built `/MT` to match. Forced
   via `CMAKE_MSVC_RUNTIME_LIBRARY` already set at the top of the root CMake.
4. **DPI** — RmlUi context `dp-ratio` must follow the monitor scale; covered by
   `RmlUiHost::resize(w,h,dpiScale)`.

## 7. Rollback

The spike is additive and gated. Rollback = delete `runtime-cpp/src/editor-native/`,
revert the two CMake additions, delete the new docs/scripts. No existing target
changes behaviour. Each phase is its own commit (prompt §29).

## 8. Acceptance criteria (prompt §27) — tracking

GO conditions that are **provable in this environment now**: editor-core
compiles from clean; web editor still builds; command/intent split; single
authority; Position X path is linear and unit-tested; scene change does not
serialize or Replace; Play does not mutate the document; Stop needs no reload;
splitter clamp; input-routing predicate. GO conditions that need the RmlUi
build (window opens, shell renders, splitters drag, DPI) are validated in the
RmlUi phase and reported with honest status.

## 9. Deliberately out of scope

Tile painting, full gizmos, multi-selection, camera preview, full Logic Board,
full Play/Stop gameplay, universal docking, undo for every command, asset import
pipeline in the native target. (Prompt §16 Phase E exclusions.)

---

## 10. Phase plan (prompt §16 / §29 commits)

1. **This document.** ✅
2. `artcade-editor-core`: model + commands + intents + coordinator +
   invalidation + play-session + input-routing, wired into CMake, **with unit
   tests passing under CTest.** ← the verifiable heart.
3. RmlUi pinned dependency + `artcade-editor-native` target + `rml_host` /
   `rml_renderer` / `rml_system`.
4. Native editor shell + ArtCade RCSS theme + split layout + panels.
5. Real ArtCade viewport (background, scene frame, grid, one entity, pan/zoom).
6. Build scripts + `THIRD_PARTY_NOTICES` + final report + GO / GO-WITH-CONDITIONS / NO-GO.

Deviations from this plan are recorded in
`docs/RMLUI_NATIVE_EDITOR_REPORT.md` under *Deviations from the initial plan*.
