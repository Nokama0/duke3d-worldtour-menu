# Screenshot mode in World Tour: the three sites

Screenshot mode hides the HUD weapon, the crosshair and the status bar. Megaton does it with three thunk hooks and two `ud` fields. This fork has no thunks, no `ud.screen_size` and no classic status bar, so each mechanism is different here; the pure rules in `shot.h` carry over unchanged. The mod ships it and it works in game.

Method: `duke3d_wt_dump.exe` read with a small PE reader plus capstone over the image (Ghidra's MCP cannot load a program headlessly), enough for "who calls X" and "what does X do". Image base `0x6B0000`, so VA = `0x6B0000` + RVA.

## 1. The HUD weapon

`FUN_007282b0`, rva `0x782B0`. **One caller in the whole image**, at rva `0x3CF29`, inside
`displayrest` (`FUN_006eca00`, rva `0x3CA00`):

```
03CEFF  cmp   word ptr [0x158fc7a], -1     ; camerasprite == -1
03CF07  jne   skip
03CF0D  mov   al, byte ptr [0x158fc35]
03CF12  cmp   al, 2
03CF14  je    ...
03CF29  call  0x7282b0                     ; <- displayweapon
```

It takes **no argument**: the call site pushes nothing, and the body reads globals
(`movzx eax, word ptr [0xc9cf94]`) rather than `[ebp+8]` - a scan of the function found no
`[ebp+8]` read at all. Prologue `55 8B EC 83 EC 40` (`push ebp; mov ebp,esp; sub esp,0x40`), so
the instruction boundary at or past five bytes is **6**.

## 2. The crosshair

Gated in `displayrest` at rva `0x3D217`:

```
03D20A  cmp   byte ptr [0x158fc35], 0
03D211  jne   skip
03D217  cmp   dword ptr [0x158fcd8], 0     ; <- the gate
03D21E  je    skip
03D224  cmp   word ptr [0x158fc7a], -1
...
03D244  mov   ecx, 0x9db                   ; the crosshair tile
```

`[0x158fcd8]` (rva `0xEDFCD8`) is the cfg's `[Misc]` / `Crosshairs` key. The parser at
rva `0x333F9` pushes the string `"Crosshairs"` (`0x93ef6c`) under section `"Misc"`
(`0x93ef5c`), runs it through `atoi` and stores the result to that global at `0x3342D`.

Because it is a user setting whose own off state is `0`, it is **held** for the flight rather
than bracketed around a draw: holding it asks the engine for nothing it does not already do to
itself.

## 3. The status bar (the trap)

There is no `drawstatusbar` here. `ScreenSize` (`DAT_0158fcd0`, rva `0xEDFCD0`) selects a named
C++ UI page, and **writing it does nothing on its own**. The game's own `#NoHud` console command
(rva `0xA2B4E`) is the whole recipe:

```
0A2B4E  xor  eax, eax                      ; #NoHud -> 0   (#MinimalHud -> 4, default -> 8)
0A2B50  cmp  eax, dword ptr [0x158fcd0]
0A2B56  je   done                          ; no change, no work
0A2B58  mov  dword ptr [0x158fcd0], eax    ; 1. write ScreenSize
0A2B5D  call 0x730890                      ; 2. recompute the 3D view (setview)
0A2B62  pop  esi
0A2B63  jmp  0x6f5b20                      ; 3. show or hide the hud page
```

`FUN_00730890` (rva `0x80890`) and `FUN_006f5b20` (rva `0x45B20`) both take **no argument**:
`0x45B20` is entered by `jmp` after `pop esi`, with `ECX` holding nothing meaningful (it was
clobbered by the string compare two instructions earlier), and its own body reads `ScreenSize`
from the global. It is a plain `void(void)`.

`FUN_006f5b20` reads `[0x158fcd0]` and switches: `0` hides both pages, `4` shows
`"HudGameMinimal"` (`0x93f60c`), `8` shows `"HudGameClassic"` (`0x93f61c`).

That three-step sequence **is** Megaton's `screen_size = 0; vscrn();`, in two calls instead of
one. Writing the global without the two calls is the trap: it looks like it worked and moves
nothing.

## Megaton's sites, for comparison

The shared `shot.h` carries the Megaton executable's constants (VA = rva + 0x400000), none of which
exist in World Tour: `displayweapon` thunk `0x004035B7`, body `0x004F7180`; `displaymasks` thunk
`0x00401852`, body `0x004F6AB0`; `displayrest` thunk `0x004036BB`, body `0x004BE940`; `vscrn` thunk
`0x004032A1`, body `0x00506530`.

## Not used, and why

`[0x158fc7a]` (rva `0xEDFC7A`, a word) gates the weapon **and** the crosshair with the same
`!= -1` test, which is the shape of Duke's `ud.camerasprite`. Setting it would take both off in
one write and no hook at all. It is left alone on purpose: the same field selects which view
`displayrooms` renders, so a bogus value could put the engine on a camera that does not exist.
Every mechanism above is one the game already performs on itself.
