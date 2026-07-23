# ADR-0010 — Materialise animation source sheet at Play start

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** shared runtime materialiser (`object-type-materialize`), Play /
`GameplaySession` sprite draw path, editor Play regression tests  
**Supersedes / extends:** the Play-materialisation clause of
[`ADR-0009-sprite-presentation-unification.md`](ADR-0009-sprite-presentation-unification.md)

## Context

ADR-0009 unified authoring under `SpritePresentationComponent` with a
discriminated source (`None` | `Image` | `Animation`). It explicitly decided
that:

- there is **no** static-image fallback behind an Animation source;
- a valid animation already identifies its source image
  (`SpriteAnimationAssetDef::sourceImageAssetId`);
- entering Play materialises an immutable runtime snapshot from that
  authoring component;
- runtime may keep separate transient renderer + animator scheduler state,
  but those are implementation details, not a second authoring authority.

The shared materialiser in
`vendor/artcade-runtime` / `runtime-cpp/src/core/object-type-materialize.cpp`
implements Image correctly (`renderer.imageAssetId = image->imageAssetId`)
but the Animation branch only constructs `SpriteAnimatorComponent`. It never
looks up the animation asset’s `sourceImageAssetId`, so:

- `SpriteRendererComponent::imageAssetId` stays empty;
- `SpriteComponent::spriteAssetId` is only copied when the renderer id is
  non-empty, and therefore also stays empty at materialise time.

Edit mode already compensates in the editor projection
(`resolveSpriteDraw` reads the catalog). Play has a **partial** compensation
in `RuntimeEntityGateway::maybePlaySpawnClip`, which binds the sheet from
`clipAssetId(animation, defaultClipId)` when both ids are non-empty and
clips are registered before World init. That path is not equivalent to
ADR-0009’s contract:

- it requires a non-empty `defaultClipId` (Edit may fall through to the
  first clip; Play does not);
- it depends on clip registration order and gateway spawn timing;
- a missing sheet still degrades to the generic entity placeholder /
  fallback square in Scene View.

Observed symptom: animated Sprite presentations can render correctly in
Edit and fail or flash to the placeholder in Play, especially when
`defaultClipId` is empty or clip binding does not run.

## Decision

At Play materialisation, an Animation source must populate the runtime
draw sheet from the animation asset’s `sourceImageAssetId` in the **same**
shared materialiser that creates the animator.

Concretely, in `ArtCade::materializeInstance` (or an adjacent shared helper
used only by that path):

1. Resolve `SpritePresentationAnimation::animationAssetId` against
   `ProjectDoc::spriteAnimationAssets`.
2. If the asset exists and `sourceImageAssetId` is non-empty, set
   `SpriteRendererComponent::imageAssetId` to that id **before** the
   existing copy into `SpriteComponent::spriteAssetId`.
3. Still create `SpriteAnimatorComponent` from the presentation’s animation
   fields (asset, default clip, autoPlay, playback speed) exactly as today.
4. Do **not** invent a second static Image source in authoring data. The
   sheet id is derived runtime state for drawing, not a parallel
   `SpritePresentationImage` written back to `ProjectDocument`.

Missing animation asset, empty `sourceImageAssetId`, or missing image asset
remain validation / Play-start diagnostics per ADR-0009. Materialisation
must not silently substitute an unrelated image.

`maybePlaySpawnClip` may keep binding/playing the default clip; it is no
longer the sole authority for exposing a drawable sheet. After this ADR,
an animated entity that materialises successfully has a non-empty sheet id
even when Auto Play is off and even when `defaultClipId` is empty (so long
as the animation asset declares a source image).

## Authority and module boundaries

| Concern | Owner |
|---|---|
| Authoring source (Image vs Animation) | `ProjectDocument` / `SpritePresentationComponent` (ADR-0009) |
| Derived sheet id at Play start | Shared runtime materialiser (`object-type-materialize`) |
| Frame selection / playback | `SpriteAnimator` scheduler (unchanged) |
| Edit preview draw | Editor projection (`resolveSpriteDraw`) — may keep catalog lookup; must agree with materialised Play sheet when assets are valid |
| UI / RmlUi | No change; does not set runtime sheet ids |

Forbidden:

- editor-only Play patch that writes sheet ids without updating the shared
  materialiser (would diverge `game.exe` / WASM / editor Play);
- reintroducing a persistent static fallback behind Animation in the
  document (rejected by ADR-0009);
- reverse-sync of materialised sheet ids into authoring on Stop.

## Alternatives rejected

1. **Fix only in the editor Play facade.** Rejected: Play and export share
   AssetLoader → materialise; an editor-only patch leaves `game.exe` broken
   and duplicates authority.
2. **Rely solely on `maybePlaySpawnClip` sheet binding.** Rejected: it is
   incomplete (requires `defaultClipId`), timing-sensitive, and contradicts
   ADR-0009’s “animation already identifies its source image” rule at the
   materialisation boundary.
3. **Teach Edit to leave the sheet empty and mirror Play’s gap.** Rejected:
   Edit already projects correctly; regressing Edit does not fix the
   contract.
4. **Store `imageAssetId` again on the Animation presentation variant.**
   Rejected: duplicates `sourceImageAssetId` on the animation asset and
   reopens stale dual-source state ADR-0009 removed.

## Implementation and verification

Implementation lives primarily in
`runtime-cpp/src/core/object-type-materialize.cpp` (and its header if the
helper needs `ProjectDoc` catalog access). `AssetLoader::parseProjectJson`
must read `spriteAnimationAssets` **before** calling
`materializeProjectEntities`, so Play/export see the catalog at materialise
time. Vendor pin in the editor follows the usual runtime → editor order.

Required verification:

1. Materialising an Object Type / instance with Animation source sets
   `spriteRenderer->imageAssetId` and `sprite.spriteAssetId` to the
   animation’s `sourceImageAssetId` when that id is valid.
2. Image source behaviour is unchanged.
3. Auto Play off still exposes the sheet (no placeholder) when the
   animation source image exists.
4. Empty `defaultClipId` still materialises the sheet from
   `sourceImageAssetId` (regression for the Edit/Play asymmetry).
5. Missing animation or source image does not invent a sheet id; Play
   diagnostics / start policy remain as ADR-0009.
6. Existing editor test
   `tests/sprite-animation-test.cpp` (“Animation-backed Sprite presentation
   resolves its source in Play”) stays green; add a focused case for
   `spritePresentation`-only Animation with empty `defaultClipId`.
7. Prefer a small runtime unit test next to the materialiser (no GL) that
   asserts sheet ids without going through the full editor PlaySession.

## Consequences

- Play, export, and Edit agree that a valid Animation presentation carries
  a drawable sheet at materialise time.
- Spawn-clip binding becomes a playback concern, not the last line of
  defence for “something to draw”.
- ADR-0009’s “no static fallback behind Animation” remains intact: the
  sheet is the animation’s own source image, derived once, not a second
  authored Image source.
