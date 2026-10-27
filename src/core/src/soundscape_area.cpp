// soundscape_area.cpp - Sphere or block area of soundscape entities (see soundscape_area.h).

#include <sme/soundscape_area.h>

#include <sme/entity_catalog.h>
#include <sme/shapes.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sme {

namespace {

constexpr double BLOCK_FALLBACK = 256;

/// True for a trigger_soundscape whose "soundscape" key names `name`.
bool triggersSoundscape(const Entity& entity, const std::string& name) {
	return !name.empty() && iequals(entity.classname(), "trigger_soundscape") && iequals(entity.get("soundscape"), name);
}

} // namespace

bool isSoundscapeEntity(const Entity& entity) {
	const std::string cls = toLower(entity.classname());
	return cls == "env_soundscape" || cls == "env_soundscape_triggerable" || cls == "env_soundscape_proxy";
}

double soundscapeRadius(const Entity& entity) {
	const std::string text = entity.get("radius");
	if (text.empty()) {
		return DEFAULT_SOUNDSCAPE_RADIUS;
	}
	char* end = nullptr;
	const double value = std::strtod(text.c_str(), &end);
	return end == text.c_str() ? DEFAULT_SOUNDSCAPE_RADIUS : value;
}

const Entity* soundscapeTrigger(const MapDocument& doc, const Entity& soundscape) {
	const std::string name = soundscape.get("targetname");
	for (const auto& entity : doc.entities) {
		if (triggersSoundscape(entity, name)) return &entity;
	}
	return nullptr;
}

Bounds soundscapeBlockFor(const Entity& soundscape) {
	double reach = soundscapeRadius(soundscape);
	if (reach < 16) reach = BLOCK_FALLBACK;   // -1 (everywhere) or tiny: a usable block
	const Vec3 o = soundscape.origin();
	Bounds box;
	box.add(o - Vec3{reach, reach, reach});
	box.add(o + Vec3{reach, reach, reach});
	return box;
}

namespace edit {

Change setSoundscapeRadius(MapDocument& doc, int entityId, double radius) {
	if (!doc.findEntity(entityId) || entityId == 0) {
		return {};
	}
	const double value = radius < 0 ? SOUNDSCAPE_EVERYWHERE : std::max(1.0, std::round(radius));
	Transaction t(doc, "Soundscape radius");
	t.touchEntity(entityId);
	doc.findEntity(entityId)->set("radius", formatNumber(value));
	return t.finish();
}

std::pair<Change, int> makeSoundscapeBlock(MapDocument& doc, int entityId, const Bounds& requested, const EntityClass* triggerClass) {
	Entity* soundscape = doc.findEntity(entityId);
	if (!soundscape || entityId == 0 || !isSoundscapeEntity(*soundscape)) {
		return {Change{}, 0};
	}
	const Bounds box = requested.empty() ? soundscapeBlockFor(*soundscape) : requested;
	if (box.size().x < 1 || box.size().y < 1 || box.size().z < 1) {
		return {Change{}, 0};
	}
	ShapeOptions block;   // a plain box
	auto solids = makeShape(doc, box, block, "TOOLS/TOOLSTRIGGER");
	if (solids.size() != 1) {
		return {Change{}, 0};
	}
	Transaction t(doc, "Soundscape block");
	t.touchEntity(entityId);
	soundscape = doc.findEntity(entityId);
	// The trigger finds its soundscape by name.
	std::string name = soundscape->get("targetname");
	if (name.empty()) {
		const std::string base = "soundscape_" + std::to_string(entityId);
		name = base;
		for (int n = 2; std::any_of(doc.entities.begin(), doc.entities.end(), [&](const Entity& e) { return iequals(e.get("targetname"), name); }); ++n) {
			name = base + "_" + std::to_string(n);
		}
		soundscape->set("targetname", name);
	}
	// Only env_soundscape_triggerable listens to triggers (a proxy keeps its class).
	if (iequals(soundscape->classname(), "env_soundscape")) {
		soundscape->set("classname", "env_soundscape_triggerable");
	}
	int triggerId = 0;
	for (auto& entity : doc.entities) {
		if (triggersSoundscape(entity, name)) {
			triggerId = entity.id;
			t.touchEntity(triggerId);
			entity.solids = std::move(solids);
			break;
		}
	}
	if (triggerId == 0) {
		Entity trigger;
		trigger.id = doc.newEntityId();
		t.touchEntity(trigger.id);
		trigger.set("classname", "trigger_soundscape");
		if (triggerClass) {
			for (const auto& property : triggerClass->properties) {
				if (property.kind == EntityProperty::Kind::Plain && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
					trigger.set(property.name, property.defaultValue);
				}
			}
		}
		trigger.set("soundscape", name);
		trigger.solids = std::move(solids);
		triggerId = trigger.id;
		doc.entities.push_back(std::move(trigger));
	}
	return {t.finish(), triggerId};
}

Change removeSoundscapeBlock(MapDocument& doc, int entityId) {
	const Entity* soundscape = doc.findEntity(entityId);
	if (!soundscape || entityId == 0) {
		return {};
	}
	Transaction t(doc, "Soundscape sphere");
	const std::string name = soundscape->get("targetname");
	std::vector<int> triggers;
	for (const auto& entity : doc.entities) {
		if (triggersSoundscape(entity, name)) triggers.push_back(entity.id);
	}
	for (const int id : triggers) {
		t.touchEntity(id);
		std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
	}
	t.touchEntity(entityId);
	Entity* changed = doc.findEntity(entityId);
	if (iequals(changed->classname(), "env_soundscape_triggerable")) {
		changed->set("classname", "env_soundscape");
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
