# ADR-0008 — Stable workspace-mode navigation

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** native RmlUi editor shell: Scene, Logic Board, and Script Editor navigation

## Context

The editor exposes **Scene**, **Logic Board**, and **Script Editor** as the
three modes of one central workspace. The selected mode is editor-only state:
`EditorState::centerWorkspaceMode`; activation already follows the established
RmlUi action -> `SwitchCenterWorkspaceIntent` -> `EditorCoordinator` path.

The current RML nests `#center-modebar` inside `#center-workspace`. In Scene
mode the Inspector and its splitter are visible beside that workspace. In
Logic and Script modes they are intentionally hidden: Logic owns its target
inline and the Scene Inspector would expose an unrelated instance target.
That policy lets Logic and Script use the released width, but it also changes
the width of the mode bar. Its flex centering therefore moves the three tabs
horizontally when the Inspector appears or disappears.

This is a shell-layout problem, not a domain, input, rendering, or Play
problem. Keeping an empty Inspector column would avoid movement only by
wasting authoring width and by weakening the deliberate mode-specific
Inspector policy.

## Decision

Make the mode navigation a stable shell band, independent of the mode-specific
content row.

The RML shell will have a persistent **workspace-main** region beginning after
the Hierarchy splitter and ending at the application’s right edge. That region
contains, in this order:

```text
workspace-main
  -> mode navigation band                 (always present; fixed reference)
  -> mode content row
       -> center workspace / Scene View   (flexes by active mode)
       -> Inspector splitter               (Scene only)
       -> Inspector                         (Scene only)
```

`#center-modebar` and `#center-mode-tabs` move from `#center-workspace` into
the persistent workspace-main region. They retain their current labels,
actions, visual treatment, focus behaviour, and active-state rendering. The
bar is horizontally centred in workspace-main, not in `#center-workspace`.

`#center-workspace` remains the content owner for the viewport, Logic Board,
Script Editor, tile palette dock, and all mode-specific responsive behaviour.
The Inspector and `#split-right` remain children of the content row and still
hide outside Scene mode. Consequently Logic and Script keep the full available
content width, while the navigation’s horizontal reference remains unchanged
across all three modes.

This uses normal flex layout only. It does not use absolute positioning,
manual pixel offsets, duplicated tab bars, placeholder Inspector panels, or a
per-mode cached tab position. Resizing the left Hierarchy correctly changes
the stable workspace-main width; switching Scene/Logic/Script does not.

## Authority, Intent, and invariants

This is presentation-only shell structure.

- `EditorState::centerWorkspaceMode` remains the sole authority for the active
  mode.
- Existing RmlUi actions and `SwitchCenterWorkspaceIntent` remain the only
  route that changes it.
- `EditorUi::refreshCenterWorkspace()` remains responsible for applying the
  active classes and the existing Inspector visibility policy.
- RmlUi owns only the derived layout and local focus/hover presentation.

The slice adds no persistent data, ViewModel, manager, cache, mutation API,
or new controller state. It must not make the DOM a second authority for the
active mode. The existing invariant remains: the Inspector is visible only in
Scene mode, and Logic/Script expose their own relevant authoring surfaces.

## Undo, persistence, and Play

Moving or selecting a tab remains workspace navigation: it creates no
`EditorCommand`, Undo/Redo entry, revision, dirty-state transition,
serialization field, or migration.

Play keeps its current navigation policy, including its temporary Scene
preview and return-to-origin behaviour. The new shell geometry must not alter
`PlaySession`, the viewport’s Raylib hole, input routing, panel-resize state,
or the ability to navigate modes according to existing Play guards.

## Alternatives rejected

1. **Keep the Inspector column visible but empty in Logic and Script.**
   Rejected: it wastes valuable authoring width and implies a panel where no
   relevant Inspector target exists.
2. **Center tabs inside `#center-workspace` for each mode.** Rejected: this is
   the current implementation and makes global navigation move whenever the
   mode-specific content geometry changes.
3. **Apply per-mode offsets or animate the horizontal jump.** Rejected:
   offsets are fragile across DPI and splitter widths; animation hides rather
   than removes the instability.
4. **Duplicate the tab bar above each mode.** Rejected: it duplicates markup,
   focus handling, active-state rendering, and the risk of divergent actions.
5. **Use absolute positioning against the window.** Rejected: it bypasses the
   responsive flex layout and becomes brittle with a resizable left panel or
   future shell changes.

## Implementation slice and verification

The implementation is limited to the native shell RML/RCSS structure:

- introduce a flex-column workspace-main wrapper after the left splitter;
- move the existing mode bar into that wrapper, above a flex-row content
  wrapper;
- keep the center workspace, right splitter, and Inspector in that content
  wrapper;
- update only the shell selectors necessary to preserve existing heights,
  overflow, minimum widths, and splitters;
- leave tab IDs/actions and `refreshCenterWorkspace()` mode semantics intact.

No coordinator, command, serializer, runtime, or domain change is required.

Verification:

1. at a fixed window and left-panel width, the mode-tab group has the same
   horizontal centre in Scene, Logic Board, and Script Editor;
2. Scene still shows the Inspector and its splitter; Logic and Script still
   hide both and use the released content width;
3. resizing the left Hierarchy re-centres the group in the new workspace-main
   region, while switching modes afterward does not move it;
4. each tab still sends its existing action, shows the correct active state,
   supports keyboard focus, and preserves its existing Play policy;
5. the viewport element retains its effective Scene bounds, Raylib input hole,
   Fit/Grid tools, tile-palette dock, and right-panel resizing behaviour;
6. Logic Board responsive width measurement remains based on
   `#center-workspace`, so its compact breakpoint reflects its actual usable
   content width;
7. build the native editor and manually switch all three modes at normal,
   narrow, and resized-panel widths, checking for no horizontal navigation
   jump and no unintended content overflow.
