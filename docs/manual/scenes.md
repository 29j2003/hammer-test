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
3. Add steps with the buttons under the list:

| Button | What the NPC does |
|---|---|
| **Walk** | Click the spot in the map; the NPC walks there (tick **Run** to run). A click on a wall puts the spot on the floor in front of it. |
| **Animation** | Plays one of its model's animations where it stands. Pick it in **Animation**; **Gesture** plays it over what the NPC does (it keeps walking or talking). |
| **Say** | Says a line: **Voice file ...** imports your recording, the field below it is the subtitle. |
| **Wait** | Does nothing for the **Length** you set. |
| **Face** | Click a spot; the NPC turns towards it. |

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

## What starts the scene

| Starts | What Mapsmith adds |
|---|---|
| **At map start** | a `logic_auto` that fires **Start** when the map has loaded |
| **When the player walks in** | a `trigger_once` box at the NPC - move and size it like any brush |
| **By input "Start"** | nothing: fire **Start** on the scene's name from your own outputs |
| **After another scene** | the other scene's **OnCompletion** fires **Start** |

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
  model does not have and walks far from any `info_node` (NPCs find their way along nodes).
* **Ship** packs the `.vcd` and the voice files with the map.

How long a walk takes is planned from the distance (walking about 80 units a second, running 180); if the NPC is
slower in the game, the scene waits for it at the end of the walk.
