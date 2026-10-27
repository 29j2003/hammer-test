---
title: Sounds, soundscapes and particles
nav_order: 7
---

# Sounds, soundscapes and particles
{: .no_toc }

1. TOC
{:toc}

## The Sounds page

The **Sounds** page (side bar) and the **Sounds** tab of the bottom panel show the same browser. On the page there is
more room; in the bottom panel you can drag sounds into the map while you build.

![The Sounds page](images/sounds.png)

On the left is a tree of everything the game can play:

- **Sound scripts**: named entries like `Doors.Squeak`, defined in `scripts/game_sounds_*.txt`. Most entities that
  play sounds want these names.
- **Sound files**: the `.wav`, `.mp3` and `.ogg` files below `sound/`.
- **Soundscapes**: ambient sound setups from `scripts/soundscapes_*.txt`.

Type in **Search sounds, files and soundscapes (words: door metal) ...** to filter; the list next to it limits the tree
to one kind.

### Listening

- **Click** a sound to see its waveform and, with **Play on click**, hear it.
- **▶ Play** plays or stops (Space). **Loop** repeats it.
- Click into the waveform to play from there. A sound's loop point (from its cue marker) is shown.
- The **Volume** slider goes down very quiet; it is in decibels, so each step sounds equally big.

Mapsmith plays WAV (including ADPCM), MP3 and OGG files itself.

### Using sounds in the map

- **Drag** a sound script or sound file into the 3D view: it becomes an `ambient_generic` playing that sound.
- **Drag** a soundscape into the view: it becomes an `env_soundscape` with that soundscape.
- **Double-click** sets the sound on the selected entity.
- **Ctrl+C** copies the name; Ctrl+V pastes it into any field.

### Sound script entries

Selecting a sound script entry shows its settings on the right:

| Field | Meaning |
|---|---|
| **Name** | the entry's name, e.g. `MyMod.DoorSqueak` |
| **Channel** | which sound channel (CHAN_AUTO, CHAN_WEAPON, CHAN_VOICE ...) |
| **Volume** | 0 to 1, or a range like `0.7,1` for a random volume each time |
| **Pitch** | 100 is normal; a range like `95,105` varies it |
| **Sound level** | how far it carries (SNDLVL_NORM, SNDLVL_80dB ...) |
| **Files** | one or more sound files; with several, the game picks one at random |

**+ Shown file** adds the sound shown in the player, **− Remove** removes the marked file. **Save to the mod** writes
the entry into `scripts/game_sounds_<mod>.txt` and adds that file to the mod's `game_sounds_manifest.txt`. Entries from
the game are saved as a changed copy in your mod.

## Soundscapes

A soundscape is the ambient sound of an area: wind outside, a hum in a factory, drips in a cave. An `env_soundscape`
entity in the map says *where* a soundscape plays; the soundscape itself says *what* plays.

Click a soundscape in the tree, or press **+ Soundscape** for a new one. The editor on the right shows:

| Field | Meaning |
|---|---|
| **Name** | the name the `env_soundscape` uses in its *soundscape* key, e.g. `mymod.cave_drips` |
| **Room effect** | the echo while it plays (the engine's DSP presets, with names like *Concrete, small*) |
| **Fade in** | seconds to fade in; empty means the engine's default |

Below are its **sounds**, one card each:

- **+ Looping**: a sound that plays all the time (wind, hum). *Sound*, *Volume*, *Pitch*, *Sound level*, *Position*.
- **+ Random**: one of several sounds every few seconds (birds, creaks). A list of *Sounds* and *Every* (seconds,
  `5,10` means between 5 and 10).
- **+ Soundscape**: plays another soundscape inside this one, e.g. a shared "city background". *Volume* and
  *Position*.

On each card: **▶** listens to it, **↑** moves it up, **✕** removes it. *Use shown* or *+ Shown* takes the sound shown
in the player. Typing a sound file name completes it.

*Position* empty means the sound is everywhere around the player. A number from 0 to 7 makes it come from the
`env_soundscape`'s position target of that number, so it sounds like it comes from a place.

**Save to the mod** writes `scripts/soundscapes_<mod>.txt` and puts it **first** in the mod's
`soundscapes_manifest.txt`. A changed copy of a game soundscape is therefore used instead of the original. **Delete**
removes a soundscape from the mod's own file. Keys the editor does not know stay in the file.

To decide *where* a soundscape plays (a sphere or a block), see
[Soundscapes in the map](entities.html#soundscapes).

### How the game picks a soundscape

The player hears the soundscape of the nearest `env_soundscape` that the player can see and whose radius they are in.
A `trigger_soundscape` switches to its soundscape while the player touches it. If no soundscape applies, the last
one keeps playing. Put an `env_soundscape` into every distinct area, and one with a calm default near the start.

## Particles

The **Particles** page shows the game's particle systems, from the `.pcf` files in `particles/`.

![The Particles page](images/particles.png)

- On the left, the systems grouped by file, with a search field.
- In the middle, a **moving preview**. Drag with the left button to turn around it, with the right button to move up
  and down; the wheel zooms. **Pause**, **Restart**, **Speed** (0.25× to 2×) and **Grid** control it.
- On the right, the system's parts as cards: the system itself, **Emitters**, **Initializers**, **Operators**,
  **Renderers** and **Children**, each with all its values. **Show** on a child opens it.

The preview simulates the common parts: emitting, lifetime, size, colour and alpha, movement with gravity and drag,
fading, scaling and rotation, with the system's sprite material; additive materials glow. It is close to the game, not
exact: parts it does not know are listed but have no effect, and sprite-sheet animation is not played.

**Drag** a system into the map (from the page or the bottom panel's *Particles* tab) for an `info_particle_system`
that plays it. In the map views, particle effects are not drawn; they only show in the game.

{: .note }
A custom `.pcf` file must be listed in the mod's `particles/particles_manifest.txt`, or the game does not load it.
