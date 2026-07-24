# ADR-0020 — Scene Background as a single Inspector color control

**Status:** Accepted  
**Date:** 2026-07-24  
**Revised:** 2026-07-24 (Appearance section; unitary chrome; Opacity slider draft)  
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

Four R/G/B/A float rows misrepresented that authority. Later hex + Opacity %
improved density but still lived under General and read as disconnected fields.
This revision places Background under **Appearance**, presents swatch / hex /
Reset as one control, and adds an Opacity slider with a panel-local draft so
drag does not fragment Undo history.

## Decision

### Authority (unchanged)

| Data | Owner |
|---|---|
| Scene background color | `ProjectDocument` → `SceneDef::backgroundColor` (`Vec4`) |
| Mutation | `SetSceneBackgroundCommand` only |
| Default for new scenes / Reset | `kDefaultSceneBackground` (`#1E1E24`, opaque) |
| Workspace / UI layout | no color field in `EditorState` / `EditorUiState` |

RmlUi remains presentation-only. No `backgroundHex` / `backgroundOpacity`
document fields, no serializer change, no PlaySession write-back.
Reset reuses `SetSceneBackgroundCommand` (no second Command type).

### Inspector presentation

```text
APPEARANCE
Background   [swatch | #RRGGBB | Reset]
Opacity      ────●────  [0–100] %
```

1. **Unitary Background field** — swatch (≥24dp) + expandable hex + Reset.
   Nested checker under swatch color so alpha &lt; 1 reveals transparency.
   Swatch click opens platform color picker (`ChooseColorW`). Cancel → no
   Command. OK → one Command with new RGB and **unchanged** `a`.
2. **Hex field** — `#RRGGBB` (also `#RGB`). Preserves `a` on commit.
3. **Reset** — Command with `kDefaultSceneBackground`; no-op if already equal.
4. **Opacity** — slider + numeric percent. Document storage remains `a ∈ [0,1]`.

Do **not** fold alpha into `#RRGGBBAA` as the primary edit surface. Do **not**
implement an in-RmlUi HSV popover in this slice (deferred).

### Opacity slider lifecycle

```text
dragstart              → panel-local draft, dragActive = true
change while dragActive → preview only (surgical DOM; no Command)
dragend                → one SetSceneBackgroundCommand; clear draft
change without drag    → one Command immediately (click / keyboard)
Escape                 → cancel draft
```

Full `InspectorPanel::refresh()` / `SetInnerRML` must **not** run during an
active drag (would replace the range that owns pointer capture). Preview
updates `#scene-bg-opacity-slider`, `#scene-bg-opacity-value`, and
`#scene-bg-swatch-color` only. Viewport follows the document (updates on
commit). Draft is reconciled away on mode/scene/Play mismatch or external
mutations that match neither original nor preview.

### Mutation flow

```text
hex / opacity text (Enter/blur) ─┐
slider (dragend or click/keys)  ─┤
swatch → ChooseColorW (OK)      ─┼→ SetSceneBackgroundCommand → ProjectDocument
Reset                           ─┘→ Inspector | Viewport invalidation
```

Rules:

- One successful edit = one Command, one Undo step (existing Command behavior).
- Hex / picker preserve `a`; opacity preserves RGB.
- Invalid / incomplete text buffers retain focus (pending-edit gate).
- Picker cancel / identical color → no dirty / no history.
- Play: Appearance controls disabled; picker must not open.
- Dialog lives in the application layer; Inspector emits
  `pick-scene-background-color` / `reset-scene-background` /
  `change-scene-background-opacity` (range; not `commit-*`).

### Parsing / formatting (core-testable)

- `formatColorHexRgb` / `parseColorHexRgb` / `incompleteColorHexBuffer`
- `formatOpacityPercent` / `parseOpacityPercent`
- `classifyOpacitySliderChange(dragActive)` → PreviewOnly | CommitImmediately
- `kDefaultSceneBackground`

## Alternatives rejected

1. Four R/G/B/A float rows — poor density, no preview.
2. Hex-only including alpha — opacity buried in 8-digit hex.
3. In-RmlUi full color wheel this slice — deferred; OS picker matches
   existing `comdlg32` pattern.
4. Store hex/percent in `SceneDef` — duplicate authority.
5. Per-channel Commands / per-tick slider Commands — history noise.
6. `ResetSceneBackgroundCommand` — second path for the same mutation.
7. CSS multi-gradient checker — conflicts with flat RCSS; use nested
   elements + small checker texture instead.

## Consequences

- Appearance can grow (background image, clear mode, …) without restuffing General.
- Opacity drag is preview-only until release; click/keyboard still commit once.
- No project format change.

## Constraints / invariants

- `ProjectDocument` sole persistent owner of `backgroundColor`.
- `SetSceneBackgroundCommand` sole mutation path.
- No `ProjectDocument` mutators from RmlUi.
- No JSON shape change for `backgroundColor`.
- Swatch / percent / slider are presentation only; draft is panel-local.
- Native editor only; no React/Tauri/WASM.

## Verification

1. Background/Opacity only under Appearance; Project starts collapsed.
2. Picker OK updates RGB, preserves `a`, one Undo; Cancel no-ops.
3. Slider drag: zero Commands during move, one on release; click/keys: one Command.
4. Escape cancels draft; Reset / picker cancel draft first.
5. Reset to `kDefaultSceneBackground`; already-default is no-op.
6. Play: Appearance controls disabled.
7. Save/Load `backgroundColor` unchanged.
