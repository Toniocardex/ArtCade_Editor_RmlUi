# ADR-0053 — Platformer Contact Classification (wall-stick residual)

**Status:** Accepted  
**Date:** 2026-07-30  
**Related:** ADR-0052, ADR-0022, ADR-0019  
**Scope:** Residual mid-air hitch on Platformer side-flush contact; dual
collision vs support overlap thresholds; directional Y crossing; floor-snap
gating; runtime identity for Editor Play.  
**Out of scope:** Capsule controllers, plane solvers, slopes, moving platforms,
Top Down rewrite, project/Logic schema.

---

## Decision

Extend the local ADR-0052 Platformer path surgically:

1. **Orthogonal collision slop** (scaled):  
   `platformerContactSlop(extent) = clamp(extent * 0.001, 0.01, 0.25)`.  
   Side edge-touch (`overlapX <= slop`) is ignored by the Y phase.

2. **Support overlap** remains ADR-0052  
   `clamp(width * 0.05, 0.5, 2)` inside `World::findGroundSupport` only.

3. **Crossing Y** uses post-X `beforeY`, ADR-0022 skins  
   (`kGroundContactSkin`, `kOneWayApproachTolerance`), and earliest face along
   the motion. `hitGround` / `hitCeiling` are transient step results only.

4. **`rt.grounded`** is established solely by final `findGroundSupport`.

5. **Floor snap** only when  
   `wasGrounded && !jumpedThisStep && !climbing && vy >= 0 && !yMove.hitGround`
   (locals; not new `PlatformerRt` fields).

6. **Runtime identity:** linked `runtimeBuildId` + `platformerGroundSupport=ADR-0052`
   must match `runtime-template.json` at Play start, or Play is blocked.

---

## Final decision

Wall side-flush must never produce a floor hit or grounded frame. Landing remains
Y-face crossing + support query; snap only maintains prior support.
