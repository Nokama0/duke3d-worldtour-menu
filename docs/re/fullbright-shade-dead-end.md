# Fullbright: why zeroing `g_shade` does nothing, and where the light is

Zeroing `g_shade` cannot brighten the world pass: the vertex shader discards every term
derived from it whenever `g_sectorVis` is 0, which is nearly every draw. The darkening the
player sees is Build's palette shade row, which the composite pass displays, plus a distance
term driven by `g_playerVis`. Fullbright therefore holds the map's own shade bytes at row 0
and zeroes player visibility, and the deferred point-light pass still lights the scene.

Method: shader disassembly from `duke.rdc` (RenderDoc capture, the same one the fog lever came
from), `duke3d_wt_dump.exe` static reading, and in-game flights.

## What Megaton does, and why none of it carries

Megaton's fullbright is three polymost levers: `nofog`, `r_usetileshades = 0`, and
`getshadefactor` patched to `fld1; ret`. World Tour draws through a D3D11 deferred renderer
and has none of that code. Only `light.h`'s decision (`light_wants`) carries.

## The world pass, and the trap

The world pass is VS 9489 / PS 9491, the pair `fogwt.cpp` already names. It binds:

```
PaletteShadeBuffer : g_paletteLookup, g_shade
SectnumBuffer      : g_sectNum, g_sectorVis
PlayerVisBuffer    : g_playerVis        <- what "Remove fog" writes
```

`g_shade` is an int and varies per draw (4, 16, 16, 13, 22, 20, 17, 22, 6, 0 across
consecutive sectors, with `g_sectNum` counting 0,1,2,3 alongside). That looks exactly like
Build's per-surface shade, and it is. **It is also very nearly inert.**

### The vertex shader throws it away

```
 6: itof r1.x, g_sectorVis.x
 7: itof r1.y, g_shade.x
 8: mad  r1.xy, r1.yyyy, l(1.66, 6.0, ..), r1.xxxx    ; shade*1.66+vis / shade*6.0+vis
 9: ilt  r1.z, g_shade.x, l(0)
10: movc r1.x, r1.z, r1.x, r1.y
11: itof r1.y, g_playerVis.x
12: mul  r1.x, r1.y, r1.x
13: mul  r1.x, r1.x, l(0.000154)
14: movc r1.x, g_sectorVis.x, r1.x, l(0)              ; <-- sectorVis == 0 discards ALL of it
15: mad  r1.x, r1.y, l(0.003908), r1.x                ; playerVis alone
16: mul  r1.x, r1.x, l(0.020833)
22: movc o1.z, r1.y, l(0), r1.x
```

Line 14 is the whole story: **with `g_sectorVis` zero, every term derived from `g_shade` is
replaced by 0**, and the output is a function of `g_playerVis` alone. In the capture
`g_sectorVis` is 0 on nearly every draw (two of the sampled fourteen had 16 and 48).

So no patch that zeroes `g_shade` can brighten those surfaces. Two were built and both armed
cleanly with no visible change:

- eight shade loads at `0x7ABDC0..0x7AC530` (legacy path; not drawn through at all)
- four at `0x14D91F/0x14D994/0x14DA6B/0x14DAAD`, which really are the
  `PaletteShadeBuffer` fill (pal and shade stored as adjacent dwords)

The second set is correct about *where the buffer is filled* and still cannot matter, because
of line 14. Patching the code that fills the buffer cannot work either: the only pal/shade
store pair in the image (`FUN_007FD6F0`) writes a surface RECORD, not the constant buffer; the
buffer is filled from that record later by an int copy no byte-load pattern will find.

## Where the light is

The pixel shader writes two colours:

```
13: mov r1.y, g_paletteLookup.x
15: ld  r2, (colourIndex, g_paletteLookup), PaletteLookupTex
17: ld  r2, r2, PaletteTex
18: mov o0.xyz, r2.xyzx            ; albedo, no shade applied
21: mul_sat r0.z, length(viewPos), v1.z
23: add o0.w, -r0.z, l(1.0)        ; a FOG factor, from the VS term above
42: ishl r0.y, g_paletteLookup.y, l(8)      ; shade row * 256
43: iadd r1.x, r0.y, r0.x                   ;   + colour index
46: ld   o3, ..., PaletteTex                ; -> GbufferShadeColor
```

and the composite pass displays the SECOND colour:

```
11: mul r2.xyz, GbufferColor.w, GbufferShadeColor.xyz
429: mul r0.yzw, ao, r2.xyz
```

`g_paletteLookup`: `.x` is 0 on every sampled draw (the palette swap). `.y` is
Build's palette SHADE ROW, and it is what darkens the picture: in the capture it tracks the
surface's own shade exactly (4/4, 16/16, 13/13, 22/22, 20/20, 17/17). Row 0 is full
brightness.

## What the mod does

- **Shade row:** `lightwt.cpp` holds the MAP's own shade bytes (sector `ceilingshade` and
  `floorshade`, wall shade) at 0, saved on the way in, restored on the way out, re-taken when
  the level changes. It is the one place the value provably comes from.
- **Distance darkening:** the world pass writes its darkening into the alpha of the albedo
  target (`o0.w = 1 - saturate(distance * visTerm)`), and with `g_sectorVis` zero what is left
  is `distance * g_playerVis`. `fogwt.cpp`'s hold on the player's visibility (a short at player
  `+0x3DC`) therefore also answers to fullbright, at 0 whatever the fog knob says. A
  fullbright that removes only one of the two comes out "a bit brighter" rather than flat.
- **Sprites:** sprite shade cannot be held like the map's (actors rewrite it every tic, and a
  recycled slot would be restored to a stale shade), so it is borrowed around the original
  displayrooms inside `smoothview.cpp`'s hook, so only the draw sees a zero.
- **The panel row** shows `(Unavailable)` only when the patch cannot arm.
- **Not touched:** the deferred point-light pass still lights the scene, so an unlit room
  stays unlit. Shade row 0 is the brightest the palette goes, which is what Megaton's
  fullbright means too; it is not a floodlight.

Other shader groups in the same capture, candidates for a floodlight lever (untested):

| shader | buffer | fields |
|---|---|---|
| VS 126 / PS 128 | `PointLightBuffer` | `g_lightOriginViewSpace, g_radius, g_lightColor, g_screenSize, g_type, g_isNegative, g_ignoreSectorBuffer` |
| | `PointLightSectorsBuffer` | `g_numSectorsIn, g_sectors` |
| VS 116 / PS 118 | `PostProcessBuffer` | `g_frustumTopLeft, g_frustumBottomRight, g_viewport, g_noAO` |
| | `PalToBuffer` | `g_palToColor, g_palToIntensity` |

## The lesson

"Reads the field the feature is about" is not evidence, and neither is a clean arm in the
log: patches on `g_shade` fingerprinted, wrote, and logged success while changing nothing.
What settles which lever matters is **the shader that consumes it**, read to its end (a value
probe that prints only component `.x` of each variable hides `.y`).

## Open questions

- Whether `g_palToIntensity` (post-process) or `g_isNegative` / `g_lightColor` (light pass)
  can neutralise the point-light accumulation for a true floodlight.
