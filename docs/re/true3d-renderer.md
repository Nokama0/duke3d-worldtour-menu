# World Tour's two renderers, and the True3D submit path

The game has a classic software path and the enhanced **True3D** D3D11 path; the mod hooks the
latter. `FUN_00780860` is the True3D path and everything the mod hooks (the camera bracket,
the per-sector emit body, the wall record builder, the six-plane frustum, the floor/ceiling
test) belongs to it. True3D draws from a per-sector tessellated mesh cache; a wall with no
cache entry takes a fallback path rather than being dropped. This note maps the submit stage,
the five render lists and the wall-ownership rules. Related: `free-camera-wall-exemption.md`,
`horizon-occlusion-override.md`, `in-world-quad-layer.md`.

Method: the Ghidra program `duke3d_wt_dump.exe` (read only), a stack-walk tally of submit
callers in play, and a RenderDoc capture. RVA = address - 0x6B0000.

## The mode switch
`FUN_00756d10` (drawrooms, rva 0xA6D10), at `0x00756f7c`:

```c
if ((DAT_00e324ae == '\0') && (DAT_00984048 == 1)) { FUN_00780860(); return; }
FUN_007571f0(); FUN_007572d0();   /* the classic software scan and bunch sort */
```

The UI field named **`enhancedRenderMode`** (string `0x00944620`) is computed in `FUN_00752060`
at `0x00752647` from the **identical expression**:

```
CMP byte ptr [0x00e324ae], CL ; MOV EAX,[0x00984048] ; CMOVNZ EAX,ECX ; CMP EAX,1 ; SETZ AL
```

So **`FUN_00780860` IS the enhanced / True3D path**, and the fall-through is classic. The
selector is `DAT_00984048` (rva `0x2D4048`, int, 1 = enhanced; written at `0x006f6487`,
`0x00751781`, `0x006f80a9`, `0x00752d03`) gated by `DAT_00e324ae` (rva `0x7324AE`, byte, 0).

Independent confirmation from the other end: `FUN_007b8990` (rva `0x108990`) registers a render
mode named **"True3D"** (string `0x00948e9c`) and the `toggleAO` SSAO cvar, and it constructs the
scene object our hooks read; it calls `FUN_007f9830`, the one function that zeroes
`scene+0x5102E0`. Source names in the image: `RenderMode_True3d.cpp`, `True3dGeometry.cpp`,
`True3dTesselator.cpp`, `HardwareRenderer.cpp`, and RTTI `.?AVRendererDX11@@`.

## Where True3D's geometry comes from
`DAT_01592918[sect]` (4096 pointers, freed by `FUN_007acb20`) is the tessellated per-sector mesh.
`FUN_007fd0b0` builds one for every sector at level load via `FUN_007fd1c0`, which installs it with
`FUN_007ac890`. `FUN_00802080`, called FIRST in `FUN_007fa1b0` before any visibility work, is
"bring this sector's True3D geometry up to date and submit it", and per wall it addresses
`cache->+0x10 + wallIndexInSector * 0x18`.

## Where it declines to produce anything (camera-independent)
1. **`FUN_00802080` at `0x008020c3`**: `if (DAT_01592918[sect] == 0) return;` - no cache, nothing
   emitted for that sector at all, silently.
2. **`FUN_007fd1c0` at its head**: after tessellating,
   `if ((uint)((local_40 - local_44) / 0x90) < 3) { ...return; }` - it returns BEFORE calling
   `FUN_007ac890`, so **a sector whose tessellation yields fewer than three 0x90-byte pieces never
   gets a cache entry**. Its geometry is still handed to `FUN_00800630`, but the per-sector cache
   stays null, and (1) then drops it every frame.
3. Per wall, `FUN_007fac80` reads that cache entry and falls back to the plain path when its first
   dword is 0, so a wall can be "accepted" by every visibility test and still submit nothing
   through the mesh route. The fallback path (`FUN_007fb760` queue, drain `FUN_007fb310`,
   span tree) draws such walls: 1084 of 2011 on the measured map
   (`free-camera-wall-exemption.md`).

## Whether the occlusion test runs
`FUN_007fb570` (the horizon-occlusion verdict) is called for item types 0-3 and 6: under the
free camera it ran 551 to 2,333 times a frame (`horizon-occlusion-override.md`). An early drain-counter
reading of zero (taken to mean `FUN_007fb260`'s queue was always empty) is superseded by these
counts.

## The submit stage

`FUN_007805a0` composites the frame and calls `FUN_0078bf00` TWICE - once for `R+0x10` (the
camera object) and once for `R+0x510830`. `FUN_0078bf00(submitter, listOwner)` walks **five
named lists** on the list owner, each a begin/end pair of **8-byte elements**, handing every
element to `FUN_0078c2f0`:

| list | begin | end | name | return address after its `FUN_0078c2f0` |
|---|---|---|---|---|
| single | `+0x354` | -- | -- | `0x78BF79` |
| normal | `+0x35C` | `+0x360` | `"normal"` | `0x78BFE2` |
| transparent | `+0x384` | `+0x388` | `"transparent"` | `0x78C092` |
| gui | `+0x348` | `+0x34C` | `"gui"` | `0x78C1BF` |
| after gui | `+0x390` | `+0x394` | `"after gui"` | `0x78C26A` |

The owner is the render-mode object: `FUN_0077ec40`, which zeroes `+0x360` at `0x0077eccb`, is
the same function `FUN_007b8990` calls to register the mode named "True3D".

**`FUN_0077ee10` (rva `0xCEE10`) is the per-frame CLEAR, not an appender**: it reads
`[ESI+0x360]` at `0x0077ee25` and writes it back at `0x0077ee40`, setting `end = begin` for four
lists (`+0x360=+0x35C`, `+0x36C=+0x368`, `+0x388=+0x384`, `+0x394=+0x390`); seven calls a frame
are seven list resets. `FUN_0077f2f0` clears the list too (`0x0077f4b8`). `FUN_0078bf00`'s own
tail resets the gui list with `*(listOwner+0x34C) = *(listOwner+0x348)` at `0x0078c2bd`.

**The real appends take the list's ADDRESS**, so they never name the offset: searching for
`LEA ..,[reg+0x35C]` finds one site, `0x007ba774` in **`FUN_007ba740`** (rva `0x10A740`).

**Chain attribution caveat.** The 196 six-index draws of chain 9 return to `0x78C1BF`, which is
the **"gui"** loop, not a world list. Six indices is one quad and every GUI quad (this mod's own
panel and ESP markers included) is six indices, so "six-index" does not by itself mean "wall".
The world's opaque geometry is the **"normal"** list; the chains returning to `0x78BFE2` showed
only 18 draws each. Because the world is submitted in two runs (below), any per-chain split
from the stack-walk tally (the 196/18/18) is blind to one of the two passes by construction;
only the ADDRESSES that tally produced are load-bearing, and those are confirmed in the dump.

## The world pass, grouped by shader (RenderDoc)

| draws | VS / PS | stride | index counts | what |
|---|---|---|---|---|
| **632** | **9948 / 9950** | 24 | 3,6,9,12,15,18 | **the whole world: walls, floors and ceilings** |
| 101 | 126 / 128 | 12 | 648 | the sky |
| 64 | 10078 / 10080 | 24 | 6 | (after the world) |
| 196 | -- | -- | 6 | the "gui" list, which includes this mod's own overlay |

The map has 2011 walls and 664 floor/ceiling surfaces, about 2675 surfaces, and **632 draws
reach the GPU**, roughly a quarter of the level. The world pass's index counts run 3..18
because its surfaces are tessellated.

The world's draws return through `0x78BFE2`, the **"normal"** list.

**THE WORLD IS SUBMITTED IN TWO RUNS.** Event IDs from the capture: 4 other draws at 51..913,
then **330 world draws at 936..7845**, one unrelated draw at 7866, then **302 world draws at
7887..14588**, then 180 other draws. Two runs, 632 in total. That is `FUN_007805a0` calling
`FUN_0078bf00` twice per frame (once for `R+0x10` and once for `R+0x510830`) **with world
geometry on BOTH**. When counting calls of a hooked function, the owner arrives in ECX: tally
by that pointer, never by a total alone (a total near 632 means both owners, near 330 or 302
means one).

## FUN_007ba740: the record and its type dispatch

`FUN_007ba740(rec)` takes a three-int record - `[0]` sector, `[1]` wall index within the
sector, `[2]` a type 0..5 - and is the function that decides whether a surface is submitted
through the mesh route:

```c
cache = DAT_01592918[rec[0]];                 if (!cache) return;            // DROP 1
entry = cache[4] + rec[1] * 0x18;             // the per-wall 0x18-byte entry
   type 4 -> entry += 0x10;  type 3 -> entry += 8;  type 5 -> entry += 8 (+ two extra appends)
if (*entry == 0) return;                                                     // DROP 2
FUN_0077edf0(entry);                          // the append; types 0/1 go to FUN_00780ac0
```

Every drop is silent. A census measured **1094 of 2011 walls with an empty cache entry**; such
walls are drawn through the fallback path (`FUN_007fac80` falls back to `FUN_007fb760`, which
queues), not lost. 2011 walls less 1094 empty leaves about 917 submittable walls plus floors
and ceilings, against 632 draws measured in the capture.

The 0x18-byte per-wall entry holds THREE sub-entries and the type selects between them. From
`0x007ba7a0`-`0x007ba7ee`, with ESI = the entry:

| type | sub-entry | behaviour |
|---|---|---|
| 0, 1 | the sector's own (`cache+8`) | appended through `FUN_00780ac0` |
| 2 | `+0x00` | appended if non-zero |
| 3 | `+0x08` | appended if non-zero; **falls back to `+0x00`** when empty (`JZ 0x007ba7e9`, which lands on `CMP [ESI],0` with ESI NOT advanced) |
| 4 | `+0x10` | appended if non-zero |
| 5 | `+0x00`, then `+0x10` (if its own `+0x5c` is set), then `+0x08` | up to three appends |

**So the `if (*entry == 0) return` is on the TYPE-ADJUSTED pointer, not always offset 0.** Each
type tests its own sub-entry and type 3 has a deliberate fallback. A wall whose middle is empty
but whose step or header is not is therefore NOT lost: it reaches the list through type 3, 4
or 5. Only a wall with ALL THREE sub-entries empty can never be appended.

The per-surface loop that builds the three-int record and picks the type is in **`FUN_007ba500`**
(the sole caller, and the last call in `FUN_007b9800`). That is where offered-against-appended
is counted.

## Who owns a portal's step and header (FUN_007fe7e0, rva 0x14E7E0)

`FUN_007fd1c0` -> `FUN_007fe6a0` loops the 0x90-byte descriptors and calls **`FUN_007fe7e0`**,
which turns ONE descriptor into ONE 0x18-byte cache entry. Its two gates are the authority on
which side of a portal owns which piece, with no sign reasoning needed:

```c
/* header (upper) */  twoSided && (desc[0x48] < desc[0x50] || desc[0x60] < desc[0x68])
                      && !(both sectors' ceilings parallax)
/* step  (lower) */   twoSided && (desc[0x54] < desc[0x4c] || desc[0x6c] < desc[0x64])
                      && !(both sectors' floors parallax)
```

The descriptor fields, from `FUN_007fd6f0`: `+0x48`/`+0x4C` are THIS sector's ceiling and floor
z at the wall's first point, `+0x50`/`+0x54` the NEIGHBOUR's at the same point, and
`+0x60`/`+0x64`, `+0x68`/`+0x6C` the same pair at the second point, so slopes are handled per
point, which a flat sector-height comparison cannot do.

Read flat, that is: **this wall owns a header iff `sector.ceilingz < nextsector.ceilingz`**
(Build z grows downward, so my ceiling is HIGHER than theirs) **and owns a step iff
`nextsector.floorz < sector.floorz`** (their floor is higher than mine). A height difference
alone means ONE of the two walls owns the surface, never necessarily this one, which is why an
unsigned "differs" count is inflated. Of 768 walls with nothing in any cache sub-entry, 754
correctly own no piece (`free-camera-wall-exemption.md`).

## Which sectors a frame emits, and the wall record's skip byte

`FUN_007fa1b0` (`0x14A1B0`) is "emit sector N". Its first act is a `std::set` insert
(`FUN_0076f470`) on the scene object's visited set, and it does nothing when the sector is already
in it. Everything after that is one sector's geometry: walls (`FUN_007f93a0` over
`sector[].wallptr/wallnum`), floor, ceiling, sprites. A frame's visibility is therefore just WHICH
sectors that function is called for:

- once from `FUN_007f9c80` at `0x007f9def`, seeded from `globalcursectnum`;
- from `FUN_007fac80` (`0x007fad12`) for a wall's `nextsector`;
- from `FUN_007fb310` (`0x007fb354`) for a queued portal that survives the horizon occlusion test
  `FUN_007fb570`.

A camera outside the map draws the room it left plus whatever portals reach, minus what the
occlusion test discards. Calling the same body for every sector fills in the rest, and the visited
set makes repeats free. The seed call site is the only place where the body's three arguments
(scene object in ECX, two stack arguments) are all in hand.

**The wall record and its skip byte.** `FUN_007f8d60` (`0x148D60`) builds a `0xA4`-byte record per
wall; its byte at `+0x74` is "skip this wall". Three tests set it, in this order, at the tail of
that function:

1. `FUN_007f9180` (`0x149180`), Build's one-sided wall test in software: `getangle` to each
   endpoint and along the wall; the wall is skipped when the camera is on the side its normal
   points away from. The renderer never asks the rasteriser for backface culling.
2. Both endpoints project behind the view axis (`0x007f9157`).
3. The nearer endpoint is past 100000 units along the axis, a far clip (`0x007f9164`).

The builder leaves the two projections it used in the record, nearer at `+0x9C` and farther at
`+0xA0`: "both behind" is exactly farther < 0 and the far test is exactly nearer >= 100000, so
whatever else set the byte was the one-sided rule. The one-sided rule removes a room's near wall
for a camera inside it, which is the cutaway; seen from outside it removes the far wall. Only
reader of the byte: `FUN_007f95d0` (`0x1495D0`) at `0x007f95df`; `FUN_007fa7b0` (`0x1497B0`) gates
the per-wall draw on its result.

**The frustum behind the byte.** After the byte `FUN_007f95d0` runs a real six-plane frustum test.
`FUN_0080d320` (`0x15D320`) unprojects the four NDC corners to eight frustum points and
`FUN_0080d420` builds exactly six planes from them (reserves 6, appends 6). `FUN_007f9230`
(`0x149230`) scores each of a wall's corner points against every plane (positive is inside; the
stored byte is 0 outside, 2 on the plane, 1 inside), and `FUN_007f95d0` culls the wall when all four
corners of its quad are outside the same plane (loop at `0x007f9600`; scores at `+0x7C` and
`+0x8C`). Floors and ceilings use the same score vectors in `FUN_007f9740` (`0x149740`, loop at
`0x007f97d0`, reads at `0x007f9766` and `0x007f978b`): a surface goes at the first plane every one
of the sector's corner points is outside. Near and far are two of the six, and the projection's
`zFar` of 100.0 in the x0.001 world is the same 100000 units the builder's far test used, so
clearing the byte duplicates nothing. The plane vector is at the scene object's `+8`, filled once
per frame before the walk and read-only for every consumer (`FUN_007f9230`, `FUN_007f9740`,
`FUN_007fa350`): an injected sector gets the camera's full frustum, not a narrowed portal window.

**The sky sentinel.** At `0x007f8e0d` `FUN_007f8d60` replaces all four z values of a two-sided wall
with `INT_MIN` when this sector's `ceilingstat` and its neighbour's both have the parallax bit
(Build's "no wall between two open skies"). Through the z scaling that is +134217.728, 134 km above
the map; a real map spans at most +-65.5 in world x and y and about +-8 in z, so a coordinate past
1e5 is the sentinel. Because floor and ceiling are judged by the same score vectors, a sector whose
walls all carry it loses its floor and ceiling; the sentinel points are outside the near plane
(about -828 on plane 0) yet inside the far plane (about +958 on plane 1), so a test on plane scores
misses them.

**Fixed lists the walk fills without a bound.**

- The masked-wall lists `FUN_007f9c80` fills after the walk (`0x007f9ea0-0x007f9ec3`) are three
  short arrays on one counter: `maskwall` `0x014c21f0`, `thewall` `0x014c91f0`, `thesector`
  `0x014db1f0`. Capacity 6144 shorts: the next array up, the dword list at `0x014c51f0`
  (`0x0075ca31`), is `0x3000` bytes above `maskwall`.
- Records are appended during the walk to a 16-byte-element vector on the scene object and copied
  out, one short per element (`0x007f9ead`); how many records one wall can append is not
  established.
- `spritesortcnt`: `FUN_007fa440` (`0x14A440`) appends a sector's sprites while
  `DAT_0154e834 < 0x400`, but that counter is raised only after the walk (it reads zero inside it),
  so the only cap is `FUN_007f9c80`'s copy loop, which stops at `0x007f9e39`: array
  `0x01565b60-0x01570b60`, `0xB000` bytes of `0x2C`-byte entries = 1024. It stops silently; the
  scene's own sprite vector is at `+0x2A4/+0x2A8`. Which sprites survive depends on sector order.
- MAXSECTORS 4096 (`sector[]` ends where `nextspritesect` begins, 40 bytes per sector, see
  `engine-facts.md` section 4.3).

**Mesh-cache entry.** The per-sector cache object (`DAT_01592918[sect]`) is `0x1C` bytes and its
per-wall array hangs at `+0x10`, one `0x18`-byte entry per wall in `wallptr` order.
`FUN_00802080` returns for a null cache pointer and `FUN_007fac80` falls back to `FUN_007fb760` for
a wall whose cache entry reads zero, so an empty entry is a hole only if the wall is also undrawn.

**The span tree's empty node.** `FUN_00814460` initialises a fresh node with both ranges inverted:
`n3 = +FLT_MAX, n4 = -FLT_MAX, n5 = +FLT_MAX, n6 = -FLT_MAX`. The coverage test asks `q2 <= n5`,
true for every finite depth against `+FLT_MAX`, so a node holding no span reports that it occludes
everything. Two shapes arrive that way: a fresh node (n5 > n6) and one from the four-allocation
branch, which copies `X.n5 = X.n6 = parent.n5`, so a fresh parent leaves a real x range with
`n5 == n6 == +FLT_MAX`.

**Culling volume and view axis.** The renderer's view axis is the gradient `(m12, m13, m14)` of the
uploaded world-to-clip matrix's fourth row (`clip.w` is depth along the axis); the frustum's is
plane 0's normal (the near plane's normal is the view axis).

## Open questions

- Why 4 walls own a header or step by this comparison yet have nothing in the cache sub-entry.
