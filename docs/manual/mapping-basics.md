---
title: Source mapping basics
nav_order: 12
---

# Source mapping basics
{: .no_toc }

This page explains how a Source 1 map works, independent of the editor: what the compile tools do with your
brushes and entities, why maps leak, how visibility and lighting work, and how to keep a map fast. It applies to
Half-Life 2, its episodes, Source SDK 2013 mods, Mapbase mods and most other Source 1 games.

Where a number is only a common rule of thumb, or where games differ, the text says so.

1. TOC
{:toc}

## Units and sizes

Source measures in **units**. A unit is roughly an inch: the player is a little under two metres tall. Numbers for
Half-Life 2 and SDK 2013 mods:

| What | Size |
|---|---|
| Player box, standing | 32 × 32 × 72 units; eyes at 64 |
| Player box, crouching | 32 × 32 × 36 units; eyes at 28 |
| Highest step the player walks up | 18 units |
| Jump height | about 20 units, about 56 with a crouch jump |
| Steepest slope the player can walk | about 45° (steeper slopes make the player slide) |
| Usual door opening | 64 × 128, or 56 × 112 for a tight door |
| Usual corridor | 128 wide, 128 to 160 high |
| The whole map must fit in | −16384 to +16384 on every axis |

Other games change some of these (Counter-Strike and Portal use different player sizes), so check your game if it
matters.

**Grid.** Build on a power-of-two grid: 64, 32, 16, 8. Most textures are made for it: a 512 × 512 texture at the usual
scale of **0.25** covers exactly 128 × 128 units. Use smaller grid steps only for small details.

## Brushes

A **brush** is a convex solid: a block, a wedge, a cylinder, never an L-shape or a shape with a dent. Each face has a
material. The compile tools need brushes to be convex because each brush is stored as the space inside a set of
planes.

- Build a room from **separate brushes** for floor, ceiling and walls that touch at the edges. Do not let them overlap
  much: overlapping faces are cut away by VBSP, but they still cost compile time and can cause lighting seams.
- Keep corners on the grid. A corner a fraction of a unit off can leave a hairline gap, which is a leak.
- Very thin brushes (under one unit) and very sharp wedges cause errors and lighting problems.

## World brushes and brush entities

Every brush belongs either to the **world** or to a **brush entity**.

**World brushes** are the static shell of the map. Only they:

- **seal** the map from the void (see [Leaks](#leaks));
- **cut visleafs** (see below);
- block light during VRAD and show the baked light.

**Brush entities** are brushes with a behaviour. The common ones:

| Class | Use |
|---|---|
| `func_detail` | Static details: trims, pillars, stairs, furniture made of brushes. Merged into the world when compiling, but does **not** cut visleafs and does **not** seal. It is not an entity in the game. |
| `func_brush` | A brush that can be hidden, shown or made non-solid by outputs. |
| `func_door`, `func_door_rotating` | Sliding and swinging doors. |
| `func_button` | A button the player presses. |
| `func_breakable`, `func_physbox` | Glass and boards that break; brushes with physics. |
| `func_illusionary` | Visible but not solid (for example hanging cloth). |
| `func_wall_toggle`, `func_movelinear`, `func_tracktrain` | Moving or switching geometry. |
| `trigger_once`, `trigger_multiple` | Invisible volumes that fire outputs when touched. Use `tools/toolstrigger` on all faces. |
| `trigger_hurt`, `trigger_push`, `trigger_teleport`, `trigger_changelevel` | Volumes that hurt, push, teleport or change the level. |
| `func_areaportal` | A portal that closes off an area (see below). |

A rule worth knowing: **the outside shell of a map must be world brushes**. A `func_detail` wall facing the void leaks.

## Leaks

A map **leaks** when there is a path from any entity to the empty space outside the map. VBSP then cannot tell
inside from outside, so it keeps everything, including the outside faces of every brush:

- VVIS cannot run, so the engine draws the whole map all the time;
- VRAD does not run in a normal compile, so the map is unlit (or fully bright);
- the map loads and runs, but slowly and with wrong lighting.

VBSP writes a **pointfile** (`<map>.lin`), a line from an entity to the hole. Common causes:

- a gap between two brushes, even a very small one;
- an entity outside the shell, including point entities that sit half inside a wall;
- a brush entity (door, `func_detail`, glass) or a displacement as part of the outer shell;
- water as the outer wall; water does not seal.

Fix leaks by closing the hole with a world brush. The outside of a map is usually `tools/toolsnodraw` (indoors) or
`tools/toolsskybox` (open to the sky). Never put a giant box around the whole map to "fix" a leak: it removes the leak
message but keeps all the costs of the void.

In Mapsmith, the line shows up in the views automatically; see [Leaks](compiling.html#leaks).

## Visleafs and optimization

### How visibility works

VBSP cuts the empty inside of the map into convex pieces called **visleafs**, along the planes of the world brushes.
Where two visleafs touch, it makes a **portal**. VVIS then works out, for every visleaf, which other visleafs could
possibly be seen from anywhere inside it: the **potentially visible set** (PVS).

In the game, the engine finds the visleaf the camera is in and draws only what is in its PVS. Everything else is
skipped, even behind a thin wall. That is why visibility matters: a map where everything can see everything is slow,
however clever the rest is.

What follows from that:

- **Only world brushes cut visleafs.** Lots of small world brushes (railings, pipes, window frames) make thousands of
  tiny visleafs, long VVIS times and a worse PVS. Turn such details into **`func_detail`**, or use props.
- **Displacements, props and brush entities** do not block visibility. A hill made of displacements does not hide
  what is behind it; a world brush inside the hill does.
- VVIS is conservative: it works with whole visleafs. From somewhere in one visleaf, everything that could be seen
  from *any* point in it counts as visible.

### Tools to control visibility

- **Hint brushes.** A brush with `tools/toolshint` on one face and `tools/toolsskip` on the others adds a cut along
  that face. Placed well (for example along the corner of an L-shaped corridor), it splits visleafs so that the area
  around the corner is no longer visible. Use them where you can see that the PVS is too generous.
- **Area portals.** A `func_areaportal` (with `tools/toolsareaportal`) in a doorway or window divides the map into
  **areas**. The engine then also checks, at run time, whether the opening is actually on screen, and a door can
  close it completely (the area portal's *target* names the door). The areas must be fully sealed from each other
  by world brushes, or VBSP stops with an area portal leak.
- **Occluders.** A `func_occluder` (`tools/toolsoccluder`) hides props and entities behind it at run time. It costs
  some CPU, so use it only for large walls that hide many props.
- **Fade distances.** Props and detail have fade distances; far-away small props need not be drawn.

### Seeing it in the game

With `sv_cheats 1` in the console:

- `mat_wireframe 1` draws everything that is rendered as lines, also through walls. If you see far rooms through the
  wall, the PVS is too generous there.
- `mat_leafvis 1` shows the visleaf the camera is in.
- `cl_showfps 1` and `showbudget` show the frame rate and what takes the time.

### Other budgets

The compile tools and the engine have fixed limits. For Source SDK 2013 the commonly cited ones are 8192 brushes,
65536 brush sides and planes, and **2048 networked entities at once** in the game (the edict limit). Point entities
without a model (logic, info) that are not networked do not count toward the edict limit. The exact values of other
limits differ between engine branches; the compile log names the limit when you reach one.

Rules of thumb, not hard numbers:

- Keep the number of **dynamic lights** and **projected textures** small; each one draws parts of the scene again.
- **Named lights** (switchable) cost lightmap memory; see [Lighting](#lighting).
- Many **physics props** close together cost CPU when they move.
- Big, plain surfaces with a small **lightmap scale** cost memory and VRAD time.

## Nodraw and tool textures

Materials in `tools/` do something special in the compile or the game. The important ones:

| Material | What it does |
|---|---|
| `tools/toolsnodraw` | Not drawn, but solid, seals the map and cuts visleafs. Put it on every face the player can never see: the back of walls, the top of a ceiling, the bottom of a floor. It saves rendering, lightmap memory and compile time. |
| `tools/toolsskybox` | Shows the 2D sky (or the 3D skybox). Seals like a normal brush. Faces with it let `light_environment` light in. |
| `tools/toolsclip` | Invisible wall for players and NPCs; bullets and objects pass. |
| `tools/toolsplayerclip` | Blocks only players. |
| `tools/toolsnpcclip` | Blocks only NPCs. |
| `tools/toolsinvisible` | Invisible, but blocks everything that moves. |
| `tools/toolsblockbullets` | Blocks bullets, but not players. |
| `tools/toolsblocklight` | Casts shadows in VRAD but is not drawn. |
| `tools/toolsblock_los` | Blocks the line of sight of NPCs. |
| `tools/toolstrigger` | For trigger brush entities. |
| `tools/toolshint`, `tools/toolsskip` | Hint brushes (see above). |
| `tools/toolsareaportal` | For `func_areaportal`. |
| `tools/toolsoccluder` | For `func_occluder`. |

Tool brushes in the world (`toolsclip` and the like) do not seal the map on their own and do not cut visleafs; only
`toolsnodraw` and `toolsskybox` faces seal like normal faces. Which clip textures exist and what exactly they block can
differ slightly between games.

## Lighting

Source bakes light: **VRAD** computes the light on every surface once, when compiling, and stores it in **lightmaps**
(for brushes) and per-vertex light (for static props). In the game, this costs almost nothing.

### Light entities

| Entity | Light |
|---|---|
| `light` | A point light shining in all directions. |
| `light_spot` | A cone: *inner* and *outer angle*, pointed with its angles. |
| `light_environment` | The sun and the sky. Lights everything that can see a `toolsskybox` face. Its *pitch* sets the sun's angle, `_light` the sun, `_ambient` the sky light. Use **one** per map. |
| `light_dynamic` | A light computed in the game; can move. Expensive; use sparingly. |
| `env_projectedtexture` | A dynamic light with real-time shadows (a flashlight-like cone). Very expensive; at most a few at once. |

The colour and brightness are in `_light`: red, green, blue and brightness, for example `255 230 200 200`.

**Falloff.** By default, light falls off with the square of the distance (`_quadratic_attn` 1). `_constant_attn` and
`_linear_attn` change that curve. Easier to use: `_fifty_percent_distance` and `_zero_percent_distance`, the distance
at which the light is half as bright and where it ends.

**Bounced light.** VRAD also bounces light from surfaces (radiosity), so a lit floor brightens the walls around it.
The colour of the surface tints the bounce. This is why sealed, dark rooms still get some light near lit areas.

**HDR and LDR.** A map can be compiled with HDR lighting, LDR lighting or both. HDR maps have keys like `_lightHDR`
and `_lightscaleHDR`; `-1` means "use the LDR values". An HDR map needs an `env_tonemap_controller` to control the
exposure well.

**Switchable lights.** An unnamed light is baked into the lightmaps for good. A light with a **name** becomes a
separate *light style* that can be turned on and off with outputs; VRAD then stores its light separately on every face
it reaches. A face can only hold a few light styles at once (four, including the normal light), so overlapping
switchable lights cause VRAD warnings and wrong light.

**Lightmap scale.** Every brush face has a lightmap scale: how many units one lightmap pixel covers. 16 is the default.
Smaller (8, 4) gives sharper shadows but much more memory and compile time; larger (32, 64) is fine for big plain
surfaces and areas the player sees from afar.

**Static props** are lit per vertex when the map is compiled with `-StaticPropLighting`, and they cast lightmap
shadows with `-StaticPropPolys`. Without these options a prop gets one light value for the whole model, and only a
rough shadow.

### Cubemaps

Shiny surfaces (water, glass, metal, wet floors) reflect the nearest **`env_cubemap`**. Cubemaps are not built by
VRAD: after compiling, the game must run `buildcubemaps`, which renders the view of each cubemap and stores it in the
`.bsp`. Until then, shiny surfaces reflect a default image.

- Place a cubemap in every room with shiny surfaces, at about head height, not inside or touching a wall.
- Each brush face uses the cubemap nearest to it (or the one it is assigned to); props use the nearest one to their
  origin.
- **Mapbase** can correct the reflection to the shape of the room (parallax-corrected cubemaps), which helps a lot on
  floors. See [Cubemaps and room boxes](entities.html#cubemaps-and-room-boxes).

## The sky

### 2D sky

The simplest sky: faces with `tools/toolsskybox` show a sky texture far away. The map's **skyname** (in the map's own
settings) chooses which. The sky looks infinitely far, so it suits distant views.

### 3D skybox

For buildings, hills and clouds far away, Source draws a second, small scene: the **3D skybox**.

1. Build a small, sealed room somewhere outside the playable area, with `toolsskybox` on its inside.
2. Build the distant scenery inside it at **1/16** of its real size.
3. Place a **`sky_camera`** in it, at the point that stands for the middle of the playable map. Its *scale* is 16.
4. Faces with `toolsskybox` in the playable map now show the 3D skybox, with the 2D sky behind it.

The 3D skybox is lit by the same `light_environment` as the rest of the map. Fog inside it is set on the `sky_camera`.

## Props

Props are models (`.mdl`) placed as entities.

| Class | Use |
|---|---|
| `prop_static` | Scenery that never moves. Built into the map when compiling: the cheapest kind. It cannot have a name or outputs. |
| `prop_dynamic` | A model that can animate, be moved by parenting, hidden, or changed by outputs. Not physics. |
| `prop_physics` | A model that falls, rolls and can be picked up. The model must have physics data. |
| `prop_door_rotating` | A door made from a model. |
| `prop_ragdoll` | A ragdoll body. |

- Props do **not** seal the map and do **not** block visibility.
- Some models are made only for physics or only for dynamic use; the compile then removes a `prop_static` with that
  model and says so in the log.
- Set **fade distances** on small props in big areas so they are not drawn from far away.
- Turn off shadows (`disableshadows`) on props where shadows do not matter; dynamic shadows cost time.

## Outputs and inputs

Entities send **outputs** when something happens to them and receive **inputs** that tell them what to do. An
output has:

| Part | Meaning |
|---|---|
| **Output** | the event, e.g. `OnTrigger`, `OnPressed`, `OnMapSpawn` |
| **Target** | the name of the entity (or entities) to tell |
| **Input** | what to do, e.g. `Open`, `Kill`, `Enable`, `SetHealth` |
| **Parameter** | a value for the input, if it takes one |
| **Delay** | seconds before it is delivered |
| **Times to fire** | `-1` for every time; `1` for only the first time ("once") |

Special target names:

- `!activator`: the entity that started the chain, often the player who touched a trigger;
- `!caller`: the entity that sent this output;
- `!self`: the entity itself;
- `!player`: the player (singleplayer).

A name ending in `*` targets every entity whose name starts that way, e.g. `light_hall*`.

**Examples.**

- A button opens a door: `func_button` named *button1* → `OnPressed` → `door1` → `Open`.
- A trigger starts an event once: `trigger_once` → `OnStartTouch` → `relay_alarm` → `Trigger`. The `logic_relay`
  *relay_alarm* then fires its own outputs: lights on, sound on, NPCs spawn.
- Something happens when the map starts: `logic_auto` → `OnMapSpawn` → …
- Counting: a `math_counter` with *max* 3 receives `Add 1` from three buttons and fires `OnHitMax` once all are pressed.

Useful logic entities: `logic_relay` (a named group of outputs), `logic_auto` (map start), `logic_timer` (repeats),
`logic_case` and `logic_branch` (choices), `math_counter` (counting), `point_template` (spawns copies of entities),
`env_fade`, `env_shake`, `ambient_generic` (sounds).

**Testing in the game** (with `developer 1` or `sv_cheats 1`): `ent_fire <name> <input> <parameter>` sends an input by
hand; `ent_messages_draw 1` shows inputs and outputs as they happen.

## Instances

A `func_instance` places the content of another `.vmf` file in the map, moved and rotated. VBSP copies the content in
when compiling (*collapsing* the instance) and renames its entities, so each copy is independent:

- names in the instance get a prefix or postfix (the instance's name), so `door` becomes `room1-door`;
- names starting with `@` or `!` are left alone, so instances can still talk to entities in the main map;
- `$variables` in the instance file are replaced with the values set on each `func_instance`.

Instances are supported by the compile tools of Mapbase, Portal 2, Counter-Strike: Global Offensive and some others;
the original Half-Life 2 compile tools do not support them. With unsupported tools, collapse the instance in the editor
before compiling.

## Displacements

A **displacement** turns one face of a brush into a grid of points that can be moved up and down: terrain, rocks,
rubble. Displacements do not seal the map and do not block visibility; put a world brush (nodraw) underneath them.
Blended materials (`WorldVertexTransition`) let a displacement paint between two textures, such as grass and dirt.

Mapsmith shows and keeps displacements from existing maps; creating and sculpting them is not built in yet.

## Common compile messages

| Message (shortened) | Meaning |
|---|---|
| `**** leaked ****` | The map is not sealed; see [Leaks](#leaks). |
| `… plane with no normal`, `… brush with no sides` | A broken brush (three corners on a line, or so thin that a side vanished). Delete and rebuild it, or snap it to the grid. |
| `… mixed face contents` | One brush has faces of different kinds, such as water and solid. Use one kind per brush. |
| `MAX_MAP_…`, `Too many …` | An engine limit is reached; see [Other budgets](#other-budgets). |
| `Could not find lights.rad` | Harmless; the file is optional. |
| `Material … not found` | A material is missing and will show as a pink and black checkerboard. |
| `… not a static prop …` | That model cannot be a `prop_static`; use `prop_dynamic` or `prop_physics`. |
| `Couldn't open …` (in VVIS or VRAD) | Usually VBSP failed or the map leaked, so the files they need are missing. |
| `… has no brushes`, `… entity has no origin` | A broken entity; delete it, give it brushes, or move it once. |

Mapsmith explains each of these in plain words in the **Warnings and errors** list of the Compile tab.

## Mapbase

**Mapbase** is a free, open-source extension of the Source SDK 2013 code for singleplayer mods. A mod that uses it
gets, among other things:

- many new and improved entities and inputs (more logic, better `env_projectedtexture`, more NPC options);
- parallax-corrected cubemaps;
- VScript scripting;
- improved compile tools (VBSP with instance support, VRAD with more options);
- shared content (`mapbase_shared`), mounted next to the mod.

To map for a Mapbase mod, choose the mod as the game in Mapsmith: its FGD files are found from the mod's folders, and
its compile tools from the game settings. Mapbase entities work only when the mod uses Mapbase's game code (its
`client.dll` and `server.dll`).

Mapbase's own documentation lists every feature; this page does not try to.
