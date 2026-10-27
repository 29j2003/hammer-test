---
title: Troubleshooting and FAQ
nav_order: 14
---

# Troubleshooting and FAQ
{: .no_toc }

1. TOC
{:toc}

## First look: the Log and the Overview

Two places answer most questions:

- the **Log** tab in the bottom panel: what Mapsmith found and did, including warnings about games, FGDs and files;
- the **Overview** tab of the [Project page](project.html#overview): the game's folder, every search path and whether
  it was found, and the loaded FGDs.

## Installing and starting

**Windows warns that the installer is from an unknown publisher.**
Mapsmith is not signed with a paid certificate, so Windows SmartScreen may show *Windows protected your PC* for a new
download. Choose *More info* and *Run anyway*. If you want to be sure the file is the real one, compare its SHA-256
checksum with the `.sha256` file on the release page.

**The window stays black or says "OpenGL 3.3 is required".**
The 3D views need OpenGL 3.3.

- Install the newest driver from the maker of your graphics card (NVIDIA, AMD, Intel), not the one Windows picked.
- On a laptop with two graphics chips, let Mapsmith use the fast one (Windows *Settings > System > Display >
  Graphics*).
- Remote desktop sessions and some virtual machines offer only an old OpenGL; run Mapsmith on the PC itself.

**The editor is slow.**
The status bar shows the frame time; hover over it for triangles, draw calls and lights. Switch off **Real-time
shadows** (F7) first, then try **Textured (unlit)** (F5). Hiding models or helpers with the
[view switches](interface.html#view-switches) also helps in very big maps.

## Games and content

**My game or mod is not in the list.**

- Mapsmith lists **Source 1** games and mods: folders with a `gameinfo.txt`. Source 2 games (Half-Life: Alyx,
  Counter-Strike 2) and GoldSrc games (Half-Life 1) are not Source 1.
- Mods must be in `steamapps/sourcemods` of a Steam library, or be a game Steam installed.
- Mapsmith finds Steam through the registry and reads every library from `steamapps/libraryfolders.vdf`. If Steam
  lives somewhere unusual, start Mapsmith with `--steam "D:\Path\To\Steam"`, or set the environment variable
  `SME_STEAM_ROOT` to that folder.
- A mod created outside Mapsmith while it runs appears after Mapsmith is restarted.

**Materials show as a pink and black checkerboard, or models are missing.**
The game cannot find those files either. Open **Project > Overview** and look at *Content*: a search path marked *no*
was not found. Check the mod's `gameinfo.txt` (**Edit gameinfo.txt**). Mapbase mods also need Mapbase's shared
content (`mapbase_shared` or similar) where their `gameinfo.txt` expects it.

**Entities are missing from the list, or show up as unknown in Problems.**
The FGD that defines them is not loaded. Open the [game settings](getting-started.html#game-settings) (⚙) and
**Add FGD ...**. Order matters: later files override earlier ones, so put the mod's own FGD last. For Mapbase mods
Mapsmith adds Mapbase's FGD by itself when it can find it; the Log says *Mapbase FGD added*.

**A folder in the list is marked *base content*.**
Some mods ship a copy of another game's content (for example an `hl2` folder). That folder is not a game of its own;
running a map with it fails. Pick the mod itself.

## Compiling and running

**"no program set" for VBSP, VVIS, VRAD or the game.**
Set the path in the [game settings](getting-started.html#game-settings). The compile tools are usually in the game's
`bin` folder (for SDK 2013 mods: in *Source SDK Base 2013 Singleplayer* or *Multiplayer*). For Mapbase mods use
Mapbase's compile tools.

**The compile stops at VBSP.**
Read the **Warnings and errors** list of the Compile tab: each message is explained there. Run **Check** first; the
[Problems](compiling.html#problems-and-leaks) tab finds broken brushes and missing materials and can often fix them.

**The map leaks.**
See [Leaks](compiling.html#leaks): follow the red line to the hole.

**The game starts, but not with my map, or says the map is missing.**

- **Copy to the game's maps folder** must be on in the compile dialog (it is by default).
- With cordons on, the game runs `<map>_cordon`.
- The game executable in the game settings must belong to the mod you compile for. Launch options such as `-game`
  are set by Mapsmith; extra ones go into *Launch options*.

**The map is completely dark, or completely bright.**
VRAD did not run: either the map leaks, VRAD is switched off in the compile dialog, or the map has no lights (Problems
shows a hint for that). A map open to the sky needs a `light_environment` and `tools/toolsskybox` on the sky faces.

**Shiny surfaces look wrong.**
Build the cubemaps; see [Cubemaps](compiling.html#cubemaps).

**The compile takes very long.**
Use the **Fast** preset while you work, turn small world brushes into `func_detail`, use a larger lightmap scale on big
surfaces, and compile only a part of the map with [cordons](building.html#cordons).

## Maps and files

**Mapsmith crashed. Is my work gone?**
Probably not. At the next start Mapsmith offers the newest autosave; see
[Autosave and recovery](safety-search.html#autosave-and-recovery). The previous saved version is also kept as
`<map>.vmx`.

**Will Mapsmith change or lose parts of my existing `.vmf` files?**
No. Keys, blocks and settings the editor does not know stay in the file as they were. Other editors can keep working
on the same files.

**Where are my settings?**
Mapsmith keeps its settings (window layout, compile choices per game, shortcuts) in the usual place of your system:
on Windows in the registry of your user account. Game settings are kept per game.

## Updates

**The update fails.**

- *The download is damaged (checksum does not match)*: nothing was changed. Try again later.
- *The editor's folder is not writable*: the portable version is in a folder you cannot write to (such as
  `Program Files`). Move it, or use the installer.
- *This release has no Windows package with a checksum*: download the new version from the release page by hand.

**I do not want update checks.**
Turn off **Help > Check for updates at start**. Mapsmith then never connects to the internet by itself.

## Reporting a problem

Mapsmith comes with a small command line tool, **`sme-probe`**, next to `Mapsmith.exe`. It shows what Mapsmith finds on
your PC:

```
sme-probe games                     list the Source 1 games and mods found
sme-probe report --out report.json  a report of Steam, games, mods and FGDs
sme-probe mounts <game>             the search order of a game
sme-probe find <game> <path>        which folder or VPK a file comes from
sme-probe vmf <map.vmf>             check a map: load, save, compare, find broken brushes
```

`<game>` is a key from `sme-probe games`, such as `220/hl2` or `sourcemods/mymod`. The report masks your user name
and home folder, so you can attach it to an issue on
[GitHub](https://github.com/29j2003/hammer-test/issues). Please also say what you did, what you expected, and the
version from the status bar.
