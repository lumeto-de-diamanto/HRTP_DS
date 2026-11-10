# Touhou Reiiden ~ The Highly Responsive to Prayers — DS port

A work-in-progress port of *Touhou Reiiden* (東方靈異伝　～ The Highly Responsive to Prayers, 1997)
to the Nintendo DS, based on the game's fully decompiled source code at
[ReC98](https://github.com/nmlgc/ReC98) (`anniversary` branch).

## How to build

Requirements:

* G++
* GNU Make 4.3 or newer
* Python 3 (with NumPy)
* The Wonderful Toolchain package manager
  [(instructions)](https://wonderful.asie.pl/wiki/doku.php?id=getting_started)
* BlocksDS [(instructions)](https://blocksds.skylyrac.net/docs/setup/)
* An .hdi image containing HRtP, placed into `assets/th01.hdi`
* PC-98 drum samples in `assets/` as .wav files
* A PC-98 font file, placed into `assets/font.bmp`

Once everything is set up, simply type:

```bash
make
```

The rom file `hrtp_ds.nds` will then appear.

## Checks:

* `make asset-tests font-tests`: tests clean/parallel asset builds
and font generation
* `make rom-checks`: rebuilds and verifies the DS header,
executable ranges, linked font and exact NitroFS contents
* `make timer-tests timer-sanitize`: checks original stage durations, frame cadence,
extensions and HURRY UP

## Current status

* [ ] Display
  * [ ] Bullets and blitter efects (*in progress*, only bullets have been implemented)
  * [x] Bitmap background
  * [x] Tiles, turrets and barriers
  * [ ] HUD (*in progress*, functional but needs some visual refinement)
  * [x] Font
* [ ] Core gameplay
  * [x] Player controls
  * [x] Orb
  * [ ] Bombs
  * [ ] Damage and game over (*in progress*, no animation)
  * [ ] Pause screen and stage results (*in progress*, functional but needs visual refinement)
* [x] Music and SFX engine
  * [x] Music
  * [x] SFX
* [ ] Bosses
  * [x] SinGyoku
  * [ ] YuugenMagan
  * [ ] Mima
  * [ ] Elis
  * [ ] Kikuri
  * [ ] Sariel
  * [ ] Konngara (*in progress*, intro implemented)
* [ ] Other screens
  * [ ] Start-up screen
  * [ ] Title screen and menu
  * [ ] Round results
  * [ ] Endings and credits
  * [ ] Hero Scroll/score table
* [ ] Miscellanea
  * [ ] Icon

## Layout

* `source/engine/` — game logic (ported from ReC98 `th01/`, kept verbatim
  where validated)
* `source/platform/` — DS hardware layer (OAM renderers, tile/canvas/text
  layers, subscreen HUD, menus)
* `source/main.cpp` — entry point and the game loop
* `tools/` — asset converters, native audio tooling, local source dependencies
* `tests/` — code and asset tests
* `assets/` — original game data, plus extracted and converted assets
* `arm7/` — various ARM7-related BlocksDS services
* `native_audio/` — native DS music and SFX driver

## Controls

Current controls:
* **Left/Right** — move Reimu; **A** — swing gohei; **B** — shoot ofuda
  (and serve the orb at the start of a card stage)
* **START** — pause (RESUME / QUIT; QUIT restarts at stage 1 until the
  title screen exists)
* **SELECT + d-pad** — dev stage navigator (←/→: stage, ↑/↓: scene;
  Shrine has five stages now, other scenes four; scene 1/2 stage 1 =
  Makai/Jigoku stage 6)
* **SELECT+X** — Konngara intro preview; returns to/restarts the current stage

## AI disclaimer

AI assistance is used throughout this codebase (tooling, engine, and platform
layers).

## Special thanks

* **Nmlgc** and the ReC98 contributors — for the complete decompilation of HRtP
* **All the BlocksDS contributors** — for the SDK that makes this port possible
* **ZUN** — for Touhou Project itself
