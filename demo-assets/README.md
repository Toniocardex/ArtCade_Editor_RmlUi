# Demo sprite sheets

Pixel-art sprite sheets for trying the Sprite Animation Editor. Each is a single
horizontal row of equal frames, so slicing is one step: **Import Sheet → set
`Cols` to the frame count (`Rows` = 1) → Slice into Frames**.

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
