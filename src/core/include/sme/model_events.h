// model_events.h - Animation events of a model's sequences (mstudioevent_t): what a sequence
// triggers at a point of its cycle - a sound (AE_CL_PLAYSOUND / 5004 "Weapon_SMG1.Reload"), a
// muzzle flash (5001), an effect on an attachment (9001 "...") or anything a game's code handles
// (AE_NPC_LEFTFOOT, AE_WPN_INCREMENTAMMO, ...). Any event, not only sounds: other editors (effects
// during an animation, particles on an attachment) use the same list.
//
// In the compiled .mdl each sequence (mstudioseqdesc_t, numevents at +24, eventindex at +28)
// points at its events, 80 bytes each (Source SDK 2013 public/studio.h):
//   float cycle; int event; int type; char options[64]; int szeventindex;
// An event written in the .qc by name (`{ event AE_CL_PLAYSOUND 12 "..." }`) has type
// NEW_EVENT_STYLE (1 << 10) and its name at szeventindex (the number is looked up when the game
// loads the model); an event written as a number keeps the number in `event`.
//
// In the .qc an event sits in its $sequence block with a frame instead of a cycle; studiomdl
// stores cycle = frame / (frames - 1). Events are read back the same way (frame = cycle *
// (frames - 1), rounded), so a decompile and recompile keeps them in place.
//
// Files are untrusted: counts and offsets are bounds-checked, broken entries are skipped.

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// NEW_EVENT_STYLE of studio.h: the event is known by its name (AE_...), not its number.
inline constexpr int MODEL_EVENT_NEW_STYLE = 1 << 10;
/// Longest options text the .mdl has room for (char options[64] with its terminating zero).
inline constexpr std::size_t MODEL_EVENT_OPTIONS_MAX = 63;

/// One event of a sequence.
struct ModelEvent {
	float cycle = 0;          ///< 0 = first frame, 1 = last frame
	int number = 0;           ///< Old-style event number (5004, 3015 ...); 0 for a named event
	std::string name;         ///< Named event ("AE_CL_PLAYSOUND"); empty for a numbered one
	std::string options;      ///< The text after the frame: a sound name, an attachment, effect parameters ...
	bool operator==(const ModelEvent&) const = default;

	/// What the .qc names it: the name, else the number ("AE_CL_PLAYSOUND", "5004").
	[[nodiscard]] std::string id() const;
	/// The frame of a sequence with `frames` frames (rounded).
	[[nodiscard]] int frame(int frames) const;
	/// Sets the cycle from a frame of a sequence with `frames` frames.
	void setFrame(int frame, int frames);
};

/// The events of one sequence of a .mdl.
struct SequenceEvents {
	std::string sequence;     ///< Its name
	int frames = 1;           ///< Frames of its animation (for frame <-> cycle)
	float fps = 30;
	std::vector<ModelEvent> events;   ///< In file order (studiomdl sorts them by cycle)
};

/// Every sequence of the .mdl with its events (also sequences without any, in file order).
/// Empty on broken data.
[[nodiscard]] std::vector<SequenceEvents> readSequenceEvents(std::span<const std::byte> mdl);

/// A known event: its name, old number (0 if it has none) and what it does.
struct KnownModelEvent {
	std::string_view name;
	int number = 0;
	std::string_view description;
	bool sound = false;       ///< Its options are a sound (sound script name or wave)
};
/// The events Half-Life 2 / Mapbase code handles that a mapper is likely to meet (client
/// effects, sounds, weapon and NPC events), for lists and descriptions.
[[nodiscard]] std::span<const KnownModelEvent> knownModelEvents();
/// The id a .qc should use for a known event: its name for events the game registers by name
/// (AE_...), else its number (CL_EVENT_*, EVENT_WEAPON_*, SCRIPT_EVENT_* are only defines).
[[nodiscard]] std::string knownEventId(const KnownModelEvent& known);
/// The known event with this id (name or number, case-insensitive), or nullptr.
[[nodiscard]] const KnownModelEvent* findKnownModelEvent(std::string_view id);
/// A readable label: "AE_CL_PLAYSOUND (sound)" style - the name, for a known number its name too.
[[nodiscard]] std::string modelEventLabel(const ModelEvent& event);
/// Whether the event plays its options as a sound.
[[nodiscard]] bool isSoundEvent(const ModelEvent& event);

/// Options text as the .qc and the .mdl can hold it: no quotes, braces or control characters,
/// at most MODEL_EVENT_OPTIONS_MAX bytes.
[[nodiscard]] std::string cleanEventOptions(std::string_view options);
/// Whether an id can be written into a .qc: a positive number, or a name of letters, digits and '_'.
[[nodiscard]] bool validEventId(std::string_view id);
/// Sets name/number from an id ("5004" -> number, "AE_CL_PLAYSOUND" -> name). False if invalid.
bool setEventId(ModelEvent& event, std::string_view id);

/// The .qc line of an event in a $sequence block: `{ event AE_CL_PLAYSOUND 12 "Weapon_SMG1.Reload" }`.
[[nodiscard]] std::string qcEventLine(const ModelEvent& event, int frames);

} // namespace sme
