# ADR-0054 — Platformer Floor Support Validation

**Status:** Accepted  
**Date:** 2026-07-31  
**Related:** ADR-0052, ADR-0053, ADR-0022  
**Scope:** Close the one-frame corner catch between Y collision slop and
`findGroundSupport` overlap policy.  
**Out of scope:** Teleport/spawn penetration recovery; wall slide / wall jump;
Top Down rewrite.

---

## Decision

| Contact | Rule |
|---|---|
| **Floor** | Directional top-face crossing **and** landing validated by `World::findGroundSupport` with full match of self shape + support entity + support shape |
| **Ceiling** | Directional bottom-face crossing + orthogonal collision overlap (`platformerContactSlop`) |
| **Wall** | Platformer X collision (`PlatformerXMoveResult`) |

Only `GroundSupport` establishes `PlatformerRt.grounded`.

`movePlatformerY` collects and sorts geometric crossings, accepts Ceiling
immediately, and accepts Floor only after support validation. Ranking uses
`travelRank`; placement uses signed `placementDeltaY` (near-flush via
`kGroundContactSkin`).
