# Source Map Editor (hammer-test)

A modern, 3D-first map editor for **Source 1** games — Half-Life 2, Mapbase mods, Black Mesa and other Source games —
that works like **Source 2 Hammer** but saves ordinary `.vmf` files for the normal compile tools (VBSP, VVIS, VRAD).

It started as an experiment: how far can an AI-built editor go? This is the first usable iteration.

![3D view with lighting preview, NPC selected](docs/images/npc.png)

## What it can do

- **Finds your games and mods by itself**: every Source 1 game in your Steam libraries and every mod in
  `steamapps/sourcemods` (Mapbase mods included). FGDs and compile tools are taken from your Hammer/Hammer++ setup.
- **3D view first**, like Source 2 Hammer: fly with the right mouse button and W A S D, orbit with Alt, gizmos to
  move/rotate/scale, Shift+drag to copy. Optional 2D views (top/front/side).
- **Lighting preview** from your light entities (light, light_spot, light_environment), using VRAD's own falloff formulas.
- **Block tool** (drag the base, then the height) and **entity tool** (click on a surface — the entity sits on it).
- **Smart properties for every entity**: each key has a fitting editor (lists, flags, colors, targets, models,
  animations, sounds) and a short description of what it does. Outputs are edited as readable cards
  ("When OnTrigger tell door_01 to Open") with drop-downs for the target's inputs.
- **NPC helper**: pick an animation from the NPC's model and pose the NPC in it (scripted_sequence) or turn it into a
  static prop_dynamic — in one click.
- **Fast texturing**: hold Ctrl + right mouse and paint the current material over faces; double-click a face to select
  the whole wall; Shift + right click picks a material from a face; drag materials onto faces (the target face lights
  up before you drop); recently used materials stay at the top of the browser.
- **Rotate anything** — brushes, brush entities, props and NPCs — with the rotate gizmo or Alt + arrow keys. Rotation
  snaps to a step you choose (1° to 90°) just like moving snaps to the grid; Shift+W turns snapping off and on.
- **Asset browser** with thumbnails: materials filtered by kind (brush, blend/displacement, model, tool, see-through,
  water, decals, skybox, used in this map), models with their animations, sounds, entity classes. Drag a material onto
  a face to apply it.
- **Problem check with autofix**: broken brushes, misspelled materials or entity classes ("did you mean …?"), outputs
  to entities that do not exist, unknown inputs, missing player start, zero texture scale, … Each problem has a
  **Select** button and, where the fix is obvious, a fix button.
- **Leaks**: after a compile the leak is shown as a red line in the 3D view, and the problem list names the entity
  that can see the void.
- **Compile & run** (F9): VBSP → VVIS → VRAD → copy to the game → start the game, with Fast/Normal/Final presets.
- Opening and saving never loses data: everything the editor does not understand stays in the file as it was.

![Problem list with autofix](docs/images/problems.png)

## Getting started (Windows)

1. Download the newest `SourceMapEditor-windows` zip from the
   [Actions page](https://github.com/29j2003/hammer-test/actions) (choose the latest green run, then *Artifacts*).
2. Unzip it anywhere and start `SourceMapEditor.exe`.
3. Choose your game or mod in the **Game** list at the top. Check the **⚙** game settings once: FGDs, VBSP/VVIS/VRAD
   and the game executable are filled in from Hammer++/Hammer when available.

**Mapbase:** use Mapbase's FGD (`sourcemods/mapbase_shared/shared_misc/bin/halflife2.fgd`) and the Mapbase compile tools.
**Black Mesa:** its FGDs (`bms.fgd`) and tools are in `Black Mesa/bin`.

### Help us test: the setup report

`sme-probe.exe report --out report.json` writes what the editor finds on your PC (games, mods, search paths, FGDs,
Hammer configs). Your user name and home folder are replaced by placeholders. `sme-probe.exe vmf yourmap.vmf` checks that
one of your maps loads and saves without changes.

## Controls

| Key | Action | Key | Action |
|---|---|---|---|
| Right mouse + W A S D, Q/E | Fly | Z | Fly mode on/off |
| Alt + left mouse | Orbit | Middle mouse | Pan |
| F | Frame selection | Shift+Alt+Z | One view / four views |
| Q / T / R / E | Select / Move / Rotate / Scale | Shift+B / Shift+E | Block tool / Entity tool |
| 3 / 5 | Face mode / Object mode | Shift+T | Apply current material |
| Ctrl + right mouse drag | Paint material on faces | Double-click (face mode) | Select the whole wall |
| Shift + right click, Ctrl+M | Pick material | [ / ] | Grid smaller / bigger |
| Alt + arrows | Rotate by the rotation snap | Ctrl+[ / Ctrl+] | Rotation snap smaller / bigger |
| Shift+W | Snapping on/off (grid and rotation) | Arrows, PgUp/PgDn | Nudge by the grid |
| H / Ctrl+H / U | Hide / hide others / show all | Ctrl+T / Ctrl+Shift+W | Tie to entity / move to world |
| F5 / F6 / F8 | Unlit / lighting preview / wireframe | F9 | Compile & run |

Press **F1** in the editor for the full list.

## Building from source

Requirements: CMake 3.25+, a C++20 compiler (Visual Studio 2022, GCC 13, Clang 16), Qt 6.4+ (Widgets, OpenGL).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release -LE ui
```

`build/tests/sme-make-fixture demo` creates a fake Steam folder with a demo mod and map for trying the editor without
any game installed:
`SourceMapEditor --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`

## Status

First iteration. Tested with generated test data and automated UI tests on Linux; real games and Windows still need
testing. Models are drawn as boxes for now; vertex editing, a mesh mode, model rendering and sun shadows are next.

## License

The editor's code is released into the public domain ([Unlicense](LICENSE)). Third-party parts (sourcepp, Qt) keep
their licenses, see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). Half-Life 2, Source, Hammer and Steam are
trademarks of Valve; this project is not affiliated with Valve and ships no game files.
