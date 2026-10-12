# The in-world layer's depth offsets, and why one number cannot serve two producers

The in-world layer (`in-world-quad-layer.md`) pulls each quad toward the camera by a
per-quad fraction of clip `z`: `W3D_PULL_COINCIDENT` (`1e-4`) for a decal on a surface,
`W3D_PULL_ROOM` (`1e-6`) for anything that outlines a volume. The rasteriser's fixed bias is
0. A single layer-wide number fails because a face has no depth extent and a room outline
does.

Method: logged corner depths of sector 148 of E1L1 (24 walls) at known distances, the
rasteriser state read from the capture, flights with the combination tint and the secret
volume.

## What the layer holds

The engine writes **z/w** to its depth buffer and packs the whole world into
roughly `[0.995941, 1.0]` - a usable range of about **0.004**. Every number
below is small because that range is small.

A quad drawn into that world needs pushing toward the camera, or it z-fights
with the surface it sits on. Two mechanisms can do that:

| Offset | Form | Size at z ~ 0.999 |
|---|---|---|
| `W3D_DEPTH_PULL`, clip z scaled by `1 - 1e-4` (now per quad, `WorldQuad3::pull`) | fraction | `1e-4` |
| `DepthBias -150` in the rasteriser state (now 0) | fixed | `~8.9e-6` |

The rasteriser figure is arithmetic, not a guess. A `DepthBias` unit on a 32-bit float
depth buffer is `2^(exponent - 23)`; at z ~ 0.999 the exponent is -1, so a unit
is `2^-24 ~ 5.96e-8` and -150 is about `-8.9e-6`. `DepthBiasClamp -0.01` never
binds.

## Why one number fails

A **coincident face** - the tint on a wall sprite - has no depth extent of its
own. Any offset past precision wins, and the size of the offset does not matter.

A **room outline** does have depth. Its near and far walls are separated by some
amount, and that separation shrinks with distance as roughly `1/dist^2`. Once
the room's whole near-to-far extent is thinner than the offset, the offset drags
every wall in front of every other and the room draws as a solid box: all
twenty-four panels at once, which reads as the room being too big.

Measured on sector 148 of E1L1, 24 walls, as a multiple of the offset in force:

| distance | room z/w extent | multiple of `1e-4` |
|---|---|---|
| 9,952 | 0.00166599 | 16.7 |
| 17,312 | 0.00025743 | 2.6 |
| 25,192 | 0.00016207 | 1.6 |
| 28,248 | 0.00010022 | **1.0** |
| 29,496 | 0.00008993 | 0.90 |
| 73,323 | 0.00001535 | 0.15 |

Monotonic, and the crossover at ~28,000 units is exactly where the fault appears. Dropping
the room's pull to `1e-6` moves the crossover to ~74,000 rather than removing it, which is what
exposes the rasteriser bias, nine times larger than the new pull and then the dominant term.

## What is shipped

- The pull is **per quad** (`WorldQuad3::pull`), not per layer.
  `W3D_PULL_COINCIDENT` is `1e-4`, `W3D_PULL_ROOM` is `1e-6`.
- The rasteriser's fixed bias is **0**. A fixed offset cannot work in a buffer
  this compressed. The tint loses `8.9e-6` against a pull eleven times larger
  and is unaffected (verified in play).
- `1e-6` is about 17 ULPs of a float at z ~ 0.999, so still clear of
  z-fighting, and 13x below the thinnest room extent measured. Its crossover
  lands near 274,000 units; the map spans about 100,000 corner to corner.

## The rule for a new producer

**Ask whether the quad has depth of its own.** A decal on a surface takes
`W3D_PULL_COINCIDENT`. Anything that outlines a volume takes
`W3D_PULL_ROOM` or smaller, and its pull must stay below the thinnest
near-to-far extent that volume can present at the furthest distance it draws
from. A quad that leaves `pull` unset gets the coincident default, which is the
safe direction for a face and the wrong one for a volume, so set it.

## Explanations ruled out by measurement

- The staged world corners were byte-identical across 74 samples, so staging was
  never camera-dependent.
- The perspective divide: `spanX * dist` held a tight cluster whenever no corner sat near the
  plane.
- `canvas_line` saturation: it reaches `ov_tri` and the GPU, which clips.
