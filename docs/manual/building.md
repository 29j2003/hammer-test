---
title: Building
nav_order: 4
---

# Building
{: .no_toc }

Brushes are the solid shapes a Source map is made of: walls, floors, stairs, pillars. They are always **convex**:
no dents, no holes. A room is several brushes around an empty space. This page shows how to make and change them.

1. TOC
{:toc}

## Selecting

**Select tool (Q)**

- **Click** an object to select it. **Ctrl+click** adds or removes one.
- **Drag a frame** in empty space to select everything inside it.
- **Double-click** frames the selection (the same as **F**).
- **Ctrl+A** selects everything, **Ctrl+Shift+D** nothing.
- **Select similar** (Ctrl+Alt+O) selects every entity of the same class, every prop with the same model, or every
  brush with the same main material.
- **Go to ID ...** (Ctrl+Shift+G) selects the brush or entity with a number. Compile errors often name a brush by its
  id.
- **Select whole path** (Ctrl+Shift+P) selects every `path_track` or `path_corner` of a path.

**Resize right away.** Selected brushes show small squares on the middle of their sides. Drag one to make the brush
longer, shorter or taller. You do not need to switch to the Scale tool for this.

With something selected, the **Tool** tab lists actions for it: Hollow, Carve, Merge, Tie to, Move to world, Flip X,
Flip Y, Flip Z, Snap to grid, Drop to floor, Select similar and Hide. They are described below.

![The Select tool with a brush selected and its actions](images/select-tool.png)

## Moving, rotating and scaling

All three tools work with a **gizmo** in the 3D view and by dragging in the 2D views.

**Move (T)**
- Drag an **arrow** to move along one axis, a **square** to move in a plane.
- **Shift+drag** moves a copy and leaves the original where it was.
- Small squares appear on the **corners** of the selection. Drag one onto a corner of another brush or model and the
  two line up exactly.
- **Arrow keys** nudge by one grid step, **Page Up / Page Down** up and down. In the 3D view, left/right and
  forward/back follow the direction the camera faces.

**Rotate (R)**
- Drag a **ring**. The angle snaps to the rotation step from the top bar (1° to 90°).
- Hold **Ctrl** for 1° steps.
- **Alt + arrow keys** rotate by one step without the mouse.
- Brushes, brush entities, props and NPCs can all be rotated.

**Scale (E)**
- Drag a **handle** on a side or corner of the box.
- Textures keep their size when texture lock is on, so they are not stretched.

**Snap.** Moving snaps to the grid, rotating to the rotation step. **Shift+W** turns snapping off and on. The grid goes
down to 0.125 units for fine detail.

## The Block tool and shapes

**Block tool (Shift+B)**

1. Drag the base on the grid, or on any surface in the 3D view.
2. Move the mouse up or down to set the height.
3. Click or press **Enter** to create it. Keep dragging to start the next block right away.
4. **Esc** cancels.

The **Tool** tab shows the shape settings:

![The Block tool's shape settings](images/block-tool.png)

| Shape | What it makes |
|---|---|
| **Block** | a box |
| **Wedge (ramp)** | a slope |
| **Cylinder** | a round column with *Sides / pieces* sides |
| **Spike (pyramid / cone)** | a pyramid or a cone |
| **Sphere** | a ball made of pieces |
| **Arch / ring / spiral** | an arch: *Arc* (360° makes a ring), *Start angle*, *Wall width*; *Add height* makes a spiral staircase |
| **Stairs** | stairs with *Steps* steps |

Change the settings **right after** creating a shape and it is rebuilt with the new values. You do not need to draw
it again.

The material of new blocks is the **current material**: the one you last picked in the Asset Browser.

## Clip

**Clip tool (Shift+X)** cuts the selected brushes along a plane.

- **2D view:** drag a line through the brushes.
- **3D view:** click three points on surfaces; they define the cutting plane.
- **Tab** switches which part stays: both parts, the back, or the front. The overlay at the bottom of the view says
  which.
- **Enter** cuts. **Esc** starts over.

## Vertex editing and edges

**Vertex tool (Shift+V)**: select a brush first, then:

- White squares are **corners**, grey squares are **edge middles**. Drag one to move it.
- **Ctrl+click** selects several, then drag one of them to move all.
- In the 3D view a corner moves across the view; use a 2D view for exact moves. In a 2D view, corners on top of each
  other are taken together.
- The brush always stays convex. Corners that fold in are merged.

### Edges: wedge, bevel, round

Click the **middle of an edge** to pick it. Its line turns yellow. Ctrl+click picks more edges. The **Edge** box in
the Tool tab then offers:

![An edge picked in the vertex tool, with the Edge box](images/vertex-edge.png)

| Button | Result |
|---|---|
| **Wedge** | cuts from the far side of one face to the far side of the other: a block becomes a ramp |
| **Bevel** | one slanted face, *Size* units in from the edge |
| **Round** | a rounded edge with radius *Size* and *Round segments* faces (2 to 32) |

The same three are in the right-click menu while edges are picked. All picked edges change in one undo step.

Another way to round a corner: in Faces mode select the **two faces** next to an edge (click one, Ctrl+click the
other) and choose **Brushes > Round or bevel edge ...** (Ctrl+Shift+R). Set the *Radius* and the number of
*Segments* (1 makes a bevel).

## Polygon

**Polygon tool (Shift+P)** draws a brush from its outline:

1. Click the corners of the base, on the floor in the 3D view or in a 2D view.
2. **Enter** or a double-click makes the brush, as high as the last block.
3. **Backspace** removes the last corner, **Esc** cancels.

## Hollow, Carve and Merge

These are in the **Tool** tab (Select tool), in the **Brushes** menu and in the right-click menu.

- **Hollow** (Ctrl+Shift+H) turns a block into walls around its inside: a room from a block. Set the wall thickness
  next to the button. A **negative** thickness puts the walls outside and keeps the inside as it is.
- **Carve** (Ctrl+Shift+C) cuts the selected brushes out of every brush they touch. Use it for doors and windows.
  Carve can produce many small brushes; for clean geometry, prefer building around openings with separate brushes.
- **Merge** (Ctrl+Shift+M) makes one convex brush around all selected brushes. If they did not fill that shape
  completely, the gaps are filled too, and the editor tells you so.

## Brush entities: Tie to and Move to world

- **Tie to** (Ctrl+T) turns the selected brushes into a brush entity. Pick the class next to the button:
  `func_detail` (does not cut visibility; use it for small details), `func_brush`, `func_door`, `trigger_once` and so
  on.
- **Move to world** (Ctrl+Shift+W) turns a brush entity back into world brushes.

See [Source mapping basics](mapping-basics.html#world-brushes-and-brush-entities) for when to use which.

## Copy, cut and paste

- **Ctrl+C** copies the selected brushes and entities, **Ctrl+X** cuts them.
- **Ctrl+V** pastes them under the mouse, resting on the surface there.
- **Ctrl+D** duplicates the selection in place.
- Copied objects are plain VMF text, so you can paste them into another map, even in another Mapsmith window.

**Paste special ...** (Ctrl+Shift+V) makes several copies at once:

| Field | Meaning |
|---|---|
| **Number of copies** | how many |
| **Offset per copy** | how far each copy is moved from the one before |
| **Rotation per copy** | how far each copy is turned from the one before |
| **First copy** | *Next to the copied objects* or *Under the mouse* |
| **Make entity names unique** | `door` becomes `door_2`, `door_3` ...; outputs between the copied entities follow |
| **Put before names** | an optional prefix for every name, e.g. `room2_` |

Example: a spiral of 12 pillars. Copy one pillar, *Number of copies* 11, *Offset per copy* 0 0 16,
*Rotation per copy* 0 30 0.

## Flip, snap, transform, drop

- **Flip** (Brushes > Flip): **left/right (X)** Ctrl+L, **front/back (Y)** Ctrl+Alt+L, **up/down (Z)** Ctrl+I. The
  selection is mirrored around its center; entity angles are mirrored too.
- **Snap to grid** (Ctrl+B) moves the selection so its corner lies on the grid.
- **Transform ...** (Ctrl+Alt+M): exact numbers for *Move by*, *Rotate by* and *Scale by*, around the selection's
  center, as one undo step.
- **Drop to floor** (Ctrl+Shift+F) lets the selected objects fall straight down onto what is below them.

## Hiding

- **H** hides the selection, **Ctrl+H** hides everything else, **U** shows everything again.
- To hide whole kinds of objects (all triggers, all tool brushes ...) use the
  [view switches](interface.html#view-switches).
- For named groups you can hide and show, use [VisGroups](entities.html#visgroups).

## Cordons

A cordon compiles only a part of the map. This is much faster while you work on one room.

1. Select what you want to compile.
2. **Map > Add cordon around selection** (Ctrl+Shift+K). A red box appears.
3. Compile as usual. Only what is inside the cordons is built; the cordon box seals it.
4. **Map > Cordons on (compile only inside)** switches them off and on. **Map > Remove all cordons** removes them.

Cordons are saved in the `.vmf`, but they are not part of the undo history.

## Tips

- Build on the grid. Brushes with corners off the grid make small gaps that cause leaks and lighting seams.
- Make walls from separate brushes that touch, not one hollowed giant block with carved holes. It compiles faster
  and is easier to change.
- Turn details (railings, pipes, trims, furniture) into `func_detail` or use props. See
  [Optimization](mapping-basics.html#visleafs-and-optimization).
- Use the [Problems](compiling.html#problems-and-leaks) check before compiling; it finds broken brushes.
