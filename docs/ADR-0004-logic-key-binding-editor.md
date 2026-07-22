# ADR-0004 — Logic Board key binding editor

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** native RmlUi editor Logic Board input properties

## Context

`Key Pressed`, `Key Released`, `While Key Held`, and `Is Key Down` all use the
same `LogicKey` property. The current native Inspector renders that property as
one long expanded dropdown. It is slow to scan, obscures the rest of a rule,
and is especially poor for frequent rule authoring.

The editor must improve this interaction without introducing a UI-specific key
format. `LogicKey`, `Logic::supportedLogicKeys()`, `logicKeyName()`, and the
runtime input dispatch remain the sole authority for supported persistent game
keys.

## Decision

Every Logic property whose value kind is `Key` uses one shared **Key Binding
Editor**, with two paths:

1. **Capture (primary path).** Activating the field enters the transient state
   `Press a key…`. The next supported physical keyboard key commits the
   existing typed `SetLogicPropertyCommand` with its corresponding `LogicKey`.
2. **Search (fallback path).** `Search key…` opens a filterable list built only
   from `Logic::supportedLogicKeys()`. Selecting an item commits the same typed
   command.

The current binding is always displayed as the canonical `logicKeyName()`.
There is no free-text key name and no RmlUi/Raylib key code in project JSON,
Logic Board data, generated Lua, or the runtime input frame.

## Capture rules

- Capture state is UI-only: it identifies the target Logic property but never
  changes `ProjectDocument`, creates no Undo entry, and is discarded on board
  switch, selection change, Play, panel teardown, or an explicit cancel.
- While armed, the editor handles the next key before global editor shortcuts
  or viewport gestures can consume it.
- `Esc` cancels capture and leaves the previous binding untouched. It is not
  captured as a gameplay binding, so cancellation remains predictable.
- Mouse buttons, modifier-only input, unknown platform keys, and unsupported
  keys do not mutate the binding. The field stays armed and gives a concise
  supported-key hint.
- Rebinding to the already-selected `LogicKey` is a command no-op and does not
  create a history entry.

## Reserved and future keys

Search is the explicit route for any supported key that conflicts with an
editor interaction (for example `Enter`, or a future editor shortcut).

`Escape` is **not currently a `LogicKey`** and therefore cannot be authored or
silently stored by either route. If the product needs gameplay `Escape`, a
separate runtime input slice must first add it consistently to `LogicKey`,
`supportedLogicKeys()`, input-code mapping, native Play collection, exported
runtime input, serialization, generated Lua, and tests. Once it is supported,
it may be selected through Search while capture keeps `Esc` as cancel.

## Alternatives rejected

1. **Keep the expanded dropdown.** It is technically complete but inefficient
   and scales badly as the registry grows.
2. **Capture-only.** It makes keyboard-driven authoring fast but gives no safe
   route to keys that the editor reserves for cancellation or commands.
3. **Persist arbitrary platform key codes or names.** It would create a second
   input vocabulary, break cross-runtime consistency, and weaken validation.
4. **Let `Esc` both bind and cancel.** Its behaviour would be ambiguous exactly
   when an author needs a reliable way out of capture.

## Consequences and verification

- The long key dropdown is replaced for every `LogicValueKind::Key`; condition
  type does not create separate implementations.
- The property update still goes through `SetLogicPropertyCommand`, preserving
  targeted `DomainChange`, validation, Undo/Redo, persistence, and generated
  Lua behaviour.
- Tests must cover capture of a supported key, Search selection, `Esc` cancel,
  unsupported-key rejection, no-op rebinding, selection/Play cancellation, and
  save/load plus generated-Lua preservation of the resulting `LogicKey`.
