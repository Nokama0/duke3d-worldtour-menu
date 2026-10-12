# The free camera's wall exemption

Why "Disable culling" showed rooms with floors, enemies and no walls, and what the fix is.
Only the walls with no True3D mesh entry go through the screen-space occlusion tree, and
that tree drops them when the camera is outside every sector. The fix is an exemption
(`nocullwallocclusion`, default 3), not a repair: nothing in the engine is wrong. The note
also records the theories that were measured and ruled out. Related:
`free-camera-two-defects.md`, `horizon-occlusion-override.md`.

Method: per-frame counters at the drain, the tree and the z test; Ghidra reading; flights to a
fixed camera pose from two positions.

## The defect

World Tour draws a wall one of two ways. A wall with a **True3D mesh entry**
goes out through `FUN_007ba740` into the renderer's "normal" list. A wall with
**no mesh entry** (1084 of 2011 on the map this was measured on) takes a fallback
path instead: `FUN_007fac80` queues it as a type 4 or 5 item, the drain
`FUN_007fb310` z-tests it, and the survivors go through `FUN_0080e500` into a
**screen-space occlusion tree** (`FUN_00813870` for leaves, `FUN_00813d00` for
interior nodes).

`FUN_0080e500` has exactly two callers: that drain branch, and its own
recursion. So **only fallback walls ever enter the tree.** Floors and ceilings
come from the per-sector mesh, sprites are their own path, mesh-path walls go to
the "normal" list. Four of the five populations bypass the occlusion entirely.

The tree's coverage test is

```
quad[0] >= node[3] && quad[1] <= node[4] && quad[2] <= node[5]   -> discard
```

an x-range containment plus one depth comparison. It is **correct**, and it is
correct *because* the drain normally meets quads roughly front to back: the
nearer wall is already in the tree and dropping the farther one behind it is
right. With the camera outside every sector that ordering is gone, farther
quads arrive first, and the same correct test eats walls that are plainly
visible. Measured: 96% of insertions discarded, against 0.2% for the z test.

**The visual defect is the asymmetry, not the occlusion.** The player sees a
room's floor and its enemies because those never enter the tree, and does not
see that room's walls because those do.

## The fix

`nocullwallocclusion`, scoped to `camera_active()`, default **3 (total)**. Modes:

- **1, subdivide.** Draws pillars; worse than either other mode.
- **2, the let-through nudge.** Where the tree would discard a covered quad, the quad's depth
  is moved a hair past the covering node's so the coverage test fails and the tree does its
  real work. The quad is `local_24` on the drain's stack, rebuilt for every item, so
  modifying it is safe; the node is live and is never touched.
- **3, total.** Opens any node that would have occluded, so nothing is occluded and nothing
  depends on arrival order.

Mode 2 cost about **25 fps against 44** in the scene it was developed on; mode 3 plus the
horizon override runs about 170 fps against a 175 cap with an identical picture
(`free-camera-two-defects.md`). All three stay reachable by ini.

This is an exemption, not a repair. Nothing in the engine is wrong; the free
camera is a viewpoint the engine was never built for, in the same way
`nocullceilings` removes surfaces the engine would happily draw.

## Theories that were wrong

Each was killed by measurement.

1. **The z test at `scene+0x254`.** Measured: it examines ~1740 type-4/5 items a frame and
   drops **3**. Not the filter. The `nocullzclip` switch that suspended it was removed: it
   had never once executed, because it was gated on a flag whose life is the walk while the
   drain runs after the walk.
2. **The horizon occlusion test** (`FUN_007fb570`). Reached only by types 0-3
   and 6; the fallback queues 4 and 5, so it never sees these walls at all. (It does cause
   a different defect, in the mesh population: `horizon-occlusion-override.md`.)
3. **The straddle.** The coverage test consults `quad[2]`, which is the FAR end
   of the depth range (larger is nearer here, established from the tree's own
   merge code, where a quad replaces a subtree when it exceeds the subtree
   maximum). A quad whose far end is behind but whose near end is in front is
   discarded whole. Implemented, fired, correct, and changed nothing, which
   proved the missing walls are *wholly* behind their occluders.
4. **The viewpoint.** The theory was that the spans were built from the player's
   view rather than the camera's. Refuted by one flight: the player flew to one pasted
   camera pose from two well-separated positions and the missing-wall pattern was identical.

Also established: every occluder that could be identified was a
real wall that **did** reach the wall-draw stage that frame. The tree is not
occluding against phantoms or against anything this mod suppressed.

## Two map faults that are not fixed

Both are Duke's own data, not the renderer:

- **Four walls** own a header or a step by the engine's own comparison and have
  nothing in the matching True3D cache sub-entry. The piece is genuinely
  missing.
- **Sector 174** has ten one-sided walls with `ceilingz == floorz`. A sector
  with no height has zero-area walls, so having no mesh for them is correct.

Of the 768 walls with nothing in any cache sub-entry, 754 correctly own no
piece: the neighbour owns it, or the two sectors' heights match so no surface
exists. The renderer is right about all of them.

## What a portal opening is, and why it is not filled

A two-sided wall is a portal, and the gap between the lower of the two ceilings
and the higher of the two floors has no surface in either engine, by design:
it is the doorway. A removed branch (`solidwt.cpp` / `solidwt.h`, in git history) generated
fill quads for all ~1300 of them so each room would read as a box. They were badly
positioned, far too numerous, and drew over the UI, and the premise was wrong: the rooms
were not missing their portal fills, they were missing their walls.
