# ADR-0020 — Scene Background as a single Inspector color control

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** native RmlUi Scene Inspector presentation of `SceneDef::backgroundColor`;
commit path for `SetSceneBackgroundCommand`; Win32 color picker affordance  
**Related:** Constitution (single authorities; RmlUi never mutates document),
[Engineering Gates](ARTCADE_RMLUI_ENGINEERING_GATES.md) § Intent/Command,
input buffers commit on Enter/blur, [ADR-0017](ADR-0017-coordinator-document-core-boundaries.md),
[ADR-0018](ADR-0018-game-view-viewport.md) (Scene Inspector section layout)

## Context

`SceneDef::backgroundColor` is already a single persistent `Vec4` on
`ProjectDocument`. Serialization, Play snapshot, and viewport clear all consume
that one field. Mutation already goes through one Command:

```text
SetSceneBackgroundCommand(SceneId, Vec4)
```

Four separate R/G/B/A float rows misrepresented that authority. The first
presentation slice unified RGB into hex + swatch with a separate alpha field.
This revision improves discoverability: the swatch opens a native color picker,
and alpha is shown as **Opacity %** rather than a 0–1 float labeled `A`.

## Decision

### Authority (unchanged)

| Data | Owner |
|---|---|
| Scene background color | `ProjectDocument` → `SceneDef::backgroundColor` (`Vec4`) |
| Mutation | `SetSceneBackgroundCommand` only |
| Workspace / UI layout | no new color field in `EditorState` / `EditorUiState` |

RmlUi remains presentation-only. No second color model, no serializer change,
no migration, no PlaySession write-back.

### Inspector presentation

```text
Background   [swatch]  #RRGGBB
Opacity      [0–100] %
```

Two Inspector rows so hex and opacity are not cramped in a narrow panel.
1. **Swatch** — preview of current RGB. Click opens the platform color picker
   (Windows `ChooseColorW`). Cancel → no Command. OK → one
   `SetSceneBackgroundCommand` with new RGB and **unchanged** `a`.
2. **Hex field** — `#RRGGBB` (also accept `#RGB`). Case-insensitive; display
   normalizes to uppercase `#RRGGBB` after refresh. Preserves `a` on commit.
3. **Opacity field** — UI percent 0–100 (optional trailing `%` on parse).
   Document storage remains `a ∈ [0,1]`. Label is **Opacity**, not `A`.

Do **not** fold alpha into `#RRGGBBAA` as the primary edit surface. Do **not**
implement an in-RmlUi HSV wheel in this slice.

### Mutation flow

```text
hex / opacity buffer (Enter/blur)  ─┐
swatch → ChooseColorW (OK)         ─┼→ SetSceneBackgroundCommand → ProjectDocument
                                   └→ Inspector | Viewport invalidation
```

Rules:

- One successful edit = one Command, one Undo step (existing Command behavior).
- Hex / picker preserve `a`; opacity preserves RGB.
- Invalid / incomplete text buffers retain focus (pending-edit gate).
- Picker cancel / identical color → no dirty / no history.
- Play: controls disabled; picker must not open.
- Dialog lives in the application layer (same category as file pickers);
  Inspector only emits `pick-scene-background-color`.

### Parsing / formatting (core-testable)

- `formatColorHexRgb` / `parseColorHexRgb` / `incompleteColorHexBuffer`
- `formatOpacityPercent` / `parseOpacityPercent` (percent ↔ `a` in `[0,1]`)

## Alternatives rejected

1. Four R/G/B/A float rows — poor density, no preview.
2. Hex-only including alpha — opacity buried in 8-digit hex.
3. In-RmlUi full color wheel — cost without reuse yet; OS picker matches
   existing `comdlg32` file-dialog pattern.
4. Store hex/percent in `SceneDef` — duplicate authority.
5. Per-channel Commands — history noise.
6. Opacity slider with per-pixel Undo — deferred; text percent is enough.

## Consequences

- Authors pick colors visually and edit hex when needed.
- Opacity reads as a percentage; document/`Vec4` stay 0–1.
- No project format change.
- Future shared color rows can reuse helpers + picker glue.

## Constraints / invariants

- `ProjectDocument` sole persistent owner of `backgroundColor`.
- `SetSceneBackgroundCommand` sole mutation path.
- No `ProjectDocument` mutators from RmlUi.
- No JSON shape change for `backgroundColor`.
- Swatch / percent are presentation only.
- Native editor only; no React/Tauri/WASM.

## Implementation slice

1. Swatch `data-action="pick-scene-background-color"` + Win32 `pickColorRgb`.
2. Opacity % field + `commit-scene-background-opacity`.
3. Keep `commit-scene-background-hex`.
4. Pending-edit gates for hex and opacity percent.
5. Core tests for parse/format; smoke for picker / Play freeze.

## Verification

1. Background row: swatch + hex; Opacity row: percent + `%`.
2. Picker OK updates RGB, preserves `a`, one Undo; Cancel no-ops.
3. Hex commit preserves opacity; opacity commit preserves RGB.
4. Invalid hex / opacity → no Command.
5. Play: no picker, fields disabled.
6. Save/Load `backgroundColor` unchanged.
