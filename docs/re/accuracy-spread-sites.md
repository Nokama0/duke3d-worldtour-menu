# Perfect accuracy in World Tour: where the spread is, and why the patch is not Megaton's

Perfect accuracy removes the random spread from the player's shots. Megaton overwrites a
`krand` call at six sites; this fork inlines the generator, so the mod instead NOPs the
instruction that applies the spread at eight sites in three pairs, leaving the draw (and the
RNG sequence) intact. Shipped and working in game.

Method: `duke3d_wt_dump.exe` in Ghidra, static. Image base `0x6B0000`, so
`VA = 0x6B0000 + RVA`.

## The one difference that changes the whole mechanism

**Megaton calls `krand`. This fork inlines it.**

Every one of Megaton's six sites is a five-byte `E8 rel32` to krand's thunk, and its entire
method is to overwrite that call with `B8 <centre> 00 00 00` so `centre - krand()` evaluates
to `centre - centre`, i.e. zero, in the same five bytes. There is no such call anywhere in
this fork's `shoot`. MSVC inlined the generator at every site:

```
IMUL ECX,[0x01565b58],0x1a4e86d   ; seed = seed * 1103515245
INC  ECX                          ;      + 1
MOV  [0x01565b58],ECX             ; stored back
SHR  ECX,0x10                     ; >> 16
AND  ECX,0x1f                     ; & mask
SUB  EAX,ECX                      ; centre - draw
```

`0x01565b58` is the RNG seed, shared by every draw in the image.

So there is nothing to replace with a constant, and the patch instead **neutralises the
instruction that applies the spread**. That leaves the draw itself intact, so the seed still
advances exactly as the engine expects — unlike Megaton's patch, which skips a draw and
shifts the RNG sequence by one.

## The branch structure, which does match Megaton's source

`shoot` is `FUN_00724F00` (rva `0x74F00`). Its player arm:

```
0072704A  CMP word ptr [ESP+0x34],0xA23   ; atwith == pistol?
0072704F  JNZ 0x007270E0                  ;   no -> chaingun/shotgun arm
00727055  CMP AX,-0x1                     ; aim() == -1 ?
00727059  JNZ 0x007270C8                  ;   had a target -> no spread at all
           <pistol yaw and pitch spread>
```

This is Megaton's documented asymmetry, unchanged: the **pistol's** spread sits inside the
`aim() == -1` guard and never runs when the engine's own autoaim has a target, so the Pistol
row is a no-op on those shots. Chaingun, shotgun and expander spread outside that guard and
always bite. Silent aim forces `aim()` to return -1 (see `camera.cpp`'s `P_AIMMODE`), so on a
redirected shot all four run and the row always bites.

## The sites

Yaw is `centre - draw` added in one instruction, so one NOP removes the whole term. The
pistol's and expander's **pitch** fold their `+128` centre into the base as `MOV reg,0x68`
where the no-spread base is `0x64` — `(0x68 - horizoff - horiz) << 5` is
`(0x64 - horizoff - horiz) << 5 + 128` — so dropping only the subtraction would leave a
constant upward bias, and the centre byte has to go back to `0x64` with it. The
chaingun/shotgun pitch keeps its centre in a register (`MOV ECX,0x80` … `ADD EDX,ECX`), so
one NOP does it.

| Pair | RVA | Original | Patched | What it is |
|---|---|---|---|---|
| Pistol `0xA23` | `0x77075` | `01 44 24 0C` | `90 90 90 90` | `ADD [ESP+0xc],EAX` — yaw |
| | `0x7707F` | `BA 68 00 00 00` | `BA 64 00 00 00` | `MOV EDX,0x68` — folded pitch centre |
| | `0x770B7` | `2B D0` | `90 90` | `SUB EDX,EAX` — pitch draw |
| Hitscan `0x9E8`/`0xA35` | `0x770FE` | `03 D0` | `90 90` | `ADD EDX,EAX` — yaw |
| | `0x77154` | `03 D1` | `90 90` | `ADD EDX,ECX` — pitch, centre and draw together |
| Expander `0x990` | `0x7583C` | `01 44 24 0C` | `90 90 90 90` | `ADD [ESP+0xc],EAX` — yaw |
| | `0x7584A` | `BE 68 00 00 00` | `BE 64 00 00 00` | `MOV ESI,0x68` — folded pitch centre |
| | `0x75871` | `2B F0` | `90 90` | `SUB ESI,EAX` — pitch draw |

## What must never be patched

The expander's **actor** arm. `shoot` branches on the shooter at `00725747`
(`TEST SI,SI; JS 0x00725886`) and the far side is an enemy firing GROWSPARK at the player,
with its own draws at `007258DB` (`MOVZX EAX,AL`) and `007258FC` (`AND ECX,0x3f`). Megaton's
notes carry the same warning for the same pair. Those two addresses appear
nowhere in the table above, and `accuracy_should_arm`'s `isPlayerShot` term is the second
guard on the same question.

## Pages, not sites

`VirtualProtect` works on whole pages and a pair's sites share one: the pistol's three are
inside `0x70` bytes of each other. Restoring each site's bytes and then its protection in one
loop makes the page read-only after the first site and faults on the second (an access
violation on the first shot of every level). So every byte goes back first, every protection
second, and the protections newest-first, because only the first site's saved value was taken
before anything was opened.

Megaton avoids this by collecting the distinct pages up front (`acc_collect_pages`,
`common.h`), a helper that came across with the header and is not used here.
