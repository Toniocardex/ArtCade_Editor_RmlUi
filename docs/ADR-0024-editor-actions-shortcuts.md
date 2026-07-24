# ADR-0024 — Professional Editor Actions and Keyboard Routing

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** Editor Action catalog, pure ShortcutRouter, single-frame keyboard
snapshot, Global + Scene migration (Phases 1–2)  
**Related:** Constitution (UI presentation-only; Commands/Intents mutate
document), [ADR-0002](ADR-0002-script-editor-authority.md) (Script buffer undo),
[ADR-0004](ADR-0004-logic-key-binding-editor.md) (Logic key capture),
[ADR-0023](ADR-0023-hierarchy-instance-ux.md) (Focus Selection closes deferred
Hierarchy affordance)

## Decision

Shortcuts are not `key → function`. The binding flow is:

```
captureKeyboardFrame (once)
  → pumpRmlInput(keyboard snapshot)
  → resolveShortcutContext (modal > capture > text > …)
  → ShortcutRouter.resolve (pure)
  → consumeUntilRelease(primary) when matched
  → ActionStateResolver(EditorActionContext projection)
  → PendingEditPolicy → EditorActionDispatcher → public application APIs
```

- Router never mutates `ProjectDocument`, never calls Raylib/RmlUi.
- Matched chords consume the primary key until release (including disabled actions).
- Menu, toolbar, and keyboard share the same dispatcher and state resolver.
- Continuous gestures (Space-pan, wheel, paint, Play WASD) stay outside Actions.

## Phases

| Phase | Scope |
|-------|--------|
| **1** | Pure foundation (catalog, scopes, router, conflicts, format, state, EscapeOwner) |
| **2** | Global + Scene migration; snapshot; consumption; focus tracker; Save/Play APIs |
| 3–7 | Logic Board, Script Actions, overlays, Shortcuts dialog, rebinding (roadmap) |

## Out of scope (this series)

Logic Board Action migration, Script Action dispatcher, Command Palette,
preferences rebinding, arrow nudge, continuous gesture → Action conversion.

## Definition of Done (Phases 1–2)

- Single keyboard acquisition per frame; `pumpRmlInput` and Play collection use it
- Pure resolve separated from state + dispatch
- Consumption persists until key release
- Modal precedes Logic key capture
- Migrated history/Scene/Tilemap off scattered `IsKeyPressed`
- Save via public `ProjectSessionController` APIs
- SceneFocusSelection pans workspace camera only
- Script text shortcuts preserved until Phase 4
- `scripts\build.bat --test` green
