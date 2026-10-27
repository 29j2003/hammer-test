---
title: Keyboard shortcuts
nav_order: 11
---

# Keyboard shortcuts
{: .no_toc }

These are the default keys. **Help > Shortcuts** (F1) shows a short list in the editor. The **Tool** tab of the left
panel always lists the keys of the current tool.

1. TOC
{:toc}

## Changing shortcuts

**Help > Change shortcuts ...** lists every **Command** with its **Shortcut**.

- Type in **Search commands ...** to find a command.
- Click a shortcut and press the new key. **Default** puts the original key back.
- If two commands share a key, the dialog says so: *Conflicts (the key does nothing until fixed)*.

The Tool tab's key tables follow your own shortcuts.

## Tools

| Key | Tool |
|---|---|
| Q | Select |
| T | Move (Shift+drag moves a copy) |
| R | Rotate (Ctrl: 1° steps) |
| E | Scale |
| Shift+B | Block |
| Shift+E | Entity |
| Shift+X | Clip |
| Shift+V | Vertex |
| Shift+P | Polygon |
| 3 | Faces mode |
| 5 or 4 | Objects mode |

### Inside the tools

| Key | Tool | Does |
|---|---|---|
| Enter | Block | create the block |
| Enter | Clip | cut |
| Enter, double-click | Polygon | make the brush |
| Tab | Clip | which part stays: both, back, front |
| Backspace | Polygon | remove the last corner |
| Esc | all | cancel what you are drawing or dragging |
| Ctrl+click | Select, Vertex, Faces | add to or remove from the selection |
| Ctrl (while rotating) | Rotate | 1° steps |
| Shift+drag | Move | move a copy |

## Camera and views

| Key | Does |
|---|---|
| Right mouse (hold) | look around (3D) |
| W A S D | fly while looking around |
| Q / E | down / up while looking around |
| Shift | fly faster |
| Mouse wheel | forward and back (3D), zoom (2D); while looking around: fly speed |
| Z | fly mode on/off |
| Alt + left mouse | orbit around the selection |
| Middle mouse | pan |
| F or Ctrl+Shift+E | frame the selection |
| F2 / F3 / F4 | top / front / side camera in the active view |
| Ctrl+F1 | 3D camera in the active view |
| Shift+Alt+Z | one view / four views |
| F5 | textured, unlit |
| F6 | lighting preview |
| F7 | real-time shadows on/off |
| F8 | wireframe |

## Grid and snapping

| Key | Does |
|---|---|
| [ / ] | grid smaller / bigger |
| Ctrl+[ / Ctrl+] | rotation step smaller / bigger |
| Shift+W | snapping on/off |
| Arrow keys | nudge by one grid step |
| Page Up / Page Down | nudge up / down |
| Alt + arrow keys | rotate by one rotation step |

## Materials

| Key | Does |
|---|---|
| Shift+T | apply the current material |
| Shift + right click, Ctrl+M | pick the material under the cursor |
| Ctrl + right mouse drag | paint the current material onto faces |
| Double-click (Faces mode) | select the whole wall |

## Edit

| Key | Command |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+X / Ctrl+C / Ctrl+V | Cut / Copy / Paste (under the mouse) |
| Ctrl+Shift+V | Paste special ... |
| Ctrl+D | Duplicate |
| Delete | Delete |
| Ctrl+A | Select all |
| Ctrl+Shift+D | Select nothing |
| Ctrl+Alt+O | Select similar |
| Ctrl+Shift+G | Go to ID ... |
| Ctrl+Shift+P | Select whole path |
| Ctrl+F | Find entities ... |
| Ctrl+Alt+R | Replace materials ... |
| Ctrl+T | Tie to entity (func_detail) |
| Ctrl+Shift+W | Move to world |
| H | Hide selection |
| Ctrl+H | Hide others |
| U | Show all |

## Brushes

| Key | Command |
|---|---|
| Ctrl+Shift+H | Hollow ... |
| Ctrl+Shift+C | Carve with selection |
| Ctrl+Shift+M | Merge into one brush |
| Ctrl+Shift+R | Round or bevel edge ... |
| Ctrl+L | Flip left/right (X) |
| Ctrl+Alt+L | Flip front/back (Y) |
| Ctrl+I | Flip up/down (Z) |
| Ctrl+B | Snap to grid |
| Ctrl+Alt+M | Transform ... |
| Ctrl+Shift+F | Drop to floor |

## File, map and help

| Key | Command |
|---|---|
| Ctrl+N | New map |
| Ctrl+O | Open ... |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as ... |
| Ctrl+Alt+E | Export for other programs ... |
| Ctrl+Shift+K | Add cordon around selection |
| F9 | Run map (compile and run; again: cancel) |
| F1 | Shortcuts |

On macOS, Ctrl is the Command key wherever the system uses it.
