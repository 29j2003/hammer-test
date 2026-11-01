---
title: Scenes / Choreography
nav_order: 6.5
---

# Scenes / Choreography
{: .no_toc }

A **scene** lets an NPC act: walk to a spot, play an animation there, say a line you recorded yourself, wait, turn
to look at something. You lay it out step by step in the **Choreography** panel and see it in the map: the path with
arrows, a numbered marker for every step and a ghost of the NPC in the pose of the step you are editing. The game
plays it with a `logic_choreographed_scene` and a `.vcd` file Mapsmith writes for you.

1. TOC
{:toc}

## Making a scene

1. Select the NPC (give it a name if you like - Mapsmith names it for you otherwise).
2. In **Object Properties**, the **Scene** box has **Choreography ...**. Press it. The **Choreography** panel opens
   under Object Properties with a new scene called `<npc>_scene`, started at map start.
3. The **Steps** tab holds what happens; **Starts** and **Conditions** decide when (see below).
4. Add steps with the buttons under the list:

| Button | What the NPC does |
|---|---|
| **Walk** | Click the spot in the map; the NPC walks there (tick **Run** to run). A click on a wall puts the spot on the floor in front of it. |
| **Animation** | Plays one of its model's animations where it stands. Pick it in **Animation**; **Gesture** plays it over what the NPC does (it keeps walking or talking). |
| **Say** | Says a line: **Voice file ...** imports your recording, the field below it is the subtitle. |
| **Wait** | Does nothing for the **Length** you set. |
| **Face** | Click a spot; the NPC turns towards it. |
| **Set variable** | Changes a variable at that moment (see Variables). |

Steps run one after another. **With previous** starts a step together with the one before it - a gesture while
walking, or a second NPC moving at the same time (choose it under **NPC**). Drag the cards to change the order,
**Remove** takes the selected step out. Every change is one step in **Undo**.

## Seeing it in the map

While the NPC (or the scene, or one of its points) is selected, the 3D and 2D views show its scene:

* an arrow for every walk (dashed when it runs), in one colour per NPC,
* a marker with its number and a short text for every step,
* a dotted line for Face steps,
* the step you are editing in white, with a ghost of the NPC in its pose. Drag **the bar under the step** to see the
  animation at any moment, or press the play button next to it.

Click a marker to edit that step. For walks and turns the click also selects the point (an `info_target`): move it
with the gizmo like any entity, with grid snapping. **Place again** lets you click a new spot instead.

When nothing of the scene is selected, nothing is drawn - the NPC just stands where you placed it.

**Preview scene** plays the whole scene in the views: every NPC as a ghost, the lines with sound and subtitles.

## Many scenes: the scenes list

The top of the **Choreography** panel lists every scene of the map, grouped by NPC. Each row shows what starts the
scene, its conditions and its state (**ok**, **no steps** or the number of problems - hover for details). A scene
with two NPCs is listed under both.

* **Search** filters by scene name, NPC, start or condition ("alyx", "enter", "intro" ...).
* Selecting an NPC in the map opens its group; clicking a scene shows it in the tabs below.
* **New** makes a scene for the selected NPC: `<npc>_scene`, then `<npc>_scene_2` ... The NPC's first scene
  starts at map start, the next ones when the player walks into an area.
* **Copy** makes a copy with the same steps, starts and conditions (`greet` becomes `greet_2`), its trigger boxes
  where the original's are - handy for a variant of the same moment.
* Rename a scene in the **Name** field or with F2 in the list. Starts and conditions of other scenes that name it
  follow, and so does its `.vcd`.
* **Delete** removes the scene with everything Mapsmith made for it. **Select in map** selects its NPC and its
  trigger boxes and moves the 3D view there.

## What starts the scene

The **Starts** tab lists the ways the scene starts - add as many as you like with **Add start**; any of them starts
it.

| Start | What Mapsmith adds to the map |
|---|---|
| **At map start** | a `logic_auto` (not again when a save game is loaded) |
| **Player enters an area** | a trigger box (`tools/toolstrigger`) in front of the NPC - **Select box**, then move and size it like any brush |
| **Player uses an entity** | an output on that entity (empty: the scene's NPC): `OnPlayerUse`, or `OnPressed` on buttons |
| **Player looks at an entity** | a `trigger_look` box: the player stands in it and looks at the entity for half a second (works once) |
| **Player comes near the NPC** | a box around the NPC that moves with it (**Radius**) |
| **After another scene** | the other scene's `OnCompletion` |
| **Output of another entity** | any output of any entity: a door's `OnFullyOpen`, a breakable's `OnBreak` ... |
| **Timer** | a `logic_timer`: after the time set, or at a random time between two times |
| **Only by input** | nothing - fire `Trigger` on `<scene>_start` from your own outputs (the conditions apply) or `Start` on the scene (skips them) |

**Only once**: the start works until the scene has played. Turned off, it starts the scene every time (an area: each
time the player comes in); the scene never restarts while it is playing. **Delay** waits that long before the scene
starts.

## Conditions

The **Conditions** tab holds what must be true when a start fires - all of them. If one is not, nothing happens; an
area keeps checking while the player stands in it, so a scene that becomes possible while the player is inside still
starts.

| Condition | True when |
|---|---|
| **Scene has played** / **Scene has not played yet** | the other scene has finished (or not) |
| **Variable** | a variable compared with a value (`=`, `≠`, `>`, `<`; for true/false variables 1 = true) |
| **Global state (counter)** | the counter of a global state (`env_global`), which is kept across maps |
| **Chance** | a random roll, in steps of 10 % |

## Variables

The **Variables** tab lists the map's variables: a **number** (a `math_counter`: count visits, remember a choice) or
**true/false** (a `logic_branch`), with their start value and the scenes that use them. Double-click a name to rename
it - every condition and step that uses it follows - or a start value to change it.

A scene changes a variable with a **Set variable** step: set it to a value, add to it (numbers) or toggle it
(true/false). It happens at that moment of the scene (a scene can have up to 16 such steps).

## Seeing the starts in the map

While the NPC or the scene is selected, the views also show what starts its scenes: the trigger boxes (even with
triggers hidden), a dashed line from each box or entity to the NPC and a label such as `enter → greet (if 1)`.
Selecting a trigger box shows its scene, with that start in the **Starts** tab.

## How-to: Alyx greets you when you enter the room, but only after the intro scene

1. Select Alyx and press **Choreography ...**. Her first scene appears; call it `intro` in **Name** and add its
   steps. It starts at map start.
2. Press **New**. The second scene starts when the player walks into an area: a trigger box appears in front of
   Alyx. Call it `greet` and add her greeting (**Say** with your recording).
3. In **Starts**, press **Select box** and move and size the box to fill the room's doorway or the whole room.
4. In **Conditions**, **Add condition** > **Scene has played** and pick `intro`.

Done: when the player walks in before the intro has finished, nothing happens yet; as soon as it has (and the player
is in the room), Alyx greets them - once. To greet only the first time the player comes back after something else
happened, use a variable instead: set it with a **Set variable** step in that other scene and test it here.

## What Mapsmith builds

You never need to touch these, but they are ordinary entities you can look at (each carries `mapsmith_scene` with its
scene's name and `mapsmith_role`):

* `<scene>_start` - a `logic_relay` every start fires,
* one entity per start: `<scene>_auto1`, `_area2`, `_look3`, `_near4`, `_timer5`, `_gate6` (for "only once" starts on
  other entities),
* one per condition in a chain after the relay: `<scene>_if1` (`logic_branch`, `logic_compare` or `logic_case`), plus
  `<scene>_if1w` (a `logic_branch_listener` for true/false variables) or `<scene>_if1g` (an `env_global`),
* the last condition fires **Start** on the scene.

Every change rebuilds them from the plan. Entities and outputs you made yourself stay as they are - even outputs
aimed at Mapsmith's entities.

## Your own voice lines

**Voice file ...** takes a WAV, MP3 or OGG file and puts it into the mod:

* the sound as `sound/<mod>/vo/<scene>/line1.wav` (mono, 44.1 kHz),
* a sound script entry `<scene>.line1` on the voice channel (in `scripts/game_sounds_mapsmith.txt`),
* the subtitle in `resource/closecaption_english.txt`, compiled to `closecaption_english.dat`.

The step takes the length of the recording. You can also type the name of any sound script entry of the game
into the line field. Subtitles show in the game with closed captions on (`closecaption 1`).

**Mouths:** the NPC moves its mouth with the loudness of the line. Mapsmith does not write phoneme (lip sync) data
into the WAV; for exact lip shapes, add them later with Valve's tools.

## Files and checks

* The scene file goes to `scenes/<map>/<scene>.vcd` in the mod, written at every change and on save. Mapbase plays
  it from there; nothing else is needed.
* The plan is kept in the scene entity's keys (`mapsmith_...`), so it is saved with the `.vmf`, survives Hammer and
  can be undone.
* **Problems** warns about steps without an NPC, NPCs that are not in the map, lines without a sound, animations the
  model does not have and walks far from any `info_node` (NPCs find their way along nodes) - and about starts and
  conditions: a trigger box outside the map, a condition on a scene or variable that is gone, a start aimed at an
  entity nobody is named, two scenes of one NPC that would start at the same moment without a condition, more than
  16 **Set variable** steps.
* **Ship** packs the `.vcd` and the voice files with the map.

How long a walk takes is planned from the distance (walking about 80 units a second, running 180); if the NPC is
slower in the game, the scene waits for it at the end of the walk.
