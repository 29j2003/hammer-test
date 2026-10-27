---
title: mapbase-fidelity
nav_order: 13
---

# mapbase-fidelity
{: .no_toc }

**mapbase-fidelity** is a graphics and sound add-on for Mapbase mods: drifting clouds, light shafts from the sun,
colour grading, ambient occlusion, walls with real depth, better water and more. Mapsmith knows it and lets you set
its look per map and preview it while you build.

Everything on this page is optional. A map made with these settings still works in a mod without the add-on; the
settings then simply do nothing.

1. TOC
{:toc}

## Does my mod have it?

Mapsmith treats a mod as having mapbase-fidelity when it finds the add-on's `cfg/fidelity.cfg`, its shaders, or its
shader library in the mod's `bin` folder. The add-on's files get into the mod through its `deploy.bat`.

Without it, the Map Look tab says so: your values are then only shown in the editor's preview, and players see them
once the mod has the add-on.

## Map Look

The **Map Look** tab in the left panel sets what the add-on shows **in this map**.

![The Map Look tab](images/panel-map-look.png)

- Every value has a slider and a number. Hover over the name for a description and the console variable behind it.
- **Grey** values are the mod's own (from `cfg/fidelity.cfg`): the map does not change them.
- Change a value and it becomes the map's own. **↺** (*Back to the mod's own value*) undoes that.
- **Show it in the lighting preview** turns the preview of the look on and off in the 3D view. The view menu's
  *mapbase-fidelity look* does the same.

Every change is one undo step (*Map look*).

### The values

| Group | Value | Console variable | Range | Meaning |
|---|---|---|---|---|
| Clouds | **Clouds** | `r_fidelity_sky_clouds` | 0 – 1 | how visible the drifting clouds in the skybox are; 0 = none |
| Clouds | **Coverage** | `r_fidelity_sky_clouds_coverage` | 0 – 1 | how much sky they cover: 0.3 is a lot, 0.7 a few wisps |
| Clouds | **Darkness** | `r_fidelity_sky_clouds_darkness` | 0 – 1 | how dark their thick parts are |
| Clouds | **Pattern size** | `r_fidelity_sky_clouds_scale` | 0.1 – 4 | bigger means smaller clouds |
| Clouds | **Speed** | `r_fidelity_sky_clouds_speed` | 0 – 4 | how fast they drift, times the wind |
| Light shafts | **Brightness** | `r_fidelity_godrays_intensity` | 0 – 2 | brightness of the shafts from the sun |
| Light shafts | **Length** | `r_fidelity_godrays_length` | 0 – 1 | how far they reach towards the sun |
| Light shafts | **Fade along** | `r_fidelity_godrays_decay` | 0.5 – 1 | how quickly a shaft fades along its length |
| Light shafts | **Sky brightness needed** | `r_fidelity_godrays_threshold` | 0 – 1 | from which sky brightness the sky casts shafts |
| Light shafts | **Glow size** | `r_fidelity_godrays_radius` | 0 – 0.5 | size of the glow around the sun, in screen heights |
| Light shafts | **Lens flare** | `r_fidelity_lensflare` | 0 – 1 | a faint lens flare from the sun |
| Image | **Exposure** | `r_fidelity_exposure` | 0.25 – 3 | brightness of the whole picture |
| Image | **Contrast** | `r_fidelity_contrast` | 0 – 1 | a soft S-curve |
| Image | **Saturation** | `r_fidelity_saturation` | 0 – 2 | 1 is unchanged, 0 is grey |
| Image | **Vibrance** | `r_fidelity_vibrance` | 0 – 1 | extra saturation for dull colours only |
| Image | **Temperature** | `r_fidelity_temperature` | −1 – 1 | cold and blue to warm and orange |
| Image | **Tint** | `r_fidelity_tint` | −1 – 1 | magenta to green |
| Image | **Lift shadows** | `r_fidelity_lift` | 0 – 0.2 | lifts the darkest parts |
| Image | **Mid-tones** | `r_fidelity_gamma` | 0.5 – 2 | above 1 brightens the mid-tones |
| Image | **Highlights** | `r_fidelity_gain` | 0.5 – 2 | gain of the bright parts |
| Image | **Clarity** | `r_fidelity_clarity` | 0 – 1 | local contrast; brings out relief and texture detail |
| Image | **Vignette** | `r_fidelity_vignette` | 0 – 1 | darker corners |
| Water | **Sun glint** | `r_fidelity_water_glint` | 0 – 6 | how brightly the sun glints on `Water_Fidelity` water |
| Water | **Shore foam** | `r_fidelity_water_foam` | 0 – 2 | foam where the water is shallow at the shore |

### How a map sets them

The game reads these values as console variables. When you change one, Mapsmith keeps two entities in the map:

- a `logic_auto` named `fidelity_look`, which fires when the map loads, and
- a `point_clientcommand` named `fidelity_look_command`, which receives the commands, such as
  `r_fidelity_exposure 1.1`.

The add-on allows maps to set exactly these variables. Players' own settings (on/off switches and quality) are not
touched. Do not edit the two entities by hand; the Map Look tab rewrites them. When every value is back to the mod's
own, both entities are removed.

The values stay set until another map sets them again. A map that sets nothing shows the values of the map before it,
or the mod's own after a restart, so give every map of a series its look, or none.

## The preview

With the mapbase-fidelity look on, the 3D view's lighting preview shows:

- the **clouds** in the skybox (standing still in the editor);
- the **Image** values: exposure, contrast, saturation, temperature and the rest of the grading;
- **relief** on walls, using the same rules as the add-on's `deploy.bat` (which materials get depth, how deep).

Light shafts, the lens flare, ambient occlusion and water glint are only shown in the game. The preview is an
approximation, close but not identical to the game.

## Materials

The Material Editor can switch materials to the add-on's shaders: **Relief** (`Fidelity_Parallax`,
`Fidelity_WorldVertexTransition`), **Water** (`Water_Fidelity`) and **Liquid** for glass props (`Fidelity_Liquid`).
See [mapbase-fidelity materials](texturing.html#mapbase-fidelity-materials).

## Tips

- Make the light right first (sun direction, `light_environment` colours, light entities), then use the Image values
  to finish. Grading cannot fix flat or wrong lighting.
- Small steps go a long way: an exposure of 1.1 or a temperature of 0.1 is already visible.
- Compare in the game. The editor preview has no ambient occlusion and no light shafts, which both darken and brighten
  parts of the picture.
