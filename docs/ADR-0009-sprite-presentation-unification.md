# ADR-0009 — Unified Sprite presentation component

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** project format, native RmlUi Inspector, sprite-presentation commands,
Edit projection, Play materialisation, and Logic animation capability

## Context

An Object Type currently persists `SpriteRendererComponent` and
`SpriteAnimatorComponent` separately, with matching sparse instance overrides.
The Renderer carries visibility and an optional static image; the Animator
carries an animation asset, default clip, autoplay, and playback speed. The
Animator is not materialisable without the Renderer.

This exposes one authoring concept as two dependent Inspector cards and two
Add Component entries. It also creates duplicate ownership resolution,
serializer branches, commands, override cleanup, diagnostics, and component
requirements. The Inspector already exposes a single source picker containing
images and animations, which confirms that authors think in terms of one
sprite presentation rather than two independent capabilities.

The existing `SetObjectTypeSpriteSourceCommand` creates/removes the Animator
as a side effect. Before this ADR, its Animation -> Image/None path did not
apply the existing Logic dependency guard used by explicit Animator removal:
a Logic Board could retain animation actions after the capability had been
removed. This is corrected immediately as a separate, backward-compatible
fix; the unified model below makes the same invariant explicit.

`SpriteComponent` is already a legacy/runtime type with unrelated retained
responsibilities such as editor fill colour. It is not an appropriate name for
the new authoring model.

## Decision

Replace the two persistent authoring components with one Object-Type-owned
`SpritePresentationComponent`, presented to users simply as **Sprite**.

The component has one visibility value and one discriminated source:

```text
SpritePresentationComponent
  visible
  source = None
         | Image(imageAssetId)
         | Animation(animationAssetId, defaultClipId, autoPlay, playbackSpeed)
```

The C++ representation must make the alternatives explicit (for example a
`std::variant` of three source definitions), rather than an enum plus unrelated
optional fields. This prevents stale image/animation fields from becoming a
second semantic state. `None` is allowed so an author can add the component
before importing an asset.

`SceneInstanceDef` receives one sparse `SpritePresentationOverride`:

- `visible` may be overridden for every source;
- an instance may replace the source with a complete Image or Animation
  alternative;
- clip, autoplay, and playback-speed deltas may exist only when the resolved
  source is Animation.

Changing an Object Type or instance source is one atomic Command. Switching
away from Animation clears animation-only override fields in the same command;
Undo restores the exact preceding component and override values. A source
change to Image or None, or removal of Sprite, is rejected when the Object
Type Logic Board contains an action requiring animation capability. This is
the same policy as explicit component removal: no silent deletion or
invalidation of gameplay logic.

There is no static-image fallback behind an Animation source. A valid animation
already identifies its source image. A missing animation asset, source image,
or clip is a diagnostic and blocks Play; it must not silently render an old
static image.

## Authority, commands, and invariants

`ProjectDocument` is the sole authority for `SpritePresentationComponent` and
its instance deltas. RmlUi renders one derived Inspector card and emits only
stable IDs and values. It does not resolve sources, allocate assets, prune
overrides, or mutate the document.

```text
RmlUi action
  -> SpritePresentationIntent
  -> EditorCoordinator policy
  -> typed Sprite-presentation EditorCommand
  -> staged ProjectDocument commit
  -> ComponentChanged / targeted invalidation
```

The Inspector must use typed sprite-presentation intents for these mutations;
it must not call a generic coordinator execution helper directly for source
semantics. This keeps the UI at the presentation boundary and makes the
policy-bearing operation explicit at the command boundary.

The replacement command set remains typed and narrow: add/remove Sprite; set
source; set visibility; set Animation settings; set/clear instance Sprite
override. A source switch is one command, never an add/remove/set sequence.
Every successful mutation has one revision and exact Undo/Redo; a no-op or a
failed dependency/asset/range check changes neither document, dirty state,
history, nor invalidation.

Required invariants:

- a component has exactly one source alternative;
- Image references an existing `ImageAssetDef`;
- Animation references an existing `SpriteAnimationAssetDef`, whose source
  image exists and whose selected clip belongs to that asset;
- playback speed is finite and strictly positive;
- animation-only override fields are absent unless their resolved source is
  Animation;
- Logic actions `animation.play_clip`, `animation.stop`, and animation speed
  actions require a resolved Animation source;
- all source transitions, asset-deletion cleanup, and Undo preserve those
  invariants atomically.

## Edit, Play, and runtime boundary

Edit resolution produces one `ResolvedSpritePresentation`/draw projection from
the Object Type component plus the single instance override. Scene View and
Inspector consume that projection; neither reimplements source precedence.

Entering Play materialises an immutable runtime snapshot from the unified
authoring component. Runtime rendering may still use separate transient render
data and the existing `SpriteAnimator` scheduler because frame selection and
frame drawing have different runtime lifetimes. They are implementation state,
not project components or alternate persistent authority. Play, Stop, runtime
clip changes, and animation time never write back to `ProjectDocument`.

The Logic catalog and executable validation express the requirement as
“Sprite source is Animation”, not as a requirement for a second authoring
component. Existing runtime animation APIs and generated Logic Lua retain
their behaviour after materialisation.

## Persistence and migration

The project schema advances from v9 to v10. The v10 writer emits the unified presentation under a distinct field:

```json
"spritePresentation": {
  "visible": true,
  "source": { "kind": "animation", "assetId": "hero.anim",
              "defaultClipId": "idle", "autoPlay": true,
              "playbackSpeed": 1.0 }
}
```

The `spritePresentation` name intentionally avoids the existing legacy/core
`sprite` record, which can retain responsibilities outside this ADR. It must
not carry the new presentation source. The exact field names for the Image and
None alternatives are part of the v10 canonical editor/runtime JSON contract
and must be shared by the native editor and `vendor/artcade-runtime`; no
editor-only adapter format is permitted.

The v9 -> v10 migration reads legacy renderer/animator components and their
overrides only in the migration path. A non-empty Animator source wins when
both are present, matching current draw precedence. A renderer-only value
migrates to Image; an Object Type with neither legacy component has no
`SpritePresentationComponent`; and a configured but
missing animation reference migrates as Animation so validation reports the
real fault rather than inventing a valid fallback. The v10 writer never emits
legacy fields. Once migration is complete, legacy authoring fields, serializers,
resolvers, commands, Inspector actions, and tests are deleted rather than
maintained in parallel.

Image/animation asset removal remains an atomic staged operation. It updates
all unified Sprite references and dependent Logic animation actions according
to the established explicit deletion policy; one Undo restores the exact
pre-removal document.

## Inspector and UX

The Inspector renders one **Sprite** card and one **Add Component -> Sprite**
entry. It shows:

- Visible and a Source picker (`None`, `Images`, `Animations`) for every Sprite;
- an animation-editor link when the selected source is Animation;
- Default Clip, Auto Play, and Playback Speed only for Animation;
- one coherent Object Type / Instance Override provenance badge and one reset
  action for the unified override.

All authoring controls are disabled in Play according to the existing policy.
The card introduces no local persistent state and no duplicate source picker.

## Alternatives rejected

1. **Keep two authoring components and only merge their UI.** Rejected: it
   hides, rather than removes, duplicated authority, validation, persistence,
   and cleanup paths.
2. **Use enum plus optional image and animation fields.** Rejected: invalid
   combinations remain representable and rely on every caller to ignore stale
   fields.
3. **Keep a static fallback for Animation.** Rejected: it introduces two
   active sources and masks broken animation references.
4. **Remove instance overrides.** Rejected: current useful per-instance
   presentation variation is retained, but made coherent under one sparse
   override.
5. **Make runtime animation state persistent authoring data.** Rejected: it
   would violate Edit/Play separation and duplicate the runtime authority.
6. **Name the new type `SpriteComponent`.** Rejected: that name is already
   occupied by legacy/runtime data and would obscure ownership.

## Implementation slice and verification

The implementation must update the shared project/runtime type contract,
canonical JSON parser and writer, v9 -> v10 migration, validator, asset
reference cleanup, typed commands, Inspector, Edit resolver, Play
materialisation, Logic capability checks, and tests as one coherent slice.

Required verification:

1. each source alternative serializes canonically, round-trips, and validates;
2. v9 renderer-only, animator-only, combined, and override-heavy fixtures
   migrate deterministically to v10; v10 output contains no legacy field;
3. source/visibility/settings/override commands are atomic, Undo/Redo exactly,
   and failed preconditions leave revision, dirty state, and history unchanged;
4. Animation -> Image/None and Sprite removal are rejected while dependent
   Logic actions exist; removing or repairing those actions permits the change;
5. source transitions prune only invalid animation-specific override values and
   Undo restores them exactly;
6. deleting referenced images/animations follows the declared atomic cleanup
   policy and Undo restores both Sprite and Logic data;
7. Inspector has one Sprite card/entry, correct conditional fields, accessible
   controls, instance provenance, and no Renderer/Animator authoring controls;
8. Edit preview and Play select the same image/frame; invalid sources diagnose
   and Play fails without partially starting; and no Play activity mutates
   authoring data;
9. native editor build, core tests, canonical runtime parser tests, and a
   focused manual smoke test cover static, animated, overridden, migrated, and
   Logic-driven sprites.
