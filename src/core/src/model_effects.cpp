// model_effects.cpp - Particle effects on models as sequence events and $keyvalues (see model_effects.h).

#include <sme/model_effects.h>

#include <sme/keyvalues.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <utility>

namespace sme {

namespace {

constexpr std::string_view CREATE = "AE_CL_CREATE_PARTICLE_EFFECT";
constexpr std::string_view STOP = "AE_CL_STOP_PARTICLE_EFFECT";

constexpr std::array<std::pair<EffectAttach, std::string_view>, 5> ATTACH_NAMES{{
	{EffectAttach::FollowAttachment, "follow_attachment"},
	{EffectAttach::StartAtAttachment, "start_at_attachment"},
	{EffectAttach::FollowOrigin, "follow_origin"},
	{EffectAttach::StartAtOrigin, "start_at_origin"},
	{EffectAttach::FollowRootBone, "follow_rootbone"},
}};

bool needsAttachment(EffectAttach attach) {
	return attach == EffectAttach::FollowAttachment || attach == EffectAttach::StartAtAttachment;
}

/// A word of an event's options or a keyvalue: no spaces, quotes, braces or control characters
/// (the game splits the options at spaces; a quote would end the .qc string).
std::string cleanWord(std::string_view text) {
	std::string out;
	for (const char c : trim(text)) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 32 || c == ' ' || c == '"' || c == '{' || c == '}' || c == '\\' || u == 127) continue;
		out += c;
	}
	return out;
}

std::vector<std::string> words(std::string_view text) {
	std::vector<std::string> out;
	std::size_t at = 0;
	while (at < text.size()) {
		while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
		const auto start = at;
		while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at]))) ++at;
		if (at > start) out.emplace_back(text.substr(start, at - start));
	}
	return out;
}

bool contains(const std::vector<std::string>& list, std::string_view name) {
	return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return iequals(s, name); });
}

} // namespace

std::string qcEventLine(const SequenceEvent& event) {
	std::string options;
	for (const char c : event.options) {
		const auto u = static_cast<unsigned char>(c);
		if (u >= 32 && c != '"' && u != 127) options += c;
	}
	return "{ event " + cleanWord(event.event) + " " + std::to_string(std::max(0, event.frame)) + " \"" + options + "\" }";
}

std::string_view effectAttachName(EffectAttach attach) {
	for (const auto& [a, name] : ATTACH_NAMES) {
		if (a == attach) return name;
	}
	return "follow_attachment";
}

std::optional<EffectAttach> effectAttachFromName(std::string_view name) {
	for (const auto& [a, n] : ATTACH_NAMES) {
		if (iequals(n, trim(name))) return a;
	}
	return std::nullopt;
}

std::vector<std::string> effectProblems(const ModelEffect& effect, const std::vector<std::string>& attachments, const std::vector<std::string>& sequences,
                                        int frames) {
	std::vector<std::string> out;
	if (cleanWord(effect.particle).empty()) out.emplace_back("Choose a particle system.");
	if (needsAttachment(effect.attach)) {
		if (cleanWord(effect.attachment).empty()) out.emplace_back("Choose an attachment.");
		else if (!attachments.empty() && !contains(attachments, effect.attachment))
			out.push_back("The model has no attachment \"" + effect.attachment + "\".");
	}
	if (effect.when == ModelEffect::When::DuringAnimation) {
		if (cleanWord(effect.sequence).empty()) out.emplace_back("Choose an animation.");
		else if (!sequences.empty() && !contains(sequences, effect.sequence))
			out.push_back("The model has no animation \"" + effect.sequence + "\".");
		if (effect.startFrame < 0) out.emplace_back("The start frame cannot be below 0.");
		if (frames > 0 && effect.startFrame >= frames) out.push_back("The start frame is after the end of the animation (" + std::to_string(frames) + " frames).");
		if (effect.endFrame >= 0 && effect.endFrame < effect.startFrame) out.emplace_back("The effect stops before it starts.");
		if (frames > 0 && effect.endFrame >= frames) out.push_back("The stop frame is after the end of the animation (" + std::to_string(frames) + " frames).");
	}
	return out;
}

std::vector<SequenceEvent> effectEvents(const ModelEffect& effect) {
	std::vector<SequenceEvent> out;
	if (effect.when != ModelEffect::When::DuringAnimation) return out;
	const std::string particle = cleanWord(effect.particle);
	const std::string sequence = cleanWord(effect.sequence);
	if (particle.empty() || sequence.empty()) return out;
	std::string options = particle + " " + std::string(effectAttachName(effect.attach));
	if (needsAttachment(effect.attach)) {
		const std::string attachment = cleanWord(effect.attachment);
		if (attachment.empty()) return out;
		options += " " + attachment;
	}
	out.push_back({sequence, std::max(0, effect.startFrame), std::string(CREATE), options});
	if (effect.endFrame >= 0) {
		out.push_back({sequence, std::max(effect.endFrame, std::max(0, effect.startFrame)), std::string(STOP), particle + (effect.stopInstantly ? " instantly" : "")});
	}
	return out;
}

std::vector<SequenceEvent> effectEvents(const std::vector<ModelEffect>& effects) {
	std::vector<SequenceEvent> out;
	for (const auto& effect : effects) {
		auto events = effectEvents(effect);
		out.insert(out.end(), events.begin(), events.end());
	}
	std::stable_sort(out.begin(), out.end(), [](const SequenceEvent& a, const SequenceEvent& b) {
		const auto la = toLower(a.sequence), lb = toLower(b.sequence);
		return la != lb ? la < lb : a.frame < b.frame;
	});
	return out;
}

std::string effectKeyValues(const std::vector<ModelEffect>& effects) {
	std::string body;
	for (const auto& effect : effects) {
		if (effect.when != ModelEffect::When::Always) continue;
		const std::string particle = cleanWord(effect.particle);
		if (particle.empty()) continue;
		body += "\t\teffect\n\t\t{\n";
		body += "\t\t\tname \"" + particle + "\"\n";
		body += "\t\t\tattachment_type \"" + std::string(effectAttachName(effect.attach)) + "\"\n";
		const std::string attachment = needsAttachment(effect.attach) ? cleanWord(effect.attachment) : std::string();
		if (!attachment.empty()) body += "\t\t\tattachment_point \"" + attachment + "\"\n";
		body += "\t\t}\n";
	}
	if (body.empty()) return {};
	return "\tparticles\n\t{\n" + body + "\t}\n";
}

std::vector<ModelEffect> effectsFromModel(const std::vector<SequenceEvent>& events, std::string_view keyValues) {
	std::vector<ModelEffect> out;
	// Creates first (in order), then each stop closes the newest open create of its system in
	// the same sequence.
	for (const auto& e : events) {
		if (!iequals(e.event, CREATE)) continue;
		const auto w = words(e.options);
		if (w.empty()) continue;
		ModelEffect effect;
		effect.particle = w[0];
		effect.when = ModelEffect::When::DuringAnimation;
		effect.sequence = e.sequence;
		effect.startFrame = e.frame;
		if (w.size() > 1) effect.attach = effectAttachFromName(w[1]).value_or(EffectAttach::FollowAttachment);
		if (w.size() > 2) effect.attachment = w[2];
		out.push_back(std::move(effect));
	}
	for (const auto& e : events) {
		if (!iequals(e.event, STOP)) continue;
		const auto w = words(e.options);
		if (w.empty()) continue;
		ModelEffect* best = nullptr;
		for (auto& effect : out) {
			if (effect.endFrame >= 0 || !iequals(effect.particle, w[0]) || !iequals(effect.sequence, e.sequence) || effect.startFrame > e.frame) continue;
			if (!best || effect.startFrame > best->startFrame) best = &effect;
		}
		if (best) {
			best->endFrame = e.frame;
			best->stopInstantly = w.size() > 1 && iequals(w[1], "instantly");
		}
	}
	if (!trim(keyValues).empty()) {
		try {
			const KvNode root = parseKeyValues(keyValues);
			const auto visit = [&](const KvNode& block) {
				for (const auto& effect : block.children) {
					if (!effect.block) continue;
					ModelEffect m;
					m.when = ModelEffect::When::Always;
					m.particle = effect.get("name");
					m.attach = effectAttachFromName(effect.get("attachment_type")).value_or(EffectAttach::FollowAttachment);
					m.attachment = effect.get("attachment_point");
					if (!m.particle.empty()) out.push_back(std::move(m));
				}
			};
			for (const auto& top : root.children) {
				if (!top.block) continue;
				if (iequals(top.key, "particles")) visit(top);
				// $keyvalues as a whole ("mdlkeyvalue { particles { ... } }") or wrapped once.
				for (const auto& inner : top.children) {
					if (inner.block && iequals(inner.key, "particles")) visit(inner);
				}
			}
		} catch (const std::exception&) {
			// Broken text: no Always effects.
		}
	}
	return out;
}

std::string_view eventNameForNumber(int number) {
	static const std::map<int, std::string_view> names{
		{1000, "SCRIPT_EVENT_DEAD"},          {1001, "SCRIPT_EVENT_NOINTERRUPT"}, {1002, "SCRIPT_EVENT_CANINTERRUPT"}, {1003, "SCRIPT_EVENT_FIREEVENT"},
		{1004, "SCRIPT_EVENT_SOUND"},         {1005, "SCRIPT_EVENT_SENTENCE"},    {1006, "SCRIPT_EVENT_INAIR"},         {1007, "SCRIPT_EVENT_ENDANIMATION"},
		{1008, "SCRIPT_EVENT_SOUND_VOICE"},   {1010, "SCRIPT_EVENT_NOT_DEAD"},    {1011, "SCRIPT_EVENT_EMPHASIS"},      {1020, "SCRIPT_EVENT_BODYGROUPON"},
		{1021, "SCRIPT_EVENT_BODYGROUPOFF"},  {1022, "SCRIPT_EVENT_BODYGROUPTEMP"}, {1100, "SCRIPT_EVENT_FIRE_INPUT"},
		{2001, "AE_NPC_BODYDROP_LIGHT"},      {2002, "AE_NPC_BODYDROP_HEAVY"},    {2010, "AE_NPC_SWISHSOUND"},          {2020, "AE_NPC_180TURN"},
		{2040, "AE_NPC_ITEM_PICKUP"},         {2041, "AE_NPC_WEAPON_DROP"},       {2042, "AE_NPC_WEAPON_SET_SEQUENCE_NAME"},
		{2043, "AE_NPC_WEAPON_SET_SEQUENCE_NUMBER"}, {2044, "AE_NPC_WEAPON_SET_ACTIVITY"}, {2050, "AE_NPC_LEFTFOOT"}, {2051, "AE_NPC_RIGHTFOOT"},
		{5001, "CL_EVENT_MUZZLEFLASH0"},      {5011, "CL_EVENT_MUZZLEFLASH1"},    {5021, "CL_EVENT_MUZZLEFLASH2"},      {5031, "CL_EVENT_MUZZLEFLASH3"},
		{5002, "CL_EVENT_SPARK0"},            {5003, "CL_EVENT_NPC_MUZZLEFLASH0"}, {5013, "CL_EVENT_NPC_MUZZLEFLASH1"}, {5023, "CL_EVENT_NPC_MUZZLEFLASH2"},
		{5033, "CL_EVENT_NPC_MUZZLEFLASH3"},  {5004, "AE_CL_PLAYSOUND"},          {6001, "CL_EVENT_EJECTBRASS1"},       {6002, "CL_EVENT_SPRITEGROUP_CREATE"},
		{6003, "CL_EVENT_SPRITEGROUP_DESTROY"}, {6004, "CL_EVENT_FOOTSTEP_LEFT"}, {6005, "CL_EVENT_FOOTSTEP_RIGHT"},  {6006, "CL_EVENT_MFOOTSTEP_LEFT"},
		{6007, "CL_EVENT_MFOOTSTEP_RIGHT"},   {6008, "CL_EVENT_MFOOTSTEP_LEFT_LOUD"}, {6009, "CL_EVENT_MFOOTSTEP_RIGHT_LOUD"},
		{9001, "CL_EVENT_DISPATCHEFFECT0"},   {9011, "CL_EVENT_DISPATCHEFFECT1"}, {9021, "CL_EVENT_DISPATCHEFFECT2"},   {9031, "CL_EVENT_DISPATCHEFFECT3"},
		{9041, "CL_EVENT_DISPATCHEFFECT4"},   {9051, "CL_EVENT_DISPATCHEFFECT5"}, {9061, "CL_EVENT_DISPATCHEFFECT6"},   {9071, "CL_EVENT_DISPATCHEFFECT7"},
		{9081, "CL_EVENT_DISPATCHEFFECT8"},   {9091, "CL_EVENT_DISPATCHEFFECT9"},
	};
	const auto it = names.find(number);
	return it == names.end() ? std::string_view{} : it->second;
}

} // namespace sme
