---
title: Home
nav_order: 1
---

# Mapsmith

Mapsmith is a map editor for **Source 1** games and mods — Half-Life 2 and its episodes, Mapbase mods, Black Mesa and
other Source games. You build in a 3D view with a lighting preview, and it saves ordinary `.vmf` files that the normal
compile tools (VBSP, VVIS, VRAD) turn into playable maps.

Mapsmith is more than a map editor. Around the map it covers the rest of a mod: materials, sounds and soundscapes,
particles, the mod's main menu, all of its maps with batch compiling, source control with Git, and packing everything
up to share.

![The Mapsmith window with the lighting preview](manual/images/overview.png)

## Where to start

| If you want to ... | read |
|---|---|
| install Mapsmith and open your first map | [Getting started](manual/getting-started.html) |
| find your way around the window | [The interface](manual/interface.html) |
| build rooms, stairs, ramps and details | [Building](manual/building.html) |
| put textures on walls and edit materials | [Texturing and materials](manual/texturing.html) |
| place lights, doors, NPCs and logic | [Entities](manual/entities.html) |
| add ambient sound and look at particle effects | [Sounds, soundscapes and particles](manual/sounds-particles.html) |
| play your map | [Compiling and running](manual/compiling.html) |
| manage a whole mod | [The Project page](manual/project.html) |
| never lose work and find things fast | [Safety and search](manual/safety-search.html) |
| look up a key | [Keyboard shortcuts](manual/shortcuts.html) |
| learn how Source maps work | [Source mapping basics](manual/mapping-basics.html) |
| use the mapbase-fidelity graphics add-on | [mapbase-fidelity](manual/mapbase-fidelity.html) |
| fix a problem | [Troubleshooting and FAQ](manual/troubleshooting.html) |

## If you have made Source maps before

Everything you know about Source maps still applies: brushes, entities, FGDs, `func_detail`, outputs and inputs,
VBSP/VVIS/VRAD. What may be new:

- **The 3D view comes first.** Hold the right mouse button and fly with W A S D. The 2D views are one key away
  (Shift+Alt+Z) but you rarely need them.
- **Tools with gizmos:** Q select, T move, R rotate, E scale, right in the 3D view, and Shift+drag makes a copy.
- **No game configuration.** Games, mods, FGDs and compile tools are found by themselves.
- **The lighting preview uses VRAD's own light formulas** and shows real-time shadows, so you see roughly what the
  compile will bake.
- **Nothing is lost.** Keys and blocks the editor does not know stay in the `.vmf` exactly as they were. Autosave and
  `.vmx` backups protect against crashes.

## Getting Mapsmith

Download `Mapsmith-Setup-<version>.exe` from the
[newest release](https://github.com/29j2003/hammer-test/releases/latest) and run it. See
[Getting started](manual/getting-started.html) for details.

Mapsmith is free and public domain. It is not affiliated with Valve and ships no game files.
