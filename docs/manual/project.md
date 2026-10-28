---
title: The Project page
nav_order: 9
---

# The Project page
{: .no_toc }

The **Project** page in the side bar is about your mod as a whole: its maps, its main menu, its source code and how to
share it. It has six tabs: **Overview**, **Maps**, **Menu**, **Texts**, **Source control** and **Ship**.

![The Project page](images/project.png)

1. TOC
{:toc}

## Overview

What Mapsmith knows about the chosen game or mod:

- the **folder**, the game it is **based on**, its **type**, and what it uses (for example *source mod*);
- **Maps**: the `.vmf` files in the map folder. Double-click one (or **Open**) to edit it, **Open map folder** shows
  the folder;
- **Content (search paths of gameinfo.txt, first wins)**: every folder and VPK the game reads, in order, and whether it
  was **found**. A file in a path higher up hides the same file further down, which is how a mod replaces the game's
  files;
- **Entities**: the loaded FGD files and how many entity classes they define.
- **Game enhancements** (mods with mapbase-fidelity): the add-on's effects for the whole mod and what is left out;
  see [Game enhancements](mapbase-fidelity.html#game-enhancements).

Buttons: **Open mod folder**, **Edit gameinfo.txt**, **Compile and game settings ...** and **New mod ...**.

{: .tip }
If materials or models are missing in the editor, look at *Content* first: a search path marked *no* is a folder the
game will not find either.

## Maps

All maps of the mod in one list, with when each was **Changed** and **Compiled**, and the state of the **Last compile**
(*not compiled*, *up to date*, *outdated*, *failed*).

- **Open** (or a double-click), **New map ...**, **Duplicate**, **Rename ...** and **Delete** (moves the file to the
  trash). A map that is open in the editor cannot be renamed or deleted here.
- **Refresh** reads the map folder again.

### Batch compile

1. Tick the box in front of each map to compile, and press **Compile checked**. Or press **Compile all outdated**:
   every map that was never compiled, or was changed since its last compile.
2. The maps are compiled one after another with your [compile settings](compiling.html#the-compile-and-run-dialog).
   The game and cubemaps are left out between the maps, since both start the game.
3. **Run the game afterwards** starts the game with the last map that compiled.
4. **Cancel** stops after the running compile.

The Compile tab shows each map as it compiles. This is handy before a release: change a shared texture or prop, then
let all maps compile while you do something else.

## New mod

**File > New mod ...** (or the button on *Overview*) creates a new mod in Steam's `sourcemods` folder:

| Field | Meaning |
|---|---|
| **Title** | the name shown in Steam and the main menu, e.g. *My Mod* |
| **Folder** | the folder name, letters, digits, `_` and `-` only, e.g. `mymod` |
| **Built on** | *Half-Life 2 singleplayer (Source SDK Base 2013 Singleplayer)*, *Half-Life 2 singleplayer with Mapbase* (offered when Mapbase's shared content is found) or *Multiplayer (Source SDK Base 2013 Multiplayer)* |
| **Maps to copy into the new mod** | tick maps of the current game to take along |

**Create mod** writes the `gameinfo.txt`, the usual folders, a `cfg/autoexec.cfg` and the resource file with the title,
and opens the new mod in the editor. Restart Steam to see the mod in your library.

{: .note }
A mod built on Mapbase also needs Mapbase's game code: put `client.dll` and `server.dll` (Mapbase's or your own
build) into the mod's `bin` folder. Without them, the mod runs with the base game's code and Mapbase entities do
nothing.

## Menu

The **Menu** tab edits what players see in your mod's main menu. On the left are the settings, on the right a
**Preview** (*As shown during a game (Resume, Save ...)* shows the in-game version). The preview is an approximation:
fonts and spacing come from the mod's scheme in the game.

### Title and logo

| Field | Saved as (in `gameinfo.txt`) |
|---|---|
| **Name** | `game`: the name in Steam |
| **Menu title**, **Second line** | `title`, `title2`: the text in the menu |
| **Show a logo image instead of the title text** | `gamelogo` (the logo comes from the mod's scheme) |
| **Window icon** | `icon`: a path without extension, e.g. `resource/icon` (a 32×32 `.tga`, and `_big.tga` at 256×256) |
| **Type**, **Developer**, **Website**, **Manual link** | `type`, `developer`, `developer_url`, `manual` |

Other keys and comments in `gameinfo.txt` stay exactly as they were.

### Chapters

The chapters of **New Game**. **+ Chapter** adds one. Each chapter has:

- **Title**: shown in New Game (saved in the mod's localization file, `resource/<mod>_english.txt` and so on);
- **Map**: the map it starts (saved as `cfg/chapterN.cfg`);
- **Background**: the map shown behind the main menu once players reached this chapter
  (`scripts/ChapterBackgrounds.txt`);
- **Earlier** / **Later** to reorder, and a button to remove it.

Without chapters, New Game has nothing to offer.

### Main menu

The entries of the menu (`resource/GameMenu.res`). **+ Entry** adds one. Each entry has:

- **Text (or #token)**: the label. A `#token` is looked up in the localization files; the result is shown;
- the **command**: pick a common one (New Game, Load Game, Options, Quit ...) or type `engine <console command>`, for
  example `engine map mymap` for an entry that starts a map;
- **Only in game** (shown only while a map is loaded), **Not in multiplayer**, **Not in singleplayer**;
- **Higher** / **Lower** to reorder, and a button to remove it.

If the mod has no `GameMenu.res` yet, the game's menu is shown, and saving gives the mod its own.

**Save to the mod** writes all changed files; **Reload** reads them again and drops what was not saved.

## Texts

The **Texts** tab edits the text files of your mod in `resource/`: its own texts (`<mod>_english.txt`: hints, signs,
entity messages, chapter titles) and its captions (`closecaption_english.txt`). Pick the file at the top; every
language the mod has (`<mod>_german.txt` and so on) is a column.

- **+ Token** adds a text. In the game you use it as `#Token` (menus, `env_message`, `game_text`, scripts).
  **Rename** and **Delete** act on every language at once.
- Click a cell to edit it in the table or in the field below. **+ Language ...** adds a translation; empty
  translations are marked red, **Only missing translations** shows just those rows.
- **Used by** tells which tokens the menu uses: chapter titles (`<MOD>_Chapter1_Title` ...) are the same ones the
  **Menu** tab edits, so a change in either tab shows in the other after saving.
- **+ File ...** starts a text or caption file if the mod has none yet.

### Captions

Captions are shown when a sound plays (subtitles with `cc_subtitles 1`, full captions with `closecaption 1`). The
token is the sound script entry (`NPC_Citizen.Hello`); **+ Caption for a sound ...** picks one from the game's sound
list. The buttons above the field insert the caption tags: **Colour ...** (`<clr:255,200,120>`), **Bold** (`<B>`),
**Italic** (`<I>`), **Line break** (`<cr>`), **Sound effect** (`<sfx>`: only shown with full captions) and
**Length ...** (`<len:3>`: seconds on screen). The dark box below shows how the line looks.

**Compile captions** saves and then writes `closecaption_<language>.dat` next to each `.txt` - the file the game
actually reads. You do not need Valve's captioncompiler.

**Save** writes the files as UTF-16, the way the game expects them; the first time a file is replaced, a copy
`<file>.bak` is kept. Comments and console-only lines (`[$X360]`) in the files stay as they were. Mapsmith only
writes into your mod's folder, never into the game's files.

## Source control

A simple front end for **git**, for a mod's source code or its content folder. Git must be installed (it is not part of
Mapsmith).

1. **Choose folder ...** picks the repository. In a folder that is not a repository yet, **Initialize a repository
   here** starts one.
2. The top line shows the branch and whether it follows a remote branch, with ↑ commits to push and ↓ commits to pull.
3. **Changes** lists every changed file with its state (*changed*, *added*, *deleted*, *renamed*, *new, not tracked
   yet*, *conflict*). Ticked files go into the next commit; double-click opens a file.
4. Write **What did you change? (commit message)** and press **Commit**. The commit is local.
5. **Push** sends your commits to the remote. **Pull** gets new commits and merges them. **Fetch** only asks what is
   new and changes nothing.

**History** lists the recent commits with their author and time. **Output** shows what git said.

Other buttons appear when they make sense: **Open on GitHub** (the repository's page), **Open in Visual Studio** (when
a `.sln` solution is in the folder) and **Run update.bat** (when the repository has one).

{: .note }
Mapsmith only talks to the network through git, and only when you press Fetch, Pull or Push. Git uses your own
login; Mapsmith never sees a password.

## Ship

The **Ship** tab gets a map or the whole mod ready for other people.

### Share this map

1. **Compiled map**: the map's `.bsp` (compile it first). **Browse ...** picks another.
2. **Check what the map needs** lists every file the map uses: materials, textures, models, sounds, particles,
   soundscapes, detail sprites. They are grouped:
   - **Your mod**: players do not have these; they must go into the BSP or the ZIP.
   - **Missing**: nobody has these; the game shows a checkerboard, an error model or silence. Fix these first.
   - **Other mod folders**: from shared content outside the installed games; players need them too.
   - **Base game**: players already have these.
3. Then either:
   - **Pack into BSP**: puts your mod's own files into the `.bsp` itself (with the game's `bspzip`), so the one file is
     enough; or
   - **Make share ZIP ...**: `maps/<name>.bsp` with your mod's files next to it, ready to unpack into a game folder.
     The **Readme for players** goes into the ZIP as `readme.txt`.

### Share the mod

**Check mod folder** lists what would go into a ZIP of the whole mod and what **Stays out**, with **Why**: map
sources (`.vmf`, `mapsrc`), material and model sources, saved games, screenshots, logs, compile leftovers (`.prt`,
`.lin`), version control, your own `cfg/config.cfg` and system files. **Make mod ZIP ...** writes it.

### Steam Workshop (advanced)

**Show upload settings** opens the Workshop form, for games with a Workshop:

- **Game (app id)** (Half-Life 2 is 220), **Item id** (empty for a new item), **Title**, **Description**, **Change
  note**, **Preview image** (JPG or PNG under 1 MB) and **Visibility** (*Public*, *Friends only*, *Private*,
  *Unlisted*);
- **steamcmd**: the path of Valve's command line tool, and your **Steam account** name.

**Prepare upload** builds the content folder from the share package and writes the item file. **Run steamcmd** starts
the upload; steamcmd asks for your login itself. The command is shown, with **Copy**, if you prefer to run it yourself.
