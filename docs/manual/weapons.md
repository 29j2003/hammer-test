---
title: Weapons
nav_order: 6.7
---

# Weapons
{: .no_toc }

1. TOC
{:toc}

## The Weapons page

The **Weapons** page (side bar, after *Animations*) lists your game's and mod's weapons (`scripts/weapon_*.txt`).

- **Listed:** player weapons and your mod's own weapons that have a first-person model (`models/weapons/v_*.mdl`).
- **Hidden:** NPC-only weapons (Alyx's gun, Annabelle, the citizens' package and suitcase), debug and unfinished ones
  (cubemap, physgun, hopwire ...), weapons whose first-person model is missing. Tick **Show NPC and developer weapons**
  to see them.
- **Class name** under each name: some weapons share a print name (NPC weapons borrow the SMG's).
- **Dot** after the name: your mod has its own copy of the script. One entry per weapon; the mod's copy wins.

The view (box above it):

- **Free view** (default): the model from all sides - left mouse turns, right/middle mouse moves, wheel zooms.
- **First person:** as the player sees it (eye at the model's origin, view model FOV 54 or the script's
  `viewmodel_fov`). A model that is no first-person model is shown in free view, with a note.
- **World model:** the model on the ground and in NPCs' hands.

Below the view: play/pause, a slider through the animation, the frame (a one-frame idle reads "one pose").

Nothing you do here changes the game's own files. Every change goes into your mod as its own copy of the file, at the
same place - the game reads your mod's folder first, so your copy wins. Every change can be taken back with a
**Restore** button.

## Animations

The **Animations** tab lists the model's animations with their activity (`ACT_VM_RELOAD` is the reload, `ACT_VM_IDLE`
the weapon in the hands, ...), length and how many events each has. Double-click one to play it.

- **Edit animation ...** - this animation in the animation editor (keys).
- **Change hand pose ...** - the animation editor in *Pose* mode: move the hands and arms, bend the fingers; every
  animation of the weapon gets it when you build it. See [the Animation editor](entities.html#the-animation-editor-keys-like-in-source-filmmaker).

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

## Effects and glow

The **Effects** tab makes the view model light up or spark.

- **Glow ...** opens the glow dialog for the view model's materials: pick a material, where it glows (its bright parts
  found automatically, a picture of your own, or all of it), colour and strength, steady or pulsing, and whether it
  glows **always** or **only during an animation** (the reload, say). The view shows it at once; **Apply** writes it
  into your mod, **Restore original** takes it out again.
  - With mapbase-fidelity the glow also lights the world – during an animation only while it plays, fading in and out.
- **Effects** lists particle systems on the weapon. **Add ...** asks for the effect (a particle system of the game or
  your own from the Particles page), where it sits (an attachment such as the muzzle, or the model's origin or root
  bone) and when it runs: always, or during an animation from one frame to another - sparks on the slide while
  reloading, smoke from the barrel after the last shot.

The effects become events and settings inside the model, so **Compile into model ...** rebuilds the view model into
your mod, like **Build model** on the Sounds tab. Both are kept together: the effects go on top of the animation's own
sounds and of your sound changes, and a later **Build model** keeps the effects. Changes on the Sounds tab that are
not built yet go into the same build. The result and `studiomdl`'s messages appear under the buttons; **Restore model**
takes the rebuilt model out again (the list of effects stays, so you can compile it again later).

## What is not tested yet

The Weapons page was tested with generated test weapons, not with the real Half-Life 2 weapons in the game. Please
tell us if something does not work as described. In particular:

- the rebuilt view models were not compiled with the real `studiomdl` and played in the game;
- particle effects and glow on a view model were not checked in the game (where the particles appear on the weapon,
  whether they stop on time);
- a replaced sound might keep playing the old version until the game is restarted (the game keeps a sound cache).

## Check model

- *Look* › *Check model* (also on the *Models* page): missing files, checksums that do not match, too many bones or vertices, missing materials, unknown sounds and effects – each with what to do.
- *Build model* checks the weapon before and after building; only missing files or wrong checksums stop it.

