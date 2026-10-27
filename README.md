# Mapsmith

A modern, 3D-first map editor for **Source 1** games — Half-Life 2, Mapbase mods, Black Mesa and other Source games —
that works like **Source 2 Hammer** but saves ordinary `.vmf` files for the normal compile tools (VBSP, VVIS, VRAD).

It started as an experiment: how far can an AI-built editor go? This is the first usable iteration. Its ideas are
inspired by Source 2 Hammer, Hammer++ and XBLAH's Modding Tool; the code, icons and design are its own.

![3D view with lighting preview, NPC selected](docs/images/npc.png)

## What it can do

- **Finds your games and mods by itself**: every Source 1 game in your Steam libraries and every mod in
  `steamapps/sourcemods` (Mapbase mods included), shown with clear names and grouped by game, with a search field.
  FGDs and compile tools are taken from your Hammer/Hammer++ setup; Mapbase mods also get Mapbase's FGD, so entities
  like `env_global_light` are there. Folders that are only content of another game (like `hl2` inside Entropy : Zero 2)
  are marked "base content" and listed last.
- **3D view first**, like Source 2 Hammer: look around with the right mouse button and fly with W A S D (also while
  placing a block), orbit with Alt, gizmos to move/rotate/scale, Shift+drag to copy. Optional 2D views (top/front/side).
  The menu in the top left corner of each view switches between 3D, top, front and side, and between lighting preview,
  textured and wireframe. Its **Show** section (also View > Show) hides kinds of objects while you map: world brushes,
  brush entities, triggers, tool brushes and faces (clip, nodraw, hint …), sky brushes, models, NPCs, lights, cubemaps
  and other entities. The view's button says how many kinds are hidden.
- **Snap to corners**: the move tool shows small squares on the corners of the selection; drag one onto a corner of
  another brush or model and they line up exactly.
- **Resize right away**: selected brushes show squares on their sides in the select tool too; drag one to make the
  brush longer, shorter or taller without switching to the scale tool.
- **Sky**: faces with the skybox tool texture show the map's sky (worldspawn *skyname*) as the game does.
- **Layout**: on the far left the side bar with the pages, next to it the tools (their key in the corner). Left of the
  views the *Tool* panel (options and keys of the current tool; with the Select tool also *Hollow, Carve, Merge, Tie to,
  Flip, Snap to grid, Drop to floor, Select similar, Hide*), the *Outliner*, *VisGroups* and *Map Look*
  as tabs. Right only *Object
  Properties*. Below: Asset Browser, Sounds, Particles, Compile, Problems, Log and History (every undo step; click one to go back
  or forward to it). The three buttons at the top right show
  or hide the left, bottom and right panels.
- **View switches** in the tool bar: helper entities, models, lights, cubemaps, triggers, tool brushes, sky, entity
  names in the 2D views, connection lines (green to what the selected entity's outputs fire at, blue from what fires at
  it) and the 3D grid; the eye button lists every kind of object.
- **Map Look (mapbase-fidelity)**: the *Map Look* panel (a tab next to the Tool panel) sets what the mod's graphics add-on shows
  in this map: drifting clouds, light shafts, colour grading (exposure, contrast, saturation, temperature, vignette …)
  and water glint and foam. The lighting preview shows the clouds and the grading at once (view menu: *mapbase-fidelity
  look*; also for mods without the add-on, to try it). The map sets the values when it loads through a `logic_auto`
  and a `point_clientcommand` the editor keeps itself; values you leave alone are the mod's own (`cfg/fidelity.cfg`).
  With the same switch, walls get the **relief** the add-on's `deploy.bat` would give them (bump-mapped walls, and
  bricks, stones and tiles without a bump map; wood stays flat) – real depth in the preview without deploying first.
- **Lighting preview with real-time shadows** from your light entities (light, light_spot, light_environment), using
  VRAD's own falloff formulas: the sun and the nearest spot and point lights cast shadows (F7 turns them off).
  Entities show their editor icons (the sprites and models Hammer uses).
- **Models**: props and NPCs are drawn with their real model and textures. The model browser shows a preview of every
  model; drag one into the 3D view to place it as a prop_static.
- **Shapes**: the block tool makes blocks, wedges, cylinders, spikes (pyramids, cones), spheres, arches (rings and
  spirals) and stairs. Change the settings in the **Tool** panel right after creating a shape and it is rebuilt.
  Click or press Enter to create a block and simply drag on for the next one.
- **Brush tools** (right-click menu or the **Brushes** menu): hollow (wall thickness, walls inside or outside), carve,
  merge into one brush, round or bevel the edge between two faces, drop to floor, select a whole path_track chain,
  flip (Ctrl+L left/right, Ctrl+I up/down), snap to grid (Ctrl+B), *Transform …* with exact numbers (Ctrl+Alt+M).
- **Select similar** (Ctrl+Alt+O: same class, same model, or brushes with the same material) and **Go to ID**
  (Ctrl+Shift+G, for ids from compile messages).
- **Keys at hand**: the Tool panel lists the keys of the current tool and what they do (with your own shortcuts).
- **Clip, vertex and polygon tools**: cut brushes with a line (2D view) or three points (3D view); drag corners and edge
  middles (brushes always stay convex); draw a brush from the corners of its base.
- **Edges** (like Source 2): with the vertex tool click the middle of an edge to pick it (Ctrl+click for more), then
  **Wedge** (a block becomes a ramp), **Bevel** (one slanted face, *Size* in from the edge) or **Round** (radius *Size*,
  2–32 segments) in the Tool panel's *Edge* box or the right-click menu. All picked edges change in one undo step.
- **VisGroups**: the *VisGroups* panel lists the map's groups (nested, each with its colour). Select objects and press
  **New from selection**; **Add selection** / **Remove selection** change a group, double-click (or **Select members**)
  selects what is in it, F2 or **Rename** renames it, **Color ...** and **Delete** (members stay) too - every change is
  one undo step. The tick shows or hides a group's members; hidden groups are saved as hidden in the .vmf (like
  Hammer) and come back hidden when the map is opened.
- **Instances** (`func_instance`, Portal 2 / CS:GO / Mapbase): the instance's brushes and entities are drawn in place,
  read-only with cyan edges, moved and turned with the func_instance; clicking them selects the func_instance. Its
  properties show the .vmf (**Browse ...**, found next to the map or in the game's map folders), the `$variables` it
  fills in (a table of replace01, replace02, ...) and the parameters the instance offers. **Read file again** after
  editing the instance; **Collapse instance** puts its content into the map, with names fixed up like VBSP does
  (`instance-name` prefix or postfix, `@name` and `!name` stay global).
- **Entity tool**: the Tool panel on the left lists every point and brush entity with its icon (search with words like
  "global light", A–Z or in groups). The class you pick is *armed*: the next click places it on the surface under the
  cursor (a preview of it follows the mouse); a brush entity (trigger, door, …) is built from the next box you drag.
  **Random yaw** turns every new prop differently.
- **Smart properties for every entity**: the class at the top (pick another class to change it, the keys stay), then
  the tabs **Keyvalues**, **Outputs** and **Inputs** with their counts. Keys are grouped by where the FGD defines them
  (Targetname, Angles, the entity's own class, Flags, Placement); click a group's title to fold it away. Each key has
  a fitting editor (lists, yes/no and flags as switches, colors, targets, models, animations, sounds, particle
  systems); what the key you are in does is shown at the bottom of the panel. *Find a property or flag* filters
  keys and flags. Outputs are edited as readable cards ("When OnTrigger tell door_01 to Open") with drop-downs for the
  target's inputs; Inputs lists the entities that send something to this one, with *Go to*. Every entity has a Name
  field. Turn **Smart Edit** off to see every key as plain text, like Hammer; add any key with the
  *new key | value | + Add* row.
- **Sky and map settings**: with nothing selected (or **Map > Map properties**) the panel shows the world's keys; the
  sky is picked from the game's skies, each with a small picture. A sky from Valve's games brings its matching sun and
  ambient light (as in the maps that use it): *light_environment* (added above the player start if the map has none),
  *env_sun*, *shadow_control* and *env_global_light* take it over in one undo step; change them afterwards as you like.
- **Copy and paste objects**: Ctrl+C / Ctrl+X copies or cuts the selected brushes and entities, Ctrl+V pastes them
  under the mouse (resting on the surface). **Paste special** (Ctrl+Shift+V) makes several copies at once, each moved
  and turned a step further, and gives entity names a number (door → door_2) so the copies' outputs stay their own.
  Copied objects are VMF text, so they also paste into another map.
- **Copy and paste names**: Ctrl+C on a material, model, sound, particle or entity class copies its name; Ctrl+V puts it into
  any field. Lines like `"_light" "255 255 255 200"` or `skyname<Tab>sky_day01_01` (from a wiki or another map) pasted
  into the properties set those keys at once.
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
  and a **Particles** tab (drag a particle system in for an info_particle_system).
- **Particles**: pick a system to see it move - fire, smoke, sparks with their sprites, child systems included - and
  turn around it (drag; right drag moves up and down; the wheel zooms), pause, restart, slow down or speed up. Next to it
  its parts as cards: emitters, initializers, operators, renderers and children with all their values. The preview runs
  the common parts (emitting, lifetime, size, color, alpha, movement with gravity and drag, fading, scaling, rotation); it
  is close to the game, not exact.
- **Sounds**: click a sound to hear it (WAV including ADPCM, MP3, OGG) with its waveform and loop point; click into the
  waveform to play from there. Drag sounds into the map for an ambient_generic or env_soundscape. Edit sound script
  entries (channel, volume, pitch, sound level, files) and **Save to the mod**: they go into
  `scripts/game_sounds_<mod>.txt`, which is added to the mod's sound manifest.
- **Soundscapes**: click a soundscape (or **+ Soundscape** for a new one) to edit it: name, room effect (DSP, with
  names like *Concrete, small*), fade-in, and its sounds as cards - a **looping** sound, **random** sounds every few
  seconds, or another **soundscape** inside it - each with volume, pitch, sound level and position. **▶** on a card
  plays its sound; type a file (it completes) or take the one shown in the player. **Save to the mod** writes
  `scripts/soundscapes_<mod>.txt` and puts it first in the mod's `soundscapes_manifest.txt`, so a changed copy of a
  game soundscape is used instead of the original. Keys the editor does not know stay in the file.
- **Side bar**: on the far left *Map*, *Materials*, *Sounds*, *Particles* and *Project* each fill
  the window. *Sounds* and *Particles* are also docks under the map while you build (drag them into the map). *Project*
  has the tabs *Overview*, *Maps*, *Menu*, *Source control* and *Ship* (below). *Overview* shows the chosen game or
  mod: its folder, the maps in its map folder (double-click opens one), the search paths of `gameinfo.txt` with what
  was found, and the entity definitions (FGDs); **New mod ...** is there and in the File menu.
- **Maps** (Project page): every map of the game or mod in one list, with when it was changed, whether its BSP is *up to date*,
  *outdated* (the map changed since) or *not compiled*, and what the last compile said. **New map ...**, **Duplicate**,
  **Rename ...** and **Delete** (to the trash) work on the files. Tick maps and press **Compile checked**, or **Compile
  all outdated**: they are compiled one after another with your compile settings (no game in between); with **Run the
  game afterwards** the game starts with the last one that compiled. The Compile panel shows each step; **Cancel**
  stops the queue.
- **New mod ...**: turns your maps into a mod of your own in Steam's `sourcemods` folder - give it a title, a folder
  name and what it is built on (Half-Life 2 singleplayer, with Mapbase when Mapbase's shared content is installed, or
  multiplayer), tick the maps to take along. The editor writes `gameinfo.txt`, the usual folders,
  `cfg/autoexec.cfg` and `resource/<mod>_english.txt` (the title), copies the maps into `mapsrc` and switches to the
  new mod. Restart Steam once to see it in the library; a Mapbase mod also needs Mapbase's `bin` files.
- **Menu** (your mod's main menu): the name, menu title and second line, logo switch, window icon and developer details
  from `gameinfo.txt`; the chapters of *New Game* as cards (map, title, background map; add, remove, reorder); and the
  menu entries (text or `#token`, command such as *OpenNewGameDialog* or `engine map mymap`, *Only in game*, *Not in
  multiplayer*; add, remove, reorder). A preview on the right shows the menu over the background map's screenshot
  (*As shown during a game* adds Resume and Save). *Save to the mod* writes `gameinfo.txt` (only these keys change),
  `cfg/chapterN.cfg`, `resource/<mod>_english.txt` (chapter titles, UTF-16), `scripts/ChapterBackgrounds.txt` and
  `resource/GameMenu.res`; each file it replaces is kept once as `.bak`.
- **Source control** (Git, e.g. your mod's code on GitHub): pick the repository once per game (the mod folder or the
  mod's source code). You see the branch, how many commits are waiting to be pushed or pulled, and the changed files
  with check boxes. Write a message and *Commit* the checked files. *Fetch*, *Pull* and *Push* talk to the remote,
  and only when you press them. *Open on GitHub* shows the repository's page; the history lists the last 30 commits.
  *Open in Visual Studio* appears when the folder has a solution; *Run update.bat* appears when it has that script
  (Windows). Needs Git installed.
- **Material editor**: the *Materials* page (right-click a material > *Edit material*, *Edit* next to a selected
  face, or *View > Material Editor*). On the left: *Open a material ...* and the materials you edited lately. In the
  middle the parameters in groups; a group with a switch in its header is a feature (*Normal map*, *Relief*,
  *Reflections*, *Detail texture*, *Shiny highlights*, ...) and folds away while it is off - turning it off and on again
  brings its values back. Texture fields show a small picture of the texture. On the right a big live preview (wall,
  cube or sphere under sun, lamp, night or no light) with *Save* and *Save as new ...* above it. Saving writes the VMT
  into your mod (a material from the game's VPKs becomes an override copy). Only the groups that fit the kind of
  material are shown (wall, blended ground, model, water). While the page is shown, the map's panels and keys step aside;
  they come back with the *Map* page.
- **mapbase-fidelity shaders**: in the material editor, also before the mod has them (the preview shows them; the game
  after the mod's `deploy.bat` ran once)
  - *Relief*: *Give this surface real depth* turns a wall or blended ground into `Fidelity_Parallax` /
    `Fidelity_WorldVertexTransition`. Set the depth, the darkening in the depths and the fade distance. The height map
    is made for you from the normal map (or from the texture's brightness); tick *Upside down* to swap high and low, or
    *Paint zones ...* to mark places that stay flat (blue) or are turned upside down (orange). Saving writes
    `<material>_relief.vtf` next to the material.
  - *Water*: *Sun glint and foam at the shore* makes it `Water_Fidelity`, with sliders for glint and foam.
  - *Liquid inside*: for glass props (`Fidelity_Liquid`): liquid and glass colour, fill level (fixed or different in
    every prop), how it sways and how thick it is, splashing and pouring out.
  A material made by `deploy.bat` is marked as such; saving it makes it the mod's own, so `deploy.bat` leaves it alone.
- **Parallax-corrected cubemaps (Mapbase)**: select an env_cubemap and press *Add room box*: the box is fitted to the
  walls around it; drag the squares on its sides in the view to change it. The editor creates and names the
  parallax_obb and links it to the cubemap.
- **Soundscape areas**: select an env_soundscape and its *Soundscape area* box shows which soundscape plays (*Edit
  soundscape* opens it on the Sounds page) and where it is heard. *Sphere*: a pink sphere in the views; drag its squares
  or use the radius field and slider; *Whole map* sets radius -1. *Block*: puts a trigger_soundscape around the entity
  (the entity becomes an env_soundscape_triggerable with a name the trigger points at); type its width, depth and
  height or *Select the block* and drag its sides. Switching back to *Sphere* removes the block. Each change is one undo
  step.
- **Problem check with autofix**: broken brushes, misspelled materials or entity classes ("did you mean …?"), outputs
  to entities that do not exist, unknown inputs, missing player start, zero texture scale, … Each problem has a
  **Select** button and, where the fix is obvious, a fix button.
- **Compile & run** (F9): VBSP → VVIS → VRAD → copy to the game → cubemaps → start the game. The compile dialog has two
  views, and both change the same settings, which are kept per game:
  - **Simple**: one card per step. Geometry (detail brushes, water), Visibility (Fast / Full), Lighting (LDR / HDR /
    Both, Fast / Normal / Final), Cubemaps (the game builds them and closes again; LDR / HDR / Both), After compiling
    (copy to the game's maps folder, run the game) and the Game window. The game starts with the console, in a window,
    at 1920 × 1080 and without the intro video by default, and you can change each of these. The presets Fast, Normal
    and Final set the quality in one go.
  - **Advanced**: switch each step on or off and add any parameter the tool knows (searchable, with a short
    explanation). You can also add your own parameter.

  The command lines are shown at the bottom (with *Copy*). The **Compile** panel shows every step with time and progress, the warnings and errors with a plain explanation, and a
  summary. A leak is shown as a red line in the 3D view. **Cordons** compile only a part of the map.
- **Ship**: get a map or the whole mod ready for players.
  - *Check what the map needs* lists every file the open map uses (materials with their textures, the sky, models with
    their files and materials, sounds, soundscapes, particle files), grouped by where it comes from: **your mod** (players
    do not have it), **other mod folders**, the **base game** (players have it) and **missing**.
  - **Pack into BSP** puts your mod's files into the compiled map with `bspzip` (next to the compile tools); what VBSP
    packed already stays.
  - **Make share ZIP** writes `maps/<name>.bsp` with your mod's files next to it, plus an optional readme.
  - **Make mod ZIP** packs the whole mod folder without map sources, logs, compile leftovers, screenshots, saved games and
    your own `config.cfg` (*Check mod folder* shows what stays out).
  - **Steam Workshop** (advanced, folded away): builds the content folder from the share package and writes the item file
    for Valve's `steamcmd`; *Run steamcmd* starts it, or copy the command. steamcmd asks for your Steam login itself –
    the editor never sees or stores a password.
- **Export** the map as glTF or OBJ (meters, Y up, textures as PNG, props included) for Blender, Unity or Unreal.
- **Status bar**: what is selected, its size (width, length, height) and center, where the mouse points in the 3D
  view and how far away that is, grid, snap, rotation step, texture lock, and how long a frame takes (hover it for
  triangles, draw calls and lights).
- **Your own shortcuts**: Help > Change shortcuts.
- Opening and saving never loses data: everything the editor does not understand stays in the file as it was.
- **Autosave and crash recovery**: every 5 minutes (setting) a changed map is copied to `autosave/` next to it
  (`<map>.<date-time>.vmf`, the newest 10 stay; a map never saved goes to the editor's own data folder). If the editor
  did not close normally, the next start offers the newest autosave that is younger than the map; opening a map with a
  newer autosave asks too. A restored map counts as unsaved until you save it. On every save the previous version stays
  as `<map>.vmx`.
- **Undo history**: every change in a list; click a line to go back (or forward) to that point. Undone steps stay
  listed in grey until you do something new.
- **Find entities** while you type: by class, name, any `key=value` (with `*` and `?`), output target, model or sound
  - or all at once: `class:prop_* name:crate*`, `target:relay_*`, `rendercolor=255*`. Click a result to select it,
  **Go to** frames it, **Select all** takes every result. The **Entity report** tab lists every entity of the map
  (class, name, origin, outputs) to sort and filter.
- **Replace materials**: one material for another on the whole map or the selection (in face mode: the selected
  faces), exact or with wildcards (`concrete/*`), keeping the alignment or aligning the new texture to the world, also
  in overlays and decals. It shows how many faces will change before you press Replace; one undo step.

![Problem list with autofix](docs/images/problems.png)

## Getting started (Windows)

1. Download `Mapsmith-Setup-<version>.exe` from the newest
   [release](https://github.com/29j2003/hammer-test/releases/latest) and run it. It installs for your Windows user
   (no administrator rights needed), adds a Start menu entry and, if you like, a desktop icon; uninstall it in the
   Windows settings like any program. (A portable `Mapsmith-<version>-windows.zip` is there too: unzip it into a folder
   you can write to and start `Mapsmith.exe`.)
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
| Ctrl+C / Ctrl+V | Copy a name / paste into a field or keys into the properties | Enter (block tool) | Create the block |
| Ctrl+F | Find entities (class, name, key=value, target, model, sound) | Ctrl+Alt+R | Replace materials |
| Tab, Enter (clip tool) | Which part stays, cut | Enter, Backspace (polygon tool) | Make the brush, remove a corner |

Press **F1** in the editor for the full list.

## Building from source

Requirements: CMake 3.25+, a C++20 compiler (Visual Studio 2022, GCC 13, Clang 16), Qt 6.4+ (Widgets, OpenGL;
Multimedia for playing sounds, optional). stb_vorbis and minimp3 are downloaded by CMake (pinned to a commit and
checked by hash).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release -LE ui
```

`build/tests/sme-make-fixture demo` creates a fake Steam folder with a demo mod and map for trying the editor without
any game installed:
`Mapsmith --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`

## Status

Early version. Tested with generated test data and automated UI tests on Linux, and built on Windows by the CI; it still
needs testing with real games. Not there yet: editing displacements with the vertex tool, a mesh mode, particles
in the 3D view, and a full physics simulation (Drop to floor only lets objects fall straight down).

## License

The editor's code is released into the public domain ([Unlicense](LICENSE)). Third-party parts (sourcepp, Qt) keep
their licenses, see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) (also stb_vorbis and minimp3 for sounds). Half-Life 2, Source, Hammer and Steam are
trademarks of Valve; this project is not affiliated with Valve and ships no game files.
