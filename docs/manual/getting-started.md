---
title: Getting started
nav_order: 2
---

# Getting started
{: .no_toc }

1. TOC
{:toc}

## What you need

- Windows 10 or 11, 64 bit. Mapsmith also builds and runs on Linux (see [Building from source](#building-from-source)).
- A graphics card with **OpenGL 3.3**. Every card from the last ten years has it.
- Steam with at least one Source 1 game or mod installed, for example Half-Life 2, a Source SDK Base 2013 mod or a
  Mapbase mod. Mapsmith reads the game's own files: materials, models, sounds and FGDs.
- To compile maps you need the game's compile tools (VBSP, VVIS, VRAD). They come with the game in its `bin` folder,
  or with the Source SDK / Mapbase compile tools.

## Install

1. Open the [newest release](https://github.com/29j2003/hammer-test/releases/latest).
2. Download **`Mapsmith-Setup-<version>.exe`** and run it.
3. The installer puts Mapsmith into your own user folder (`%LOCALAPPDATA%\Programs\Mapsmith`). It does not need
   administrator rights. Tick *Create a desktop shortcut* if you want an icon on the desktop.
4. Start Mapsmith from the Start menu.

To remove Mapsmith, uninstall it in the Windows settings like any other program. Your maps are never touched.

### The portable version

The same release also has **`Mapsmith-<version>-windows.zip`**. Unzip it into a folder you can write to, for example
`Documents\Mapsmith`, and start `Mapsmith.exe`. Do not use `Program Files`, because the built-in updater has to
replace the files later.

Each download has a `.sha256` file next to it with its checksum, if you want to check the download.

## First start

When Mapsmith starts, it looks through every Steam library on your PC. It finds:

- every Source 1 game, such as Half-Life 2, the episodes, Black Mesa and Counter-Strike: Source;
- every mod in `steamapps/sourcemods`, Mapbase mods included;
- the FGDs (entity definitions) and compile tools of each game, also from an existing editor configuration
  (`GameConfig.txt` in the game's `bin` folder) when there is one.

The game that was open last time is chosen again. On the very first start it is the first game found.

### Choosing a game or mod

Click the **Game** button at the top left (it shows the current game's name). The list is grouped by the game a mod
is based on and has a search field.

- Names are shown the way the game or mod calls itself, with the folder in brackets when two have the same name.
- Mods built on **Mapbase** are marked *[Mapbase]*. Mapsmith adds Mapbase's own FGD by itself, so Mapbase entities
  such as `env_global_light` are there.
- Folders that are only the content of another game, such as the `hl2` folder inside some mods, are marked
  *base content* and listed last. Pick the mod itself instead.

Switching the game reloads materials, models and entities. The open map stays open.

### Game settings (the ⚙ button)
{: #game-settings }

Next to the Game button is **⚙ Game settings**. Check it once for every game you map for:

| Setting | What it is |
|---|---|
| **Entity definitions (FGD)** | The FGD files, loaded top to bottom; a later file overrides classes of an earlier one. **Add FGD ...**, **Remove**, **Up**, **Down**. |
| **VBSP**, **VVIS**, **VRAD** | The compile tools. For Mapbase mods use Mapbase's compile tools. |
| **Game executable** | `hl2.exe` or the game's own program, used to run the map and build cubemaps. |
| **Launch options** | Extra options for starting the game. |
| **Map sources (.vmf)** | Where your `.vmf` files live. The *Maps* tab of the Project page lists this folder. |

Mapsmith fills these in by itself when it can. If a field is empty, click **...** and pick the file.

{: .note }
Black Mesa keeps its FGD (`bms.fgd`) and tools in `Black Mesa/bin`.

## Your first room in five minutes

1. **File > New map** (Ctrl+N).
2. Press **Shift+B** for the Block tool. Drag a rectangle on the grid, move the mouse up for the height, and click.
   This is the floor.
3. With the block still selected, open the **Tool** tab on the left (or right-click the block) and press **Hollow**.
   The block becomes six walls around an empty room.
4. Hold the **right mouse button** and fly inside with **W A S D**.
5. Press **Shift+E** for the Entity tool, type `player start` in the search field, pick `info_player_start`, and
   click on the floor inside the room.
6. Do the same with `light`: click once on a wall near the ceiling.
7. Press **F9** (**Run map**). Mapsmith asks where to save the map, compiles it and starts the game.

The [Building](building.html) and [Entities](entities.html) pages go through every tool in detail.

## The demo

Mapsmith comes with a demo you can try without any game installed. It has a fake Steam folder, a small mod with
textures, models, sounds and particles, and a demo map. With the source build:

```
build/tests/sme-make-fixture demo
Mapsmith --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf
```

The screenshots in this manual are made with this demo.

## Sample files to try the importers

Mapsmith comes with a **samples** folder (next to `Mapsmith.exe`; in a build of your own: `build/samples`) with files
to try each importer - all made by Mapsmith itself, free to use in your mods. Every import dialog has a
**Use a sample ...** button that opens the right part of it; `README.txt` in the folder lists every file.

| Folder | Files | Try it |
|---|---|---|
| `sounds` | `hum_loop.wav` (a seamless loop), `wind_loop.ogg`, `click.mp3` (one shot), `door_chime.flac` (stereo) | *Sounds* page › **Import ...** › **Use a sample ...** › `hum_loop.wav`, tick **Loops in the game**, **Import**; then drag it into the map: an `ambient_generic` that hums. |
| `pictures` | `terracotta_tiles.png` with `terracotta_tiles_normal.png`, `rusty_grate.png` (see-through holes) | *Materials* page › **Import ...** › **Use a sample ...** › `terracotta_tiles.png`, **Import**; the normal map comes along. The grate becomes a cut-out (`$alphatest`). Put them on a floor and a window. |
| `models` | `crate/crate.obj` (+ `.mtl`, `.png`), `hanging_sign/hanging_sign.smd` (+ `.png`, two bones) | *Models* page › **Import ...** › **Use a sample ...** › `crate.obj`: **Units** metres, **Y is up** on, **Kind** physics prop, **Import** (needs the SDK's `studiomdl`). The sign: `hanging_sign.smd`, **Units** inches, **Kind** dynamic prop - it keeps its two bones, so it can be animated. |
| `animations` | `hanging_sign_swing.smd`, `hanging_sign_spin.smd`, `hl2_human_nod.smd`, `hl2_human_wave.smd` | *Animations* page: pick the imported `hanging_sign` model › **Use a sample ...** › `hanging_sign_swing.smd`, **Add and preview** - it swings at once; **Build into the mod** makes it part of the game. The `hl2_human` files work on Half-Life 2's people, e.g. `models/humans/group01/male_07.mdl`. |
| `particles` | `mapsmith_samples.pcf`: `mapsmith_sample_sparks`, `mapsmith_sample_smoke` | *Particles* page › **Import ...** › **Use a sample ...** › `mapsmith_samples.pcf`, **Import**; the two systems appear in the list. Drag one into the map (an `info_particle_system`) and run the map. |

The `hl2_human` animations move only a few bones and say so in their header (`// mapsmith: relative`): their values
are added to the rest pose of the model you put them on, and every other bone keeps its rest pose. How exactly the
arm moves depends on the model - they are there to try the importer, not as finished animations.

## Updates

At every start Mapsmith asks GitHub whether a newer version is out. If there is one, it offers **Update now**,
**Later** or **Skip this version**.

- *Update now* downloads the package, checks its SHA-256 checksum, installs it and restarts Mapsmith.
- The version is shown at the bottom right of the window. When an update is out, it turns into an **Update to …**
  button.
- **Help > Check for updates** checks by hand. **Help > Check for updates at start** turns the automatic check off.

Nothing else is ever sent anywhere: no data about you, your PC or your maps.

## Building from source

You need CMake 3.25 or newer, a C++20 compiler (Visual Studio 2022, GCC 13 or Clang 16) and Qt 6.4 or newer
(Widgets and OpenGL; Multimedia is optional and is needed for playing sounds).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release -LE ui
```

The program is `build/src/app/Mapsmith`.
