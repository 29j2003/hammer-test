// doors.h - Doors made simple: turn a block into a working door and read/write how it opens.
//
// Source has three door classes. func_door slides a brush along "movedir" by its own size
// (minus "lip"); func_door_rotating turns a brush around its "origin" key by "distance"
// degrees (no origin brush needed); prop_door_rotating turns a door model around its origin,
// which sits at the hinge. Getting the keys right by hand (movedir angles, the sign of the
// rotation, spawnflags, the hinge origin, sounds) is what makes doors a pain in Hammer.
//
// A door is described by DoorSettings (how it opens, how fast, how long it stays open,
// locked, sounds) and the helpers translate between those and the keys. "Left" and "right"
// and "front" refer to the door's own frame: the thin side of its box is its front/back,
// "right" is to the right of someone looking at the front. The viewport preview draws the
// motion from doorMotion(), so the user sees which way a door opens instead of guessing.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/editing.h>

namespace sme {

/// Which door class (what it is made of and how it moves).
enum class DoorType {
	Sliding,    ///< func_door: a brush that slides
	Swinging,   ///< func_door_rotating: a brush that turns around a hinge
	Prop,       ///< prop_door_rotating: a door model that turns around its hinge
};

/// Hinge edge of a swinging door, seen from its front.
enum class DoorHinge { Left, Right };
/// Which way a swinging door turns: away from whoever opens it, or always to one side.
enum class DoorSwing { AwayFromPlayer, Front, Back };
/// Where a sliding door goes (Other: a "movedir" that is none of these; it is kept).
enum class DoorSlide { Up, Down, Left, Right, Other };

/// Everything the Door helper and the Door box edit.
struct DoorSettings {
	DoorType type = DoorType::Swinging;
	DoorHinge hinge = DoorHinge::Left;
	DoorSwing swing = DoorSwing::AwayFromPlayer;
	DoorSlide slide = DoorSlide::Up;
	double angle = 90;        ///< Degrees a swinging door turns
	double speed = 100;       ///< Units (sliding) or degrees (swinging) per second
	double closeAfter = 4;    ///< Seconds until it closes again; -1 = stays open
	double lip = 0;           ///< Sliding: units left sticking out when open
	bool locked = false;      ///< Starts locked (an Unlock input opens it up)
	bool useOpens = true;     ///< Brush doors: +use opens it (props always do)
	bool touchOpens = false;  ///< Brush doors: walking into it opens it
	bool toggle = false;      ///< Brush doors: stays until triggered again (no closeAfter)
	bool doubleDoor = false;  ///< Make door: two halves that open together
	std::string model;        ///< Prop: door model
	int hardware = 1;         ///< Prop: 0 none, 1 lever, 2 push bar, 3 keypad
	/// Sounds by slot (see doorSoundSlots); empty = none / the model's own.
	std::vector<std::string> sounds;
};

/// A sound slot of a door class: the label shown and the keys it writes (all get the value).
struct DoorSoundSlot {
	std::string label;
	std::vector<std::string> keys;
};

/// The door's frame from its (closed) box: the thin horizontal axis is front/back.
struct DoorFrame {
	Vec3 front;      ///< Unit, horizontal, along the thin side
	Vec3 right;      ///< Unit, horizontal: up x front, the right hand of someone looking at the front
	double width = 0;
	double thickness = 0;
	double height = 0;
	Bounds box;
};

/// The motion the viewport preview draws for a selected door.
struct DoorMotion {
	DoorType type = DoorType::Sliding;
	Bounds closed;            ///< Box of the closed door (world space; props: local box placed by the angles)
	Vec3 hinge;               ///< Rotation point (swinging and prop doors)
	Vec3 leaf;                ///< Unit, horizontal: from the hinge to the free edge
	double leafLength = 0;    ///< Hinge to free edge
	std::vector<double> turns;   ///< Swinging/prop: yaw changes shown (two when it opens both ways), degrees CCW from above
	Vec3 slide;               ///< Sliding: offset of the open position
	std::vector<Vec3> corners;   ///< Prop: the 8 corners of the closed model box (world space); else the box corners
};

/// func_door, func_door_rotating or prop_door_rotating.
[[nodiscard]] bool isDoor(const Entity& entity);
/// The type of a door class, nullopt for other classes.
[[nodiscard]] std::optional<DoorType> doorType(std::string_view classname);
[[nodiscard]] std::string doorClass(DoorType type);

/// Frame of a box (see DoorFrame).
[[nodiscard]] DoorFrame doorFrame(const Bounds& box);
/// Box of all brushes of an entity (empty if it has none).
[[nodiscard]] Bounds entityBrushBounds(const Entity& entity);
/// The hinge point of a box: the left or right edge, centered in thickness and height.
[[nodiscard]] Vec3 hingePoint(const DoorFrame& frame, DoorHinge hinge);

/// "movedir" angles for a direction ("-90 0 0" up, "90 0 0" down, "0 yaw 0" level).
[[nodiscard]] std::string moveDirAngles(const Vec3& direction);
/// Unit direction of "movedir" angles (Source: pitch down positive). nullopt if unreadable.
[[nodiscard]] std::optional<Vec3> moveDirVector(std::string_view angles);
/// Offset a func_door moves when it opens: along `direction` by its size minus 2 and the lip
/// (CBaseDoor::Spawn).
[[nodiscard]] Vec3 slideOffset(const Bounds& box, const Vec3& direction, double lip);
/// World direction of a slide in a frame (zero for Other).
[[nodiscard]] Vec3 slideDirection(const DoorFrame& frame, DoorSlide slide);

/// Sound slots of a door type (brush doors: noise1 ...; props: sound...override).
[[nodiscard]] std::vector<DoorSoundSlot> doorSoundSlots(DoorType type);
/// Whether a sound script name is meant for doors (shown first in the sound lists).
[[nodiscard]] bool isDoorSound(std::string_view name);
/// Fitting door sounds for each slot of a brush door from the game's sound names (empty where
/// nothing fits). Props keep their model's own sounds, so this is empty for them.
[[nodiscard]] std::vector<std::string> defaultDoorSounds(DoorType type, const std::vector<std::string>& soundNames);
/// Door models among the game's models ("door" in the name, not frames or handles).
[[nodiscard]] std::vector<std::string> doorModels(const std::vector<std::string>& modelNames);

/// Reads the settings from a door's keys (a prop's hinge comes with its model: always Left).
[[nodiscard]] DoorSettings readDoor(const Entity& door);
/// What the preview draws (see DoorMotion). `modelBounds`: local box of a prop's model if known.
[[nodiscard]] std::optional<DoorMotion> doorMotion(const Entity& door, const Bounds* modelBounds = nullptr);

/// Seconds a door takes to open fully (for the "Test open" animation), at least 0.1.
[[nodiscard]] double doorTravelTime(const DoorMotion& motion, double speed);

/// A free name like "door_1" for a new entity of this class (prefix from the class: door,
/// button, relay, sound, light, trigger, ...).
[[nodiscard]] std::string uniqueTargetName(const MapDocument& doc, std::string_view classname);

/// The func_areaportal touching a door's box, or nullptr.
[[nodiscard]] const Entity* doorAreaportal(const MapDocument& doc, const Entity& door);
/// Whether that areaportal is linked to the door (its "target" names the door).
[[nodiscard]] bool areaportalLinked(const Entity& areaportal, const Entity& door);

namespace edit {

/// Makes a door of the selected world brushes (one undo step "Make door"): brush doors keep
/// the brushes (a double door cuts them in the middle into two halves sharing one name); a
/// prop door replaces them with the model standing at the hinge. `modelBounds` is the local box
/// of the prop's model (yaw and height are fitted to it; without it HL2's convention is used:
/// the leaf along local +Y). `definition` (optional) supplies FGD defaults. Returns the new
/// door ids (empty if nothing could be made).
[[nodiscard]] std::pair<Change, std::vector<int>> makeDoor(MapDocument& doc, const Selection& selection, const DoorSettings& settings,
                                                           const EntityClass* definition = nullptr, const Bounds* modelBounds = nullptr);

/// Writes the settings to an existing door ("Edit door"): keys, spawnflags bits (other bits
/// stay), the hinge origin of swinging doors. A brush door switches between func_door and
/// func_door_rotating when its type changes; props stay props.
[[nodiscard]] Change setDoor(MapDocument& doc, int doorId, const DoorSettings& settings);

/// Links the areaportal to the door (names the door if it has no name). One undo step.
[[nodiscard]] Change linkAreaportal(MapDocument& doc, int doorId, int areaportalId);

} // namespace edit

} // namespace sme
