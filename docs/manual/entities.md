---
title: Entities
nav_order: 6
---

# Entities
{: .no_toc }

Entities are everything in a map that is not plain geometry: lights, the player start, props, NPCs, doors,
triggers, sounds, logic. **Point entities** sit at one place (a light, a prop). **Brush entities** are brushes with a
behaviour (a door, a trigger, `func_detail`). Which entities a game has, and which keys they take, comes from its
**FGD** files.

1. TOC
{:toc}

## Placing entities: the Entity tool

Press **Shift+E**. The **Tool** tab lists every entity class of the game, with its icon.

![The Entity tool with the class list](images/entity-tool.png)

1. Type in **Find an entity class (e.g. light, door, global light) ...**. Words work too: `global light` finds
   `env_global_light`. **Groups** switches between A–Z and groups (AI, brush entities, effects, filters ...).
2. Click a class. It is now **armed**: the panel says *Armed: <class>*, and a preview of it follows the mouse in the
   view.
3. **Click** in the view to place it on the surface under the cursor. You can place several; it stays armed.
4. A **brush entity** (trigger, door ...) is built from the next box you **drag** with the Block tool's gestures.
5. Press the **✕** next to *Armed* to disarm; clicks in the view then select again.

- **Random yaw (3D placement)** turns every new prop to a random direction, so a row of barrels does not look copied.
- You can also **drag** a model from the Asset Browser's *Models* tab into the view (it becomes a `prop_static`), a
  sound from the Sounds browser (an `ambient_generic`, or an `env_soundscape` for a soundscape) or a particle system
  (an `info_particle_system`).
- To turn existing brushes into a brush entity, select them and use **Tie to** (Ctrl+T); see
  [Building](building.html#brush-entities-tie-to-and-move-to-world).

## The properties panel

Select an entity and the right panel shows it.

![The properties of an NPC](images/panel-npc.png)

- **The class** at the top. Pick another class and the entity changes class; its keys stay.
- The **id** next to it is the number compile errors use.
- Tabs: **Keyvalues**, **Outputs** and **Inputs**, each with how many there are.

### Keyvalues

Keys are grouped by where the FGD defines them: **Targetname** (the entity's name), **Angles**, the entity's own class,
**Flags**, **Placement** and so on. Click a group's title to fold it away.

Every key gets a fitting editor:

- lists of choices (with the value in brackets);
- yes/no keys and flags as **switches**;
- colours with a colour picker and the brightness;
- target names with a list of the map's entity names;
- models, sounds, particle systems and materials with a browser;
- animations with the model's sequence list.

The help area at the bottom of the panel explains the key you are in. With nothing in focus, it describes the class.

**Find a property or flag** filters keys and flags as you type.

**Smart Edit** off shows every key as plain text in a table. You can edit the raw key names and values
there, and **− Remove selected key** deletes one.

**+ Add**: the *new key | value | + Add* row at the bottom adds any key, even one the FGD does not know.

**Paste keys.** Lines like `"_light" "255 255 255 200"` or `skyname<Tab>sky_day01_01`, copied from a wiki or another
map, can be pasted into the panel (Ctrl+V); every key in them is set at once.

Several entities selected? The panel shows the keys they have in common; *(different)* marks keys whose values differ.
A change applies to all of them.

### Outputs and inputs

Source entities talk to each other with **outputs** and **inputs**: when something happens to one entity (an
*output*, like `OnTrigger`), it tells another entity to do something (an *input*, like `Open`).

The **Outputs** tab shows each output as a readable card: *When OnTrigger tell door_01 to Open*.

- **+ Add output** adds one.
- The *target* list offers the map's entity names; the *input* list offers that target's inputs.
- *Parameter* is passed to the input (e.g. a number for `SetSpeed`).
- *Delay* is in seconds; **once** removes the output after it fired the first time.

The **Inputs** tab lists every entity that sends something to this one, with **Go to** to select it.

**Connection lines:** with the *connection lines* [view switch](interface.html#view-switches) on, the views draw a
**green** line from the selected entity to every entity its outputs name, and a **blue** line from every entity that
fires at it. Names ending in `*` match every entity whose name starts that way, as in the game.

See [Source mapping basics](mapping-basics.html#outputs-and-inputs) for examples.

## The Models page

The **Models** button in the side bar shows every model of the game and its mods.

- **Folders** on the left (`props_c17`, `props_junk` ...) with the number of models in each; **All models** shows
  everything. The search box finds models by words in their path, in any order (`bench wood`).
- **Pick a model** to see it in 3D on the right: left drag turns it, right drag moves it, the wheel zooms, a
  double-click frames it again. **Wireframe** shows its triangles. The grid under it has squares of 16 units (64 for
  big models), so you can judge its size.
- **Skin** and the **body group** boxes (a lid, a helmet, a sign ...) change the model as it will look in the map.
- Below: its size in units, triangles, animations, surface, whether it was made as a **static prop** and its
  **physics** (collision model, mass, breakable settings). **Materials** lists what it is painted with; click one to
  open it in the Material Editor. Missing ones are shown in red - the game draws them as a checkerboard.
- **Place in map** (or a double-click in the list) switches to the map with the entity tool ready: every click places
  the model. **Place as** picks the entity: `prop_static` for models made as static props, `prop_physics` for models
  with a collision model, `prop_dynamic` otherwise - change it if you need something else. The skin goes with it (and
  the body groups, except for `prop_static`, which has none in the map file).
- You can also **drag** a model onto the **Map** button of the side bar; the map opens and you drop the model into a
  view. **Copy path** puts `models/.../name.mdl` on the clipboard.

## NPCs and animated models

Select an NPC or a `prop_dynamic`. The **Animation / pose** box (**Animation** for props) lists the model's
animations.

- **Pose NPC in this animation**: places a `scripted_sequence` that keeps the NPC in that animation (a real NPC, with
  its idle loop). **Keep quiet (no idle speech)** and **Don't drop weapons** set the matching flags.
- **Turn into a static prop_dynamic**: replaces the NPC with its model, standing in the animation. Good for scenery
  people who should never move.
- For a `prop_dynamic`: **Play this animation (DefaultAnim)**.

The 3D view shows props and NPCs with their real model and textures.

![An NPC in the 3D view](images/npc.png)

## Lights and the lighting preview

The 3D view's **Lighting preview** (F6) lights the map from its light entities: `light`, `light_spot` and
`light_environment`. It uses the same falloff formulas as VRAD, so the brightness is close to what the compile bakes.

- **Real-time shadows** (F7): the sun and the nearest spot and point lights cast shadows.
- `_light` sets colour and brightness; `_constant_attn`, `_linear_attn`, `_quadratic_attn` and the 50% / 0% distances
  change the falloff, exactly as in the game.
- Bounced light (radiosity) is not previewed; the compiled map is brighter in corners.

See [Lighting](mapping-basics.html#lighting) for how Source lighting works.

## The sky and its light

With nothing selected, the panel shows the map's own settings (worldspawn). **Map > Map properties ...** does the same.

![The map's own settings with the sky picker](images/panel-worldspawn.png)

- **SkyBox Texture Name** is a list of every sky of the game, each with a small picture. Faces with
  `tools/toolsskybox` show the chosen sky in the 3D view.
- A sky from Valve's games **brings its light**. Its sun direction, sun colour and ambient colour (as used in Valve's
  own maps) go into `light_environment`, `env_sun`, `shadow_control` and `env_global_light`, in one undo step. If the
  map has no `light_environment`, one is added above the player start. Change the values afterwards as you like.
- A red frame around the sky name means the game has no sky by that name.

## Cubemaps and room boxes

An `env_cubemap` captures the surroundings for reflections. Place one in every room with shiny surfaces, at head
height, away from walls.

With **Mapbase**, reflections can be **parallax-corrected**: they line up with the room instead of floating. Select an
`env_cubemap`:

1. Press **Add room box (fit to the walls)**. A box is fitted to the walls around the cubemap.
2. Drag the squares on its sides in the view to change it, or type the sizes.
3. **Fit to the walls** fits it again, **Select box** selects the box entity, **Remove** removes it.

The editor creates and names the `parallax_obb` and links it to the cubemap. Cubemaps are built after compiling; see
[Compiling](compiling.html#cubemaps).

## Soundscapes

An `env_soundscape` plays a soundscape (ambient sound) while the player is in its area.

With a soundscape entity selected, its **area** can be a **sphere** or a **block**:

- **Sphere**: the entity's *radius*. The views draw the sphere while the entity is selected; drag its squares to
  change the radius. A radius of -1 means the whole map.
- **Block**: a `trigger_soundscape` brush around the entity, which you can resize like any brush. The editor sets up
  the link: the trigger's *soundscape* key names the entity, and the entity becomes `env_soundscape_triggerable`.

The soundscape itself (what it plays) is edited on the **Sounds** page; see
[Soundscapes](sounds-particles.html#soundscapes).

## Instances (func_instance)

A `func_instance` places the content of another `.vmf` in your map, moved and turned with the entity. Mapbase, Portal 2
and Counter-Strike: Global Offensive's compile tools support them.

- The instance's brushes and entities are **drawn in place**, read-only, with cyan edges. Clicking them selects the
  `func_instance`.
- The **Instance** box in the properties shows the file (**Browse ...**; it is looked for next to the map and in the
  game's map folders), the `$variables` it fills in (*replace01*, *replace02* ...) and the parameters the instance
  offers.
- **Read file again** after you changed the instance's `.vmf`.
- **Collapse instance** puts its content into the map for real, with names fixed up the way VBSP does it
  (`instance-name` prefix or postfix; `@name` and `!name` stay global). One undo step.

Instances inside instances are not drawn.

## VisGroups

VisGroups are named groups of objects you can show and hide together, like "Detail", "Lights" or "Room 2".

![The VisGroups tab](images/panel-visgroups.png)

1. Select objects, type a name in **Name of a new group**, and press **New from selection**.
2. The tick next to a group shows or hides its members.
3. **Add selection** and **Remove selection** change the group marked in the list.
4. **Select members** (or a double-click) selects everything in it.
5. **Rename** (or F2), **Color ...** and **Delete** (the members stay in the map).

Groups can be nested: a new group is created inside the marked group. Every change is one undo step. Hidden groups are
saved as hidden in the `.vmf` and come back hidden when you open the map.

## Tips

- Give entities names only when something needs to find them (outputs, parenting). Unnamed lights compile into static
  light; a named light becomes switchable and costs more.
- Use the Outliner (left panel) to find entities by name or class, or [Find entities](safety-search.html#find-entities)
  for searches by key.
- The [Problems](compiling.html#problems-and-leaks) check finds outputs to entities that do not exist and inputs a
  target does not have.
