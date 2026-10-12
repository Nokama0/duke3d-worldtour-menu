# The debug dump key

A key the player presses while standing in front of the thing being measured. Every producer
that knows about it reports the frame after the press. Default off; configured in
`wtmod.ini` (`debugdump`, `debugdumpkey`, default `P`).

## Why

Nine instruments in this port came back silent or wrong, and two of those were
wrong for the same reason: they sampled a moment that could not contain the
answer. The in-world tint's quads were reported as landing "wholly off the
picture" from the first drawn frame, which is the spawn point with every
combination lock across the map, while the player was standing in front of one.
A per-frame table was read after the code that clears it.

Choosing the moment in code means guessing, and the guesses have to be re-made
for every feature. The player knows the moment exactly. This hands them the
trigger.

## Using it

In `wtmod.ini`:

```
[wtmod]
debugdump=1
debugdumpkey=80
```

`debugdumpkey` is a Windows virtual-key code in decimal, 1..254. The default is
80, which is `P`.

Stand in front of what you want measured, press the key, and read the log. One
press produces one report from each producer. Press it again for another.

## The key, and what it was checked against

`P` collides with nothing on either side:

- **The game.** The player's own
  `Duke Nukem 3D Twentieth Anniversary World Tour\DUKE3D.CFG`,
  `[KeyDefinitions]` binds `W A S D E Q R F G H I J K M N T U V Z B C`, the
  digits, `Space Enter Tab BakSpc CapLck LShift RShift LCtrl RCtrl LAlt RAlt`,
  `Insert Delete Home End PgUp PgDn ScrLck`, the numeric pad, and
  `- = [ ] ' ; /`. `P` is not among them. Note `ScrLck` IS taken, by
  `Holster_Weapon`: it is the obvious choice and it is wrong.
- **The mod.** The panel is `Insert`, the free camera `V`, its teleport `T`, its
  movement `W A S D Space C LShift LCtrl`, and the clipboard chords are
  `Ctrl+Insert` / `Shift+Insert`. None is `P`.
- **The free camera.** The key works whether the camera is up or not; it is read
  in the same window procedure, with the panel closed, and is swallowed like the
  camera's own toggle so a rebound key cannot also fire a game action.

The panel's own "Also bound in the game" warning reads the same `DUKE3D.CFG`
through `gamecfg_key_bound`, so a player who rebinds `debugdumpkey` onto a game
function can still see the collision there.

## What reports

- `worldwt.cpp`: the bound targets and viewport at the draw, the matrix and
  both of the engine's copies, every queued quad's NDC span with a verdict, the
  chosen on-screen quad, and the depth readback with a full-buffer census.
- `esp.cpp`'s tint push: the raw sprite fields the geometry reads, the
  endpoints and z range produced, the sector clamp, and where the marker for the
  same sprite lands.

Adding a producer is three lines: a `static long lastDump = 0;`, a
`dbgdump_take(&lastDump)`, and the lines to print. Producers are independent, so
one press answers all of them.

## Rules it follows

- **The press always logs**, before any producer runs. A dump that finds nothing
  to say must not look like a key that never registered.
- **Default off.** A shipping build binds no key and prints no lines.
- **Counts may still be one-shots; positions may not.** A count is a condition
  and the first frame is a fine moment for it. Anything describing where
  something is, or comparing it against the scene, waits for the key.
