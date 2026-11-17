---
title: Claude in Mapsmith
nav_order: 9.5
---

# Claude in Mapsmith
{: .no_toc }

**Project > Claude** lets Claude look at your materials and change them – only through Mapsmith. Claude runs in
**Claude Desktop** or **Claude Code** on the same computer and uses your own Claude account; Mapsmith needs no key and
sends nothing anywhere.

1. TOC
{:toc}

## Switch it on

1. Open **Project > Claude**.
2. Switch on **Let Claude use Mapsmith**. It is off until you do this, and Mapsmith remembers your choice.
3. Choose what Claude may do:
   - **Read only** – look at materials, the map's problems, the log and the 3D view.
   - **Read and change materials** – also change the decision file, the mod's own material files and files in the
     exchange folder.
4. If you want to see every change before it happens, switch on **Ask me before each change**.

The line under the switch says what is going on: *waiting for Claude*, *Claude is connected*, or why it could not start.

## Connect Claude Desktop (once)

Click **Connect Claude Desktop**. Mapsmith adds itself to Claude Desktop's settings file
(`%APPDATA%\Claude\claude_desktop_config.json`, or the Microsoft Store version's file), keeps everything else in it and
writes a backup next to it (`.mapsmith-backup`). Restart Claude Desktop. In a new chat Claude now has Mapsmith's tools.

For **Claude Code**, click **Copy setup for Claude Code** and paste the line into a terminal:

```
claude mcp add mapsmith -- "C:\Program Files\Mapsmith\mapsmith-mcp.exe"
```

## What you can ask

- "Look at the brick materials and switch parallax off where the bricks are too small."
- "Which of my facade textures have windows? Set windows=2x3 where you can see them clearly."
- "Why does this wall look flat? Check its material and the 3D-check pictures."
- "Give my concrete floors a generated bump map with strength 1.5."
- "Make an editor report and tell me what went wrong with the last compile."

## What Claude can and cannot do

| Claude can | Needs |
|---|---|
| list materials, read a material (VMT text, picture, what Mapsmith reads from it), the 3D-check pictures (base, relief heights, normal map) | Read only |
| read the map's problems, the log, the last compile's log, a picture of the 3D view | Read only |
| list and read text files in the exchange folder | Read only |
| change a line of the decision file (`cfg/mapsmith_materials.txt`: role, windows, floors, parallax, depth, pattern, bump, …) | Read and change materials |
| change one `$parameter` in a `.vmt` file **the mod has itself** (`materials/…` in the mod folder; never in VPKs or the base game) | Read and change materials |
| write a text file (`.txt .md .csv .json .log .vmt`) in the exchange folder, make an editor report there | Read and change materials |

Claude **cannot**: open or write any other folder, run programs, compile, use Git, change your map, or reach the
internet through Mapsmith. The tools above are all there is.

**The exchange folder** is `Documents\Mapsmith\Claude` (**Open folder** shows it). It is the only folder Claude can read
and write files in. Its `backups` folder keeps a copy of every file before Claude changes it.

## See and undo what Claude did

**Claude activity** lists every call: the time, the tool, what it was about and the result (refused calls in orange).
Every change keeps a backup; **Undo Claude's last change** puts the last one back (click again for the one before).
The log shows Claude's changes too.

Switching **Let Claude use Mapsmith** off disconnects Claude at once.

## Safety

- The connection is local: Claude Desktop starts `mapsmith-mcp.exe`, which talks to Mapsmith over a local pipe only your
  Windows user can open. There is no network port, so no web page can reach it.
- Mapsmith writes a new random secret each time it starts; the bridge and Mapsmith prove to each other that they know
  it before anything else happens. A program without it is refused (shown in Claude activity).
- Every name Claude sends is checked: no `..`, no other drives or network paths, no links leading out of the folder,
  no program files.
- Text from your files (material names, VMT text, logs) can contain anything. Mapsmith marks it for Claude as data, not
  instructions – still, keep **Ask me before each change** on when you work with materials from someone else.
- At most 30 changes a minute.
