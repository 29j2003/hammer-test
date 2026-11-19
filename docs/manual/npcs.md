---
title: NPCs
nav_order: 6.8
---

# NPCs
{: .no_toc }

1. TOC
{:toc}

## The NPCs page

- Side bar **NPCs**: every NPC the game has a model for, grouped (Resistance, Characters, Combine, Creatures, Mod).
- Not listed: helpers (bullseye, makers, vehicle drivers), classes without art (bullsquid, houndeye in Half-Life 2).
- Your mod's own NPC classes appear when its FGD gives them a model.
- The middle shows the model in 3D: drag to turn around it, right-drag to move, wheel to zoom.
- Double-click an animation to play it; the slider scrubs.

## Stats

- Health and damage of the NPC (`sk_<npc>_*`), plus the damage of its weapons when NPCs shoot them (`sk_npc_dmg_*`).
- Empty field: the game's value stays. The grey hint shows the game's value.
- **Save to mod** writes the changed values into the mod's `cfg/skill.cfg` (made from the game's on first save).
- **Restore original** sets this NPC's values back to the game's.
- The game reads `skill.cfg` at every map start: load a new map to see a change.
- Weapon damage is shared: every NPC with that weapon hits that hard.

## Weapons

- The weapons the NPC can carry (from the FGD's *additionalequipment*, else Half-Life 2's defaults).
- **Place in map**: click in the map to place the NPC with the chosen weapon and the model shown on *Look*.
- A citizen keeps the chosen head (*citizentype* Unique).

## Look

- **Model**: the NPC's models the game has (all citizen heads, soldier variants).
- **Skin**, the model's materials with pictures.
- **Edit material**, **Enhance textures ...**, **Glow ...** (eyes, lamps, screens).

## Sounds

- Sound script entries named after the NPC (`NPC_MetroPolice.*`, `Zombie.*` ...) down to their wave files.
- **Play**, **Replace ...** (your file at the same path in the mod), **Edit ...** (Sounds page), **Restore**.
- Spoken lines of citizens and soldiers come from sentences and response rules: not listed.

## Animations

- All sequences with activity and length.
- **Edit animation ...** opens the Animation editor on the selected sequence.

## Not tested

- Nothing of this page was tried in the game or on Windows yet.
