# The free camera's horizon occlusion override

Under the free camera, walls on the True3D mesh path popped in and out because the engine's
span tree was used as an occlusion oracle for quads that arrive in the wrong order. The mod
overrides that verdict for the record decision (`nocullhorizon=1`, shipped on). It costs work,
not correctness, because the mesh path is depth-tested on the GPU. See also
`free-camera-two-defects.md` (the two culling defects) and `free-camera-wall-exemption.md`
(the fallback population).

Method: behavioural isolation (hiding one half of the map at a time), per-frame counters on
the test, Ghidra reading of the refusal, and frame-rate measurement on the shipped build.

## The refusal

The population that popped was the True3D mesh path: with only no-mesh walls on screen there
was no popping, with only mesh walls it appeared.

The refusal is the first line of `FUN_007fb490`:

```c
if (FUN_007fb570(item)) return;          // no record is appended
FUN_007fa860(sector, wall - wallptr, type, depth);   // the 0x10-byte record
```

`FUN_007fb570` answers by building two quads from the queued item and asking
`FUN_00813ea0` - the span tree, alongside its leaf (`0x813870`), interior
(`0x813d00`) and allocator (`0x814460`). Its coverage test is x-range
containment plus one depth comparison against a single stored payload per node,
with only the far end kept and no field for the near end. That is correct
exactly when quads arrive nearest-first.

The mod's sweep offers every sector cold, at top level, in a fixed order, so quads do
not arrive nearest-first and the precondition fails. Measured: the test ran 551
to 2,333 times a frame and answered OCCLUDED between 65 and 1,608 times, a
factor of twenty-five swing with view direction over identical geometry. 715 of
the mesh-routed walls a frame were losing their record, and 84% of the walls
that flickered had all four corners inside the viewport.

## Why overriding it is safe

On this path the span tree occlusion is a PERFORMANCE optimisation, not a
correctness mechanism. Mesh-path records reach the renderer's "normal" list,
which is depth-tested on the GPU, so a wall drawn that should have been hidden
is discarded by the depth buffer rather than painted over something nearer.
Overriding the verdict costs work, not correctness. Measured on the shipped
build against a 175 fps cap: about 170 typical, worst case high 80s, with
roughly 715 extra walls a frame.

## Scope, and why not narrower

`nocullhorizon=1` overrides the verdict only for the RECORD decision (item types
0..3, via `FUN_007fb490`). Item type 6 is the portal traversal decision made in
`FUN_007fb310`, which nothing measured implicates and which the sweep already
makes largely redundant; nocullhorizon mode 2 overrides that too and exists
only for comparison. (Not to be confused with nocullwallocclusion 2.)

Two narrower forms were considered and rejected:

- **Only when the camera is outside every sector.** Wrong, because the ordering
  precondition is broken by the mod's sweep rather than by being outside. Emitting 332
  sectors cold in a fixed order destroys front-to-back arrival even with the
  camera indoors, which the free camera passes through constantly.
- **Only for walls the frustum says are fully on screen.** A projection on a
  path that runs about 2,000 times a frame is a cost trap (a build with one ran at 17 fps),
  and 14% of the flicker was edge cases, which is exactly where such a test is least
  reliable.

## Caveats

- Letting more items past the test also feeds more quads into the span tree via
  `FUN_007fb760`, so the no-mesh population sees MORE contention than before.
  Nothing visible was seen, but the takeover rate is not free.
- Item types 4 and 5 never call this test - `FUN_007fb310` dispatches them
  straight to the z threshold and the span tree - so the tree-drawn
  population's own resolution is untouched by this override.
