# Research notes

Reference notes on the internals of **Duke Nukem 3D: 20th Anniversary World Tour** (Steam,
32-bit, Direct3D 11) that the mod builds on. They are static reading of the executable plus
captures and in-game measurements. Addresses are given as RVA (`address - 0x6B0000`); the
image base moves with ASLR. A note says "static" where a finding was not measured live.

All notes are in [`re/`](re/).

## Engine and renderer

- [engine-facts.md](re/engine-facts.md): mouse counts to yaw and pitch, the status bar and
  its tiles, the drawn camera (float yaw and pitch, hook points), the projection matrix, the
  sprite, sector and wall arrays, secrets, the CON action table, player fields, and the input
  record's action bits, the getinput/processinput/shoot sites, weapon choice, and the
  Megaton-executable sites the shared headers quote.
- [true3d-renderer.md](re/true3d-renderer.md): the classic and True3D renderers, the per-sector
  mesh cache, the five render lists, the world pass, and which side of a portal owns a step or
  header.
- [world-tour-landscape.md](re/world-tour-landscape.md): what is publicly known about the game
  (and what is not): injection routes, ultrawide, `profile.dat`, engine lineage, cheats.

## Free camera and culling

- [free-camera-two-defects.md](re/free-camera-two-defects.md): the holes and the popping in
  "Disable culling", the shipped switches, their cost, and the borderless settle.
- [free-camera-wall-exemption.md](re/free-camera-wall-exemption.md): why walls with no mesh
  entry vanish in the span tree, the exemption modes, and the theories ruled out.
- [horizon-occlusion-override.md](re/horizon-occlusion-override.md): the span tree used as an
  occlusion oracle for mesh walls, and why overriding it is safe.

## In-world drawing

- [in-world-quad-layer.md](re/in-world-quad-layer.md): the D3D11 pass that draws coloured quads
  among the world's geometry (seam, matrix, depth convention, clipping, depth pull).
- [in-world-layer-depth-offsets.md](re/in-world-layer-depth-offsets.md): why a decal and a room
  outline need different depth pulls, with the measurements.
- [debug-dump-key.md](re/debug-dump-key.md): the key that triggers every instrument's report
  at a moment the player chooses.

## Features ported from the Megaton edition

- [screenshot-mode-sites.md](re/screenshot-mode-sites.md): hiding the HUD weapon, crosshair and
  status bar.
- [autosave-save-call.md](re/autosave-save-call.md): the callable save function and its clock.
- [accuracy-spread-sites.md](re/accuracy-spread-sites.md): the eight spread sites patched for
  perfect accuracy, and why the patch is not Megaton's.
- [fullbright-shade-dead-end.md](re/fullbright-shade-dead-end.md): why zeroing `g_shade` does
  nothing, the shader that decides the darkness, and how fullbright is done.
