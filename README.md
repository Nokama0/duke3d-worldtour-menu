<div align="center">

# Duke Nukem 3D World Tour Mod Menu

**Outlines, aim assist, a free camera and a smooth view, one key away.**

A mod menu for Duke Nukem 3D: 20th Anniversary World Tour on Steam.

![Game](https://img.shields.io/badge/Game-World%20Tour-FFB000?labelColor=0E0F13&style=flat)
![Platform](https://img.shields.io/badge/Platform-Windows-FFB000?labelColor=0E0F13&style=flat)
![Release](https://img.shields.io/github/v/release/Nokama0/duke3d-worldtour-menu?label=Release&color=FFB000&labelColor=0E0F13&style=flat)
![Build](https://img.shields.io/github/actions/workflow/status/Nokama0/duke3d-worldtour-menu/build.yml?branch=main&label=Build&labelColor=0E0F13&style=flat)
![License](https://img.shields.io/badge/License-Unlicense-FFB000?labelColor=0E0F13&style=flat)

</div>

---

A mod menu for **Duke Nukem 3D: 20th Anniversary World Tour** on Steam (build 386, 2016.10.26). Press **Insert** in
game to open it.

It loads as an `XINPUT1_3.dll` next to the game's `duke3d.exe`, forwarding the game's controller calls to the real
XInput in System32. It does not change any game file. Features that patch the game's code do so in memory only, and
each checks the exact bytes it expects first: if they are not there, that feature stays off.

## What it does

The menu has five tabs.

**Visuals**
- Outlines for enemies with health bars, names and distance
- Markers for items and pickups, secret areas (with the secret's volume and a route to the nearest one), explodables,
  breakables, and switches and locks
- Secret triggers; damage numbers; draw distance with an unlimited option; fullbright

**Combat**
- Aimbot: hold key, smoothing
- Triggerbot: fire condition, delay
- Silent aim: maximum redirect
- Target choice, aim point, cone, range and lead for moving targets

**Weapons**
- Perfect accuracy for the pistol, chaingun, shotgun and expander
- Modern mouse aim: raw input, equal axes, sensitivity and vertical scale
- Fire rate and weapon switch speed

**View**
- Custom field of view
- A free camera: flight speed, teleport, fullbright, no fog or culling, followed teleporters, screenshot mode

**Settings**
- Automatic weapon switching, actions for mouse buttons 4 and 5, hotkeys
- Menu scale, autosave, menu animations and a debug log

Cheat-style features start off. Hover a row for a short explanation. Rows show "(Unavailable)" if the game's code
does not match what the feature expects.

**Beyond the menu**, `wtmod.ini` switches on:
- A borderless window that fills the monitor and a frame cap
- A smooth view that draws the camera between the game's whole-number turning steps
- A small readout of frame rate, position and facing

## Get it

You need Duke Nukem 3D: 20th Anniversary World Tour on Steam, build 386. The installer script refuses any other
`duke3d.exe`, including the Megaton Edition's. Other builds are not supported.

1. **Download** `XINPUT1_3.dll` from the [latest release](https://github.com/Nokama0/duke3d-worldtour-menu/releases/latest).
2. **Copy** it next to `duke3d.exe`.
3. **Start** the game and press **Insert**.

If an `XINPUT1_3.dll` is already there, it belongs to something else (a controller wrapper such as x360ce, or
another mod). Do not overwrite it.

To remove it, delete `XINPUT1_3.dll`, `wtmod.ini` and `wtmod.log` from the game folder.

## Good to know

- **Where files go.** Settings are saved to `wtmod.ini` and the log to `wtmod.log`, both beside the game.
- **Ultrawide.** The game accepts any resolution but has no menu to choose one. Its settings are in
  `%LOCALAPPDATA%\Gearbox\DukeNukem\profile.dat`: the screen width is a four-byte number at offset 179 and the
  height at offset 183. Back the file up first.

---

## Build it

You need Windows with Visual Studio 2022 (the MSVC x86 32-bit toolset) and a Windows 10 SDK. The scripts find them
with `vswhere`.

```powershell
powershell -File src/wtmod/build.ps1
powershell -File tools/wt_install.ps1 -GameDir "<game folder>"
```

### Tests

`build.ps1` runs the unit tests and the installer's self-test first, then compiles `build\XINPUT1_3.dll`. A red test
fails the build; a good run prints a line ending in `N passed, 0 failed`.

### Install

`wt_install.ps1` copies the DLL next to `duke3d.exe` and writes a default `wtmod.ini` if there is none. It refuses
while the game is running, if `duke3d.exe` is not the supported build, and if the `XINPUT1_3.dll` already there is
not one it installed. It prints `MATCH` once the installed file equals the build.

Design notes and reverse-engineering findings are in [`docs/`](docs).

## Other menus

The same menu, ported to other Duke Nukem games.

| Game | Repository |
|---|---|
| Duke Nukem Forever | [dukenukem-forever-menu](https://github.com/Nokama0/dukenukem-forever-menu) |
| Duke Nukem: Manhattan Project | [dukenukem-manhattanproject-menu](https://github.com/Nokama0/dukenukem-manhattanproject-menu) |
| Alien Armageddon on EDuke32 | [eduke32-aa-menu](https://github.com/Nokama0/eduke32-aa-menu) |
| Duke Nukem 3D on EDuke32 with the High Resolution Pack | [eduke32-hrp-menu](https://github.com/Nokama0/eduke32-hrp-menu) |
| Duke Nukem 3D: Megaton Edition | [duke3d-megaton-menu](https://github.com/Nokama0/duke3d-megaton-menu) |

## License

[The Unlicense](LICENSE): public domain, no warranty.
