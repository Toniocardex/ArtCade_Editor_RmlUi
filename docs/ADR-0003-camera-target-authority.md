# ADR-0003 — Camera Target authority and selection

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** native RmlUi editor, `ProjectDocument`, and Play materialisation

## Context

`CameraTargetComponent` already exists in the shared runtime with `offsetX`,
`offsetY`, and `followSpeed`. Its current automatic fallback selects the active
candidate with the lowest runtime entity ID. That ordering is an implementation
detail, not an authoring decision: it changes when instances are duplicated,
spawned, or reordered.

The native editor must not expose a component whose ownership and selection are
ambiguous. In particular, putting the component on an Object Type would make
every instance and every spawn of that type a candidate for a camera that is
normally intended to follow one placed actor.

## Decision

`CameraTarget` is an **instance-owned, scene-scoped authoring component**.

- Persistent authority is the selected `SceneInstanceDef`, never the Object
  Type, the Inspector, or the runtime.
- A scene may contain **at most one** authored Camera Target instance.
- `offsetX` and `offsetY` are finite world-space offsets. `followSpeed` is
  finite and non-negative; `0` means snap and a positive value is the existing
  exponential follow rate in seconds inverse.
- Entering Play materialises the chosen scene instance once into an independent
  runtime component. Editing, Play, and Stop never synchronise the value back
  to the project.
- On scene activation the camera follows that scene's authored target. If the
  target is destroyed, following stops and the camera keeps its last position;
  it does not select another entity by ID.
- A scene with no target applies no automatic follow.

The runtime's explicit follow API remains runtime-only. Scripts may temporarily
follow a spawned or different runtime entity, stop following, or restore the
scene default. Those actions do not mutate `ProjectDocument`, do not create
Undo entries, and are discarded with the PlaySession.

## Intent and command boundary

The implementation uses explicit, instance-scoped commands:

- `AddCameraTarget(instance)`;
- `RemoveCameraTarget(instance)`;
- `SetCameraTargetOffset(instance, x, y)`;
- `SetCameraTargetFollowSpeed(instance, speed)`.

Adding a target fails atomically when another instance in the same scene already
owns it. Every successful mutation has exact Undo/Redo and a targeted
instance-component `DomainChange`. The Inspector is presentation only and is
disabled during Play.

No Logic Board action is introduced by this ADR. A static scene default belongs
in authoring; a temporary gameplay camera change belongs to the existing
runtime/script camera API.

## Persistence and migration

The native editor schema persists the component under its owning scene instance
as `scenes[].instances[].cameraTarget`; it round-trips deterministically.

The existing runtime `EntityDef.cameraTarget` is a type-level compatibility
field, not an authority for the native editor. It cannot be migrated
automatically to an instance because the intended target is not inferable when
several instances share a type. A native project load that contains that legacy
type-level field must therefore be rejected with a clear migration error rather
than silently discarding or guessing a target.

## Alternatives rejected

1. **Object Type ownership.** Rejected because every duplicate/spawn becomes a
   camera candidate and makes the scene camera depend on runtime creation order.
2. **Several automatic targets with priority or lowest-ID selection.** Rejected
   because it introduces arbitration, tie-breaking, and another authoring
   policy without a current product need.
3. **Script-only camera selection.** Rejected because the normal scene camera
   is important, inspectable design-time data and should work without scripts.
4. **Runtime edits written back on Stop.** Rejected because it violates the
   PlaySession isolation boundary.

## Consequences and verification

- The runtime selection policy must be changed by the slice so an authored
  scene never falls back to lowest-ID automatic selection.
- The viewport needs no second camera authority: it projects only the selected
  instance's authored data in Edit and the isolated Play camera in Play.
- Tests must cover uniqueness, invalid numeric input, command Undo/Redo,
  persistence, legacy type-level rejection, scene activation, target
  destruction, explicit runtime override, and no authoring mutation during
  Play/Stop.
