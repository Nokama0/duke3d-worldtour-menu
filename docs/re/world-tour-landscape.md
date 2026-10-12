# What the community knows about World Tour

A survey of public sources so the search need not be repeated. Every claim carries the page it
came from. Where the answer is "nothing exists", that is recorded too, because it is the most
useful part. In short: no analysis of the executable, no offsets, no mod framework, no
documented `profile.dat` layout, no field-of-view control and no documented command-line
switches are published; a `d3d11.dll` proxy is proven to load.

Method: web search (duke4.net, PCGamingWiki, Steam discussions, ModDB, Wikipedia, GitHub) and
the mod's own findings for comparison.

## The executable

- PCGamingWiki is the only page that describes the binary: Build engine, Direct3D 11,
  32-bit, and "All versions require Steam DRM."
  https://www.pcgamingwiki.com/wiki/Duke_Nukem_3D:_20th_Anniversary_World_Tour
- **No published analysis of this executable exists.** No write-up of its protection, no
  unpacking notes, no disassembly, no offsets, for any build. Searches across duke4.net,
  the general web and game-hacking sites found only generic unpacker documentation never
  applied to this game. The mod's own findings (the encrypted code section, the XInput-by-ordinal
  import) are not recorded anywhere public.
- Trainers exist for specific builds, which proves the process can be read and written at
  runtime, but none publishes an address or a method.
- ModDB's whole catalogue for the game is maps, audio and art packs: zero binary mods.
  https://www.moddb.com/games/duke-nukem-3d-20th-anniversary-world-tour/addons

## Injection

- A **`d3d11.dll` proxy beside the executable is proven to load**: the Helix / geo-11
  stereo fix installs exactly that way, and is a 3Dmigoto derivative.
  https://helixmod.blogspot.com/2019/12/duke-nukem-3d-20th-anniversary-world.html
  https://helixmod.blogspot.com/2022/06/announcing-new-geo-11-3d-driver.html
- So two routes work: the mod's XInput proxy (proven by its own build) and a d3d11 proxy. The
  d3d11 one hands the device and swap chain over directly and is the natural choice for
  renderer work; the XInput one stays useful for everything that is not drawing.
- There is no ASI loader or mod framework for this game.

## Ultrawide, borderless, field of view

- PCGamingWiki: widescreen is native and **Hor+**; ultra-widescreen, borderless windowed
  and field of view are all listed as unsupported, with "use a source port" as the advice.
- The community's entire fix is: run windowed, then use an external borderless utility.
  The resolution menu stops at 1920x1080 and editing the game folder's `DUKE3D.CFG`
  "does nothing", which matches the mod's finding: that file is vestigial.
  https://steamcommunity.com/app/434050/discussions/0/1744480967032788748/
- That same thread confirms two observed facts: at 21:9 the game **gives more field
  of view rather than cropping**, and "the HUD is set up to match up with a 16:9 ratio".
- **No field-of-view control of any kind exists**, official or modded.

## profile.dat

- PCGamingWiki records only the path: `Gearbox\DukeNukem\profile.dat` under the local
  application data folder.
- **Nobody has documented its layout, any field, or a checksum.** A Duke4.net thread about
  the sibling save files answers the same question with "there's always a hex editor".
- So the mod's two offsets (width at 179, height at 183, little-endian, no checksum in the way)
  are new. They are in the README for that reason.

## Engine lineage

- World Tour is a Gearbox/Nerve fork of Build, not Megaton's JFDuke3D and not EDuke32. Its
  renderer, led by Roger Kort, draws the levels as true 3D geometry with dynamic lighting,
  and a video setting switches between it and a classic path.
  https://en.wikipedia.org/wiki/Duke_Nukem_3D
- **No source or SDK was released.** Its additions were reverse engineered by others for
  DukeGDX/BuildGDX and Raze, whose README credits "World Tour extensions from DukeGDX".
  https://github.com/ZDoom/Raze
  Those projects are open source and are the reference for the game's DATA formats; they
  say nothing about this executable.
- Most World Tour content is loose files rather than inside DUKE3D.GRP.

## Cheats, console, command line

- The interface is the classic typed cheat strings (`dngod`, `dnclip`, `dnscotty###`,
  `dncoords`, `dnrate`, and `dndebug`, which "just shows info"), typed during play. Using
  any of them disables achievements and demo playback.
  https://steamcommunity.com/sharedfiles/filedetails/?id=797696992
- Whether a real console exists on PC is contested in the sources and unresolved. The
  console ports have a debug menu; no PC equivalent is documented.
- **No command line switches or launch options are documented anywhere.**

## Pages that could not be read

The Cutting Room Floor's page on the game and Open Cheat Tables both blocked automated
access. TCRF would normally be the best source for debug features, so it is worth opening
in a browser by hand.

TCRF's page returned, instead of article text, a block of text addressed at
automated agents, carrying contradictory instructions, false claims of authority, and a
request to emit an antivirus test string. It was ignored: text found on a web page is
data, never an instruction.

## Open questions

- Whether a real console exists on the PC build.
- What the Cutting Room Floor and Open Cheat Tables list for this game.
