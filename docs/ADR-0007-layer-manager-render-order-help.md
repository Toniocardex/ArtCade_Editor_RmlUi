# ADR-0007 — Layer Manager and render-order help

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** native RmlUi Scene Inspector and Hierarchy

## Context

A scene exposes layers in two editor projections.

- The **Hierarchy** groups instances beneath their layers, so an author can
  navigate scene contents and operate on the currently visible composition.
- The Scene Inspector exposes the scene-level layer stack. Its existing
  controls create, rename, select, reorder, lock, hide, and (when allowed)
  remove layers.

Both projections are derived from the same `SceneDef.layers` and current
`EditorState`. They do not store independent layer data. Their different
purposes are not visually explicit enough: the Inspector header currently
reads `Layers`, and the foreground-first ordering can be mistaken for a second
copy of the Hierarchy.

## Decision

Keep the two projections separate. The Inspector section is named **Layer
Manager**; the Hierarchy continues to use **Layers** for its content tree.

The Layer Manager header includes the established compact `?` help affordance.
Hovering or focusing it shows exactly this English tooltip:

> Rendering order: top to bottom.

The explanation is intentionally contextual rather than permanent text. It
does not consume vertical space in a narrow Inspector, does not introduce a
modal, and does not need a new preference or persisted state. The help control
is informational and remains available in both Edit and Play.

The existing presentation order remains intentional:

- Layer Manager: foreground first, so the upper row represents the visually
  upper render stratum and can be moved with the existing order controls.
- Hierarchy: composition/content order, with each layer followed by its
  instances.

The repeated eye and lock controls remain deliberate quick actions. They use
the same established operation paths as the Layer Manager; neither panel
creates a local copy or a synchronisation mechanism.

## Authority, mutation, and invariants

`ProjectDocument::SceneDef.layers` remains the sole persistent authority for
layer definitions and their order. `EditorState` remains the authority for the
active layer and editor-only hidden-layer state. RmlUi is presentation only.

This ADR adds no authoring mutation. It introduces no Intent, Command,
`ProjectDocument` field, serializer field, cache, manager, or ViewModel.

Existing actions keep their current paths:

```text
Layer stack edits -> existing EditorIntent / EditorCommand -> ProjectDocument
Select active layer / editor visibility -> existing workspace Intent -> EditorState
Tooltip hover/focus -> local RmlUi presentation state only
```

All established layer invariants remain unchanged, including `SceneDef.layers`
as the only source of render order, a valid `defaultLayerId`, and valid
instance layer references.

## Undo, persistence, and Play

Because this slice introduces no mutation, it creates no Undo/Redo entry,
revision, dirty-state transition, persistence change, or migration.

Existing Layer Manager actions retain their present Command/Undo semantics;
workspace selection and editor-only visibility remain non-dirty state. The
tooltip has no runtime effect. During Play authoring controls keep their
existing disabled policy, while the informational help remains readable and
never modifies `PlaySession` or authoring data.

## Alternatives rejected

1. **Merge Layer Manager into the Hierarchy.** Rejected: it mixes render-stack
   management with instance navigation, makes the left tree denser, and removes
   a stable scene-level management surface from the Inspector.
2. **Remove the Inspector stack as redundant.** Rejected: it would discard
   direct scene-level operations such as foreground-first ordering, add,
   rename, and remove, merely because another projection contains the same
   entities.
3. **Show explanatory text permanently.** Rejected: the Inspector is narrow
   and the explanation is useful only when the ordering is unclear; a standard
   help tooltip is more immediate and lower-noise.
4. **Create a new Layer Manager state/store.** Rejected: `SceneDef.layers` and
   `EditorState` already own the required data. A second store would violate
   the single-authority architecture.

## Implementation slice and verification

The implementation is limited to the native Inspector presentation and its
existing shared help affordance/style:

- rename the Inspector section title to `Layer Manager`;
- add an accessible, focusable `?` control beside that title;
- use the exact tooltip text `Rendering order: top to bottom.`;
- make no changes to layer commands, serialization, runtime materialisation,
  or Hierarchy grouping.

Verification:

1. the Scene Inspector renders `Layer Manager` and its help affordance in a
   normal scene and a scene with several layers;
2. mouse hover and keyboard focus expose the exact tooltip text, without
   changing active layer, visibility, document revision, or dirty state;
3. existing add/rename/reorder/remove, lock, visibility, Undo/Redo, and Play
   guards continue to use their current paths;
4. the relevant RML contract covers the required help element and no duplicate
   IDs or residual listeners are introduced;
5. build the native editor and perform the focused manual smoke test at normal
   and narrow Inspector widths.
