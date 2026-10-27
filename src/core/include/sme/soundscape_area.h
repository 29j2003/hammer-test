// soundscape_area.h - Where a soundscape entity is heard. An env_soundscape plays its
// soundscape for players inside a sphere around it ("radius"; -1 = everywhere it can see).
// For a block-shaped area the map uses a trigger_soundscape brush instead: it names an
// env_soundscape_triggerable in its "soundscape" key and switches to it when a player enters.
// These helpers read that and switch an entity between the two shapes, each as one undo step.

#pragma once

#include <utility>

#include <sme/editing.h>

namespace sme {

/// The radius the engine uses when the key is missing (env_soundscape's FGD default).
constexpr double DEFAULT_SOUNDSCAPE_RADIUS = 128;
/// Radius value meaning "no limit" (heard everywhere the entity can see).
constexpr double SOUNDSCAPE_EVERYWHERE = -1;

/// env_soundscape, env_soundscape_triggerable or env_soundscape_proxy.
[[nodiscard]] bool isSoundscapeEntity(const Entity& entity);
/// Its "radius" key, or the default when it is missing or not a number.
[[nodiscard]] double soundscapeRadius(const Entity& entity);
/// The first trigger_soundscape whose "soundscape" key names this entity, or nullptr.
[[nodiscard]] const Entity* soundscapeTrigger(const MapDocument& doc, const Entity& soundscape);
/// The box a new trigger gets: a cube around the entity reaching `radius` each way
/// (256 when the radius is unlimited or too small to work with).
[[nodiscard]] Bounds soundscapeBlockFor(const Entity& soundscape);

namespace edit {

/// Sets the radius (rounded to whole units; anything below 1 except -1 becomes 1).
[[nodiscard]] Change setSoundscapeRadius(MapDocument& doc, int entityId, double radius);

/// Block shape: makes the entity an env_soundscape_triggerable (giving it a name if it has none)
/// and puts a trigger_soundscape brush (tools/toolstrigger) around it whose "soundscape" names
/// it. `box` empty = soundscapeBlockFor(). `triggerClass` (optional) supplies FGD defaults.
/// An existing trigger is reused and resized. Returns the trigger's id (0 if nothing happened).
[[nodiscard]] std::pair<Change, int> makeSoundscapeBlock(MapDocument& doc, int entityId, const Bounds& box = {},
                                                         const EntityClass* triggerClass = nullptr);

/// Sphere shape again: removes every trigger_soundscape naming the entity and makes it an
/// env_soundscape. Its radius stays.
[[nodiscard]] Change removeSoundscapeBlock(MapDocument& doc, int entityId);

} // namespace edit

} // namespace sme
