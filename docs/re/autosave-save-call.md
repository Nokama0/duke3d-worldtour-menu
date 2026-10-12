# Autosave in World Tour: the callable save

Megaton calls `dnSaveGame(slot)`, one `__cdecl int(int)`. This fork has nothing of the kind:
saving goes through a C++ screen controller driven by **string commands**. The callable save
is `FUN_0071cc30` (rva `0x6CC30`), `__fastcall(short slot, std::string* titleOut)`, returning
`-1` on failure, called with a null second argument from the game-thread tic. The mod ships
autosave this way.

Method: `duke3d_wt_dump.exe` read with a PE reader plus capstone (image base `0x6B0000`,
VA = `0x6B0000` + RVA).

## The two entry points that are not used

**The command registry.** `"saveGame"`, `"Save the game to a slot"`, `"loadGame"`,
`"saveGameList"`, `"deleteSaveGameSlot"` are registered at rva `0x6BA0` into an object at
VA `0x1591A20`, building a `std::string` per argument. Dispatch runs through a formatted-command
helper (`0x1B9A0`) taking a printf-style format, an argument array and a count, seen in use at
rva `0x95F70` building
`gui_showMustConfirmPopup #InvalidSaveTitle "%s" "deleteSaveGameSlot %i"`. Callable in
principle, ugly in practice, and it would put a synthesised console command into the game's
command history.

**The command handler.** `saveGame`'s own handler is rva `0x955A0` (`FUN_007455A0`),
`__thiscall`, `ret 4`: `this` is the `ScreenControllerSaveLoad` instance and the stack argument
is a `{begin, end}` token vector of 0x30-byte entries (`cmp eax, 0x30 / jge`). Calling it needs
both the instance and a hand-built token vector.

## The save call

The handler's **last act** is one plain call, and that call is the save:

```
095716  mov  esi, [ebp-0x1c]     ; the slot it parsed at 0x955FA
095719  lea  edx, [ebx + 0x14]   ; the slot record's title string
09571C  mov  ecx, esi
09571E  call 0x71cc30            ; <- the save
095723  cmp  eax, -1             ; -1 is failure
095726  jne  ok
095728  mov  byte ptr [ebx], 0   ; failure: mark the slot unused again
```

**`FUN_0071cc30`, rva `0x6CC30`, `__fastcall(short slot, std::string* titleOut)`**, returning
`-1` on failure. The handler is its only caller in the image.

Prologue:

```
06CC30  push ebp; mov ebp,esp; sub esp,0x14; push ebx; push esi; push edi
06CC39  mov  edi, ecx          ; slot in ECX
06CC3B  mov  [ebp-0x10], edx   ; titleOut in EDX
06CC3E  test di, di
06CC41  jns  ok                ; a negative slot returns -1 without saving
```

so the slot is a signed 16-bit and the function guards it itself.

### The second argument is an OUT parameter, and null is the game's own case

`[ebp-0x10]` is touched exactly twice in the whole function: stored on entry, read once:

```
06CDD5  mov  eax, [ebp-0x10]
06CDDB  test eax, eax
06CDDD  je   0x71cdff          ; <- null skips it entirely
06CDDF  cmp  cl, 1             ; did the write succeed?
06CDE4  mov  ecx, [ebp-8]; add ecx, 0xfa18   ; the saved level's name
06CDF1  push 0; push 0x91f028                ; or "" on failure
06CDF8  mov  ecx, eax
06CDFA  call 0x6c4d30                        ; std::string::assign
```

It is where the save *writes back* the resulting name, and the engine null-checks it. So
passing `0` is the engine's own supported case, and the mod never has to build or own an MSVC
`std::string`.

(Hand-building one would also work: the SSO layout is directly observable in
`.data` at rva `0xEE1A20`, where `"saveGame\0"` is followed by `08 00 00 00 0F 00 00 00`:
16-byte inline buffer, then size, then capacity 15.)

## What the handler does around the call, and why none of it is reproduced

```
0955FF  mov  esi, [0x158fcd0]        ; save ScreenSize
095615  mov  dword [0x158fcd0], 0
09561F  mov  byte [0xc8c2a7], 1
095626  mov  byte [0xe324ae], 1
09562D  mov  byte [0xe32486], 1
095634  call 0x6edac0                ; render a frame
095650  mov  [0x158fcd0], esi        ; restore, clear the three flags
0956A0  call 0x745be0                ; build the slot's thumbnail image
```

That is the **save-slot thumbnail** for the load menu, and all three flags are cleared before
the save call itself, so the save does not depend on any of it. It is deliberately not
reproduced: it forces a render, and a feature that fires on a timer has no business doing that.
The consequence is cosmetic: an autosaved slot may show a stale thumbnail until the list is
rebuilt.

## The clock

Megaton calls `autosave_frame` from `hooked_poll`'s drain-complete point and is explicit that it
is *not* a tic-clock write. This port has no poll hook on the game thread, and its `Present`
runs on the **render thread**; saving from there would read player state while the game thread
was mid-tic. So `autosave_tic` runs at the end of the tic, after the original, which is the only
game-thread frame boundary this port has. It writes no engine memory.

## Game-mode bits

`autosave.h`'s rule wants `inPlay`. Classic Duke's `MODE_*` bits survive in this fork: getinput's
own first test is `test byte ptr [esi+0x398], 0x11` at rva `0x79855`, which is
`MODE_MENU|MODE_TYPE` exactly. `MODE_GAME` is the `4` beside them, so `inPlay` asks for
`(gm & 4) && !(gm & 0x11)`, strictly more tolerant than Megaton's `gm == 4`, and the `0x11`
half is the engine's own test rather than an assumption.
