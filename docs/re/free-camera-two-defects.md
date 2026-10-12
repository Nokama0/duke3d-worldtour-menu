# The free camera's two culling defects

The free camera's "Disable culling" shipped with two separate faults fixed, often mistaken for
each other: holes (our own infinities in the span tree) and popping (the span tree used as an
occlusion oracle for the mesh population). The shipped feature is six switches behind one row
and costs about 170 fps typical against a 175 fps cap.

Method: per-frame counters in the hooks, Ghidra reading of the span tree and the record
decision, behavioural flights, frame-rate measurement on the shipped build. Related notes:
`horizon-occlusion-override.md` (defect 2 in full), `free-camera-wall-exemption.md` (which
walls reach the tree), `in-world-quad-layer.md`.

## Defect 1 - the holes. Our own infinities in the span tree.

The fallback wall path stores quads in a screen-space span tree: one payload per
node, only the far end kept, no field for the near end. `FUN_00814460` opens a
node with `n5 = +FLT_MAX`. The mod's "nudge", which moved a covered quad slightly
nearer so it would draw, added an epsilon to that sentinel and overflowed it to
`+inf`, creating the very infinities it was working around. Refusing a
non-finite result, and refusing to nudge against a node that still carries the
sentinel, took coverage-based loss from thousands a frame to zero.

`nocullclampspan` ships **on**. A span whose x/w falls outside
[-1,+1] is off screen, and a structure recording SCREEN coverage cannot be
occluded by what is not on the screen, so cutting it there removes nothing
visible.

## Defect 2 - the popping. The same tree used as an occlusion oracle.

`FUN_007fb490`'s first line is `if (FUN_007fb570(item)) return;`, and past it
`FUN_007fa860(sector, wall - wallptr, type, depth)` appends the record that
`FUN_007ba740` later drains. `FUN_007fb570` answers by asking `FUN_00813ea0` -
the span tree. So the tree is not merely the fallback population's storage; it
is the **occlusion oracle for the mesh population**, and its coverage test is
correct only when quads arrive nearest-first. The mod's sweep offers 332 sectors cold
in a fixed order, so they do not.

Measured: the test ran 551-2,333 times a frame and answered OCCLUDED between 65
and 1,608 - a factor of twenty-five swing with view direction over identical
geometry. 715 of 873 mesh-routed walls a frame lost their record.

`nocullhorizon=1` overrides the verdict for the RECORD decision only, leaving
item type 6 - the portal traversal decision - to the engine. Safe because on
this path the tree is a performance optimisation, not a correctness mechanism:
mesh records reach the depth-tested "normal" list, so a wall drawn that should
have been hidden is discarded by the depth buffer. Caveats: more items past the
test feed more quads into the tree, so the no-mesh population sees more
contention; and types 4/5 never call the test, so the tree-drawn population's
own resolution is untouched.

## What did not work, and is not worth retrying

Map order, nearest-sector order, nearest-projection heap key, span clamping as a
popping fix, forced subdivision (worse - "pillars"), a 50-sector sweep cap
(lost most of the map, popping unchanged). All of them moved the loss around
because all of them acted on the wrong population: takeovers were a proxy for
contention, and contention was never the symptom.

## Counter-design errors, and what exposes each

1. Flags scoped to the walk (`s_armed`) read in the drain, which runs after it.
   Exposed by a counter reading a flat zero.
2. A fold placed below the memset that clears its own table. Exposed by the
   unconditional first-fold line the rule now requires.
3. A fold gated on a frame id that never advances: `wt_frame_id` came from a
   Present hook on a WARP probe swap chain the game does not present through.
   Exposed by printing the fold count itself.
4. A mismatch counter comparing against `s_meshSect`, a table nobody clears, so
   it read "the sector that last appended this wall at any time". Exposed by
   asking what the number could physically mean.
5. A sampler that guarded `base` and `rel` where the engine does not, making a
   null entry pointer indistinguishable from a zero dword. Exposed by reading the
   disassembly instead of the decompiler.
6. Printing changes and never levels, so every input read "0 changed" while the
   output moved. Exposed by asking what a stable input and a moving output could
   both be true of.

Reading the target function before instrumenting it paid most: it ruled out
`FUN_007f93a0` (a builder, not a filter) and the sub-entry hypothesis without a flight, and
it found the actual refusal.

## The cost, measured on the shipped build

Against a 175 fps cap, with every row on: about **170 typical, worst
case high 80s** in wide open rooms. The feature is very nearly free. The claim that it
"roughly halves the frame rate" came from two baselines that each carried a `VirtualQuery`
on a per-wall path and is not true of the shipped build (nor was a 44 to 25 fps drop).

`nocullwallocclusion` defaults to **3 (total)**: flying 2 against 3 after the horizon fix
gave an identical picture at 170 fps against about 100. Mode 2's per-quad work was only
justified by a picture difference that the horizon fix removed. Mode 1 is worse than either
(it draws pillars). All three stay reachable by ini.

## What the feature ships as

Six switches behind one row: `nocullsweep` 1, `nocullwalls` 1, `nocullceilings` 0,
`nocullwallocclusion` 3, `nocullclampspan` 1, `nocullhorizon` 1. The Cutaway view,
Cutaway sky and Nearest sector rows do not exist: all three were built for a picture in
which the walls were missing, and the seed choice behind Nearest sector cannot matter when
the context arguments are frame-constant. `nocullceilings` is 0 for the same reason: with
walls drawing correctly the whole map is the better picture.

## The pure-rule host

It pins 8 checks. The cutaway's `nocullwt_wall_cuts_away`, `nocullwt_cutaway_applies` and
`nocullwt_dist2_xy` (19 checks) were removed from the header with the toggle; the rules they
covered no longer exist.

## The borderless settle, and why drawing need not wait on it

The panel opens as soon as the borderless re-apply loop ends: it used to be twenty posts at
250 ms, with `present_frame` gating all drawing on `s_windowSettled`. It now ends as soon as
the window reads back already-correct four passes running, "almost instant, initialises
during the loading screen".

**The hazard that gate was built for no longer exists.** The recorded white hang was a
SYNCHRONOUS `SetWindowPos` from the worker thread blocking on the window thread while it sat
inside `overlay_end`. Two changes each removed a leg of that: `SWP_ASYNCWINDOWPOS` posts the
request instead of blocking on it, and `wt_apply_borderless` now runs ON the window thread,
reached through the posted `WTM_BORDERLESS` rather than called directly. A window move and
the overlay's first frame are therefore serialised by the message loop itself and cannot
collide. So drawing need not be gated on the settle at all. The gate is deliberately kept:
being wrong costs a white hang, which is worse than a short wait.

## The in-world quad layer

Megaton draws three things through a depth-tested in-world layer: the combination-lock tint,
the free camera's teleport marker and secret triggers. World Tour has the equivalent:
`worldwt.cpp`, a D3D11 pass at the seam `FUN_0078bf00` provides. Full account:
`in-world-quad-layer.md`; depth offsets: `in-world-layer-depth-offsets.md`.

The seam is discriminated by LIST OWNER, not by a frame counter: `FUN_007805a0` calls the
submit twice a frame and the first call's owner is `R+0x10`, the camera object, so drawing
after that call puts the quads among the world's geometry and under the HUD. A list owner is
needed because this DLL has no working frame boundary to count against.

`FUN_007f0dc0` builds the projection as gluPerspective - `m[10]=(n+f)/(n-f)`,
`m[11]=2nf/(n-f)`, `m[14]=-1` - right-handed with matrix clip depth in [-1,1]. What the
engine writes to its depth buffer is z/w unchanged: a `[-1,1]` to `[0,1]` remap
(`z' = (z + w) / 2`) is wrong, and the engine's own matrix at `R+0x178` (`0x178` into the
renderer object) feeds the pass as it is (`in-world-quad-layer.md`).
A removed earlier branch (`solidwt`, `solidwt.cpp`) drew at this same seam; its portal-fill premise was
wrong (`free-camera-wall-exemption.md`).

**Why the overlay's pass cannot be reused.** `overlay_begin` saves the game's render targets
and its DSV and then deliberately binds `OMSetRenderTargets(1, &rtv, 0)` with depth disabled
in both directions: it goes on top of everything and must leave nothing in the depth buffer.
Its point in the frame is also after the HUD, which is what "drew over the UI" means. The two
seams are for different things and neither should be folded into the other.

## Open questions

- 4 walls own a piece with no True3D mesh entry; sector 174 has 10 zero-height one-sided
  walls. Neither affects the picture.
- `inMirror` is unwired and cannot be wired: the only frame boundary this DLL had was the
  dead Present counter, now removed.
