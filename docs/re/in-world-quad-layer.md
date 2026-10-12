# The in-world quad layer

Megaton draws the combination-lock tint on the switch's own face, in the world, occluded by
walls. World Tour's equivalent is a D3D11 pass that draws coloured quads among the world's own
geometry. It needs five things right (the seam, this frame's matrix, the depth convention,
clipping at the eye plane, a fractional depth pull). The dead ends are recorded too, because
several were confidently wrong for a long time. Related: `in-world-layer-depth-offsets.md`
(the pull per producer), `debug-dump-key.md` (the instrument), `true3d-renderer.md`.

Method: RenderDoc capture of the running game, Ghidra reading of the clear and submit paths,
in-game flights with logged depth readbacks.

## What it is

`worldwt.cpp` is a D3D11 pass that draws coloured quads among the world's own
geometry: a queue of Build-space quads, a pass-through clip-space vertex shader,
and one draw a frame at a seam inside the engine's frame. `esp.cpp`'s
`esp_tint_push` is its first producer. The pure rules are in `worldwt.h`, pinned
by a host of 19 checks.

## The five things that have to be right

**The seam is the engine's own clear.** `FUN_00805900` (rva `0x155900`) is
True3D's clear: ECX is a descriptor holding the colour at `+0x04`, the depth
value at `+0x14`, the render target view at `+0x30` and the depth view at
`+0x34`; the one stack argument is a mask with bit 0 colour, bit 1 depth, bit 2
stencil. It reaches D3D through the context at `[[0x0158EFA0] + 0xB398]`,
calling vtable `+0xC8` (`ClearRenderTargetView`) and `+0xD4`
(`ClearDepthStencilView`). The frame clears depth **twice**, both times just
before the first-person weapon is drawn so the weapon cannot intersect the
world. Everything before the first of those still holds the world's depth: 66.5%
far plane there, 99.6% after. The quads go out on the first qualifying clear.

*Qualifying* needs all three of: the mask has a depth bit, the view being
cleared is the one bound at the output merger, and exactly one colour target is
bound. Mask alone is not enough: the runtime makes four indistinguishable
depth clears a frame. Ordering comes from `FUN_0078bf00`'s call with the camera
object at `R+0x10`, the world's own render-list submit, which arms a latch.

**The matrix must be this frame's.** A mid-frame pass tests against depth this
frame wrote, so it needs the matrix this frame drew with. The Present-time copy
is a frame behind: measured over 3323 draws, consecutive frames' matrices
differed by as much as **5.771**. That flickers on its own whatever else is
right, and it hurts most up close, because a given camera movement displaces
near geometry far more in depth than distant geometry. `R+0x178` reads identity
at the clear seam but is populated at the world submit, so
`project_capture_render_matrix` takes it there, with the `R+0x190` flag check,
because the right moment and a valid matrix are different conditions.

**The depth convention is `z/w`, written through unchanged.** No remap. (The matrix is
gluPerspective-shaped with clip `z` in `[-1,1]` to six decimals, but that is not what the
engine writes to the depth buffer; see the first dead end.)

**Quads are clipped at the eye plane, not discarded.** `world3d_clip_near` is
Sutherland-Hodgman against `w >= 1e-4` in homogeneous clip space, where the
edges are still straight; interpolating after the divide would be wrong. Only
`w` is clipped: a vertex nearer than the near plane has a usable `w` and the
GPU clips it against `z` because `DepthClipEnable` is on. Walking up to a lock
straddled 4 of 7 quads, and each tint vanished whole as its nearest corner
crossed. The interpolated `w` is **assigned** to the plane, not interpolated
onto it: arithmetically they are the same and in floating point the latter can
land a hair behind the plane, which is the vertex the clip exists to remove.

**The tint is pulled a fixed FRACTION nearer, not a fixed amount.** The tint is
coplanar with its sprite by design, and this renderer compresses the whole world into four
thousandths of the depth range (near 0.01, far 100). A polygon offset is the right tool for a
coplanar decal and it is what Megaton uses, but at this precision a fixed offset in depth
units cannot reliably separate two copies of one plane: measured across three
sprites at clip `w` 0.56 to 0.90, the residual held at 4.5-5.2e-05 while the same
gap in world units varied 2.2x: constant in depth, and larger than the engine's
own bias buys. Scaling clip `z` by `1 - 1e-4` is scale-invariant: correct at
every distance, no tuning, no dependence on how the range is distributed.

The engine itself pairs a polygon offset with its wall sprites: `DepthBias -150`,
`DepthBiasClamp -0.01`, `SlopeScaledDepthBias 0`, on all 70 biased draws in the capture.
The mod's own rasteriser state has a fixed bias of **0** (a fixed offset cannot work in a
buffer this compressed; `in-world-layer-depth-offsets.md`), so the fractional pull is the
whole offset. Megaton's `glPolygonOffset(-1, -2)` is the same design with Polymost's
magnitude, and -2 buys 1.2e-07 here against a 1.0e-06 coincidence residual.

## Divergences from Megaton, and why

- **The offset** is a per-quad fractional pull, not Megaton's fixed polygon offset, for the
  precision reason above. The design (pull a coplanar decal toward the camera) is Megaton's.
- **The fractional depth pull has no Megaton counterpart.** Polymost's depth
  distribution does not need it.
- **No `glDepthRange(0, 0.99999)`.** Megaton sets it to match the depth range of
  the masks pass it draws in; every draw in this engine runs at `0..1`, so
  matching *this* engine means leaving it alone.

## The dead ends, and what made each of them convincing

- **The `[-1,1]` to `[0,1]` depth remap** survived many attempts because the check
  could not fail. A `gluPerspective` fit proved what the MATRIX does (clip
  `z` really is in `[-1,1]` to six decimals) but never what the engine WRITES; a
  shader's output mapping and the viewport's MinDepth/MaxDepth sit between the
  two. Three flights of `2*ours-1` landing within 5.5e-05 of the buffer settled
  it. **Verifying a claim you did not make is not verification.**
- **"The sprite is drawn proud of where it is stored"** was read as live twice
  and refuted twice. Both times the evidence was a cluster of samples that
  happened to sit at similar `w`, so a constant-in-depth offset looked
  constant-in-world. Distance coincidence is very convincing.
- **The near-plane refusal was called dead** on a press that caught a frame with
  none refused. One press is one moment, and a zero is only a zero for that
  frame.
- **"The quads land wholly off the picture"** was concluded from spawn-frame
  geometry while the player stood in front of the lock. That is what the debug dump key
  (`debug-dump-key.md`) exists to prevent.
- **The capture is right about what it contains and not exhaustive about what
  happens**: two depth clears in its frame, four in the running game.

## What the capture is good for

Structure, not counts. It settles the window layout, the bound state in each,
the bias values, and, through the mesh output viewer, that our clip transform
and the engine's agree (`w - z = (1-P)w - Q` to 3e-06 on its vertices, 7e-08 on
ours) and that our world scale constants are exactly its own: its POSITION
floats back-convert through `-0.001 / 0.001 / -0.0625*0.001` to exact integer
Build coordinates.
