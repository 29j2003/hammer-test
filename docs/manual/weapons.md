---
title: Weapons
nav_order: 6.7
---

# Weapons
{: .no_toc }

1. TOC
{:toc}

## The Weapons page

The **Weapons** page (side bar, after *Animations*) lists every weapon of your game and mod - every
`scripts/weapon_*.txt` the game can see. A dot after the name means your mod already has its own copy of the script.

Pick a weapon and its **first-person model** turns up in the middle, the way the player sees it in the game: the eye at
the model's origin, with the weapon's *view model FOV* (54 in Half-Life 2, or the script's own `viewmodel_fov` in
Mapbase mods). The box above the view switches to **Free view** (turn it around with the mouse, wheel to zoom) and to
the **World model** (the model on the ground and in NPCs' hands).

Below the view are play/pause, a slider through the animation and the frame number.

Nothing you do here changes the game's own files. Every change goes into your mod as its own copy of the file, at the
same place - the game reads your mod's folder first, so your copy wins. Every change can be taken back with a
**Restore** button.

## Animations

The **Animations** tab lists the model's animations with their activity (`ACT_VM_RELOAD` is the reload, `ACT_VM_IDLE`
the weapon in the hands, ...), length and how many events each has. Double-click one to play it.

**Edit animation ...** opens the animation in the animation editor.

## Sounds

The **Sounds** tab has two parts.

**Weapon script** - the sounds the game plays for the weapon: *Shot (player)*, *Reload (player)*, *Dry fire (empty)*,
the NPC versions and so on. Each one names a sound script entry (`Weapon_SMG1.Reload`), and under it are the sound
files it plays.

- **Play** plays the selected sound.
- **Replace ...** takes a file of your own (WAV, MP3, Ogg, FLAC) and writes it into your mod at the same place as the
  game's sound, so the game plays yours instead. It gets the sample rate and channels of the original
  (Source plays 44.1, 22.05 or 11.025 kHz, mono or stereo).
- **Edit ...** opens the sound on the Sounds page: cut, fade, louder or quieter, then save it into the mod.
- **Restore** takes your replacement out again.
- **Use another sound ...** lets a slot play another sound of the game (the reload of the AR2 for your SMG, say). This
  is saved in your mod's copy of the weapon script.

**Animation events** - what the model's animations trigger while they play: sounds (`AE_CL_PLAYSOUND`, `5004`),
muzzle flashes (`5001`), shells, effects. Each line says the animation, the frame and the event; sound events show the
files they play. Double-click a line to jump to that frame and hear it.

- **Change sound ...** lets a sound event play another sound.
- **Frame ...** moves the event to another frame.
- **Remove** deletes the event, **Add sound ...** adds a sound at the frame shown under the view.

Events live inside the compiled model, so changing them needs the model rebuilt: the changed lines turn italic and
**Build model** takes the model apart, puts the new events in, and has the game's `studiomdl` (from the SDK tools, next
to `vbsp.exe`) build it into your mod at the same path. Its messages appear below the buttons. **Restore model** takes
the rebuilt model out again. Every rebuild starts from the original model again, so building twice does not lose
quality.

## Look

The **Look** tab shows the model's materials with their pictures, and its skins.

- **Edit material** opens the material in the Material Editor (shine, bump map, colour, ...).
- **Replace texture ...** puts a picture of your own (PNG, JPG, TGA, BMP) in as the material's texture, at the same
  place in your mod. A model's texture can have any size: it is made a power of two, at most 2048 pixels.
- **Restore texture** takes it out again.
- **Enhance textures ...** makes the weapon's textures sharper and larger and gives them bump maps.

## Stats

The **Stats** tab shows the weapon script's values with a short explanation each (hover a name): name, magazine size,
rounds when picked up, ammo type, HUD slot and position, weight, whether the game switches to it, the models, and in
Mapbase mods the view model FOV, bob and sway.

Below them are the **damage and ammo** values. In Half-Life 2 these belong to the ammo type, not the weapon: the
SMG's bullets do `sk_plr_dmg_smg1` damage, the player carries at most `sk_max_smg1`. They are console variables that
your mod's `cfg/skill.cfg` sets at every map start.

- **Save to mod** writes the values you changed: the script as your mod's own copy, the damage and ammo values into
  your mod's `cfg/skill.cfg` (made from the game's if your mod has none). Start or reload a map in the game to see
  them.
- **Restore original** takes your copy of the script out and sets the damage and ammo values back to the game's.

## What is not tested yet

The Weapons page was tested with generated test weapons, not with the real Half-Life 2 weapons in the game. Please
tell us if something does not work as described. In particular:

- the rebuilt view models were not compiled with the real `studiomdl` and played in the game;
- a replaced sound might keep playing the old version until the game is restarted (the game keeps a sound cache).
