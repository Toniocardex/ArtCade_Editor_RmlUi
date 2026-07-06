# Demo assets

Pixel-art sprite sheets for trying the Sprite Animation Editor, a couple of
static terrain tiles for building a quick test level, and one grid sheet for
the Tileset Editor. The animation sheets below are each a single horizontal
row of equal frames, so slicing is one step: **Import Sheet → set `Cols` to
the frame count (`Rows` = 1) → Slice into Frames**.

| File | Size | Frames (`Cols`) | Cell | Animation |
|------|------|-----------------|------|-----------|
| `coin-spin.png`   | 192×24 | 8 | 24×24 | Spinning gold coin |
| `bounce-ball.png` | 144×24 | 6 | 24×24 | Bouncing ball with squash |
| `heart-pulse.png` | 96×24  | 4 | 24×24 | Beating heart |
| `gem-shine.png`   | 96×24  | 4 | 24×24 | Diamond with a travelling sparkle |
| `orb-states.png`  | 96×48  | 4×2 | 24×24 | **Two animations in one sheet**: row 0 = blue "idle" pulse, row 1 = orange "power" pulse |
| `star-twinkle.png` | 144×24 | 6 | 24×24 | Star pulsing size and brightness, with a sparkle glint at its peak |
| `flag-wave.png`   | 120×24 | 5 | 24×24 | Flag rippling on a pole — the one **odd** frame count in the set |
| `torch-flicker.png` | 96×24 | 4 | 24×24 | Torch flame flickering in height and lean, layered outer/mid/core color |
| `switch-states.png` | 72×48 | 3×2 | 24×24 | **Two animations in one sheet** (3-wide, unlike `orb-states`' 4-wide): row 0 = grey "off" idle wobble, row 1 = green "on" glow pulse |
| `hero-walk.png`   | 96×24 | 4 | 24×24 | Simple humanoid walk cycle (legs/arms swing, tiny body bob) |
| `critter-hop.png` | 96×24 | 4 | 24×24 | Small round critter hop cycle (squash → stretch → peak → land) |

## Movement testing: characters and terrain tiles

`hero-walk.png` and `critter-hop.png` slice like any other sheet above, but are
meant to be **used on an entity with a movement component**, not just previewed:

- **Hero** (`hero-walk.anim`): add a `TopDownController` + `BoxCollider2D`
  (e.g. offset `{0, 4}`, size `14×10` — a footprint smaller than the sprite,
  so the collider is at the feet, not the whole 24×24 cell).
- **Critter** (`critter-hop.anim`): add a `LinearMover` (e.g. speed `30`,
  direction `{1, 0}`) + `BoxCollider2D`, for a simple patrol that bounces off
  whatever it hits.

`tile-wall.png` and `tile-ground.png` are **not** sprite sheets — each is one
static 48×48 image (48 = this editor's default Grid/Snap cell size, so they
place edge-to-edge with no gaps or overlap):

- `tile-wall.png` — brick block. Import as a plain Image asset, use it on an
  object type with a `BoxCollider2D` (mode `solid`, size `48×48`) so a
  `TopDownController`/`LinearMover` entity actually stops at it.
- `tile-ground.png` — grass-over-dirt floor. No collider — just the walkable
  background under everything else.

Build a quick test room: turn on **Grid** + **Snap to Grid**, place
`tile-wall` instances around a rectangle, fill the inside with `tile-ground`,
then drop a Hero and a Critter inside and Play to confirm the wall actually
blocks them.

## Tileset Editor (`dungeon-tileset.png`)

A 128×96 sheet of 12 solid-color, numbered 32×32 tiles (4 columns × 3 rows) —
built for the **Tileset Editor**, not the Sprite Animation Editor. Each cell
is a flat color with no border baked in, so the editor's own grid overlay is
the only boundary indicator: if slicing is correct, every grid line lands
exactly on a color change and every cell shows exactly one clean number.

1. On the `dungeon-tileset.png` row in **Assets → Images**, click **Tileset**.
2. Tile Width/Height default to 32×32, which already matches this sheet
   exactly (4×3, 12 tiles, no remainder) — Apply commits immediately with no
   adjustment needed.
3. To test a *mismatched* size on purpose (remainder pixels, dropped/renumbered
   tiles), try Tile Width/Height 40 — the sheet no longer divides evenly.

## Multiple animations in one sheet (`orb-states.png`, `switch-states.png`)

One sprite sheet can hold several animations (game states) as separate **clips**:

1. Set `Cols`/`Rows` to match the sheet's grid (4×2 for `orb-states`, 3×2 for
   `switch-states`).
2. Select a clip and **click the cells** that belong to it on the sheet — the top
   row for the first clip ("idle" / "off"), the bottom row for the second
   ("power" / "on").
3. Use **Add Clip** to make the second clip, then click its cells.

"Slice into Frames" grabs *every* cell of the grid — handy for a single-animation
strip. For a sub-animation (one row/region), click its cells instead.

All frames are 24×24 with a transparent background. Loop playback at ~8–12 FPS
reads well; set `coin-spin` a touch faster for a snappier spin. `flag-wave` is
the odd one out at 5 frames, useful for checking a slice grid isn't assumed to
always divide evenly.
