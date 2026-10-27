# Source Map Editor (hammer-test)

A modern, 3D-first map editor for **Source 1** games — Half-Life 2, Mapbase mods, Black Mesa and other Source games —
that works like **Source 2 Hammer** but saves ordinary `.vmf` files for the normal compile tools (VBSP, VVIS, VRAD).

It started as an experiment: how far can an AI-built editor go? This is the first usable iteration.

![3D view with lighting preview, NPC selected](docs/images/npc.png)

## What it can do

- **Finds your games and mods by itself**: every Source 1 game in your Steam libraries and every mod in
  `steamapps/sourcemods` (Mapbase mods included), shown with clear names and grouped by game, with a search field.
  FGDs and compile tools are taken from your Hammer/Hammer++ setup; Mapbase mods also get Mapbase's FGD, so entities
  like `env_global_light` are there.
- **3D view first**, like Source 2 Hammer: look around with the right mouse button and fly with W A S D (also while
  placing a block), orbit with Alt, gizmos to move/rotate/scale, Shift+drag to copy. Optional 2D views (top/front/side).
- **Lighting preview with real-time shadows** from your light entities (light, light_spot, light_environment), using
  VRAD's own falloff formulas: the sun and the nearest spot and point lights cast shadows (F7 turns them off).
- **Models**: props and NPCs are drawn with their real model and textures. The model browser shows a preview of every
  model; drag one into the 3D view to place it as a prop_static.
- **Shapes**: the block tool makes blocks, wedges, cylinders, spikes (pyramids, cones), spheres, arches (rings and
  spirals) and stairs. Change the settings in the **Tool** panel right after creating a shape and it is rebuilt.
- **Brush tools** (right-click menu or the **Brushes** menu): hollow (wall thickness, walls inside or outside), carve,
  merge into one brush, round or bevel the edge between two faces, drop to floor, select a whole path_track chain.
- **Clip, vertex and polygon tools**: cut brushes with a line (2D view) or three points (3D view); drag corners and edge
  middles (brushes always stay convex); draw a brush from the corners of its base.
- **Entity tool**: choosing it opens the entity list in the Tool panel (search, groups, descriptions, recently used);
  click on a surface and the entity sits on it.
- **Smart properties for every entity**: each key has a fitting editor (lists, flags, colors, targets, models,
  animations, sounds, particle systems) and a short description of what it does. Outputs are edited as readable cards
  ("When OnTrigger tell door_01 to Open") with drop-downs for the target's inputs.
- **NPC helper**: pick an animation from the NPC's model and pose the NPC in it (scripted_sequence) or turn it into a
  static prop_dynamic — in one click.
- **Fast texturing**: hold Ctrl + right mouse and paint the current material over faces; double-click a face to select
  the whole wall; Shift + right click picks a material from a face; drag materials onto faces. Justify (fit, left, right,
  top, bottom, center) with **Treat as one face**: one texture spans all selected faces, seamless where they meet.
  Scaling keeps the texture size (no stretching).
- **Rotate anything** — brushes, brush entities, props and NPCs — with the rotate gizmo or Alt + arrow keys. Rotation
  snaps to a step you choose (1° to 90°) just like moving snaps to the grid (down to 0.125 units); Shift+W turns snapping off and on.
- **Browsers**: materials filtered by kind (brush, blend/displacement, model, tool, see-through, water, decals, skybox,
  used in this map), models with previews, a **Sounds** tab with every sound script entry, sound file and soundscape
  (play them, drag them into the map for an ambient_generic or env_soundscape) and a **Particles** tab (drag a particle
  system in for an info_particle_system).
- **Problem check with autofix**: broken brushes, misspelled materials or entity classes ("did you mean …?"), outputs
  to entities that do not exist, unknown inputs, missing player start, zero texture scale, … Each problem has a
  **Select** button and, where the fix is obvious, a fix button.
- **Compile & run** (F9): VBSP → VVIS → VRAD → copy to the game → start the game, with Fast/Normal/Final presets. The
  **Compile** panel shows every step with time and progress, the warnings and errors with a plain explanation, and a
  summary. A leak is shown as a red line in the 3D view. **Cordons** compile only a part of the map.
- **Export** the map as glTF or OBJ (meters, Y up, textures as PNG, props included) for Blender, Unity or Unreal.
- **Your own shortcuts**: Help > Change shortcuts.
- Opening and saving never loses data: everything the editor does not understand stays in the file as it was.

![Problem list with autofix](docs/images/problems.png)

## Getting started (Windows)

1. Download `SourceMapEditor-<version>-windows.zip` from the newest
   [release](https://github.com/29j2003/hammer-test/releases/latest).
2. Unzip it into a folder you can write to (e.g. `Documents\SourceMapEditor`, not `Program Files`) and start
   `SourceMapEditor.exe`.
3. Choose your game or mod in the **Game** list at the top. Check the **⚙** game settings once: FGDs, VBSP/VVIS/VRAD
   and the game executable are filled in from Hammer++/Hammer when available.

**Mapbase:** the editor adds Mapbase's FGD from `sourcemods/mapbase_shared` by itself; the log warns if it cannot find
one. Use the Mapbase compile tools.
**Black Mesa:** its FGDs (`bms.fgd`) and tools are in `Black Mesa/bin`.

### Updates

At start the editor asks GitHub whether a newer version is out and offers **Update now / Later / Skip this version**.
*Update now* downloads the package, checks its SHA-256 checksum and restarts the editor in the new version. The version
is shown at the bottom right; when an update is out it turns into an **Update to …** button. *Help > Check for updates*
checks by hand, *Help > Check for updates at start* turns the automatic check off. Nothing else is ever sent: no data
about you or your maps.

### Help us test: the setup report

`sme-probe.exe report --out report.json` writes what the editor finds on your PC (games, mods, search paths, FGDs,
Hammer configs). Your user name and home folder are replaced by placeholders. `sme-probe.exe vmf yourmap.vmf` checks that
one of your maps loads and saves without changes, and `sme-probe.exe model GAME models/props_c17/bench01a.mdl` shows
whether a model loads.

## Controls

| Key | Action | Key | Action |
|---|---|---|---|
| Right mouse + W A S D, Q/E | Look around and fly | Z | Fly mode on/off |
| Alt + left mouse | Orbit | Middle mouse | Pan |
| F | Frame selection | Shift+Alt+Z | One view / four views |
| Q / T / R / E | Select / Move / Rotate / Scale | Shift+B / Shift+E | Block (shapes) / Entity tool |
| Shift+X / Shift+V / Shift+P | Clip / Vertex / Polygon tool | Right click | Menu for the clicked object |
| 3 / 5 | Face mode / Object mode | Shift+T | Apply current material |
| Ctrl + right mouse drag | Paint material on faces | Double-click (face mode) | Select the whole wall |
| Shift + right click, Ctrl+M | Pick material | [ / ] | Grid smaller / bigger |
| Alt + arrows | Rotate by the rotation snap | Ctrl+[ / Ctrl+] | Rotation snap smaller / bigger |
| Shift+W | Snapping on/off (grid and rotation) | Arrows, PgUp/PgDn | Nudge by the grid |
| H / Ctrl+H / U | Hide / hide others / show all | Ctrl+T / Ctrl+Shift+W | Tie to entity / move to world |
| Ctrl+Shift+H / C / M / R | Hollow / carve / merge / round edge | Ctrl+Shift+F | Drop to floor |
| F5 / F6 / F8, F7 | Unlit / lighting preview / wireframe, shadows | F9 | Compile & run |
| Tab, Enter (clip tool) | Which part stays, cut | Enter, Backspace (polygon tool) | Make the brush, remove a corner |

Press **F1** in the editor for the full list.

## Building from source

Requirements: CMake 3.25+, a C++20 compiler (Visual Studio 2022, GCC 13, Clang 16), Qt 6.4+ (Widgets, OpenGL;
Multimedia for playing sounds, optional).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release -LE ui
```

`build/tests/sme-make-fixture demo` creates a fake Steam folder with a demo mod and map for trying the editor without
any game installed:
`SourceMapEditor --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`

## Status

Early version. Tested with generated test data and automated UI tests on Linux, and built on Windows by the CI; it still
needs testing with real games. Not there yet: editing displacements with the vertex tool, a mesh mode, particle previews
in the 3D view, and a full physics simulation (Drop to floor only lets objects fall straight down).

## License

The editor's code is released into the public domain ([Unlicense](LICENSE)). Third-party parts (sourcepp, Qt) keep
their licenses, see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). Half-Life 2, Source, Hammer and Steam are
trademarks of Valve; this project is not affiliated with Valve and ships no game files.
