# ADR-0005 — Top Down movement through Logic Board input

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** shared runtime input, Logic Board, generated Logic Lua, and the native RmlUi editor

## Context

`TopDownControllerComponent` owns speed, acceleration, friction, and the
four-direction constraint, but it has no gameplay input source of its own.
The runtime controller moves only when `World` receives a movement intent for
the runtime entity.

The native editor already collects the canonical `LogicKey` input snapshot in
Play and ADR-0004 provides a shared capture/search editor for the `Key`
property used by `Key Pressed`, `Key Released`, `While Key Held`, and `Is Key
Down`. That input reaches the Logic and Script runtimes, but the existing
Logic catalog exposes only `platformer.move_horizontal`; it has no Top Down
action able to submit an intent. Consequently a Top Down controller always
uses its friction path and remains stationary.

Restoring an editor-side `WASD`/arrow polling special case would make the
native editor a gameplay authority and would diverge from exported runtimes.
It would also bypass the authored Logic Board that is already the input model
for gameplay.

## Decision

Top Down keyboard movement is authored in the **Logic Board** with the
existing canonical key triggers and a new runtime capability:

```text
While Key Held (key selected with Capture or Search)
    -> Top Down Move (direction)
    -> context.self
    -> RuntimeLogicHostAdapter
    -> World input-frame movement intent
    -> TopDownControllerComponent
```

The new Logic action has the stable id `topdown.move`, display name **Top Down
Move**, and a required `TopDownController` component. Its `direction` is a
required closed choice: **Left**, **Right**, **Up**, or **Down**. It is not a
free numeric `Vec2` field. The action is available only for an Object Type
that owns that controller, exactly as the existing Platformer actions are
restricted to `PlatformerController`.

The standard authoring recipe is four `While Key Held` rules. The author may
choose `W/A/S/D`, the arrows, or any supported `LogicKey` through ADR-0004's
capture/search binding editor:

| Key binding | `Top Down Move.direction` |
|---|---:|
| Left / A | `Left` |
| Right / D | `Right` |
| Up / W | `Up` |
| Down / S | `Down` |

There is deliberately no default key map stored on the component, no hidden
"player" entity, and no automatic `WASD` or arrow behaviour. An Object Type
with a Top Down controller but without these rules remains valid and idle.

## Runtime contract

`GameplayInputFrame` is the sole physical input snapshot. At the beginning of
each dispatched input frame, the runtime clears the previous **input-sourced
Top Down contributions**. Every `topdown.move` action fired while dispatching
that snapshot converts its closed direction choice to the canonical vector for
`context.self`; the runtime sums contributions per entity and clamps each axis
to `[-1, 1]`.

This makes simultaneous held keys deterministic and independent of Logic rule
order:

- Left + Up is `(-1, -1)` and becomes a normalized diagonal when the component
  allows eight directions.
- Left + Right, and Up + Down, cancel on their respective axis.
- With `fourDirections == true`, the existing controller constraint selects a
  single axis; equal magnitudes prefer X, as its current deterministic runtime
  rule already specifies.
- No contribution in the next input frame clears the intent, so the controller
  applies its configured friction. An intent must never remain latched after a
  key is released, focus is lost, a dialog blocks gameplay, or Play stops.

The aggregate remains valid for every fixed step executed for the same
`GameplayInputFrame`; it is not consumed by the first fixed step. This keeps
movement correct when a host performs zero, one, or several fixed steps per
render/input frame.

The host capability validates the owner, internal finite vector/range, and
presence of `TopDownController` before accepting a contribution. It must be
exposed by the same runtime host/proxy used by generated Logic Lua. Future
user-script support, if exposed, must call this capability too; it must not
introduce a second input path.

`Set Velocity` remains a low-level physics action and is not a substitute for
this feature: `TopDownController` owns its velocity every gameplay step.

## Authority, lifecycle, and Play

- `ProjectDocument` remains the authority for the persisted Logic Board and
  Top Down component configuration.
- `LogicKey`, `supportedLogicKeys()`, `logicKeyName()`, and the input-code
  mapping remain the authority for accepted game keys. No platform key code or
  free-text name is persisted.
- `GameplayInputFrame` is the authority for the current physical input; the
  `World` is the authority for the derived, ephemeral movement intent and
  velocity.
- The RmlUi key-binding control is presentation state only. Capture/Search
  still commits `SetLogicPropertyCommand`, with normal Undo/Redo, dirty state,
  save/load, generated Lua, and validation behaviour.
- Entering Play materialises a separate runtime snapshot. The action mutates
  only that runtime world; it creates no authoring Command and cannot write
  back on Stop.
- Keyboard forwarding still respects the existing Play focus policy: text
  fields, popups, unfocused windows, and gameplay-blocking dialogs contribute
  no gameplay input.

No project-schema migration is required: the action is represented by the
existing versioned Logic Board block format and does not change its wire
shape. The existing Logic API version remains valid; the catalog and the
generated-runtime feature allowlist advance together instead. An older runtime
must reject the unavailable `topdown.move` feature explicitly rather than
silently ignoring movement.

## Intent and Command boundary

Authoring a movement rule follows the existing mutation path:

```text
RmlUi capture/search or property edit
    -> Logic Board Intent
    -> EditorCoordinator
    -> typed EditorCommand
    -> ProjectDocument
```

At Play time the resulting compiled action is a runtime request, not an
`EditorIntent` or `EditorCommand`:

```text
immutable GameplayInputFrame
    -> LogicRuntime dispatch
    -> topdown.move runtime host request
    -> World input-frame contribution
```

Therefore gameplay movement has no Undo/Redo entry and never changes project
revision or dirty state.

## Alternatives rejected

1. **Hardcode arrows/WASD in the native editor or `GameplaySession`.** Rejected:
   it makes one host decide gameplay controls, bypasses Logic Board authoring,
   and would not be portable to exported runtimes.
2. **Add four key fields directly to `TopDownControllerComponent`.** Rejected:
   it duplicates the canonical `LogicKey` input model, gives the component a
   second gameplay language, and cannot express conditions or alternative
   controls without more component configuration.
3. **Use `Set Velocity` from four key rules.** Rejected: Top Down owns velocity
   and will overwrite it; it also loses acceleration, friction, and direction
   constraints.
4. **Last executed movement rule wins.** Rejected because diagonal movement and
   opposite-key cancellation would depend on registry/rule dispatch order.
5. **Persist a global player/input target.** Rejected: `context.self` already
   makes the target explicit per runtime instance without a second authority.

## Consequences and verification

The implementation slice must add the new component requirement, catalog
descriptor, executable validation, compiler output, runtime allowlist/proxy,
host adapter, and World input-frame aggregation. It must not add editor-only
movement polling or a new persistent controller field.

Required tests:

- a captured or searched key persists as canonical `LogicKey` and the compiled
  `While Key Held -> Top Down Move` program validates and emits the new API;
- the action is unavailable and executable validation fails for an Object Type
  without `TopDownController`;
- left/right/up/down each move only the owning runtime entity, with the
  documented Y-axis convention;
- simultaneous diagonal input, opposite-key cancellation, and four-direction
  tie behaviour are deterministic and independent of rule order;
- release, lost gameplay focus, modal/dialog blocking, and Stop clear the
  input-sourced intent and friction resumes;
- multiple fixed steps after one input dispatch retain that frame's aggregate
  intent, while the following empty input frame clears it;
- no Play input changes `ProjectDocument`, revisions, Undo/Redo, or saved
  Logic Board data;
- native editor Play and the runtime application consume the same
  `GameplayInputFrame` contract.
