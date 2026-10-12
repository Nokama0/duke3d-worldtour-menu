# Duke Nukem 3D: 20th Anniversary World Tour - engine facts

Static facts about the executable: mouse to view, the status bar, the drawn camera, the projection, the sprite/sector/wall arrays, secrets, the CON action table, player fields and the input record.

Method: the Ghidra project, program `duke3d_wt_dump.exe` (runtime dump of the decrypted image), read only, nothing renamed or annotated; the game's own data files (`DEFS.CON`, `GAME.CON`, ART headers); logging of the input record in play. Image base `0x006B0000`; **RVA = address - 0x6B0000**, given after every address. ASLR, so use the RVA. Items marked "static", "inferred" or "not established" were not measured live.

Gearbox/Nerve fork of Build. The classic `control.c` layer survives ("MouseAnalogAxes%i", "analog_turning", "MouseAnalogScale%i", all parsed in `FUN_006e20c0` / `0x320C0`), with a new data-driven UI layer over it. No DirectInput, no raw input, no `GetCursorPos` in the imports; `SetCursorPos`, `ClipCursor`, `ClientToScreen` are present.

**RTTI is not usable here.** `list_classes` returns only CRT/ConcRT/PPL names (`Concurrency::*`, `std::*`); no game class names exist, so everything below is `FUN_*`.

## 1. Mouse to view
### 1a. Where raw movement enters
WndProc `FUN_007ab3c0` (`0xFB3C0`) -> mouse router `FUN_0079d530` (`0xED530`). Case `0x200` (WM_MOUSEMOVE) calls `FUN_0079d7e0((int)(short)lParam, (int)(short)((uint)lParam >> 0x10))` - the client-area cursor position. Deltas are position-minus-window-centre; the cursor is warped back by `FUN_0079ddb0` (`0xEDDB0`, `SetCursorPos`). Accumulator object `DAT_0158ef58` (`0xEDEF58`).

`FUN_0079d7e0` (`0xED7E0`), decompiled:
```c
piVar2 = &DAT_015f2c20;                             /* fullscreen {w,h}, 0xF42C20 */
if (DAT_015f2b90 == '\0') piVar2 = &DAT_015f2d30;   /* windowed   {w,h}, 0xF42D30 */
iVar1 = piVar2[1];
*(float *)(param_1+0x428) = (float)(param_2 - *piVar2/2) / (float)*piVar2  + *(float*)(param_1+0x428);
*(float *)(param_1+0x42c) = (float)(param_3 - iVar1/2)   / (float)piVar2[1]+ *(float*)(param_1+0x42c);
```
Fields: `+0x420/+0x424` last x/y, `+0x428` accumulated dx, `+0x42c` accumulated dy (float), `+0x450` cursor-free flag (zeroes both, `0x0079d8aa`). **dx is divided by screen WIDTH, dy by screen HEIGHT** (`DIVSS XMM1,XMM0` at `0x0079d866` with `XMM0=[EDI]`; at `0x0079d889` with `XMM0=[EDI+4]`). Units are fractions of the screen per axis, so equal pixel motion yields a vertical value larger by W/H. Drain `FUN_0079dd00` (`0xEDD00`) copies both out and zeroes them; it is vtable slot `+0x14` of `PTR_FUN_00946f94` (`0x296F94`).

### 1b. Sensitivity global and its readers
`"MouseSensitivity"` (`0x0093ed5c` / `0x28ED5C`) is `atoi`'d in `FUN_006e20c0` into **`DAT_00983b20` (`0x2D3B20`)** at `0x006e25c1`. Complete reader list: `FUN_006e7460` (`0x37460`, site `0x006e7497`) - the only gameplay reader; `FUN_00740eb0` (`0x90EB0`, options-menu build, string "mouseSensitivity" `0x00943d44`); `FUN_00751d40` (`0xA1D40`, settings snapshot); `FUN_00751610` (`0xA1610`, settings deserialise, also writes); `FUN_00752060` (`0xA2060`, menu apply). Other writers: `FUN_006e20c0`, `0x00752d93`. Axis-map/scale defaults: `FUN_006e73b0` (`0x373B0`).

### 1c. Counts to yaw and pitch
**Step 1, `FUN_006e7460` (`0x37460`, CONTROL_GetMouseDelta):**
```
0x006e748a: CALL dword ptr [EAX + 0x14]      ; FUN_0079dd00 -> float dx, dy
0x006e7497: MOV EAX,[0x00983b20]             ; MouseSensitivity
0x006e74ae: ADDSD XMM0,qword ptr [EAX*0x8 + 0x94dfc0]  ; uint->double fixup (see below)
0x006e7522: MULSS XMM1,XMM4                  ; dy * sens
0x006e7526: MULSS XMM0,XMM3                  ; dx * sens
0x006e752a: CVTTSS2SI EAX,XMM0
0x006e752e: MOV [0x017c3e10],EAX             ; MouseAxes[0].analog (0x1113E10)
0x006e7533: CVTTSS2SI EAX,XMM1
0x006e7537: MOV [0x017c3e18],EAX             ; MouseAxes[1].analog (0x1113E18)
```
`0x0094dfc0`/`0x0094dfc8` hold `0.0` and `4294967296.0` (`0x41F0000000000000`) - the standard MSVC unsigned-to-double fixup indexed by the sign bit, **not** a bias table. Factor = `(float)(uint32)MouseSensitivity`. **An integer truncation happens here once per poll on both axes** (`CVTTSS2SI`, toward zero); the fraction is discarded and never carried forward (1e).

Smoothing branch (`DAT_00c8b31c != 0`, `0x5DB31C`): `axis = (prev + cur) * 0.5f * sens` (`0x0094d908 = 0.5f`), with `prev` stored as `(int)dx` / `(int)dy` into `DAT_0211f648` / `DAT_0211f644` (`0x006e7505`, `0x006e7512`) - truncating a sub-1.0 fraction, so `prev` is always 0. `DAT_00c8b31c` has exactly one xref in the image and it is a READ (`0x006e74a7`): **no code writes it, so this branch is dead (static; a dump cannot prove the value is never set at runtime).**

**Step 2, `FUN_006e7780` (`0x37780`, CONTROL_PollDevices):** `analog = (analog * MouseAnalogScale[axis]) >> 16`, clamped `[-0x7FFF, 0x7FFF]`, then added into the `ControlInfo` slot named by `DAT_017c3dc8`/`DAT_017c3dcc` (`0x1113DC8`/`0x1113DCC`); scales `DAT_017c4204`/`DAT_017c4208` (`0x1114204`/`0x1114208`). From `FUN_006e20c0`: `analog_turning=0 -> info[3] (dyaw)`, `analog_lookingupanddown=2 -> info[4] (dpitch)`, `analog_strafing=1 -> info[0]`, `analog_moving=5 -> info[2]`. Driver `FUN_006e7b20` (`0x37B20`).

**Step 3, `FUN_00729820` (`0x79820`, getinput).** Both axes use the same divisor, 64, with the remainder carried in a static - so **no loss at this stage**:
```c
_DAT_013327c8 = info.dyaw + _DAT_013327c8;                           /* 0xC827C8 */
avel = (int)(_DAT_013327c8 + (_DAT_013327c8 >> 0x1f & 0x3fU)) >> 6;  /* /64, toward zero */
_DAT_013327c8 = _DAT_013327c8 + avel * -0x40;                        /* keep remainder */
/* pitch, only when AimingFlag DAT_00e32564 (0x782564) is set */
_DAT_01332578 = info.dpitch + _DAT_01332578;                         /* 0xC82578 */
horz = (int)(_DAT_01332578 + (_DAT_01332578 >> 0x1f & 0x3fU)) >> 6;
_DAT_01332578 = _DAT_01332578 + horz * -0x40;
if (MouseAimingFlipped == 0) horz = -horz;                           /* DAT_01590080, 0xEE0080 */
```
The flip uses `DAT_01590084` (`0xEE0084`) instead when `*(int *)(DAT_0158ef20 + 0x2c + *(int *)(DAT_0158ef20 + 1000) * 4) == 1` (gamepad active). With the strafe modifier held, dyaw goes through `>>3` into svel (`_DAT_013347d8`, `0xC847D8`). Outputs: `avel -> _DAT_00ca6128` (`0x5F6128`) and `horz -> _DAT_00ca6120` (`0x5F6120`), clamped +/-0x7F; `fvel -> _DAT_00ca612c` (`0x5F612C`) and `svel -> _DAT_00ca74bc` (`0x5F74BC`), clamped +/-0x5A. Packed as `_DAT_00dee1b0 = CONCAT11((char)horz,(char)avel)`.

**Step 4, `FUN_0072a7a0` (`0x7A7A0`, processinput)** - where the axes diverge. `sync[]` base `DAT_00de2fc0` (`0x732FC0`), stride 12, `+0 avel`, `+1 horz`.
```
; yaw, 0x0072c0ba - 0x0072c10c
0x0072c0ba: MOV AL, byte ptr [EAX*0x4 + 0xde2fc0]   ; sync[snum].avel
0x0072c0cd: ADD EDI,EDI                             ; angvel = avel * 2
0x0072c0dd: SAR ECX,0x3 / SUB EDI,ECX               ; conditional *7/8
0x0072c0f4: MOV word ptr [ESI + 0x2d0],AX           ; p->angvel
0x0072c102: ADD AX,word ptr [ESI + 0x2d0]           ; AX was p->ang
0x0072c109: AND AX,CX                               ; CX = 0x7ff
0x0072c10c: MOV word ptr [ESI + 0x2cc],AX           ; p->ang = (ang + angvel) & 2047

; pitch, 0x0072ca99 - 0x0072caa6
0x0072ca99: LEA EAX,[ECX + ECX*0x2]                 ; snum * 3
0x0072ca9c: MOVSX EAX,byte ptr [EAX*0x4 + 0xde2fc1] ; sync[snum].horz
0x0072caa4: SAR EAX,0x1                             ; horz / 2, rounds toward -inf
0x0072caa6: ADD dword ptr [ESI + 0x220],EAX         ; p->horiz += horz>>1
```
So in the final application **yaw is multiplied by 2 and pitch divided by 2 - a 4:1 integer asymmetry** - on top of the width-versus-height normalisation in 1a. **No aspect or "pitch match" float factor exists anywhere on this path**; the asymmetry is exactly these two integer steps. Auto-centre band and clamp in the same function: `if (0x5f < horiz < 0x69) horiz = 0x64` (`0x0072cab4`), then `> 0x12b -> 0x12b` and `< -0x63 -> -0x63` (`0x0072caea`-`0x0072cb02`).

### 1d. Player yaw and pitch fields
Player array base **`0x00cad5c0` (`0x5FD5C0`)**, stride `0x408` (1032), 16 slots. Proof: `MOV AX,word ptr [ESI + 0x2cc]` at `0x0072b30e`, whose xref target is `0x00cad88c`.

| field | off | abs (p0) | rva | type / units |
|---|---|---|---|---|
| posx / posy | `+0x214` / `+0x218` | `0x00cad7d4` / `0x00cad7d8` | `0x5FD7D4` / `0x5FD7D8` | int, Build units |
| posz | `+0x21c` | `0x00cad7dc` | `0x5FD7DC` | int, Build z units |
| **horiz (pitch)** | `+0x220` | `0x00cad7e0` | `0x5FD7E0` | int32, centre 100, range `[-99, 299]` |
| ohoriz / ohorizoff | `+0x224` / `+0x228` | `0x00cad7e4` / `0x00cad7e8` | `0x5FD7E4` / `0x5FD7E8` | int32 |
| **ang (yaw)** | `+0x2cc` | `0x00cad88c` | `0x5FD88C` | short, 0..2047 (`& 0x7ff`) |
| oang / angvel | `+0x2ce` / `+0x2d0` | `0x00cad88e` / `0x00cad890` | `0x5FD88E` / `0x5FD890` | short |
| horizoff | `+0x300` | `0x00cad8c0` | `0x5FD8C0` | short |

posz was not assumed to follow posy; it is confirmed twice. `0x007321f9` pushes `[0x00cad7dc]` as the z of a spawn whose x and y are `[0x00cad7d4]` and `[0x00cad7d8]` (`FUN_007321b0`, picnum `0x57d`, ang from `[0x00cad88c]`), and `FUN_006ff1e0` at `0x006ff209`-`0x006ff21b` walks the array at stride `0x408` summing four consecutive ints from `+0x214`, i.e. posx, posy, posz, horiz.

sintable `0x013fb230` (`0xD4B230`), 2048 shorts. Both fields are written each tic by `FUN_0072a7a0` (`0x7A7A0`). Tic loop `FUN_006ea0c0` (`0x3A0C0`) runs while `ototalclock + 100 <= DAT_013f6a28` (`0xD46A28`) and copies the local record with `FUN_00701810(&DAT_00dee1b0, 1)`. `DAT_013e6810 = DAT_013f6a28 / 0x19` (`FUN_00756310`, `0xA6310`), so one tic = 4 units of `DAT_013e6810` (`0xD36810`). **Not established: the wall-clock tic rate** - the fine clock is advanced in `FUN_007ad710` (`0xFD710`) from `QueryPerformanceCounter` times a runtime rate at `*(DAT_0158ef74 + 0x18)`, which a static dump does not give. 26 Hz is not confirmed. Local input record `DAT_00dee1b0` (`0x73E1B0`): `+0 avel` s8, `+1 horz` s8, `+2 fvel` s16, `+4 svel` s16, `+8 bits` u32.

## 2. The status bar
### 2a. Selection and screen rectangle
`ScreenSize` = `DAT_0158fcd0` (`0xEDFCD0`), stepped +/-4 in `FUN_006f6180` (`0x46180`), clamped 0..8. `FUN_00730890` (`0x80890`) recomputes the 3D view (disasm `0x007308e9`-`0x0073095e`):
```
LEA EAX,[ESI + -0x8] / CMOVG EDI,EAX  ; extra = max(ScreenSize-8,0)  -> always 0 after the clamp
(xdim * extra) / 0xa0                 -> windowx1
MOV ECX,0xc8 / CMP ESI,0x8 / MOV ECX,0xa6 / SUB ECX,EDI   ; h = (ScreenSize >= 8) ? 166 : 200
(ydim * h) / 200 - 1                  -> windowy2
CALL 0x007642a0                       ; setview(x1,y1,x2,y2)
```
At `ScreenSize == 8` (classic full bar) the 3D view is the top **166/200** of the screen; the bottom **34/200** is the bar band. `setview` = `FUN_007642a0` (`0xB42A0`): `xdimen = DAT_00983c88` (`0x2D3C88`), `ydimen = DAT_014b7188` (`0xE07188`), window edges `DAT_0154b028` / `DAT_0154f044` / `DAT_01565b50` / `DAT_0154f048`. `setaspect` = `FUN_007644e0` (`0xB44E0`); `xdim = DAT_014b6238` (`0xE06238`), `ydim = DAT_014b6234` (`0xE06234`).

The 320x200 mapping lives inside rotatesprite, for `stat & 2` and not `stat & 8`:
```c
if (DAT_00e32483 == 0)   /* 4:3 or narrower */
    sx' = (sx - 0xa00000) * DAT_00983d10 / 0x140;            /* xdimen / 320 */
else                     /* WIDER than 4:3 */
    sx' = (sx - 0xa00000) * DAT_014b7188 / ((DAT_014b712c * 200) >> 16);  /* ydimen / 240 */
sy' = ((sy - 0x640000) * yscale >> 16) + (cy1 + cy2 + 2) * 0x8000;
sx' = sx' + (cx1 + cx2) * 0x8000 + 0x10000;
```
`0xa00000 = 160<<16`, `0x640000 = 100<<16`: coordinates are centre-relative on a 320x200 virtual screen and re-centred on the midpoint of the clip rect passed in. `DAT_00e32483` (`0x782483`) is set in `FUN_0075ae10` (`0xAAE10`) when `(ydim*320<<16)/(xdim*240) < 0x10000`, i.e. wider than 4:3. `DAT_014b712c` (`0xE0712C`) is the pixel aspect, default `0x13333` (1.2 = 240/200). **Mechanism behind the narrow bar:** on any widescreen display the 2D scale is `screenHeight / 240` on both axes and the art is centred, so the covered width is fixed at `tilewidth * H / 240` regardless of screen width and the remainder either side is never touched. The bar tile's authored width is in 2e.

UI-pass viewport: `FUN_007808b0` (`0xD08B0`) saves the view state and calls `FUN_007809f0` (`0xD09F0`), which sets `windowx1=windowy1=0`, `xdimen=*(DAT_0158ef28+0x24c)`, `ydimen=*(DAT_0158ef28+0x250)`; `FUN_00780970` (`0xD0970`) restores it. Those two fields come from `FUN_0077f7b0` (`0xCF7B0`) as `{0, 0, screenWidth, screenHeight * ydimen / ydim}` - **full screen width, no 16:9 clamp at this level**.

**Not found: a single "draw the full status bar" function.** This build has no classic `drawstatusbar`. `displayrest` = `FUN_006eca00` (`0x3CA00`) draws the weapon (`FUN_007282b0`, `0x782B0`) and the crosshair, then defers to the UI layer. The bar is a named UI page: `FUN_006f5b20` (`0x45B20`) and `FUN_006f5fb0` (`0x45FB0`) switch on ScreenSize and show/hide `"HudGameClassic"` (`0x0093f61c` / `0x28F61C`, ScreenSize 8) or `"HudGameMinimal"` (`0x0093f60c` / `0x28F60C`, ScreenSize 4); ScreenSize 0 hides both. Setter `FUN_0078fd00` (`0xDFD00`, field `this+0x7b`). Children are named `hudHealth`, `hudArmor`, `hudAmmo`, `hudItemImage`, `hudFrags`, `hudPistolAmmo`, ... from `0x00943170` (`0x293170`), plus a `hudStyle` setting (`0x00944634`). The UI layer's own layout and draw for these widgets are not traced.

### 2b. The drawing primitive
`rotatesprite` = **`FUN_00764b80` (`0xB4B80`)**, classic 12-argument Build signature, MSVC `__fastcall` (sx in ECX, sy in EDX, the rest pushed):
```c
void __fastcall FUN_00764b80(int sx, int sy, uint z, short a, short picnum,
                             char dashade, uint dapalnum, uint dastat,
                             int cx1, int cy1, int cx2, int cy2);
```
Tables it indexes: `tilesizx = 0x0154f350` (`0xE9F350`), `tilesizy = 0x013f6a30` (`0xD46A30`), offsets `0x0155cb50`, `gotpic = 0x01553b50` (`0xEA3B50`, 4 bytes/tile - proved by `FUN_007300a0` (`0x800A0`): tile `0xc00`<->`0x01556b50`, `0xb06`<->`0x01556768`, `0x226`<->`0x015543e8`). Hardware branch at `0x00764ceb`:
```c
if ((DAT_00e324ae == '\0') && (DAT_00984048 == 1))     /* D3D11 renderer active */
    { FUN_007baa50(&local_28, picnum, dashade, dapalnum, dastat); return; }  /* 4 corners, 16.16 */
```
`FUN_007baa50` (`0x10AA50`) maps the four corners to NDC - `x = 2*(px/DAT_00984164) - 1`, `y = -(2*(py/DAT_00984160) - 1)` where `0x2D4164`/`0x2D4160` are screen width/height - builds four vertices with alpha `1.0 / 0.66 / 0.33` selected by `stat` bits 0 and 5 and a monotonically decreasing depth (`this+0x444 -= 1e-05`), then calls **`FUN_007f8490(picnum, verts)` (`0x148490`)**, which resolves the texture (`FUN_0079c500` / `FUN_007f8630`) and submits via `FUN_007eec20`. The ultimate primitive is a **textured-quad submit**, not a direct `ID3D11DeviceContext::Draw` at this level. Shipped fudge in `FUN_007baa50`: for `picnum` in `(0x9f6,0x9fa)` or `{0x1414..0x1416}` at exactly 1920x1080, `+1` is added to every y coordinate. D3D11 context pointer is `*(DAT_0158efa0 + 0xb398)` (`0xEDEFA0`); `RSSetViewports` (vtable `+0xB0`) in `FUN_007e8cd0` (`0x138CD0`); page flip `FUN_0075b3a0` (`0xAB3A0`) -> `FUN_007805a0` (`0xD05A0`) hardware, `FUN_00794bf0` software.

### 2c. Is the frame cleared
Yes. `clearview` = **`FUN_00767ba0` (`0xB7BA0`)**:
```c
if ((DAT_00e324ae == '\0') && (DAT_00984048 == 1)) { FUN_0077f6e0(colour); return; }  /* hardware */
/* software: memset rows windowy1..windowy2, cols windowx1..windowx2 of DAT_0158ef3c to 0 */
```
`FUN_0077f6e0` (`0xCF6E0`) walks the render-target list at `*(DAT_0158ef28+0x3b0) .. *(+0x3b4)` (8-byte entries) and calls each target's vtable slot `+4` with flags `*(DAT_0158ef28+0x3ac) = 1`. That slot is `FUN_0080a7a0` (`0x15A7A0`) or `FUN_00805900` (`0x155900`):
```c
piVar1 = *(int **)(DAT_0158efa0 + 0xb398);                    /* ID3D11DeviceContext */
if ((flags & 1) != 0)
    (**(code **)(*piVar1 + 200))(piVar1, rtv, this + 4);      /* ClearRenderTargetView, +0xC8 */
    (**(code **)(*piVar1 + 0xd4))(...)                        /* ClearDepthStencilView, +0xD4 */
```
`ClearRenderTargetView` takes no rect, so each call clears the **whole** view; the clear colour lives in the target object at `+4`. There are 22 call sites of `clearview`, including `FUN_006edac0` (`0x3DAC0`, displayrooms) at `0x006edb46`, `FUN_006eca00` at `0x006ed07b`, the main loop `FUN_006fa4a0` (`0x4A4A0`) twice, and the menu/transition paths. **Not established (static):** which targets are in that list (scene target vs. backbuffer) and whether the composite in `FUN_007805a0` covers the side strips. The engine does clear every frame on the hardware path, but that the cleared surface is the one the strips are read from needs a live capture.

### 2d. Identifying the bar's texture
**Not found as a tile number in the code** (2e has it from the game's data). `"BOTTOMSTATUSBAR"` (`0x0133e550` / `0xC8E550`) and `"WIDESCREENSTATUSBAR"` (`0x01341d50` / `0xC91D50`) exist, but both are heap strings in the dump with **zero xrefs**, and their spacing (`0x3800`) is not a whole multiple of the spacing to the neighbouring name (`BOOT_ICON` at `+0x50`), so no record stride and therefore no tile index could be recovered. Searching for the stock Duke3D value `0x994` (2452) as an instruction operand returns **no matches**, so this fork does not use the original numbering. Usable instead: `gotpic` base `0x01553b50` (`0xEA3B50`) - any `[tile*4 + 0x1553b50]` reference names its tile; `tilesizx 0x0154f350` / `tilesizy 0x013f6a30` - once the tile number is known its authored size is readable, and that is what decides how wide the drawn bar is (see 2a); and the widescreen-variant convention in `FUN_007282b0` (`0x782B0`, displayweapon), which when `DAT_00e32483 != 0` swaps `0x9e1->0x1402`, `0x9f0->0x1401`, `0x9fc->0x1407`, `0x9f4->0x1403`, `0x9f6->0x1404` - widescreen art lives in the `0x1400+` block, so the widescreen status bar is there too (2e confirms it is the block's first tile). Reference draws: fullscreen backgrounds tile `0xcd1`/`0xcd0` and `0x98d` with `stat = 0x4a`/`0x5a` over `(0,0)-(xdim-1,ydim-1)` (`FUN_006fa4a0`; `FUN_006eca00` at `0x006ecdd9`); crosshair tile `0x9db` with `stat = 3` clipped to the view window (`FUN_006eca00`).

### 2e. The tile numbers and authored sizes

The tile numbers are written in plain text in the game folder:

```
DEFS.CON:511   define BOTTOMSTATUSBAR 2462
DEFS.CON:736   define WIDESCREENSTATUSBAR 5120
```

5120 is `0x1400`: the widescreen art block (see the weapon swaps in 2d) starts exactly there,
and the widescreen bar is its first tile. `TILES020.ART`
begins at tile 5120 for the same reason.

Authored sizes, read from the ART headers (`artversion, numtiles, localtilestart,
localtileend`, then `tilesizx[]`, `tilesizy[]`):

| tile | name | size |
|---|---|---|
| 2462 | BOTTOMSTATUSBAR | 320 x 34 |
| 5120 | WIDESCREENSTATUSBAR | 448 x 34 |

Through 2a's scaling rule (2D scale = `ydimen/240` on both axes, centred on the clip
rect), the widescreen bar covers `448 * H/240` pixels of width whatever the screen is:

- 1920 x 1080: `448 * 4.5` = 2016 px on a 1920 px screen, so it slightly overflows and is
  clipped. That is why the art is 448 wide and not the 426 that 16:9 would need.
- 3440 x 1440: `448 * 6` = 2688 px on a 3440 px screen, leaving **376 px uncovered on each
  side**. That is the uncovered width a fix has to fill.

### 1e. The mouse conversion in full

`FUN_006e7460` (`0x37460`) decompiles to the whole defect in one page:

```c
if (DAT_0158ef20 != 0) (**(code **)(*DAT_0158ef20 + 0x14))(&x, &y);  /* drains the WM_MOUSEMOVE accumulator */
sens = (float)(uint)DAT_00983b20;                                    /* MouseSensitivity */
DAT_017c3e10 = (int)(sens * x);                                      /* x axis, 0x1113E10 */
DAT_017c3e18 = (int)(sens * y);                                      /* y axis, 0x1113E18 */
```

Three facts follow, and together they are the whole aiming problem:

1. **The fraction is discarded and never carried.** Each poll truncates toward zero, so a
   slow movement biases to nothing. The `>>6` in getinput does carry its remainder; this,
   the earlier and larger truncation, does not.
2. **The axes are not comparable.** `x` arrived as `dx/screenWidth` and `y` as
   `dy/screenHeight` (1a), so vertical is larger by W/H before sensitivity is applied
   and **the ratio moves with the screen's aspect**. The 4:1 in processinput then pulls the
   other way, netting 1.67:1 at 21:9 against 2.25:1 at 16:9: the same settings feel
   different on a different monitor.
3. **The smoothing branch is dead**, as 1c says: `DAT_00c8b31c` is never written, and its
   `prev` is `(int)` of a sub-1.0 fraction, so zero even if it ran.

Its only caller is `FUN_006e7780` (`0x37780`, CONTROL_PollDevices): it zeroes
`0x017c3e10..1c`, calls the above, scales each axis by a 16.16 factor from `DAT_017c4208`,
clamps to +-0x7FFF and adds into the caller's six-int axis array by the mapping in
`DAT_017c3dcc`. Since `FUN_006e7460` **assigns** rather than accumulates, nothing between
polls survives except what the window's own accumulator (`obj+0x428/+0x42c`) still holds.

So the hook point is `FUN_006e7460` itself: replacing it outright lets us read the
same source, keep a fractional carry of our own, put the two axes on one scale and
pre-compensate the 4:1, all before any of the engine's clamping. Nothing else need change.

### Prologues of two hook targets (rotatesprite, GetMouseDelta)

Both are clean; an x86 five-byte JMP patch needs six bytes copied to a trampoline, and
neither prologue has anything position-dependent in it:

| function | rva | first bytes | boundary |
|---|---|---|---|
| `FUN_00764b80` rotatesprite | `0xB4B80` | `55 8B EC 83 E4 F8` (push ebp; mov ebp,esp; and esp,-8) | 6 |
| `FUN_006e7460` GetMouseDelta | `0x37460` | `55 8B EC 83 EC 08` (push ebp; mov ebp,esp; sub esp,8) | 6 |

## 3. The drawn camera, and where a fractional view angle can go

### 3a. The per-frame camera path
`FUN_006edac0` displayrooms (`0x3DAC0`) -> `FUN_00756d10` drawrooms (`0xA6D10`) -> hardware branch `FUN_00780860`
(`0xD0860`) -> `FUN_007b9800` (`0x109800`) -> **`FUN_007b9870` (`0x109870`), the camera and matrix build**, then
`FUN_007f9c80` (`0x149C80`), the scene snapshot. Call site `0x006ee2af` (`0x3E2AF`) is `__fastcall` ECX=cposx,
EDX=cposy, pushed right to left sect, choriz, cang, cposz, i.e. `drawrooms(cposx,cposy,cposz,cang,choriz,sect)`.
Its early-out `if ((DAT_00e324ae == 0) && (DAT_00984048 == 1)) { FUN_00780860(); return; }` means the software
scan (`FUN_007571f0`, `FUN_007572d0`) never runs on this path.

### 3b. The drawn yaw and pitch are floats, in degrees
`FUN_007b9870`, verbatim:
```
0x007b9c91: MOVSX EAX,word ptr [0x014b7164]    ; globalang   (rva 0xE07164)  <- last integer read
0x007b9c98: MOV   ECX,0x80
0x007b9c9d: MOV   EDX,dword ptr [0x014b7178]   ; globalhoriz (rva 0xE07178)  <- last integer read
0x007b9ca7: CVTDQ2PS XMM0,XMM0                 ; (float)globalang
0x007b9caa: LEA   EDX,[EDX + -0x64]            ; globalhoriz - 100
0x007b9cad: MULSS XMM0,dword ptr [0x0094d8e8]  ; * 0.17578125  == 360/2048
0x007b9cb5: MOVSS dword ptr [ESP + 0x88],XMM0  ; YAW, degrees, float
0x007b9cbe: CALL  0x0075c220                   ; getangle(128, globalhoriz-100) -> int 0..2047
0x007b9cea: MULSS XMM2,dword ptr [0x0094d8e8]  ; * 0.17578125
0x007b9d09: MOVSS dword ptr [ESP + 0x78],XMM2  ; PITCH, degrees, float
```
Constants read from the dump: `0x0094d8e8 = 0x3E340000 = 0.17578125` (exactly 360/2048), `0x0094da8c = 90.0f`,
`0x0094d8d8 = 0.017453292f` (pi/180), `0x0094db34 = -0.001f`, `0x0094db38 = -0.0625f`, `0x0094db74 = -90.0f`.
Matrix build, same function, same frame:
```
0x007b9d62: CALL 0x007f12a0   ; ECX=&M[ESP+0x30], XMM1 = -pitchDeg - 90.0  -> rotate-X matrix
0x007b9d78-0x007b9ec4           ; if (*(float*)(DAT_0158ef28+0x510c14) != 0.0) fold in the screen roll
0x007b9f2c: MOVSS XMM0,[ESP+0x88]   ; yaw degrees
0x007b9f35: SUBSS XMM0,[0x0094da8c] ; - 90.0
0x007b9f3d: MULSS XMM0,XMM1         ; * pi/180 -> radians, kept at [ESP+0x14]
0x007b9f47: CALL 0x007e59e0         ; sinf
0x007b9f58: CALL 0x007e59c0         ; cosf   -> yaw rotation folded into M
```
`FUN_007f12a0` (`0x1412A0`) is "rotate-X matrix from degrees" (`a*pi/180`, sin/cos into rows 1-2). The same yaw
radians are applied again at `0x007ba1c6`/`0x007ba1dd` to a second matrix at `[ESP+0xb8]` seeded by
`FUN_007f12a0(-90.0f)`. `FUN_007e7100` (`0x137100`) receives the assembled view matrix and the yaw in degrees;
`FUN_007b8810` (`0x108810`) combines. The D3D11 constant buffer `*(ESI+0x3bc)` is Mapped at `0x007ba440` and
Unmapped at `0x007ba4f0` **inside this function**, so the camera reaches the GPU only as that matrix plus the
float camera position at CB `+0x80/+0x84/+0x88` (`globalposx*-0.001`, `globalposy*0.001`,
`globalposz*-0.0625*0.001`); yaw is not uploaded separately. Pitch quantises twice: `globalhoriz` is an int, and
`FUN_0075c220` (`0xAC220`, Build `getangle`, table `0x0207d5c0`) returns an int 0..2047 before the conversion.

### 3c. Interpolated every frame, but the interpolation result is integer
smoothratio is displayrooms' EDX. `FUN_006fa4a0` (`0x4A4A0`), `0x006faa61`-`0x006faab2`:
```
0x006faa71: MOV ECX,[0x013f6a28]   ; totalclock          (0xD46A28)
0x006faa77: SUB ECX,[0x00dee5c0]   ; - ototalclock       (0x73E5C0)
0x006faa7d: CMP ECX,0x64 / CMOVG ECX,EDI     ; EDI = 0x64 (MOV EDI,0x64 at 0x006fa8e6 / 0x006fa990)
0x006faa8d: SHL ECX,0x10 / IMUL ECX / SAR EDX,0x5        ; (delta<<16)/100
0x006faaa5: CMP [0x00cb2788],0x1 / CMOVLE ESI,0x10000    ; tic counter <=1 -> ratio 65536
```
so `smoothratio = min(totalclock-ototalclock,100) * 65536 / 100`, in `[0,0x10000]`. The two other displayrooms
callers pass a literal `0x10000` (`0x006f6ce2`, `0x00745634`). In displayrooms both results truncate to `short`:
```c
cang   = oang + mulscale16(((ang - oang - 1024) & 2047) - 1024, smoothratio) + p[+0x2d4];
choriz = (ohoriz+ohorizoff) + mulscale16((horiz+horizoff) - (ohoriz+ohorizoff), smoothratio);
/* choriz then clamped to [-99, 299] before drawrooms */
```
So the view does update per frame, but it is rounded to a whole build unit (0.176 deg) before it reaches
`globalang`; the float conversion in 3b cannot recover what was rounded away.

### 3d. The global copies
All written at the top of `FUN_00756d10` (drawrooms):

| global | addr | rva | type | written | read by |
|---|---|---|---|---|---|
| globalang | `0x014b7164` | `0xE07164` | short | `0x00756d57` | `0x007b9c91` (camera), `0x007f9ccb`, `0x00800b1e`, + software |
| globalhoriz | `0x014b7178` | `0xE07178` | int | `0x00756d86` | `0x007b9c9d` only - one reader in the image |
| globalposx/y/z | `0x014b7180`/`0x014b7174`/`0x014b7170` | `0xE07180`/`0xE07174`/`0xE07170` | int | drawrooms | `0x007b9cc3`/`0x007b9cfb`/`0x007b9d38` |
| singlobalang | `0x014b7154` | `0xE07154` | int, 16384-scaled | `0x00756e73` | `0x0075c49a`, `0x007f9cdb`, + software |
| cosglobalang | `0x014b7168` | `0xE07168` | int, 16384-scaled | `0x00756e6c` | `0x0075c490`, `0x007f9cc2`, + software |
| sin/cos * viewingrange | `0x014b715c`/`0x014b7150` | `0xE0715C`/`0xE07150` | int | drawrooms | `0x0075c49a`/`0x0075c490` (drawmasks sort) |
| globalhorizx (horizon row) | `0x014b717c` | `0xE0717C` | int | `0x00756dbc` | software only |

Polymost-style float/double copies exist, in the render-scene object `EBX = DAT_0158ef28 + 0x18`, written in
`FUN_007f9c80` (`0x149C80`) behind `CMP byte ptr [EBX+0x5102e0],0`: `+0x27c = (int)(short)globalang`
(`0x007f9cd5`); `+0x274 = (float)cosglobalang * 0x38800200` (1/16383) (`0x007f9d33`); `+0x278 =
(float)singlobalang * same` (`0x007f9d3b`); `+0x5102d0 / +0x5102d8 = (double)cos/singlobalang *
3.814697265625e-06` (2^-18) (`0x007f9d00`/`0x007f9d2b`).

**Which ones the geometry is actually drawn from:** on the D3D11 path the world is drawn from the float view
matrix built in `FUN_007b9870`, which reads **only** globalang, globalhoriz and the three globalpos. No wall or
floor geometry on this path is built from the integer trig - the software users (`FUN_007572d0`, `FUN_0075cfa0`,
`FUN_00768940`, `FUN_007690d0`, `FUN_007591a0`, `FUN_007599d0`) are all behind the early-out. Two hardware-path
readers of the integer trig survive: drawmasks `FUN_0075c3b0` (`0xAC3B0`) at `0x0075c490`/`0x0075c49a`, using it
only as a sprite sort key (the sprites go to `FUN_007ba920`), and face-sprite quad expansion in `FUN_00800af0`
(`0x150AF0`) at `0x00800b1e`, which offsets the four corners by `sintable[(globalang-1024)&2047]`. So a fraction
added only to the float degrees leaves the sort key and the face-sprite quads on the integer angle - at most half
a build unit, 0.088 deg - and cannot shear walls against the view. **Not established:** what consumes the float
copies at `+0x274/+0x278`; their only other access is `LEA EAX,[EDI+0x274]` at `0x007fa237` in `FUN_007fa1b0`.

### 3e. The hookable point
Stated plainly: **no function-entry hook can add the fraction by itself.** Every function on the path takes the
angle as an integer; the float exists only as a stack local inside `FUN_007b9870`, and that function Maps and
Unmaps the constant buffer itself, so there is no "after". The real patch is inline, at the two degree
conversions; both are 8-byte instructions, so a 5-byte JMP plus 3 NOPs fits with no straddling:

| what | address | rva | first 8 bytes |
|---|---|---|---|
| yaw `MULSS XMM0,[0x0094d8e8]` | `0x007b9cad` | `0x109CAD` | `F3 0F 59 05 E8 D8 94 00` |
| pitch `MULSS XMM2,[0x0094d8e8]` | `0x007b9cea` | `0x109CEA` | `F3 0F 59 15 E8 D8 94 00` |

In each trampoline: do the original multiply, then add `frac * 0.17578125f`. **Nothing has to be restored** -
both destinations are stack locals rebuilt every frame, and the tic reads `p->ang` (`+0x2cc`), never `globalang`.
Function-entry hooks, for a camera reimplementation instead:

| function | address | rva | first 8 bytes | boundary at/past 5 |
|---|---|---|---|---|
| `FUN_007b9870` camera build | `0x007b9870` | `0x109870` | `55 8B EC 83 E4 F8 81 EC` | **6** (push ebp; mov ebp,esp; and esp,-8) |
| `FUN_00780860` hw drawrooms | `0x00780860` | `0xD0860` | `55 8B EC 83 E4 F8 83 EC` | 6 |
| `FUN_00756d10` drawrooms | `0x00756d10` | `0xA6D10` | `55 8B EC 51 A1 50 5B 56` | 5 (push ebp; mov ebp,esp; push ecx) |

`FUN_007b9870` takes ECX = `DAT_0158ef28` (`0xEDEF28`). Hooking `FUN_00756d10` or `FUN_00780860` and writing
`globalang`/`globalhoriz` first is safe (drawrooms rewrites them every call, the tic never reads them) but gives
only whole-unit steps - the defect itself.

## 4. The world and the projection
Renderer-object fields are offsets from a heap pointer.

### 4.5 The projection
#### The objects
`DAT_0158ef28` (`0xEDEF28`) is a **pointer**, not a struct: `00780860: MOV EDI,dword ptr [0x0158ef28]`. Call
it `R`. `FUN_00780860` (`0xD0860`) then does `LEA ECX,[EDI + 0x10] / CALL 0x007b9800`, so `FUN_007b9870`'s
`this` is `R+0x10`, and inside it `0x007b9896: LEA EDI,[ESI + 0x18]` makes the **camera object `C = R + 0x28`** -
confirmed independently by `FUN_00780860` copying `0x96` dwords (600 = `0x258` bytes) from `LEA ESI,[EDI+0x28]`.

| slot | off in C | abs | written by |
|---|---|---|---|
| world 4x4, **identity** | `+0x000` | `R+0x28` | ctor `FUN_007e3c80` (`0x133C80`) via `FUN_007e3c10` (`0x133C10`: 1.0 at 0/0x14/0x28/0x3c) |
| **view** 4x4 / aux 4x4 / camera pos 3f | `+0x040` / `+0x080` / `+0x100` | `R+0x68` / `R+0xA8` / `R+0x128` | `FUN_007b8810` (`0x108810`) |
| **projection** 4x4 / its params | `+0x0C0` / `+0x22C` | `R+0xE8` / `R+0x254` | `FUN_007b88a0` (`0x1088A0`) |
| view x world, flag `+0x14C` | `+0x10C` | `R+0x134` | `FUN_007b8930` (`0x108930`) |
| **proj x view x world**, flag `+0x190` | `+0x150` | `R+0x178` | `FUN_007e3fa0` (`0x133FA0`) |
| viewport {x,y,w,h} ints | `+0x21C` | `R+0x244` | `FUN_0077f7b0` (`0xCF7B0`) |

`FUN_007f0eb0` (`0x140EB0`) is a 4x4 multiply `out = A*B` (ECX=A, arg1=out, arg2=B; `out[1] =
A[0]*B[1]+A[1]*B[5]+A[2]*B[9]+A[3]*B[13]`, a row of A against a column of B). `FUN_007b8930` calls it with
`LEA ECX,[EDX + 0x40]` / `PUSH EDX` = **view x world**; `FUN_007e3fa0` with `LEA ECX,[ESI + 0xc0]` / `PUSH EAX`
(= `C+0x10c`) = **projection x (view x world)**. World is identity and nothing else in the image clears the
`+0x14c` flag (only `FUN_007b8810`, `FUN_007b88a0`, `FUN_007e3c80` do), so there is no "set world matrix"
path: **`C+0x150` is the finished world-to-clip view-projection matrix.**

#### The projection matrix
`FUN_007b88a0(zFar)` stores `{0, XMM1, XMM2, -, -, XMM3, zFar}` at `C+0x22C` and calls `FUN_007f1430`
(`0x141430`); type 0 dispatches to `FUN_007f0dc0` (`0x140DC0`), which builds, flat row-major,
`m[0]=cot(fovY/2)/aspect, m[5]=cot(fovY/2), m[10]=(n+f)/(n-f), m[11]=2nf/(n-f), m[14]=-1`, rest 0. That is
**gluPerspective: right-handed, depth to [-1,1], column-vector convention** (`clip = M * (x,y,z,1)^T`;
translation in the 4th **column**, flat indices 3/7/11). The ortho branch confirms it: `m[3]=(r+l)/(l-r)`.
Arguments, `0x007b987c`-`0x007b9910`:
```
0x007b987c/87: MOVD XMM1,[0x0154f040] (viewingrange, rva 0xE9F040, 16.16); MOVSS XMM0,[0x015f2ae0]
             (runtime setting, 200.0f in the dump, rva 0xF42AE0, written only at 0x006bceb3)
0x007b98a9/b1: MULSS XMM1,[0x0094d8a4] (2^-17) then XMM0   ; fov = viewingrange * 200 / 131072
0x007b98c1/d0: MINSS 175.0f [0x0094da9c] / MAXSS 45.0f [0x0094da7c]   ; fov = clamp(fov, 45, 175)
0x007b98c5/d4/df: MOVD XMM1,[EDI+0x224]; MOVD XMM0,[EDI+0x228]; DIVSS  ; aspect = vw/vh (1.0 if vh==0)
0x007b98f4: DIVSS XMM4,XMM1  -> fovY(deg) = clamp(...)/aspect  |  0x007b98fb: zFar = 0x42c80000 = 100.0f
0x007b9902: MOVSS XMM3,[0x0094d8d0] -> zNear = 0.01f
```
Planes are in the scaled world units (x1/1000), i.e. 10 and 100000 Build units. `viewingrange` is `setaspect`'s
first argument: `FUN_007644e0` (`0xB44E0`), `0x007644e4: DAT_0154f040 = param_1`; the same function writes
`viewingrangerecip 0x014B7130`, `yxaspect 0x0154E838`, `xyaspect 0x014B7128`, `xdimenscale 0x014B71B0`, `xdimscale 0x014B71AC`.

#### The viewport
`FUN_0077f7b0` (`0xCF7B0`, called at the tail of `setview` `FUN_007642a0`) writes, with `iVar2 = R`:
`R+0x244 = 0; R+0x248 = 0; R+0x24c = screenW; R+0x250 = screenH * ydimen/ydim` (same rect again at
`R+0x510bdc` and `R+0x510ba4`). `R+0x244` is `C+0x21C`, so the hardware `windowx1/y1/x2/y2` equivalent is
**always x=0, y=0, full screen width, height scaled by `ydimen/ydim`** - the status-bar band is removed by
shortening the viewport, not by insetting it. Build's own globals still exist and still drive it (re-confirmed
in `FUN_007642a0`): `xdimen 0x00983C88`, `ydimen 0x014B7188`, `xdim 0x014B6238`, `ydim 0x014B6234`,
`windowx1 0x0154B028`, `windowy1 0x0154F044`, `windowx2 0x01565B50`, `windowy2 0x0154F048`. The normalized rect
`{x/W, y/H, (x+w)/W, (y+h)/H}` goes at `0x007b9c22`-`0x007b9c6e` into the aux constant buffer `*(R+0x3D4)` byte
`+0x20`, with the camera right/up vectors at `+0x00`/`+0x10`.

#### Where a mod should read the matrix
**Read the 16 floats at `C + 0x150` = `R + 0x178`, where `R = *(void**)0x0158EF28`.** That is literally the
memory the renderer uploads: `0x007ba453: CALL 0x007e3fa0` (fills it, sets the flag at `C+0x190`), then
`0x007ba458`-`0x007ba481` `MOVUPS`-copy `[EDI+0x150..0x18F]` into the mapped scene constant buffer `*(R+0x3CC)` at
offset 0. The same CB takes the view matrix (`C+0x10C`) at `+0x40` and the camera position at `+0x80/84/88`
(`globalposx*-0.001`, `globalposy*0.001`, `globalposz*-0.0625*0.001`; disassembly `0x007ba4bb`-`0x007ba4cf` - the
decompiler mislabels `+0x88` as the yaw). Validity: `FUN_007b8810`, `FUN_007b88a0` and `FUN_007e3fa0` have
**exactly one caller each, `FUN_007b9870`** (`FUN_007b88a0` has a second at `0x007f9da9` in `FUN_007f9c80`,
but with `LEA ECX,[EBX + 0x14]` - a different camera object, not `C`). So `C+0x150` is written once per 3D
frame, late in `FUN_007b9870`, and the 2D/UI passes do not disturb it; it is valid from the moment
`FUN_007b9870` returns until the next `displayrooms`. A stable per-frame copy sits at `R + 0x510998`
(`= R + 0x510848 + 0x150`), but that copy deliberately preserves its own viewport field (`MOVUPS`
save/restore of `R+0x510a64`), so prefer the live one.

World coordinates to feed it: the view matrix's translation is the negated scaled camera position
(`0x007ba0a0`-`0x007ba0bd`), so a Build point becomes `(x * -0.001, y * 0.001, z * -0.0625 * 0.001)`. The same
constants `0x0094DB34 = -0.001f` and `0x0094DB38 = -0.0625f` are read by the geometry builders
(`FUN_007fe7e0`, `FUN_00801600`, `FUN_007fdb00`, `FUN_00800af0`, `FUN_00804860`, `FUN_007f8d60`), e.g.
`0x00800bb2`/`0x00800bc2: MULSS XMM1,[0x0094db34]` while writing float3 vertices 12 bytes apart. Then
`clip = M * (X,Y,Z,1)`, `ndc = clip.xy / clip.w`, and - **inference**, the `RSSetViewports`
arguments of the 3D pass were not read - `px = vx + (ndc.x*0.5+0.5)*vw`, `py = vy + (0.5-ndc.y*0.5)*vh` from `C+0x21C`.

#### Depth and the sprite quad

Depth is z/w; `FUN_007f0dc0` builds a `gluPerspective`-style projection. A near-plane clip is
needed for geometry close to the eye: without one, walking up to a combination lock lost 4 of its 7
quads.

### 4.1 The sprite array
**Base `0x01336810` (rva `0xC86810`), stride `0x2C` (44), MAXSPRITES 16384.** Stride established three ways,
not assumed: `FUN_0075fc90` (`0xAFC90`, initspritelists) steps by `0x16` shorts while `(int)p < 0x13e6828`,
giving `(0x13E6828-0x1336828)/0x2C = 16384`; the savegame loader `FUN_0071b400` (`0x6B400`) memcpys the array
as one `0xB0000`-byte block (`16384 * 44`); and `FUN_0074a370` indexes `base + n*0x2c + 0xe`.

| field | off | evidence |
|---|---|---|
| x, y, z | `+0x00/04/08` | `FUN_007321b0` copies them into player posx/posy/posz; `FUN_0074a370` reads `+8` as z |
| cstat | `+0x0C` | `FUN_007321b0` writes bytes `+0x0C`/`+0x0D` = 1,1 (cstat 257, Duke's player sprite) |
| picnum | `+0x0E` | `FUN_0074a370`: `*(short*)(base + n*0x2c + 0xe) == 0x57d` (APLAYER) and `== 1` (SECTOREFFECTOR) |
| shade, pal, clipdist, xrepeat, yrepeat, xoffset | `+0x10/11/12/14/15/16` | `FUN_007321b0`: `DAT_01336820 = 0`; `DAT_01336821` -> player palette byte; `DAT_01336822 = 0x40`; `*(short*)DAT_01336824 = 0x242a` (42, 36); `DAT_01336826 = 0` |
| sectnum / statnum | `+0x18` / `+0x1A` | `FUN_0075fc90` sets them to `0x1000` (MAXSECTORS) / `0x400` (MAXSTATUS); `FUN_0075fe80` / `FUN_0075ff50` use them as list keys |
| ang, owner | `+0x1C/1E` | `FUN_007321b0` copies ang into player ang and sets owner `= n`; `FUN_0074a370` does `ang += 0x400` |
| xvel, yvel, zvel, lotag, hitag, extra | `+0x20/22/24/26/28/2A` | `+0x22` = player number and `+0x2A` = health (`FUN_007321b0`); lotag/hitag matched as SE type and channel (`FUN_0071b400`, `FUN_0074a370`); `+0x20`/`+0x24` **inferred** |

**numsprites: not found.** Neither `FUN_0075fdf0` (insert) nor `FUN_0075ff50` (delete) maintains a count and
the savegame stores none - walk the stat lists instead.

**Sprite quad sizing.** `FUN_00801600` (`0x151600`) sizes a sprite quad `xrepeat * (cstat & 0x30 ?
0.25 : 0.2) * tilesizx * 0.001` across and `yrepeat * 0.25 * tilesizy * 0.001` up; `FUN_00801f80`
(`0x151F80`) gives the corners +-0.5 laterally and 0..1 vertically.

### 4.2 The statnum and sector lists
All six exist, with **MAXSTATUS = 1024 (0x400), not the classic 16**, and **MAXSECTORS = 4096**. From
`FUN_0075fc90` plus the savegame block sizes: `headspritesect[4097]` `0x014B4230` (`0xE04230`, `0x2002` bytes;
`headspritesect[4096]` = `DAT_014b6230` = base+0x2000); `headspritestat[1025]` `0x014B6240` (`0xE06240`,
`0x802`; free-list head `DAT_014b6a40` = base+0x800); `nextspritesect` `0x01424230` (`0xD74230`);
`prevspritesect` `0x013EE820` (`0xD3E820`); `nextspritestat` `0x0142C230` (`0xD7C230`); `prevspritestat`
`0x013E6820` (`0xD36820`) - the four link arrays are `0x8000` bytes each (16384 shorts), and
`headspritestat[MAXSTATUS]` doubles as the free-list head. `FUN_0075fdf0` (`0xAFDF0`) is `insertspritestat`,
`FUN_0075ff50` (`0xAFF50`) its delete, `FUN_0075fe80` (`0xAFE80`) the sector-list delete. Walk with
`for (i = headspritestat[st]; i >= 0; i = nextspritestat[i])` - exactly what `FUN_007321b0` and `FUN_0074a370` do,
using `DAT_014b6254` = `headspritestat[10]` (STAT_PLAYER) and `DAT_014b6246` = `headspritestat[3]` (STAT_EFFECTOR).

### 4.3 Sectors and walls
**`sector[]` `0x013FC230` (rva `0xD4C230`), stride `0x28` (40), MAXSECTORS 4096** - the array ends exactly at
`nextspritesect` (`0x013FC230 + 4096*40 = 0x01424230`). **`wall[]` `0x01434230` (rva `0xD84230`), stride
`0x20` (32), MAXWALLS 16384** (`0x01434230 + 16384*32 = 0x014B4230` = `headspritesect`). Both strides come off
`FUN_0071b400`: `numwalls*0x20` into `&DAT_01434230`, `numsectors*0x28` into `&DAT_013FC230`. **`numwalls` =
`DAT_013F6A20` (rva `0xD46A20`)**, short, read just before the wall block; **`numsectors` = `DAT_01570B60`
(rva `0xEC0B60`)**, short, just before the sector block and the bound in `updatesector`.

sector: `wallptr +0x00`, `wallnum +0x02` (`FUN_00763320`, `0xB3320`, updatesector: `wal =
&wall[sector[s].wallptr]`, loop count `sector[s].wallnum`); `ceilingz +0x04`, `floorz +0x08` (`FUN_0071b400`
registers `&DAT_013fc234 + s*10` / `&DAT_013fc238 + s*10` for interpolation, `FUN_0074a370` uses them as a
door's two z limits); `ceilingstat +0x0C` (`0x006d0ed6: TEST byte ptr [EAX*0x8 + 0x13fc23c], 0x1`, the parallax
bit); `floorstat +0x0E` (**inferred**); `lotag +0x22` (the debug overlay `FUN_006eb0e0`, `0x3B0E0`, prints
`"SECTL= %i"` from `(&DAT_013fc252)[cursectnum*0x14]`); `hitag +0x24` (`FUN_0074a370` matches it against
`sprite[].hitag`); `extra +0x26` (`FUN_0074a370` passes it as the door speed). Note `FUN_0074a370` (`0x9A370`,
operatesectors) switches on `lotag & 0x3FFF` and toggles bit 15 as the open/closed state.
wall: `x +0x00`, `y +0x04`, `point2 +0x08`, `nextsector +0x0C` (all four in `FUN_00763320`'s inlined
`inside()` and its neighbour walk); `cstat +0x0E` (`0x006e07fd: OR word ptr [EAX + 0x143423e], 0x10`).
`nextwall +0x0A`, `picnum +0x10`, `lotag +0x1A` are **inferred** from the stride and the classic layout.

### 4.4 Secrets
Classic marking, unchanged: **a secret sector is `sector[].lotag == 32767`**. `FUN_00750240` (`0xA0240`,
checksectors) reads the lotag of `p->cursectnum` (player `+0x2D2`) and branches:
```
0x007502a3: CMP EAX,0x7fff        |  0x007502b2: JZ 0x00750f96   |  0x00750f96: XOR ECX,ECX
0x00750f9f: MOV word ptr [EAX*0x8 + 0x13fc252],CX   ; sector[cursectnum].lotag = 0
0x00750fee: INC dword ptr [ESI + 0x3f0]             ; p->secret_rooms++
```
The "open all doors" cheat (`FUN_006f4b80`, `0x44B80`, case 8) independently excludes it:
`if (lotag != -1 && lotag != 0x7fff && (lotag & 0x7fff) > 2) operatesectors(...)`.

Counters, both int32 in the player struct (stride `0x408`, base `0x00CAD5C0`): **secrets found at `+0x3F0`**
(player 0 `0x00CAD9B0`, rva `0x5FD9B0`) and **secrets in the level at `+0x3EC`** (`0x00CAD9AC`, rva
`0x5FD9AC`). Evidence: the end-of-level screen `FUN_006fbb10` (`0x4BB10`) binds UI fields by name -
`"secretsFound"` (`0x009406A0`) and `"secretsMissed"` (`0x009406B0`) - in the block at
`0x006fd440`-`0x006fd537`, which reads `[EAX + 0xcad9b0]` and computes `max(0, [EAX+0xcad9ac] - [EAX+0xcad9b0])`.
The parallel block at `0x006fd2ab`-`0x006fd2fd` does the same with the globals `DAT_01591098` (`0xEE1098`) and
`DAT_01591094` (`0xEE1094`) for `"enemiesKilled"`/`"enemiesLeft"`, so those two are the kill counters, not the
secrets. Both pairs are saved by `FUN_0071b400`. **Not established:** where `+0x3EC` is totalled at level start - its use is known, the `prelevel` scan that fills it was not found.

## 5. The CON action table

`hittype[i].temp_data[4]` is the actor's current **action**, held as an **INDEX**, not as a
pointer. Megaton's build stores a pointer and `common.h`'s `action_read` dereferences it; doing
that here hands `VirtualQuery` an unmapped address (every live sample read `t4 = 0x594` = 1428)
and the three finer actor states could never be reached.

**Table base: rva `0x7454E8`** (VA `0xDF54E8` in the dump), in the exe's own `.data`. Records are
five `int32` — `startframe, numframes, viewtype, incval, delay` — so record `n` is at
`0x7454E8 + n*20`. That is exactly `common.h`'s `ACTION_OFF_*` layout, so `ActorAction` can be
filled from it unchanged.

Found by scanning committed memory for the 25 longs that `GAME.CON` declares consecutively at
lines 4164-4168, which put `ATROOPDYING`'s record at rva `0x74C554`. Verified six ways, each
from a different source:

| index | record | action |
|---|---|---|
| 1439 | `50,5,1,1,16` | `ATROOPDYING` |
| 1440 | `54,0,0,0,0` | `ATROOPDEAD` |
| 1441 | `54,0,0,0,0` | `ATROOPPLAYDEAD` |
| 1442 | `58,2,1,-4,24` | `ATROOPSUFFERDEAD` — a live `t[4]`, trooper mid-death |
| 1443 | `59,2,1,1,21` | `ATROOPSUFFERING` — a live `t[4]`, trooper choking |
| 1428 | `0,1,5,1,1` | `ATROOPSTAND` — a live `t[4]`, every sleeping trooper |

The last row is the one that settles it: 1428 is what a live sample printed for every
dormant trooper in the level, and `GAME.CON:4153` is `action ATROOPSTAND 0 1 5 1 1`.

`actorstatewt.cpp` does not trust the base — it searches the table for those five consecutive
records at install, so one check proves both the address and that the loaded CONs are the ones
the rules were written against.

**The neighbouring slots, for anyone tempted to reuse them.**

- `temp_data[4]` — action index (above).
- `temp_data[5]` — the **ai**, and `0` until the actor's first executed tic. That is what
  `GAME.CON`'s `ifai 0` blocks test, so it is also what says whether the CON's spawn-time
  overrides (`state checktrooppalette`, the boss palette branch) have fired yet. `esp_pics.h`'s
  `esp_display_hp` uses it.
- A separate pair of tables at VA `0xCB2794` / `0xCB4BA4` (spacing `0x2410`) is indexed `*4` by a
  temp slot and holds two values per entry — the shape of a `move` (horizontal/vertical
  velocity), **not** an action. Noted because it looks like an action table and is not.

### 5a. temp_data[4] as an index

`temp_data[4]` is a one-based index into a `vector<int>` of script addresses (begin/end at rva
`0xEDF144` / `0xEDF148`), proven from the CON `action` handler (rva `0x64710`), `execute()`
(`0x66DE4`) and `0x68B40`. `actorscrptr` is at rva `0x7350B0`. Hardcoded actors and sector
effectors use `temp_data[4]` as scratch; rva `0x23BC5` counts it down. `GAME.CON:4153`
(`action ATROOPSTAND 0 1 5 1 1`) is the decode probe, at index 1428. A health rule alone cannot hide
a commander: two hidden at `extra` 0 never came back because `temp_data[4]` was 2745 =
`ACOMMSHOOT` (mid-attack).

## 6. Other player fields

- **`got_access`**, player + `0x31E`, one byte, bits 1/2/4 = blue/red/yellow. Found from the
  three quotes `USER.CON` numbers 70/71/72 (`#BlueAccessCardReq`, `#RedAccessCardReq`,
  `#YellowAccessCardReq`): exactly one window in the image pushes all three, and each is gated
  by `test byte ptr [eax + 0xcad8de], 1/2/4` at rva `0x9BDC9` / `0x9BE3A` / `0x9BEA4`. The
  pal-to-bit mapping matches `esp_switch.h`'s `switch_classify` exactly.
- **`gm`** keeps classic Duke's `MODE_*` bits: `getinput`'s own first test is
  `test byte ptr [esi+0x398], 0x11` at rva `0x79855`, which is `MODE_MENU|MODE_TYPE`, so
  `MODE_GAME` is the 4 beside them.

## 7. Mouse buttons and the input record's action bits

### 7a. The game cannot bind a thumb button, and why

`DUKE3D.CFG` keeps `MouseButton0..4` / `MouseButtonClicked0..2`, and the parser at rva `0x320C0`
formats `"MouseButton%i"` for each index but stores the result only under

```
032187  cmp  edi, 5
03218A  jae  (skip the store)
```

so a hand-written `MouseButton5` line is read and discarded. That is exactly the five slots the
Mouse Config menu offers: left, right, middle (each with a double-click variant) and the two
wheel directions.

**Nor can the Keyboard Config express one.** `[KeyDefinitions]` stores bindings as quoted key
NAMES, and the label set is keyboard-only — `KeyBlank | ESC | F1..F12 | LALT | RALT | LCTRL |
RCTRL | LSHIFT | RSHIFT | BACK | CAPLCK | TAB | PGUP | PGDN | SCRLK | PRTSCN | PAUSE | INS |
DEL | HOME | END | KP 0..9 | KP - | KP + | KP . | KP / | KP * | ClearKeyBind`. There is no
mouse-button name anywhere in the image. Megaton's key-name table *does* have them (`"Mouse 11"`,
`"MOUSE 5"`), which is why binding one is cheap there and a fork of the binding system here.

`mousebind.h` supplies the binding instead: the window subclass already decodes
`WM_XBUTTONDOWN/UP`, and the action's bit is OR'd into the input record in the getinput hook.

### 7b. The input record's action bits, measured

The record at rva `0x73E1B0` carries `bits` at `+8` — Duke's netcode action mask. **This fork's
layout is NOT classic Duke's**: classic puts quick kick at 19 and the jetpack at 22, and neither
is right here. Each of these was read out of the record by logging every change to the mask
while one bound key was pressed at a time (`mouseaim.cpp`'s `tbot_report_bits`):

| bit | action |
|---|---|
| 2 | fire (already known; `REC_BIT_FIRE`) |
| 12 | steroids |
| 15 | night vision |
| 16 | medkit |
| 22 | quick kick |
| 24 | holoduke |
| 25 | jetpack |

Bits 17 and 23 sit up at rest and belong to no key. Anything else wanted here should be measured
the same way rather than taken from a Duke source table — that instrument is still in the build,
capped at 40 lines and gated on `debuglog`.

Not every action is in this mask: movement is the record's own velocity fields and weapon select
is a packed multi-bit field, so those need more than a single OR.

## 8. getinput, processinput and shoot: the sites

### 8a. getinput (`FUN_00729820`)

- Convention: `__fastcall(short snum in CX)`. One call site, `0x006ea265` (`MOV ECX,EDX`, EDX =
  local player index, no stack cleanup); the callee has `MOVSX EAX,CX` at `+25`. `FUN_006ea0c0`
  calls it at `0x006ea267` and `domovethings` at `0x006ea406`.
- Input record: the caller zeroes 8+4 bytes at `0x006ea27d` and `0x006ea287` and hands both to the
  packet copier `FUN_00701810` as one object. Record at `0x00dee1b0`.
- Record stores: `avel` `0x0072a01c`, `horz` `0x0072a02b`, `svel` `0x0072a024`, `bits`
  `0x0072a06a`. The velocity fields use `mulscale9` (`SHRD EAX,EDX,9` at `0x0072a00f`) and a 16-bit
  store (`ADD SI,AX` at `0x0072a016`).
- Strafe: `svel` is clamped +-90 (`CMP EDI,-0x5a` / `CMOVG` at `0x00729ec9`) and stored at
  `0x00729eda` into `0x00ca74bc`. The strafe-modifier test (bit 4 of the button global) is at
  `0x00729cc2`; with it, mouse x is divided by 8 into `svel` (`0x00729cd6`) or by 64 into `avel`
  (`0x00729cfb`), the sign flip is `NEG EDI` at `0x00729ce8`. Pitch invert reads at `0x00729d2e`
  and `0x00729e28`.
- Gamefunc bits: 0/1 add and subtract forward velocity, 15/16 strafe, 9/10 become the record's jump
  and crouch bits (packing at `0x00729b7a`), 7 is the run key (0x50 versus 0x28 speed cap). Turn
  keys with the strafe modifier are at `0x00729dxx` (bit 2 adds to `svel`, bit 3 subtracts).
- Menu lockout (`gm & 0x11`, from `0x0072984c`): stores the four velocity globals, the record's
  `avel`/`horz` short and `fvel`/`svel` int, then reduces the action bits to the one bit that path
  keeps. Reproducing those six stores suppresses every action while a camera flies.
- The mouse accumulator vtable call is `MOV ECX,[mouse source]; MOV EAX,[ECX]; push &y; push &x;
  CALL [EAX+0x14]`.

### 8b. processinput (`FUN_0072A7A0`)

- Identifies its player by `snum == *(int*)(base + 0x7824CC)`, seen at `0x0072ccee` and
  `0x0072b3c1`. It doubles `avel` at `0x0072c0cd`.
- Previous-tic position: it stores `opos` at `0x0072b667` / `0x0072b66d` before the move (`oposz` at
  player `+0x240`, `0x0072b69a`; loads at `0x0072b636`); respawn sets all three from `pos`
  (`0x0072b308` / `0x0072b338`). `pos - opos` is one tic of the player's own movement.
- Tail: clamps `horiz` to -99..299 at `0x0072caea`; with `GameMouseAiming` off it snaps the horizon
  toward 100 at `0x0072cabe`.
- `horizoff` (int16, read signed, `MOVSX word`) is the second half of every shot's vertical:
  `shoot` computes `(100 - horizoff - horiz) * K` and reads the field ten times across its weapon
  branches (e.g. `0x00725295`: `MOVSX EAX,word ptr [EBX + 0xcad8c0]`). `processinput` adds
  `(floorz change * 0xA0) >> 16` to it at `0x0072aae8` when the floor changes by at most `0x400`,
  then decays it with `horizoff -= (horizoff>>3)+1`; it is nonzero only while the player moves.
- `pyoff` (player `+0x244`) is read whole by `shoot` at `0x00724f59` (`MOV EDI,dword ptr
  [EDX+0xcad804]`) and added to `posz` plus `0x400` for the shot's origin; it ran 0..2446 in play.
- `ang`/`horiz` interpolation partners: `oang` at player `+0x2CE` (int16), `ohoriz` at `+0x224`
  (int32).

### 8c. shoot (`FUN_00724f00`) and aim (`FUN_00724ac0`)

- `shoot` is identified by its body, where six known offsets agree: `CMP AX,0x57D` (APLAYER),
  `IMUL EDX,EAX,0x408` (player stride), `[EDX+0x214/0x218/0x21C]` posx/posy/posz, `[EDX+0x244]`
  pyoff `+0x400`, `[EDX+0x2CC]` ang. Arguments are register-passed (ECX, EDX); the prologue is
  `PUSH EBP; MOV EBP,ESP; AND ESP,0xFFFFFFF8`, six bytes, position-independent. Callers:
  `processinput` for the player and eight functions in the `0x6dxxxx` AI cluster for enemies.
- `FUN_006ee810` (rva `0x3E810`) matches a size-and-callers scan for `shoot` and is SPAWN, not shot
  code.
- `shoot` runs inside `processinput`, before the mod's per-tic aim snapshot is published, so that
  snapshot is one tic stale. `shoot` calls `checkhitsprite`, which calls `shoot` back with a blood
  or decal picnum; that nested call can carry the player's own sprite index.
- This fork renumbered its tiles: the pistol arrives as `atwith` `0x9E5` where Megaton's list has
  `0xA23`, so Megaton's `atwith` filter (section 10) matches nothing here.
- `aim()` opens with `CMP dword ptr [EAX + 0xcad848],0x1` at `0x00724b07`, `JLE <search>` (EAX =
  player index * `0x408`): any value above 1 returns -1 without searching. `0xcad848` is player
  `+0x288`, `GameMouseAiming` (int32; ps base rva `0x5FD5C0`, VA `0xCAD5C0`), bound by name in the
  cvar table at `FUN_006e2e10`. This fork has no `player.auto_aim`: the `AutoAim` cvar binds to a
  global (VA `0x015910A4`) that no instruction reads. `aim()` searches around the player SPRITE's
  `ang`, not `ps[].ang`, so a redirected shot is dragged onto an enemy near the real crosshair
  unless `GameMouseAiming` is above 1.
- Substituting for one `shoot` call takes five fields: `ps[].ang`, `ps[].horiz` (what `shoot`
  reads), `oang`/`ohoriz` (so interpolation readers never see them disagree) and the `GameMouseAiming`
  value 2.
- A shot fired from the muzzle starts `pyoff + 1024` below the eye, so a pair solved from the eye
  lands that much low at the target (8.2% to 27.8% of a pigcop's height across the measured pyoff
  range, against a head 6.25% below the sprite top).

### 8d. Player fields and visibility

- Player `+0x268`: visibility, a SHORT holding 512 (`const_visibility`), the value True3D's world
  pass uploads as `g_playerVis` in the `PlayerVisBuffer` constant buffer. Megaton's `PS_VISIBILITY`
  is also `0x268` but a long: `0x268` and `0x26A` both read 512 here and a dword reads `0x02000200`.
  The classic engine global at rva `0xE07138` reads 769 and feeds a renderer this game does not use.
- Player `+0x328` is the player's sprite index (`displayrooms` indexes the sprite array with it at
  `0x006edb8a`); that sprite's `extra` is the health (section 4.1).
- Keycards are player `+0x31E` (section 6).

### 8e. The drawrooms call chain

`FUN_00780860` (hw drawrooms, rva `0xD0860`) -> `FUN_007b9800` -> `FUN_007b9870` (the matrix, from
the five globals) and `FUN_007f9c80` (the scene pass, a sibling call). `FUN_007f9c80` copies
`globalposx/globalposy` to the scene object at `+0x26C/+0x270` and `globalang` at `+0x27C`, calls
`FUN_007fa150`, which refreshes `globalcursectnum` (`0xE07160`) through the engine's own
`updatesector` (`FUN_00763320`) from those copies (a failure keeps the old sector), then
`FUN_007fa1b0`, the portal walk seeded from `globalcursectnum`; its first line is `if (-1 < sect)`,
so a negative sector draws an empty frame. `FUN_00780860` has one caller in the image
(drawrooms, `0x00756f85`), no arguments, a six-byte position-independent prologue (`push ebp; mov
ebp,esp; and esp,-8`, eight-byte aligned) and contains both the matrix and the visibility walk.
`displayrooms` computes the frame fraction at `0x006faa71` (`FUN_006fa4a0`) and derives the sin/cos
globals at `0x00756e5c`.

`FUN_007ba920`, the per-sprite geometry update, bakes billboard yaw (called first by
`FUN_0075cfa0`, before drawrooms writes `globalang`). Of the globals a camera substitutes, only
`globalang` is read anywhere in its subtree (xref enumeration); nothing there reads position,
sector or the trig pair. Prologue: the same first six bytes as `FUN_00780860`, then `sub esp,8` and
a `push esi` / `push edi` pair before it loads the render object. The engine's `transporter_hold`
is the tics-left counter that stops a transport carrying the camera straight back.

`gm` equal to 4 exactly is `MODE_GAME` alone; the menu sets `MODE_MENU` beside it and the
end-of-level tally sets `MODE_EOL`.

## 9. Weapon choice (autoswitch)

- `ud.wchoice` is at VA `0x158FCE0` (stride `0x34`, 13 ints a player). `DUKE3D.CFG`
  `WeaponChoice0..9` under `[Misc]` is parsed at rva `0x335B1`.
- `checkavailweapon` decodes `if (v == 0) v = 9; else v--` at rva `0x211B2`; an unowned or dry
  queued weapon falls through `0x21153` / `0x2115E` -> `0x21168`.
- `addweapon` (rva `0x20F90`) writes its fields at rva `0x20FB5..0x20FFD`; re-entry for a weapon you
  hold is the `jne` at `0x20F9C` -> `0x20FB3`.

## 10. Sites in the Megaton executable (shared `target.h`)

These belong to the Megaton binary, not World Tour (section 8c: tiles are renumbered here). They are
the player's `atwith` picnums at `processinput`'s own `shoot` calls (`PUSH <atwith>; PUSH ps[].i;
CALL 0x0040316b`, the shoot thunk), all sixteen calls the player can take:

| atwith | name | VA | weapon | player's aimed shot |
|---|---|---|---|---|
| `0xa23` | SHOTSPARK1 | `0x004fe458` | pistol, at `kickback_pic == 1` | yes |
| `0xa35` | SHOTGUN | `0x004fe58c` x7 | shotgun, 7 pellets at `kb == 4` | yes |
| `0x9e8` | CHAINGUN | `0x004fe876` | chaingun | yes |
| `0x990` | GROWSPARK | `0x004fe956` | expander, `curr_weapon` 11 | yes |
| `0x9fc` | SHRINKSPARK | `0x004fe9cf` | shrinker, `curr_weapon` 6 (spawns `0x66e` at fixed 768) | yes |
| `0xa2d` | RPG | `0x004fea65` | rpg | yes |
| `0xa2d` | RPG | `0x004fecf8` | devastator | yes |
| `0x669` | FREEZEBLAST | `0x004feb2b` | freezer | yes |
| `0xa03` | TRIPBOMB | `0x004febd8` | wall placement, not a shot | no |
| `0x9d9` | KNEE | `0x004fec4a` | mighty foot, melee | no |

Melee gives up past two metres and never calls `aim()`. The other `shoot` dispatches are not the
player's: SPIT, COOLEXPLOSION1 and FIRELASER are enemy weapons (SPIT/FIRELASER at `0x004f4d58`), and
the blood and decal hitscans (`0x8f8..0x8fb`) are `checkhitsprite` firing back into `shoot` on
impact, with the sprite that was HIT, which can be the player's own.

Other Megaton-binary sites quoted by the shared headers (VA):

- Shot-latency order in `processinput`: saves `opos*` at `0x004fb020-0x004fb03e`, moves via
  `clipmove` (`0x004fcf4d-0x004fcf9f`), then calls `shoot` (`0x004fe462-0x004fed02`).
- The hit handler treats `extra <= 0` as dead (`0x004e2ea0`).
- `processinput` overwrites pos/ang from `sprite[newowner]` while a camera is active
  (`0x004fb20d-0x004fb216`); the normal `oang = ang` copy is at `0x004fb633`.
- `moveactors` stores `bpos` at the top of the think (`0x004a08e4`, `0x004a08f7`, `0x004a090a`);
  this is the source of the grounded-actor `vz` artefact.
- `aim()` is at `0x004f3010`; `shoot`'s own `sz = ps.posz + ps.pyoff + 0x400` at
  `0x004f36c0-0x004f36d3`.
- `makeitfall`: floorz tests at `0x004dd12d-0x004dd13f` (snap `floorz - 0x100` at `0x004dd1da`);
  `gc` (VA `0x005ceb38`, init 176) is loaded at `0x004dd05e` and added to `zvel` at `0x004dd187`;
  `zvel` is added to `z` at `0x004dd1a9-0x004dd1af`; `zvel` is capped at `0x1800` (`0x004dd179`).
- `processinput` horiz clamp and snap: `CMP [EDX+0x220],0x12b` at `0x004fd559`, `CMP [ECX+0x220],-0x63`
  at `0x004fd577`; mouse-aim test at `0x004fd4da`; the 95..105 snap to 100 at `0x004fd50a-0x004fd522`.
- Engine input: the engine adds the mouse at the top of `processinput`, which is where a HOLD VIEW
  mouse turn is removed.

## Open questions

- Wall-clock tic rate (26 Hz not confirmed): the runtime rate at `*(DAT_0158ef74 + 0x18)`.
- Whether the smoothing branch (`DAT_00c8b31c`) is ever set at runtime (static: no writer).
- Which render targets `clearview` clears, and whether the composite covers the side strips.
- How the UI layer lays out and draws the `hud*` widgets.
- What consumes the float copies at `+0x274` / `+0x278` of the scene object.
- Where `+0x3EC` (secrets in the level) is totalled at level start.
- Inferred layouts: sprite `+0x20`/`+0x24`, sector `floorstat`, wall `nextwall`/`picnum`/`lotag`; the 3D pass viewport mapping.
- Weapon and movement action bits in the input record (not single-bit; only the table in 7b is measured).
