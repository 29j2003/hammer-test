// model_events.cpp - Reading animation events from .mdl files and writing them as .qc lines
// (see model_events.h).

#include <sme/model_events.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sme {

namespace {

// studiohdr_t / mstudioseqdesc_t / mstudioanimdesc_t / mstudioevent_t offsets (public/studio.h).
constexpr long long HDR_SIZE = 408;
constexpr long long HDR_NUM_ANIMS = 180, HDR_ANIM_INDEX = 184;
constexpr long long HDR_NUM_SEQS = 188, HDR_SEQ_INDEX = 192;
constexpr long long SEQ_SIZE = 212, ANIMDESC_SIZE = 100, EVENT_SIZE = 80;
constexpr int MAX_SEQUENCES = 1 << 15;
constexpr int MAX_EVENTS = 1024;   // Per sequence; studiomdl allows far fewer (MAXSTUDIOEVENTS 1024 in later versions)

class Bytes {
public:
	explicit Bytes(std::span<const std::byte> data) : m_data(data) {}
	[[nodiscard]] bool fits(long long offset, long long size) const {
		return offset >= 0 && size >= 0 && static_cast<unsigned long long>(offset) <= m_data.size() &&
		       static_cast<unsigned long long>(size) <= m_data.size() - static_cast<std::size_t>(offset);
	}
	[[nodiscard]] std::int32_t i32(long long offset) const {
		std::int32_t v = 0;
		if (fits(offset, 4)) std::memcpy(&v, m_data.data() + offset, 4);
		return v;
	}
	[[nodiscard]] std::int16_t i16(long long offset) const {
		std::int16_t v = 0;
		if (fits(offset, 2)) std::memcpy(&v, m_data.data() + offset, 2);
		return v;
	}
	[[nodiscard]] float f32(long long offset) const {
		float v = 0;
		if (fits(offset, 4)) std::memcpy(&v, m_data.data() + offset, 4);
		return std::isfinite(v) ? v : 0.f;
	}
	/// A zero-terminated string of at most `limit` bytes.
	[[nodiscard]] std::string str(long long offset, std::size_t limit = 260) const {
		std::string out;
		for (long long i = offset; fits(i, 1) && out.size() < limit; ++i) {
			const char c = static_cast<char>(m_data[static_cast<std::size_t>(i)]);
			if (c == '\0') break;
			out.push_back(c);
		}
		return out;
	}

private:
	std::span<const std::byte> m_data;
};

bool iequal(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	       });
}

/// Printable text without characters that would break a .qc line.
std::string printable(std::string_view text, std::size_t limit) {
	std::string out;
	for (const char c : text) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 32 || u == 127 || c == '"' || c == '{' || c == '}') continue;
		if (out.size() >= limit) break;
		out.push_back(c);
	}
	return out;
}

// The events HL2/Mapbase code reacts to (game/shared/eventlist.h, game/client/cl_animevent.h,
// game/shared/npcevent.h, game/shared/scriptevent.h).
constexpr std::array KNOWN = {
	KnownModelEvent{"AE_CL_PLAYSOUND", 5004, "Plays a sound (sound script name or wave) on the client", true},
	KnownModelEvent{"AE_SV_PLAYSOUND", 0, "Plays a sound on the server (heard by NPCs too)", true},
	KnownModelEvent{"AE_CL_STOPSOUND", 0, "Stops a sound started by the animation", true},
	KnownModelEvent{"AE_WPN_PLAYWPNSOUND", 0, "Plays a sound slot of the weapon script (\"reload\", \"special1\" ...)", false},
	KnownModelEvent{"CL_EVENT_MUZZLEFLASH0", 5001, "Muzzle flash on attachment 1 (first person)", false},
	KnownModelEvent{"CL_EVENT_MUZZLEFLASH1", 5011, "Muzzle flash on attachment 2 (first person)", false},
	KnownModelEvent{"CL_EVENT_NPC_MUZZLEFLASH0", 5003, "Muzzle flash on attachment 1 (third person)", false},
	KnownModelEvent{"CL_EVENT_SPARK0", 5002, "Sparks on attachment 1", false},
	KnownModelEvent{"CL_EVENT_EJECTBRASS1", 6001, "Ejects a shell from attachment 1 (options: shell type)", false},
	KnownModelEvent{"CL_EVENT_DISPATCHEFFECT0", 9001, "Runs a client effect on attachment 1 (options: effect name)", false},
	KnownModelEvent{"CL_EVENT_FOOTSTEP_LEFT", 6004, "Left footstep sound", false},
	KnownModelEvent{"CL_EVENT_FOOTSTEP_RIGHT", 6005, "Right footstep sound", false},
	KnownModelEvent{"AE_MUZZLEFLASH", 0, "Muzzle flash of a weapon held by the player", false},
	KnownModelEvent{"AE_NPC_MUZZLEFLASH", 0, "Muzzle flash of a weapon held by an NPC", false},
	KnownModelEvent{"AE_CL_CREATE_PARTICLE_EFFECT", 0, "Starts a particle system (options: name, how it follows, attachment)", false},
	KnownModelEvent{"AE_CL_STOP_PARTICLE_EFFECT", 0, "Stops a particle system started by the animation", false},
	KnownModelEvent{"AE_CL_ADD_PARTICLE_EFFECT_CP", 0, "Adds a control point to a particle system", false},
	KnownModelEvent{"AE_CLIENT_EFFECT_ATTACH", 0, "Runs a client effect on an attachment", false},
	KnownModelEvent{"AE_CL_ENABLE_BODYGROUP", 0, "Shows a body group", false},
	KnownModelEvent{"AE_CL_DISABLE_BODYGROUP", 0, "Hides a body group", false},
	KnownModelEvent{"AE_CL_BODYGROUP_SET_VALUE", 0, "Sets a body group to an option (options: group value)", false},
	KnownModelEvent{"AE_WPN_PRIMARYATTACK", 0, "The weapon fires at this point (grenades, melee)", false},
	KnownModelEvent{"AE_WPN_INCREMENTAMMO", 0, "Adds ammo during a reload (shotgun shells)", false},
	KnownModelEvent{"AE_WPN_HIDE", 0, "Hides the player's weapon", false},
	KnownModelEvent{"AE_WPN_UNHIDE", 0, "Shows the player's weapon again", false},
	KnownModelEvent{"EVENT_WEAPON_RELOAD", 3015, "An NPC's weapon is reloaded", false},
	KnownModelEvent{"EVENT_WEAPON_RELOAD_SOUND", 3017, "An NPC's reload sound", false},
	KnownModelEvent{"EVENT_WEAPON_RELOAD_FILL_CLIP", 3018, "An NPC's clip is filled", false},
	KnownModelEvent{"EVENT_WEAPON_SMG1", 3002, "An NPC fires the SMG", false},
	KnownModelEvent{"EVENT_WEAPON_AR2", 3007, "An NPC fires the AR2", false},
	KnownModelEvent{"EVENT_WEAPON_SHOTGUN_FIRE", 3004, "An NPC fires the shotgun", false},
	KnownModelEvent{"EVENT_WEAPON_MELEE_HIT", 3001, "An NPC's melee hit", false},
	KnownModelEvent{"EVENT_WEAPON_THROW", 3005, "An NPC throws a grenade", false},
	KnownModelEvent{"EVENT_WEAPON_SEQUENCE_FINISHED", 3900, "The weapon's animation is done", false},
	KnownModelEvent{"SCRIPT_EVENT_SOUND", 1004, "Plays a wave on CHAN_BODY (scripted sequences)", true},
	KnownModelEvent{"SCRIPT_EVENT_SOUND_VOICE", 1008, "Plays a wave on CHAN_VOICE (scripted sequences)", true},
	KnownModelEvent{"SCRIPT_EVENT_FIREEVENT", 1003, "Fires OnScriptEventNN of the scripted_sequence (options: NN)", false},
	KnownModelEvent{"SCRIPT_EVENT_BODYGROUPON", 1020, "Turns a body group on", false},
	KnownModelEvent{"SCRIPT_EVENT_BODYGROUPOFF", 1021, "Turns a body group off", false},
	KnownModelEvent{"AE_NPC_LEFTFOOT", 2050, "NPC left footstep", false},
	KnownModelEvent{"AE_NPC_RIGHTFOOT", 2051, "NPC right footstep", false},
	KnownModelEvent{"AE_NPC_BODYDROP_LIGHT", 2001, "NPC body falls (light)", false},
	KnownModelEvent{"AE_NPC_BODYDROP_HEAVY", 2002, "NPC body falls (heavy)", false},
	KnownModelEvent{"AE_NPC_SWISHSOUND", 2010, "NPC swish sound", false},
	KnownModelEvent{"AE_NPC_WEAPON_FIRE", 0, "An NPC fires its weapon", false},
	KnownModelEvent{"AE_NPC_HOLSTER", 0, "An NPC puts its weapon away", false},
	KnownModelEvent{"AE_NPC_DRAW", 0, "An NPC draws its weapon", false},
	KnownModelEvent{"AE_NPC_RESPONSE", 0, "An NPC speaks a response concept (options: concept)", false},
	KnownModelEvent{"AE_RAGDOLL", 0, "Turns the model into a ragdoll", false},
};

} // namespace

std::string ModelEvent::id() const {
	return name.empty() ? std::to_string(number) : name;
}

int ModelEvent::frame(int frames) const {
	if (frames <= 1) return 0;
	return std::clamp(static_cast<int>(std::lround(cycle * float(frames - 1))), 0, frames - 1);
}

void ModelEvent::setFrame(int frame, int frames) {
	cycle = frames > 1 ? std::clamp(float(frame) / float(frames - 1), 0.f, 1.f) : 0.f;
}

std::vector<SequenceEvents> readSequenceEvents(std::span<const std::byte> data) {
	std::vector<SequenceEvents> out;
	const Bytes mdl(data);
	if (!mdl.fits(0, HDR_SIZE) || mdl.i32(0) != 0x54534449) return out;   // "IDST"
	const int numSeqs = mdl.i32(HDR_NUM_SEQS);
	const long long seqIndex = mdl.i32(HDR_SEQ_INDEX);
	if (numSeqs <= 0 || numSeqs > MAX_SEQUENCES || !mdl.fits(seqIndex, numSeqs * SEQ_SIZE)) return out;
	const int numAnims = mdl.i32(HDR_NUM_ANIMS);
	const long long animIndex = mdl.i32(HDR_ANIM_INDEX);
	const bool animsOk = numAnims > 0 && numAnims <= MAX_SEQUENCES && mdl.fits(animIndex, numAnims * ANIMDESC_SIZE);
	out.reserve(std::size_t(numSeqs));
	for (int i = 0; i < numSeqs; ++i) {
		const long long s = seqIndex + i * SEQ_SIZE;
		SequenceEvents seq;
		seq.sequence = mdl.str(s + mdl.i32(s + 4));
		// Frames and fps of the sequence's (first) animation.
		const long long blends = s + mdl.i32(s + 60);
		const int anim = mdl.i16(blends);
		if (animsOk && anim >= 0 && anim < numAnims) {
			const long long desc = animIndex + anim * ANIMDESC_SIZE;
			seq.frames = std::max(1, mdl.i32(desc + 16));
			const float fps = mdl.f32(desc + 8);
			seq.fps = fps > 0 ? fps : 30.f;
		}
		const int count = mdl.i32(s + 24);
		const long long first = s + mdl.i32(s + 28);
		if (count > 0 && count <= MAX_EVENTS && mdl.fits(first, count * EVENT_SIZE)) {
			for (int e = 0; e < count; ++e) {
				const long long at = first + e * EVENT_SIZE;
				ModelEvent event;
				event.cycle = std::clamp(mdl.f32(at), 0.f, 1.f);
				const int number = mdl.i32(at + 4);
				const int type = mdl.i32(at + 8);
				event.options = printable(mdl.str(at + 12, 64), MODEL_EVENT_OPTIONS_MAX);
				const int nameOffset = mdl.i32(at + 76);
				std::string name = (type & MODEL_EVENT_NEW_STYLE) && nameOffset != 0 ? mdl.str(at + nameOffset, 128) : std::string();
				if (!name.empty() && validEventId(name)) {
					event.name = std::move(name);
				} else if (number > 0) {
					event.number = number;
				} else {
					continue;   // Neither a name nor a number: nothing a .qc could say
				}
				seq.events.push_back(std::move(event));
			}
		}
		out.push_back(std::move(seq));
	}
	return out;
}

std::span<const KnownModelEvent> knownModelEvents() {
	return KNOWN;
}

std::string knownEventId(const KnownModelEvent& known) {
	if (known.number != 0 && !known.name.starts_with("AE_")) return std::to_string(known.number);
	return std::string(known.name);
}

const KnownModelEvent* findKnownModelEvent(std::string_view id) {
	if (id.empty()) return nullptr;
	int number = 0;
	const bool numeric = std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
	if (numeric && id.size() < 9) number = std::stoi(std::string(id));
	for (const auto& known : KNOWN) {
		if (numeric ? (number != 0 && known.number == number) : iequal(known.name, id)) return &known;
	}
	return nullptr;
}

std::string modelEventLabel(const ModelEvent& event) {
	if (!event.name.empty()) return event.name;
	if (const auto* known = findKnownModelEvent(std::to_string(event.number))) return std::to_string(event.number) + " " + std::string(known->name);
	return std::to_string(event.number);
}

bool isSoundEvent(const ModelEvent& event) {
	const auto* known = findKnownModelEvent(event.id());
	return known && known->sound;
}

std::string cleanEventOptions(std::string_view options) {
	std::string out = printable(options, MODEL_EVENT_OPTIONS_MAX);
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

bool validEventId(std::string_view id) {
	if (id.empty() || id.size() > 64) return false;
	if (std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
		return id.size() < 9 && std::stoi(std::string(id)) > 0;
	}
	if (std::isdigit(static_cast<unsigned char>(id.front()))) return false;
	return std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

bool setEventId(ModelEvent& event, std::string_view id) {
	if (!validEventId(id)) return false;
	if (std::isdigit(static_cast<unsigned char>(id.front()))) {
		event.number = std::stoi(std::string(id));
		event.name.clear();
	} else {
		event.name = std::string(id);
		event.number = 0;
	}
	return true;
}

std::string qcEventLine(const ModelEvent& event, int frames) {
	std::string line = "{ event " + event.id() + " " + std::to_string(event.frame(frames));
	const std::string options = cleanEventOptions(event.options);
	line += " \"" + options + "\" }";
	return line;
}

} // namespace sme
