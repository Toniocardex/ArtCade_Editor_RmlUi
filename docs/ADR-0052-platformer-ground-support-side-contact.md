# ADR-0052 — Platformer Ground Support and Side-Contact Separation

**Status:** Accepted  
**Date:** 2026-07-30  
**Scope:** Native runtime platformer movement, the existing `World::findGroundSupport`
path, Platformer-only axis-separated kinematic resolution, canonical
`PlatformerRt` state, and runtime regression tests.

**Related:** ADR-0014, ADR-0016, ADR-0021, ADR-0022.

**Out of scope:** Project schema, editor Commands/Intents, Logic Board schema/API,
BoxCollider2D authoring changes, authored Feet-shape schema, Top Down movement
rewrite, rotation/OBB physics, slopes, moving platforms, and a general-purpose
physics rewrite.

---

## Decision summary

A platformer entity must be considered grounded only when the existing
`World::findGroundSupport` authority finds a valid supporting surface below its
feet.

A side collision, corner touch, ceiling hit, or zero-width/tangential overlap
must never:

- set `grounded`;
- refresh coyote time;
- suppress gravity;
- zero vertical velocity;
- publish `PlatformerState::Stopped` or `PlatformerState::Moving`.

The existing ADR-0022 support path will be tightened rather than duplicated.
`GroundSupport` remains the support result, `World::findGroundSupport` remains
the single support query, and the existing ADR-0022 contracts for resting
flush contact and One Way approach from above remain authoritative.

Platformer kinematic movement will resolve the horizontal and vertical axes
separately:

```text
horizontal intent
→ Platformer-only X resolution
→ strict findGroundSupport
→ climbing / jump / gravity
→ Platformer-only Y resolution
→ bounded floor snap
→ canonical PlatformerRt state
```

Horizontal resolution may modify `position.x` and horizontal velocity only.
Vertical resolution may modify `position.y` and vertical velocity only.

The existing `groundedFrames` / `stableGrounded` hysteresis will no longer be
an authority for gravity, coyote refresh, or published locomotion state.
Coyote time remains the explicit grace mechanism after leaving valid support.

`resolveKinematicCollisionBody` remains available for Top Down and other
existing users. The new axis-separated path is Platformer-only.

---

## 1. Context

During Editor Play, a Platformer entity became suspended in mid-air after
repeated jumps while horizontal movement was held.

The immediate authoring mistake was an enabled `BoxCollider2D` attached to the
tilemap Object Type. The editor correctly displayed that collider as a green
solid-collider overlay. Removing it removed the unintended obstacle.

The incident nevertheless exposed a separate runtime defect:

```text
airborne platformer
→ horizontal or corner contact with a Solid collider
→ contact classified as support
→ grounded stabilizes
→ vertical velocity becomes zero
→ gravity is suspended
→ entity remains attached to the collider in mid-air
```

A Solid wall is allowed to block horizontal motion. It is not allowed to act
as a floor merely because the platformer touches its side or corner.

This is a runtime correctness defect. Logic Board rules, Script, animation
selection, and rendering only observe the invalid state.

---

## 2. Current ownership and baseline

The current runtime already has the correct ownership boundary:

```text
World
├─ GroundSupport
├─ findGroundSupport(...)
├─ PlatformerRt
├─ stepPlatformerController(...)
├─ resolveKinematicCollisionBody(...)
└─ platformerState(...)
```

The fix extends that path.

It must not introduce:

```text
CollisionWorld::queryGroundSupport
GroundSupportHit parallel to GroundSupport
PlatformerPhysicsManager
editor-side movement correction
```

### 2.1 Current ADR-0022 constants

The existing contract defines:

```cpp
inline constexpr float kGroundContactSkin = 1.0f;
inline constexpr float kOneWayApproachTolerance = 2.0f;
inline constexpr float kGroundHorizontalOverlapEpsilon = 0.01f;
```

`kGroundContactSkin` remains the near-flush recognition/correction tolerance.

`kOneWayApproachTolerance` remains the One Way “came from above” tolerance.

The fixed `kGroundHorizontalOverlapEpsilon` is insufficient for the discovered
defect and will be replaced in the support decision by a shape-scaled minimum
overlap.

### 2.2 Current `GroundSupport`

The existing result is extended, not replaced:

```cpp
struct GroundSupport {
    EntityId supportEntityId = INVALID_ENTITY;
    std::size_t selfShapeIndex = 0;
    std::size_t supportShapeIndex = 0;

    float supportTopY = 0.f;
    float correctionY = 0.f;

    bool oneWay = false;

    // ADR-0052 additions for diagnostics/ranking.
    float horizontalOverlap = 0.f;
    float feetBottomY = 0.f;
};
```

Additional fields may remain private/debug-only if production callers do not
need them. There must still be one support result type.

### 2.3 Current defect hotspots

The baseline currently has four interacting issues:

| Required ADR-0052 contract | Current behavior |
|---|---|
| Strict support below the feet | `findGroundSupport` accepts `overlapX > 0.01` and near-flush vertical gap |
| X contact never zeroes Y velocity | combined sweep/separation can choose a Y correction and zero `vy` |
| X → support → jump/gravity → Y | support/gravity are evaluated before a combined X+Y move |
| `stableGrounded` has no authority | it controls gravity and coyote refresh |
| Canonical state in `PlatformerRt` | `platformerState()` performs another grounded query |
| Top Down unchanged | current generic kinematic resolver is shared and must remain available |
| Dual physics prevented | already enforced; preserve it |

The wall-hang defect is produced by:

```text
false support
+
stableGrounded
+
combined resolver capable of vertical correction on lateral/corner contact
```

---

## 3. Architectural invariants

### 3.1 One runtime authority

`World` and `PlatformerRt` remain the only runtime authority for:

- platformer velocity;
- grounded state;
- coyote timer;
- jump buffer;
- climbing state;
- canonical `PlatformerState`.

`CollisionWorld` continues to own collision geometry and shape data. It does
not gain a second platformer-support API.

Logic, Script, renderer, and editor Play remain consumers.

### 3.2 One support query

All platformer support decisions flow through:

```cpp
World::findGroundSupport(...)
```

The query may receive a derived policy/helper, but no second query path may
reimplement support eligibility.

### 3.3 Axis ownership

```text
horizontal collision
→ may alter X and vx only

vertical collision
→ may alter Y and vy only

ground support
→ may establish grounded only from below
```

### 3.4 Platformer-only resolution

The axis-separated resolution introduced here applies only to Platformer.

`resolveKinematicCollisionBody` remains unchanged for Top Down unless a
minimal shared-helper extraction is required for compilation.

This ADR does not change Top Down semantics.

### 3.5 Support is semantic

Ground support is not equivalent to generic overlap.

It requires:

- an eligible self shape;
- an eligible supporting shape;
- meaningful horizontal overlap;
- valid near-flush or floor-snap vertical distance;
- ADR-0022 One Way approach eligibility.

### 3.6 Fixed-step determinism

Given the same initial runtime state, collider data, fixed `dt`, and input
sequence, support selection and movement results must be deterministic and
independent of render frame rate.

---

## 4. Terminology

### Contact

Any blocking or sensor interaction between collision shapes.

### Side contact

A blocking contact whose separating direction is horizontal.

### Ceiling contact

A vertical contact above the platformer while it is rising.

### Ground hit

A vertical blocking contact below the platformer while it is descending.

### Ground support

A valid surface below the platformer's support shape that satisfies
`findGroundSupport`.

### Near-flush support

Support whose feet/surface vertical gap is within `kGroundContactSkin`.

This preserves the ADR-0022 resting contract:

```text
feet.maxY == support.minY
→ supported
```

### Floor snap

A bounded post-Y downward correction to a valid support surface.

Floor snap is not used to classify arbitrary distant surfaces as grounded.

---

## 5. Support-policy helper

A private/internal helper may derive scale-aware thresholds for one support
query. It is policy input to `findGroundSupport`, not a parallel public
authority.

Recommended shape:

```cpp
struct GroundSupportPolicy {
    float contactSkin = kGroundContactSkin;
    float minHorizontalOverlap = 0.f;
    float bodyInsetX = 0.f;
    float maxFloorSnapDistance = 0.f;
};
```

Derived from the selected self support shape:

```cpp
GroundSupportPolicy groundSupportPolicyFor(
    float supportWidth,
    float supportHeight)
{
    GroundSupportPolicy policy;

    policy.contactSkin = kGroundContactSkin;

    policy.bodyInsetX =
        std::clamp(supportWidth * 0.05f, 0.25f, 2.0f);

    policy.minHorizontalOverlap =
        std::clamp(supportWidth * 0.05f, 0.5f, 2.0f);

    policy.maxFloorSnapDistance =
        std::clamp(supportHeight * 0.10f, 0.5f, 4.0f);

    return policy;
}
```

Non-finite or non-positive dimensions fail closed.

The exact helper name/location may follow existing `world.cpp` conventions.

---

## 6. Self support-shape selection

### 6.1 Prefer Feet

When the platformer's collision body contains at least one enabled Solid shape
with role `Feet`, only Feet shapes participate in `findGroundSupport`.

```text
one or more Feet shapes exist
→ Body shapes cannot establish support
```

### 6.2 Body fallback is the current default

Current `BoxCollider2D` materialization produces a `Body` shape. Therefore the
Body fallback is the normal current path, not an edge case.

When no Feet shape exists:

- use eligible Body shapes;
- apply the derived horizontal inset before measuring support overlap.

Conceptually:

```cpp
probeMinX = bodyAabb.minX + policy.bodyInsetX;
probeMaxX = bodyAabb.maxX - policy.bodyInsetX;
```

If the inset collapses the width, fail closed.

### 6.3 No authoring/schema change

This slice does not add a Feet authoring UI or project schema.

Feet preference is future-compatible with runtime collision bodies that
already contain or later materialize Feet roles.

---

## 7. Strict horizontal support overlap

The support overlap is computed against the selected Feet AABB or the inset
Body probe:

```cpp
const float horizontalOverlap =
    std::min(probeMaxX, support.maxX)
    - std::max(probeMinX, support.minX);
```

Reject when:

```cpp
horizontalOverlap < policy.minHorizontalOverlap;
```

Consequences:

```text
zero-width edge touch
→ rejected

tiny floating-point sliver
→ rejected

meaningful ledge overlap
→ accepted
```

Generic collision overlap may remain inclusive for other collision features.
`findGroundSupport` must not use generic inclusive overlap as its final support
decision.

---

## 8. Vertical support contract

Let:

```text
feetBottomY = selected self support AABB maxY
supportTopY = candidate AABB minY
gapY        = supportTopY - feetBottomY
```

### 8.1 Near-flush recognition

For normal grounded recognition:

```cpp
std::fabs(gapY) <= kGroundContactSkin
```

This preserves ADR-0022 parity for:

- exact resting flush;
- minor floating-point separation;
- minor penetration correction.

`kGroundContactSkin` is not replaced by a scaled value.

### 8.2 Floor-snap eligibility

After the Y phase, a descending or resting platformer may snap downward only
when:

```cpp
gapY > kGroundContactSkin
&& gapY <= policy.maxFloorSnapDistance;
```

The candidate must still satisfy every normal support filter, including strict
horizontal overlap and One Way eligibility.

### 8.3 No broad upward probe

The support query must not extend a large probe upward through the Body.

A wall or collider whose top is not near/below the feet cannot become support
merely because its side overlaps the platformer.

---

## 9. ADR-0022 One Way compatibility

This ADR tightens overlap and separates movement axes. It does not replace the
ADR-0022 One Way contract.

A One Way shape may support/block only when:

- the platformer is descending or vertically stationary;
- the previous support-shape bottom was above the platform top within
  `kOneWayApproachTolerance`;
- horizontal support overlap satisfies ADR-0052;
- the contact is from above.

Preserve:

```cpp
previousFeetBottomY
    <= supportTopY + kOneWayApproachTolerance
```

A One Way platform must never:

- block Platformer X movement;
- establish support from the side;
- block upward movement;
- establish support from below.

Existing One Way tests remain mandatory compatibility tests.

---

## 10. Candidate filtering and deterministic ranking

A support candidate is rejected when:

- it belongs to the platformer itself;
- either shape is disabled;
- either shape is non-Solid;
- collision masks/layers reject the pair;
- the support geometry is invalid or non-finite;
- the self role is not the selected Feet/Body role;
- horizontal overlap is below the scaled minimum;
- vertical gap is outside the requested near-flush/snap policy;
- One Way approach eligibility fails.

When multiple candidates remain, rank deterministically:

```text
1. smallest absolute correctionY
2. Solid before One Way when otherwise equal
3. greatest horizontalOverlap
4. supportTopY
5. supportEntityId
6. supportShapeIndex
7. selfShapeIndex
```

The exact tuple may be adjusted to preserve existing seam behavior, but it
must be stable and must not depend on unordered-container iteration.

---

## 11. Platformer-only axis-separated resolution

Add private `World`/`WorldInternal` helpers for Platformer. No new manager is
introduced.

Example internal result:

```cpp
struct PlatformerAxisMoveResult {
    bool blocked = false;
    bool hitNegative = false;
    bool hitPositive = false;
    EntityId otherEntityId = INVALID_ENTITY;
};
```

A richer internal result is acceptable, provided it does not become a second
support authority.

### 11.1 X phase

```text
apply desired dx
→ sweep/resolve X only
```

Rules:

- Solid shapes may block X.
- One Way shapes do not block X.
- Trigger/sensor shapes do not block.
- correction is applied only to `transform.position.x`.
- blocked movement sets `vx = 0`.
- `vy`, grounded, coyote, and support are untouched.

Forbidden:

```cpp
verticalVelocity = 0.f;
grounded = true;
```

### 11.2 Y phase

```text
apply desired dy
→ sweep/resolve Y only
```

Moving downward:

```text
valid Solid/One Way top hit
→ place support shape on surface
→ vy = 0
→ expose ground hit/support
```

Moving upward:

```text
Solid underside hit
→ place platformer below ceiling
→ vy = 0
→ grounded remains false
```

One Way is ignored while rising or approaching from below.

The Y phase never modifies `vx`.

### 11.3 Corner behavior

The Platformer path no longer uses a combined minimum-penetration
`resolveAabbSeparation` decision to choose between X and Y.

A corner reached during X is resolved as X. A subsequent Y movement is
resolved independently.

This eliminates the corner tie capable of turning lateral movement into a
vertical correction.

### 11.4 Top Down remains unchanged

`resolveKinematicCollisionBody` remains the current Top Down/shared path.

Do not rewrite Top Down as part of ADR-0052.

Shared geometry helpers may be extracted only when they preserve existing
Top Down behavior byte-for-byte/semantically.

---

## 12. Platformer fixed-step order

The controller rewrite must preserve climbing/ladder behavior.

Authoritative order:

```text
1. Read movement and jump intents.
2. Detect/update ladder contact and climbing engagement.
3. Resolve Platformer X movement.
4. Run strict findGroundSupport at the X-updated position.
5. Update jump buffer and coyote eligibility.
6. If jump fires: detach climbing and apply jump velocity.
7. Else if climbing: preserve existing climbing velocity/gravity suspension.
8. Else if unsupported: apply gravity.
9. Resolve Platformer Y movement.
10. Run post-Y findGroundSupport and bounded floor snap.
11. Publish rt.grounded, rt.velocity, rt.state, lastAirState.
12. Refresh or decay coyote time from final valid support.
13. Push final Transform/velocity to the optional kinematic physics body.
```

### 12.1 Climbing compatibility

Existing climbing behavior is preserved:

- ladder contact is discovered through interaction/sensor shapes;
- climbing engages only with input on the ladder axis;
- gravity is suspended while climbing;
- vertical climb input sets climb velocity;
- jump detaches from the ladder;
- `PlatformerState::Climbing` remains dominant over grounded/airborne states.

This ADR does not redesign ladders.

### 12.2 Walking off an edge

Because support is queried after the X phase:

```text
tick N: supported
tick N+1: X movement leaves ledge
→ support is false in tick N+1
→ gravity begins in tick N+1
```

Coyote time still permits a jump, but does not report grounded or suppress
gravity.

---

## 13. `stableGrounded` authority removal

The current two-frame hysteresis was introduced to mask overlap flicker. It
must no longer control:

- gravity;
- vertical-velocity zeroing;
- coyote refresh;
- `PlatformerState`;
- `isPlatformerGrounded`.

A valid final support result establishes grounded immediately.

Leaving support establishes airborne immediately for movement/gravity
purposes.

Coyote time is the explicit grace policy.

`groundedFrames` and `airborneFrames` may remain temporarily for
characterization/debugging during implementation. They should be removed when
the new support and seam tests prove they are no longer needed.

No replacement watchdog or hidden hysteresis authority is introduced.

---

## 14. Canonical `PlatformerRt` state

Extend the existing runtime state:

```cpp
struct PlatformerRt {
    float coyoteTimer = 0.f;
    float jumpBufferTimer = 0.f;

    Vec2 velocity{};

    bool jumpPendingPrev = false;
    bool climbing = false;
    bool grounded = false;

    PlatformerState state = PlatformerState::Stopped;

    // ADR-0016 apex hysteresis remains authoritative for airborne phase.
    PlatformerState lastAirState = PlatformerState::Jumping;
};
```

`groundedFrames` / `airborneFrames` are removed after migration of tests.

### 14.1 State publication

At the end of the Platformer step:

```text
climbing
→ Climbing

grounded + |vx| > horizontal epsilon
→ Moving

grounded
→ Stopped

airborne + vy < -vertical epsilon
→ Jumping

airborne + vy > vertical epsilon
→ Falling

airborne near apex
→ ADR-0016 lastAirState
```

### 14.2 Read APIs

`World::platformerState(id)` returns `rt.state`.

`World::isPlatformerGrounded(id)` returns `rt.grounded`.

They must not perform a second `collisionGrounded()` / `findGroundSupport()`
query.

Logic and Script adapters therefore observe the same post-step canonical
state.

---

## 15. Coyote time and jump buffer

### 15.1 Coyote refresh

Refresh coyote time only from final valid support:

```cpp
if (rt.grounded)
    rt.coyoteTimer = pc.coyoteTime;
else
    rt.coyoteTimer = std::max(0.f, rt.coyoteTimer - dt);
```

A wall, ceiling, trigger, ladder side, or corner cannot refresh it.

### 15.2 Jump eligibility

A buffered jump may fire when:

```text
valid support
OR coyote timer > 0
OR climbing
```

A jump:

- sets upward velocity;
- clears support for the current step;
- detaches climbing;
- clears coyote and jump buffer;
- produces airborne `Jumping`.

Existing rising-edge jump-request behavior remains unchanged.

---

## 16. Floor snap

Floor snap is an extension of `findGroundSupport`, not a separate query.

It is allowed only when:

- not climbing;
- no jump fired during the current step;
- `vy >= 0`;
- candidate satisfies all strict support filters;
- candidate satisfies ADR-0022 One Way approach rules;
- positive `correctionY` is within the scaled
  `maxFloorSnapDistance`.

Near-flush correction still uses `kGroundContactSkin`.

Floor snap modifies:

```text
position.y
vy
grounded/support
```

It never modifies X or `vx`.

---

## 17. Physics integration

The current dual-physics protection is already correct and must remain:

- Platformer owns Transform;
- Platformer runs before `physics.step`;
- optional physics body follows the final Transform;
- Box2D/native gravity is disabled for the Platformer body;
- `syncPhysicsToEntities` skips Platformer entities.

ADR-0052 does not introduce another integration path.

Regression tests must ensure Physics Off/Auto/On produce equivalent
Platformer kinematic results for the same collision setup.

---

## 18. Invalid-data policy

Invalid geometry fails closed for support:

```text
non-finite bounds
non-positive extent
collapsed Body inset
invalid shape index
missing entity
disabled shape
unsupported response/role

→ not a support candidate
```

A malformed collider must not become an implicit floor or freeze the
Platformer.

Debug diagnostics are allowed; release per-frame logging is not.

---

## 19. Compatibility

No change is required to:

- `ProjectDocument`;
- `.artcade` schema;
- Logic Board schema or Lua API;
- BoxCollider2D authoring;
- editor history;
- Object Type/instance model;
- Top Down semantics.

Behavior intentionally changes only where an invalid side/corner/ceiling
contact was previously treated as support.

Required compatibility:

- exact resting flush remains grounded;
- ADR-0022 One Way from-above behavior remains;
- coyote jump remains;
- jump buffering remains;
- ladder/climbing remains;
- ADR-0016 apex state remains;
- general collision events remain generated from final transforms.

---

## 20. Rejected alternatives

### Relocate support to `CollisionWorld`

Rejected. `World::findGroundSupport` is the existing ADR-0022 owner and must be
extended, not duplicated.

### Rewrite Top Down

Rejected. Axis separation in this ADR is Platformer-only.

### Add authored Feet schema/UI

Rejected for this slice. Current BoxCollider2D materializes Body, and the Body
fallback is explicitly supported.

### Gravity watchdog

Rejected because it masks invalid grounded state.

### Detach after N wall-contact frames

Rejected because it introduces timer-dependent behavior and breaks future wall
mechanics.

### Retune only the fixed overlap epsilon

Rejected because a fixed epsilon is not robust across collider scales and does
not fix combined-axis correction.

### Keep `stableGrounded` as gravity authority

Rejected because repeated false support can still satisfy the hysteresis.

### Automatically remove tilemap BoxCollider2D

Rejected because the collider may be intentional and authoring cleanup does
not repair runtime wall handling.

### Logic Board/animation workaround

Rejected because those systems only observe the invalid runtime state.

---

# Implementation plan

## Phase 0 — Failing characterization

Add a runtime regression test, for example:

```text
vendor/artcade-runtime/tests/
    world-platformer-side-contact-test.cpp
```

Fixture:

- fixed `dt = 1/60`;
- floor;
- floating Solid wall/box;
- Platformer;
- jump;
- hold Right into the wall.

Capture baseline:

```text
raw grounded
stable grounded
grounded/airborne frames
velocity
coyote timer
final transform
```

Pre-fix expected reproduction:

```text
vy ≈ 0
grounded true
Y stops changing
```

Post-fix expectation:

```text
grounded false
vx = 0
gravity continues
Y increases while falling
```

Keep all existing One Way tests green, especially
`world-oneway-landing-test.cpp`.

## Phase 1 — Tighten `World::findGroundSupport`

In the existing support path:

1. detect whether any eligible Feet shape exists;
2. when Feet exists, evaluate only Feet;
3. otherwise evaluate Body with scaled X inset;
4. derive scaled `minHorizontalOverlap`;
5. preserve `kGroundContactSkin` for near-flush;
6. add scaled maximum post-Y floor-snap distance;
7. preserve `kOneWayApproachTolerance`;
8. fail closed on invalid geometry;
9. extend deterministic ranking;
10. extend `GroundSupport`, not replace it.

Query-level tests:

- side wall;
- zero overlap;
- sub-threshold sliver;
- meaningful ledge overlap;
- exact resting flush;
- small penetration within skin;
- One Way from above;
- One Way from below;
- deterministic adjacent seam.

## Phase 2 — Platformer-only X/Y resolution

Add private helpers on `World`/`WorldInternal`:

```text
movePlatformerX
movePlatformerY
```

`movePlatformerX`:

- corrects X only;
- sets `vx = 0` when blocked;
- never modifies `vy` or grounded;
- ignores One Way side contact.

`movePlatformerY`:

- corrects Y only;
- distinguishes ceiling and ground;
- never modifies `vx`;
- applies One Way only from above.

Do not use minimum-penetration corner tie-breaking in this Platformer path.

Leave `resolveKinematicCollisionBody` unchanged for Top Down.

## Phase 3 — Rewrite `stepPlatformerController`

Implement the accepted order:

1. input;
2. ladder/climbing detection;
3. X movement;
4. strict support;
5. jump buffer/coyote eligibility;
6. jump, climbing, or gravity;
7. Y movement;
8. post-Y support/floor snap;
9. final grounded/velocity/state;
10. coyote refresh/decay;
11. body follow.

Preserve all existing climbing behavior.

Remove `stableGrounded` authority from gravity, coyote, and state.

## Phase 4 — Canonical state

Extend `PlatformerRt` with:

```text
grounded
state
```

Preserve `lastAirState` from ADR-0016.

Change:

```text
World::platformerState
World::isPlatformerGrounded
```

to read the post-step stored state without another collision query.

Verify Logic and Script hosts observe the same value.

## Phase 5 — Hardening and Definition of Done

Cover the essential ADR matrix:

- floor landing;
- right wall;
- left wall;
- zero-width corner;
- ceiling;
- walk-off with same-tick gravity;
- coyote jump;
- jump buffer;
- One Way from above/below/side;
- adjacent seams;
- repeated 100-jump stress;
- scaled bodies;
- Physics Off/Auto/On equivalence;
- climbing regression.

Cleanup:

- remove obsolete grounded-frame authority;
- remove duplicate probes;
- update comments describing inclusive overlap as grounding;
- correct all ADR self-references;
- add a brief runtime bug-fix note.

Verification:

```text
runtime vendored test suite
scripts\build.bat --test
Editor Play smoke using the vendored runtime
standalone/export smoke when runtime binary/template is refreshed
WASM verification where applicable
```

No claim of completion is made until those commands have actually passed.

---

# Test matrix

## A. Valid floor

```text
feet flush with Solid top
→ grounded true
→ vy = 0
```

```text
descending onto Solid
→ Y ground hit
→ exact placement
→ grounded true
```

## B. Side contact

```text
airborne + Right into wall
→ X blocked
→ vx = 0
→ grounded false
→ vy/gravity continue
```

Mirror for Left.

## C. Corner/tangent

```text
probe edge == support edge
→ horizontal overlap 0
→ unsupported
```

```text
overlap below scaled minimum
→ unsupported
```

## D. Ledge

```text
overlap >= scaled minimum
+ valid vertical support
→ supported
```

## E. Ceiling

```text
rising into underside
→ Y blocked
→ vy = 0
→ grounded false
→ subsequent gravity/fall
```

## F. Walk-off

```text
supported at tick N
X leaves edge at tick N+1
→ unsupported in tick N+1
→ gravity in tick N+1
→ coyote still available
```

## G. Coyote and buffer

```text
jump shortly after valid support loss
→ succeeds
```

```text
jump shortly before valid landing
→ buffered jump succeeds
```

Side contact cannot refresh either timer.

## H. One Way

```text
descending from above
→ land/support
```

```text
rising from below
→ pass through
```

```text
horizontal side approach
→ no X block
→ no support
```

## I. Seams

```text
move across adjacent Solid colliders
→ no visible grounded flicker
→ deterministic support
```

## J. Stress

```text
100 jumps
alternate A/D
include wall and corner contacts
→ no suspended state
```

## K. Scale

Representative self sizes:

```text
0.25×
1×
4×
```

Policy values remain bounded and finite.

## L. Physics modes

```text
Off / Auto / On
→ equivalent Platformer kinematic outcome
```

## M. Climbing

```text
ladder engage
→ gravity suspended

jump from ladder
→ climbing false
→ upward velocity

leave ladder
→ normal gravity/support resumes
```

---

# Primary implementation areas

```text
docs/ADR-0052-platformer-ground-support-side-contact.md

vendor/artcade-runtime/src/world/include/world.h
    GroundSupport extension
    PlatformerRt grounded/state
    private Platformer-only helpers

vendor/artcade-runtime/src/world/src/world.cpp
    findGroundSupport tightening
    Platformer-only X/Y collision helpers
    existing generic resolver retained

vendor/artcade-runtime/src/world/src/world_platformer_controller.cpp
    accepted fixed-step order
    climbing preservation
    stableGrounded authority removal

vendor/artcade-runtime/src/world/src/world_movement.cpp
    canonical platformerState/isPlatformerGrounded reads

vendor/artcade-runtime/tests/
    new support/side-contact/controller tests
```

Do not add editor-side correction.

Do not change Top Down resolution except for strictly behavior-preserving shared
helper extraction required to compile.

---

# Definition of Done

ADR-0052 is implemented only when:

- a side contact cannot establish grounded;
- a corner/tangential contact cannot establish grounded;
- a ceiling contact cannot establish grounded;
- X resolution never changes `vy`;
- Y resolution never changes `vx`;
- an airborne Platformer pushing into a wall continues falling;
- walking off an edge applies gravity in the same fixed step;
- exact resting flush remains supported;
- bounded floor snap remains stable;
- ADR-0022 One Way behavior remains green;
- coyote and jump buffer remain green;
- climbing remains green;
- valid landings do not require `stableGrounded` authority;
- `PlatformerRt` stores canonical grounded/state;
- `platformerState()` and `isPlatformerGrounded()` perform no second support query;
- Logic and Script observe the same state;
- Top Down behavior is unchanged;
- dual physics protection remains intact;
- no project or Logic schema changes are introduced;
- the original floating-collider scenario blocks X but never suspends gravity;
- runtime, editor, export, and WASM verification required by the project are green;
- obsolete duplicate grounding paths and comments are removed.

---

## Final decision

A Platformer is grounded only through the existing ADR-0022
`World::findGroundSupport` authority, tightened with Feet preference, the real
Body fallback, scaled horizontal-overlap requirements, preserved near-flush
skin, and bounded floor snap.

Platformer movement resolves X and Y independently. A side/corner contact can
block X but cannot modify vertical velocity, gravity, coyote time, or
locomotion state.

The controller publishes one canonical post-step state in `PlatformerRt`.
`stableGrounded` no longer acts as a hidden authority, Top Down remains on its
existing resolver, and no schema, manager, or parallel collision path is
introduced.
