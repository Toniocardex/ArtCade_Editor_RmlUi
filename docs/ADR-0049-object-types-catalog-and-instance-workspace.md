# ADR-0049 — Object Type Catalog and Instance Workspace

**Status:** Accepted  
**Date:** 2026-07-30  
**Scope:** native RmlUi Object Types dock, workspace selection, Inspector mode
and creation/placement flow.  
**Related:** `ADR-0023-hierarchy-instance-ux.md`,
`ADR-0048-object-type-names-as-entity-labels.md`,
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §§4–7/§12,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4.

## Context

The authoring model already has two different persistent aggregates:

- `ProjectDoc.objectTypes` owns reusable Object Type definitions, components,
  Logic Boards, scripts and default variables.
- `SceneDef.instances` owns one placement in one scene: transform, layer,
  visibility and explicit instance overrides.

The old left dock made scene instances its primary content. A scene with no
placements therefore appeared to have no objects, even when the project still
contained valid reusable Object Types. It also made Object Type authoring
depend on selecting a visible instance, which is not reliable for invisible,
off-canvas, overlapping or purely logical placements.

## Decision

The left dock is named **OBJECT TYPES** and projects the complete project
catalog before its existing layer-organised placement projection. Each Object
Type row shows its name and the count of its instances in the active scene.
The row selects the definition; its `+` affordance runs the existing typed
placement path (`CreateEntityCommand`) for the active scene. The secondary
layer list remains a recovery surface for instances that cannot be picked on
the canvas; it is a projection, not another state store.

`Create Object Type` is a distinct operation. It creates only an `EntityDef`
in `ProjectDocument`; it does not create a scene, layer, instance or synthetic
selection. It is an atomic `CreateObjectTypeCommand`, is undoable/redoable,
updates dirty/revision normally, and is rejected during Play by the existing
command policy.

`SelectionState` now represents exactly one of:

- `primaryEntity`, for a selected `SceneInstanceDef`; or
- `selectedObjectTypeId`, for a selected catalog definition.

Changing either is an Intent and clears the other. This workspace selection is
not persisted and never creates an Undo entry. Reconciliation clears an Object
Type selection whose definition was removed through a command, Undo/Redo or
project replacement.

The Inspector has an Object Type mode that is valid even when its active-scene
instance count is zero. It exposes the shared name, technical ID, active-scene
count, **Place Instance**, **Open Logic Board**, and a derived component
summary. Existing detailed component editors retain their current instance
entry point in this slice: their commands already resolve and mutate the
shared Object Type, while instance-specific transform and overrides remain
available only after selecting a placement. This avoids duplicating a large
controller/action surface while preserving the authority boundary.

Scene View click continues to select the instance and show Instance Inspector
mode. Delete from that context remains `DeleteEntityCommand`: it removes only
that placement and never deletes its Object Type. The Object Type catalog also
offers a separate trash affordance. It opens a destructive confirmation that
reports the total number of referencing instances across every scene; this
count is a warning, not a blocker. Confirmation executes one atomic,
undoable `DeleteObjectTypeCommand` that removes the type and every such
placement. The layer-organised secondary projection and the Layer Manager in
the Scene Inspector remain non-canvas navigation surfaces for placed instances.

## Invariants

- `ProjectDocument` is the only persistent authority for both Object Types and
  instances; RmlUi rows, counts and summaries are rebuildable projections.
- An Object Type selection and an instance selection cannot coexist.
- `CreateObjectTypeCommand` never creates an instance; `CreateEntityCommand`
  never creates an Object Type.
- `Place Instance` references an existing `ObjectTypeId`, validates the target
  scene/layer and uses the normal Command/Undo/Play policy.
- Removing the last instance never removes its Object Type.
- `DeleteObjectTypeCommand` is the only cascading delete path: it removes the
  selected type and all of its instances, or changes nothing.
- Undo restores the Object Type and every removed instance to its original
  scene and structural index.
- Active-scene counts are derived from `SceneDef.instances`, never stored in an
  Object Type or UI state.

## Alternatives rejected

- **Keep instances as the primary hierarchy:** conflates catalog definitions
  with one scene’s placements and hides zero-use Object Types.
- **Automatically place a new Object Type:** makes a project-catalog action
  silently mutate a scene and prevents empty-scene authoring.
- **Store Object Type selection in RmlUi or a panel-local field:** duplicates
  semantic workspace state and breaks cross-panel reconciliation.
- **Create a synthetic instance for the Inspector:** leaks placement semantics
  into Object Type editing and fails for a genuinely empty scene.
- **Silently delete a type after its last instance disappears:** conflates an
  instance operation with a catalog operation and hides a destructive action.

## Consequences

- The scene can be empty while the project’s reusable catalog stays visible,
  editable and placeable.
- Object Type and instance operations are visibly distinct in the UI and have
  distinct Intent/Command paths.
- The detailed component UI is intentionally not copied into a second panel in
  this slice. A future direct component-editor entry from Object Type mode must
  route the same actions through the selected Object Type and retain explicit
  instance-only controls; it must not introduce a second document model.
- Object Type deletion is intentionally stronger than instance deletion and
  is always preceded by a confirmation that reports its cross-scene instance
  impact. It is recoverable through Undo during the open editor session.

## Verification

- Selecting an Object Type clears instance selection, does not dirty the
  project and is rejected for an unknown ID.
- Selecting an instance clears Object Type selection.
- Creating an Object Type creates no placement, selects the new type and
  Undo removes it and reconciles its selection.
- Placing a selected type creates one normal scene instance; deleting that
  instance leaves the type in the catalog.
- The dock count is active-scene-local, updates through normal invalidation,
  and a zero-count row remains visible.
- Deleting a type with zero, one and multiple cross-scene instances is atomic;
  Undo restores the exact Object Type and placements.
- Play rejects Create/Place/Rename through the existing authoring policy.
