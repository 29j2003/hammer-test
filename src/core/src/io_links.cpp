// io_links.cpp - Ranked output/input pairs and one-step connections (see io_links.h).

#include <sme/io_links.h>

#include <sme/doors.h>
#include <sme/text.h>

#include <algorithm>
#include <string_view>

namespace sme {

namespace {

/// Preferred names for a class (exact name) or a family ("trigger_" = every trigger_*).
struct Preference {
	std::string_view classes;
	std::vector<std::string_view> names;
};

const std::vector<Preference>& outputPreferences() {
	static const std::vector<Preference> table = {
		{"trigger_once", {"OnTrigger", "OnStartTouch"}},
		{"trigger_", {"OnStartTouch", "OnTrigger", "OnEndTouch"}},
		{"func_button", {"OnPressed", "OnUseLocked"}},
		{"func_rot_button", {"OnPressed", "OnUseLocked"}},
		{"logic_relay", {"OnTrigger"}},
		{"logic_auto", {"OnMapSpawn", "OnNewGame"}},
		{"logic_timer", {"OnTimer"}},
		{"math_counter", {"OnHitMax", "OnHitMin"}},
		{"func_door", {"OnFullyOpen", "OnOpen", "OnFullyClosed", "OnClose"}},
		{"func_door_rotating", {"OnFullyOpen", "OnOpen", "OnFullyClosed", "OnClose"}},
		{"prop_door_rotating", {"OnFullyOpen", "OnOpen", "OnFullyClosed", "OnClose"}},
		{"func_breakable", {"OnBreak"}},
		{"prop_physics", {"OnBreak", "OnPhysGunPickup"}},
		{"npc_", {"OnDeath"}},
		{"", {"OnTrigger", "OnPressed", "OnStartTouch", "OnUser1"}},
	};
	return table;
}

const std::vector<Preference>& inputPreferences() {
	static const std::vector<Preference> table = {
		{"func_door", {"Open", "Toggle", "Close", "Unlock", "Lock"}},
		{"func_door_rotating", {"Open", "Toggle", "Close", "Unlock", "Lock"}},
		{"prop_door_rotating", {"Open", "Toggle", "Close", "Unlock", "Lock"}},
		{"ambient_generic", {"PlaySound", "ToggleSound", "StopSound"}},
		{"light", {"Toggle", "TurnOn", "TurnOff"}},
		{"light_", {"Toggle", "TurnOn", "TurnOff"}},
		{"logic_relay", {"Trigger", "Enable", "Disable"}},
		{"logic_timer", {"Enable", "Disable"}},
		{"math_counter", {"Add", "Subtract"}},
		{"env_explosion", {"Explode"}},
		{"env_shake", {"StartShake"}},
		{"env_fade", {"Fade"}},
		{"env_spark", {"SparkOnce", "StartSpark"}},
		{"point_template", {"ForceSpawn"}},
		{"env_entity_maker", {"ForceSpawn"}},
		{"func_brush", {"Toggle", "Enable", "Disable"}},
		{"trigger_", {"Enable", "Disable", "Toggle"}},
		{"func_areaportal", {"Open", "Close", "Toggle"}},
		{"", {"Trigger", "Toggle", "Open", "Enable", "TurnOn", "PlaySound", "Start", "Use"}},
	};
	return table;
}

/// The class's own preferences (exact name, else the family it starts with) and then the general ones.
std::vector<std::string_view> preferencesFor(const std::vector<Preference>& table, const std::string& classname) {
	const std::string cls = toLower(classname);
	std::vector<std::string_view> out;
	const Preference* best = nullptr;
	for (const auto& entry : table) {
		if (entry.classes.empty()) continue;
		if (entry.classes == cls) {
			best = &entry;
			break;
		}
		if (entry.classes.ends_with('_') && cls.starts_with(entry.classes) && (!best || entry.classes.size() > best->classes.size())) {
			best = &entry;
		}
	}
	if (best) out = best->names;
	for (const auto& name : table.back().names) {
		if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
	}
	return out;
}

/// Names the class has, preferred ones first, then the others in FGD order. Without a class
/// the preferences themselves are the guess.
std::vector<std::string> ranked(const std::vector<Preference>& table, const std::string& classname, const std::vector<EntityIo>* io) {
	const auto preferred = preferencesFor(table, classname);
	std::vector<std::string> out;
	if (!io) {
		for (const auto name : preferred) out.emplace_back(name);
		return out;
	}
	for (const auto name : preferred) {
		for (const auto& entry : *io) {
			if (iequals(entry.name, name)) {
				out.push_back(entry.name);
				break;
			}
		}
	}
	for (const auto& entry : *io) {
		if (std::none_of(out.begin(), out.end(), [&](const std::string& n) { return iequals(n, entry.name); })) out.push_back(entry.name);
	}
	return out;
}

} // namespace

std::vector<std::string> rankedOutputs(const std::string& classname, const EntityClass* definition) {
	return ranked(outputPreferences(), classname, definition ? &definition->outputs : nullptr);
}

std::vector<std::string> rankedInputs(const std::string& classname, const EntityClass* definition) {
	return ranked(inputPreferences(), classname, definition ? &definition->inputs : nullptr);
}

std::vector<ConnectionSuggestion> suggestConnections(const std::string& sourceClass, const EntityClass* sourceDefinition, const std::string& targetClass,
                                                     const EntityClass* targetDefinition, std::size_t limit) {
	const auto outputs = rankedOutputs(sourceClass, sourceDefinition);
	const auto inputs = rankedInputs(targetClass, targetDefinition);
	std::vector<ConnectionSuggestion> out;
	// The first few of each side: an output ranked 5th with the best input beats the 2nd output
	// with the 5th input only by order, which is what people expect from the lists.
	for (std::size_t o = 0; o < std::min<std::size_t>(outputs.size(), 4); ++o) {
		for (std::size_t i = 0; i < std::min<std::size_t>(inputs.size(), 6); ++i) {
			out.push_back({outputs[o], inputs[i], static_cast<int>(o * 2 + i)});
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
	if (out.size() > limit) out.resize(limit);
	return out;
}

namespace edit {

std::pair<Change, std::string> connect(MapDocument& doc, int sourceId, int targetId, Connection connection) {
	if (sourceId == 0 || targetId == 0 || !doc.findEntity(sourceId) || !doc.findEntity(targetId) || connection.output.empty() || connection.input.empty()) {
		return {Change{}, {}};
	}
	Transaction t(doc, "Connect " + connection.output + " to " + connection.input);
	t.touchEntity(targetId);
	t.touchEntity(sourceId);
	Entity* target = doc.findEntity(targetId);
	std::string name = target->get("targetname");
	if (name.empty()) {
		name = uniqueTargetName(doc, target->classname());
		target->set("targetname", name);
	}
	connection.target = name;
	doc.findEntity(sourceId)->connections.push_back(std::move(connection));
	return {t.finish(), name};
}

} // namespace edit

} // namespace sme
