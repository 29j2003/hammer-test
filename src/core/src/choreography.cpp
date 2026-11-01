// choreography.cpp - NPC scenes: plan in the map, helpers, timeline, .vcd (see choreography.h).

#include <sme/choreography.h>

#include <sme/brush.h>
#include <sme/localization.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <numbers>
#include <set>

namespace sme {

namespace {

const char* kindName(ChoreoStep::Kind kind) {
	switch (kind) {
		case ChoreoStep::Kind::Move: return "move";
		case ChoreoStep::Kind::Animation: return "anim";
		case ChoreoStep::Kind::Say: return "say";
		case ChoreoStep::Kind::Wait: return "wait";
		case ChoreoStep::Kind::Face: return "face";
		case ChoreoStep::Kind::SetVariable: return "setvar";
	}
	return "wait";
}

std::optional<ChoreoStep::Kind> kindFromName(std::string_view name) {
	for (const auto kind : {ChoreoStep::Kind::Move, ChoreoStep::Kind::Animation, ChoreoStep::Kind::Say, ChoreoStep::Kind::Wait, ChoreoStep::Kind::Face,
	                        ChoreoStep::Kind::SetVariable}) {
		if (iequals(name, kindName(kind))) return kind;
	}
	return std::nullopt;
}

/// A text field of a step: '%' and '|' escaped, '"' (a VMF value cannot hold it) turned into '\''.
std::string escapeField(std::string_view text) {
	std::string out;
	for (const char c : text) {
		if (c == '%') out += "%25";
		else if (c == '|') out += "%7C";
		else if (c == '"') out += '\'';
		else if (c == '\n' || c == '\r') out += ' ';
		else out += c;
	}
	return out;
}

std::string unescapeField(std::string_view text) {
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '%' && i + 2 < text.size()) {
			const std::string_view code = text.substr(i + 1, 2);
			if (code == "25") { out += '%'; i += 2; continue; }
			if (iequals(code, "7C")) { out += '|'; i += 2; continue; }
		}
		out += text[i];
	}
	return out;
}

std::vector<std::string_view> splitBars(std::string_view text) {
	std::vector<std::string_view> parts;
	std::size_t from = 0;
	while (true) {
		const auto bar = text.find('|', from);
		parts.push_back(text.substr(from, bar == std::string_view::npos ? std::string_view::npos : bar - from));
		if (bar == std::string_view::npos) break;
		from = bar + 1;
	}
	return parts;
}

double parseDouble(std::string_view text, double fallback = 0) {
	const std::string s(trim(text));
	if (s.empty()) return fallback;
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	return end && *end == '\0' && std::isfinite(v) ? v : fallback;
}

/// A scene's single start as older maps kept it ("mapsmith_trigger").
void decodeLegacyTrigger(std::string_view text, ChoreoScene& scene) {
	const auto t = trim(text);
	ChoreoStart start;
	if (iequals(t, "trigger")) start.kind = ChoreoStart::Kind::Area;
	else if (iequals(t, "input")) return;
	else if (startsWithI(t, "after:")) {
		start.kind = ChoreoStart::Kind::AfterScene;
		start.target = std::string(trim(t.substr(6)));
	} else start.kind = ChoreoStart::Kind::MapStart;
	scene.starts.push_back(std::move(start));
}

const char* startKindName(ChoreoStart::Kind kind) {
	switch (kind) {
		case ChoreoStart::Kind::MapStart: return "mapstart";
		case ChoreoStart::Kind::Area: return "area";
		case ChoreoStart::Kind::Use: return "use";
		case ChoreoStart::Kind::LookAt: return "look";
		case ChoreoStart::Kind::Near: return "near";
		case ChoreoStart::Kind::AfterScene: return "after";
		case ChoreoStart::Kind::Output: return "output";
		case ChoreoStart::Kind::Timer: return "timer";
		case ChoreoStart::Kind::Input: return "input";
	}
	return "mapstart";
}

const char* conditionKindName(ChoreoCondition::Kind kind) {
	switch (kind) {
		case ChoreoCondition::Kind::ScenePlayed: return "played";
		case ChoreoCondition::Kind::SceneNotPlayed: return "notplayed";
		case ChoreoCondition::Kind::Variable: return "var";
		case ChoreoCondition::Kind::Global: return "global";
		case ChoreoCondition::Kind::Random: return "random";
	}
	return "played";
}

const char* compareName(ChoreoCondition::Compare compare) {
	switch (compare) {
		case ChoreoCondition::Compare::Equal: return "eq";
		case ChoreoCondition::Compare::NotEqual: return "ne";
		case ChoreoCondition::Compare::Greater: return "gt";
		case ChoreoCondition::Compare::Less: return "lt";
	}
	return "eq";
}

const char* opName(ChoreoStep::VariableOp op) {
	switch (op) {
		case ChoreoStep::VariableOp::Set: return "set";
		case ChoreoStep::VariableOp::Add: return "add";
		case ChoreoStep::VariableOp::Toggle: return "toggle";
	}
	return "set";
}

bool isNumberedKey(std::string_view key, std::string_view prefix) {
	if (!startsWithI(key, prefix) || key.size() == prefix.size()) return false;
	return std::all_of(key.begin() + static_cast<std::ptrdiff_t>(prefix.size()), key.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string numberedKey(std::string_view prefix, std::size_t index) {
	std::string number = std::to_string(index + 1);
	if (number.size() < 2) number.insert(0, 1, '0');
	return std::string(prefix) + number;
}

bool isStepKey(std::string_view key) {
	return isNumberedKey(key, CHOREO_STEP_KEY);
}

std::string stepKey(std::size_t index) {
	return numberedKey(CHOREO_STEP_KEY, index);
}

double yawOf(const Entity& entity) {
	Vec3 angles;
	if (!parseVec3(entity.get("angles", "0 0 0"), angles)) return 0;
	return angles.y;
}

double yawTowards(const Vec3& from, const Vec3& to, double fallback) {
	const double dx = to.x - from.x, dy = to.y - from.y;
	if (dx * dx + dy * dy < 1) return fallback;
	return std::atan2(dy, dx) * 180.0 / std::numbers::pi;
}

std::string fmt(double v) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%.6f", v);
	return buffer;
}

bool stepUsesPoint(const ChoreoStep& step) {
	return step.kind == ChoreoStep::Kind::Move || step.kind == ChoreoStep::Kind::Face;
}

bool isHelperOf(const Entity& entity, std::string_view scene) {
	return !scene.empty() && iequals(entity.get(CHOREO_SCENE_KEY), scene) && !iequals(entity.classname(), "logic_choreographed_scene");
}

} // namespace

std::string encodeChoreoStep(const ChoreoStep& step) {
	std::string out = kindName(step.kind);
	out += '|' + escapeField(step.actor);
	out += step.withPrevious ? "|1" : "|0";
	out += '|' + formatVec3(step.point);
	out += step.run ? "|1" : "|0";
	out += '|' + escapeField(step.sequence);
	out += step.gesture ? "|1" : "|0";
	out += '|' + formatNumber(step.seconds);
	out += '|' + escapeField(step.sound);
	out += '|' + escapeField(step.caption);
	if (step.kind == ChoreoStep::Kind::SetVariable) {
		out += '|' + escapeField(step.variable);
		out += '|' + std::string(opName(step.op));
		out += '|' + formatNumber(step.value);
	}
	return out;
}

std::optional<ChoreoStep> decodeChoreoStep(std::string_view text) {
	const auto parts = splitBars(text);
	if (parts.size() < 2) return std::nullopt;
	const auto kind = kindFromName(trim(parts[0]));
	if (!kind) return std::nullopt;
	ChoreoStep step;
	step.kind = *kind;
	const auto field = [&parts](std::size_t i) { return i < parts.size() ? parts[i] : std::string_view(); };
	step.actor = unescapeField(field(1));
	step.withPrevious = trim(field(2)) == "1";
	if (!parseVec3(field(3), step.point)) step.point = {};
	step.run = trim(field(4)) == "1";
	step.sequence = unescapeField(field(5));
	step.gesture = trim(field(6)) == "1";
	step.seconds = std::max(0.0, parseDouble(field(7)));
	step.sound = unescapeField(field(8));
	step.caption = unescapeField(field(9));
	step.variable = unescapeField(field(10));
	const auto op = trim(field(11));
	step.op = iequals(op, "add") ? ChoreoStep::VariableOp::Add : iequals(op, "toggle") ? ChoreoStep::VariableOp::Toggle : ChoreoStep::VariableOp::Set;
	step.value = parseDouble(field(12));
	return step;
}

std::string encodeChoreoStart(const ChoreoStart& start) {
	std::string out = startKindName(start.kind);
	out += start.once ? "|1" : "|0";
	out += '|' + formatNumber(start.delay);
	out += '|' + escapeField(start.target);
	out += '|' + escapeField(start.output);
	out += '|' + formatNumber(start.a);
	out += '|' + formatNumber(start.b);
	return out;
}

std::optional<ChoreoStart> decodeChoreoStart(std::string_view text) {
	const auto parts = splitBars(text);
	const auto field = [&parts](std::size_t i) { return i < parts.size() ? parts[i] : std::string_view(); };
	ChoreoStart start;
	bool known = false;
	for (const auto kind : {ChoreoStart::Kind::MapStart, ChoreoStart::Kind::Area, ChoreoStart::Kind::Use, ChoreoStart::Kind::LookAt, ChoreoStart::Kind::Near,
	                        ChoreoStart::Kind::AfterScene, ChoreoStart::Kind::Output, ChoreoStart::Kind::Timer, ChoreoStart::Kind::Input}) {
		if (iequals(trim(field(0)), startKindName(kind))) {
			start.kind = kind;
			known = true;
		}
	}
	if (!known) return std::nullopt;
	start.once = trim(field(1)) != "0";
	start.delay = std::max(0.0, parseDouble(field(2)));
	start.target = unescapeField(field(3));
	start.output = unescapeField(field(4));
	start.a = std::max(0.0, parseDouble(field(5)));
	start.b = std::max(0.0, parseDouble(field(6)));
	return start;
}

std::string encodeChoreoCondition(const ChoreoCondition& condition) {
	std::string out = conditionKindName(condition.kind);
	out += '|' + escapeField(condition.name);
	out += '|' + std::string(compareName(condition.compare));
	out += '|' + formatNumber(condition.value);
	return out;
}

std::optional<ChoreoCondition> decodeChoreoCondition(std::string_view text) {
	const auto parts = splitBars(text);
	const auto field = [&parts](std::size_t i) { return i < parts.size() ? parts[i] : std::string_view(); };
	ChoreoCondition condition;
	bool known = false;
	for (const auto kind : {ChoreoCondition::Kind::ScenePlayed, ChoreoCondition::Kind::SceneNotPlayed, ChoreoCondition::Kind::Variable,
	                        ChoreoCondition::Kind::Global, ChoreoCondition::Kind::Random}) {
		if (iequals(trim(field(0)), conditionKindName(kind))) {
			condition.kind = kind;
			known = true;
		}
	}
	if (!known) return std::nullopt;
	condition.name = unescapeField(field(1));
	for (const auto compare : {ChoreoCondition::Compare::Equal, ChoreoCondition::Compare::NotEqual, ChoreoCondition::Compare::Greater, ChoreoCondition::Compare::Less}) {
		if (iequals(trim(field(2)), compareName(compare))) condition.compare = compare;
	}
	condition.value = parseDouble(field(3));
	return condition;
}

std::string describeChoreoStart(const ChoreoStart& start) {
	const std::string target = start.target.empty() ? std::string("the NPC") : start.target;
	std::string text;
	switch (start.kind) {
		case ChoreoStart::Kind::MapStart: text = "At map start"; break;
		case ChoreoStart::Kind::Area: text = start.once ? "Player enters an area" : "Player enters an area (every time)"; break;
		case ChoreoStart::Kind::Use: text = "Player uses " + target; break;
		case ChoreoStart::Kind::LookAt: text = "Player looks at " + target; break;
		case ChoreoStart::Kind::Near: text = "Player comes within " + formatNumber(start.a > 0 ? start.a : 128) + " units of " + target; break;
		case ChoreoStart::Kind::AfterScene: text = "After scene " + (start.target.empty() ? std::string("?") : start.target); break;
		case ChoreoStart::Kind::Output: text = (start.target.empty() ? std::string("?") : start.target) + " " + (start.output.empty() ? std::string("?") : start.output); break;
		case ChoreoStart::Kind::Timer:
			text = start.b > start.a ? "After " + formatNumber(start.a) + "-" + formatNumber(start.b) + " s (random)" : "After " + formatNumber(start.a) + " s";
			break;
		case ChoreoStart::Kind::Input: text = "By input \"Start\""; break;
	}
	if (start.delay > 0) text += ", " + formatNumber(start.delay) + " s later";
	return text;
}

std::string describeChoreoCondition(const ChoreoCondition& condition) {
	const char* compare = condition.compare == ChoreoCondition::Compare::Equal      ? "="
	                      : condition.compare == ChoreoCondition::Compare::NotEqual ? "!="
	                      : condition.compare == ChoreoCondition::Compare::Greater  ? ">"
	                                                                                : "<";
	const std::string name = condition.name.empty() ? std::string("?") : condition.name;
	switch (condition.kind) {
		case ChoreoCondition::Kind::ScenePlayed: return "scene " + name + " has played";
		case ChoreoCondition::Kind::SceneNotPlayed: return "scene " + name + " has not played yet";
		case ChoreoCondition::Kind::Variable: return name + " " + compare + " " + formatNumber(condition.value);
		case ChoreoCondition::Kind::Global: return "global " + name + " " + compare + " " + formatNumber(condition.value);
		case ChoreoCondition::Kind::Random: return formatNumber(condition.value) + " % chance";
	}
	return {};
}

std::string defaultUseOutput(std::string_view classname) {
	if (startsWithI(classname, "func_button") || startsWithI(classname, "func_rot_button") || iequals(classname, "momentary_rot_button")) return "OnPressed";
	return "OnPlayerUse";
}

bool isChoreoScene(const Entity& entity) {
	return iequals(entity.classname(), "logic_choreographed_scene") && !entity.get(CHOREO_SCENE_KEY).empty();
}

ChoreoScene readChoreoScene(const Entity& entity) {
	ChoreoScene scene;
	scene.name = entity.get("targetname");
	// Steps, starts and conditions by their number (the keys may come in any order).
	std::map<int, ChoreoStep> steps;
	std::map<int, ChoreoStart> starts;
	std::map<int, ChoreoCondition> conditions;
	bool hasStarts = false;
	for (const auto& [key, value] : entity.keyValues) {
		if (isStepKey(key)) {
			const int number = static_cast<int>(parseDouble(std::string_view(key).substr(CHOREO_STEP_KEY.size()), -1));
			if (number < 0) continue;
			if (auto step = decodeChoreoStep(value)) steps.emplace(number, std::move(*step));
		} else if (isNumberedKey(key, CHOREO_START_KEY)) {
			hasStarts = true;
			const int number = static_cast<int>(parseDouble(std::string_view(key).substr(CHOREO_START_KEY.size()), -1));
			if (auto start = decodeChoreoStart(value)) starts.emplace(number, std::move(*start));
		} else if (isNumberedKey(key, CHOREO_CONDITION_KEY)) {
			const int number = static_cast<int>(parseDouble(std::string_view(key).substr(CHOREO_CONDITION_KEY.size()), -1));
			if (auto condition = decodeChoreoCondition(value)) conditions.emplace(number, std::move(*condition));
		}
	}
	for (auto& [number, step] : steps) scene.steps.push_back(std::move(step));
	for (auto& [number, start] : starts) scene.starts.push_back(std::move(start));
	for (auto& [number, condition] : conditions) scene.conditions.push_back(std::move(condition));
	// Older maps kept one start in "mapsmith_trigger" (the edits replace it with start keys).
	if (!hasStarts && !entity.get(CHOREO_TRIGGER_KEY).empty()) decodeLegacyTrigger(entity.get(CHOREO_TRIGGER_KEY), scene);
	return scene;
}

ChoreoScene readChoreoScene(const MapDocument& doc, const Entity& entity) {
	ChoreoScene scene = readChoreoScene(entity);
	for (std::size_t i = 0; i < scene.steps.size(); ++i) {
		auto& step = scene.steps[i];
		if (step.kind != ChoreoStep::Kind::Move && step.kind != ChoreoStep::Kind::Face) continue;
		const Entity* point = findNamedEntity(doc, choreoPointName(scene.name, i));
		if (point && iequals(point->classname(), "info_target") && iequals(point->get(CHOREO_SCENE_KEY), scene.name) && point->hasOrigin()) {
			step.point = point->origin();
		}
	}
	return scene;
}

std::vector<int> choreoScenes(const MapDocument& doc) {
	std::vector<int> ids;
	for (const auto& entity : doc.entities) {
		if (isChoreoScene(entity)) ids.push_back(entity.id);
	}
	return ids;
}

std::vector<int> choreoScenesOfActor(const MapDocument& doc, std::string_view actor) {
	std::vector<int> ids;
	if (trim(actor).empty()) return ids;
	for (const auto& entity : doc.entities) {
		if (!isChoreoScene(entity)) continue;
		const auto scene = readChoreoScene(entity);
		if (std::any_of(scene.steps.begin(), scene.steps.end(), [actor](const ChoreoStep& s) { return iequals(s.actor, actor); })) ids.push_back(entity.id);
	}
	return ids;
}

std::string choreoPointName(std::string_view scene, std::size_t stepIndex) {
	return std::string(scene) + "_p" + std::to_string(stepIndex + 1);
}

std::string choreoSceneFile(std::string_view mapName, std::string_view scene) {
	std::string map = toLower(trim(mapName));
	std::string name = toLower(trim(scene));
	const auto clean = [](std::string& s) {
		for (auto& c : s) {
			const auto u = static_cast<unsigned char>(c);
			if (!std::isalnum(u) && c != '_' && c != '-') c = '_';
		}
	};
	clean(map);
	clean(name);
	if (map.empty()) map = "maps";
	if (name.empty()) name = "scene";
	return "scenes/" + map + "/" + name + ".vcd";
}

const Entity* findNamedEntity(const MapDocument& doc, std::string_view targetname) {
	if (trim(targetname).empty()) return nullptr;
	for (const auto& entity : doc.entities) {
		if (iequals(entity.get("targetname"), targetname)) return &entity;
	}
	return nullptr;
}

double choreoStepLength(const ChoreoStep& step, const Vec3& from) {
	switch (step.kind) {
		case ChoreoStep::Kind::Move: {
			const double dx = step.point.x - from.x, dy = step.point.y - from.y;
			const double distance = std::sqrt(dx * dx + dy * dy);
			return std::max(0.5, distance / (step.run ? CHOREO_RUN_SPEED : CHOREO_WALK_SPEED));
		}
		case ChoreoStep::Kind::Animation: return step.seconds > 0 ? step.seconds : 2.0;
		case ChoreoStep::Kind::Say: return step.seconds > 0 ? step.seconds : 3.0;
		case ChoreoStep::Kind::Wait: return std::max(0.0, step.seconds);
		case ChoreoStep::Kind::Face: return 1.0;
		case ChoreoStep::Kind::SetVariable: return 0;
	}
	return 0;
}

namespace {

struct ActorState {
	Vec3 position;
	double yaw = 0;
};

ActorState startState(const MapDocument& doc, std::string_view actor) {
	ActorState state;
	if (const Entity* entity = findNamedEntity(doc, actor)) {
		state.position = entity->origin();
		state.yaw = yawOf(*entity);
	}
	return state;
}

} // namespace

std::vector<ChoreoTiming> choreoTimeline(const MapDocument& doc, const ChoreoScene& scene) {
	std::vector<ChoreoTiming> timeline;
	std::map<std::string, ActorState> actors;
	double groupStart = 0, groupEnd = 0;
	for (std::size_t i = 0; i < scene.steps.size(); ++i) {
		const auto& step = scene.steps[i];
		if (i == 0 || !step.withPrevious) groupStart = groupEnd;
		const std::string key = toLower(step.actor);
		auto found = actors.find(key);
		if (found == actors.end()) found = actors.emplace(key, startState(doc, step.actor)).first;
		ActorState& state = found->second;
		ChoreoTiming timing;
		timing.start = groupStart;
		timing.end = groupStart + choreoStepLength(step, state.position);
		timing.from = state.position;
		if (step.kind == ChoreoStep::Kind::Move) {
			state.yaw = yawTowards(state.position, step.point, state.yaw);
			state.position = step.point;
		} else if (step.kind == ChoreoStep::Kind::Face) {
			state.yaw = yawTowards(state.position, step.point, state.yaw);
		}
		timing.to = state.position;
		timing.yaw = state.yaw;
		groupEnd = std::max(groupEnd, timing.end);
		timeline.push_back(timing);
	}
	return timeline;
}

double choreoLength(const std::vector<ChoreoTiming>& timeline) {
	double length = 0;
	for (const auto& t : timeline) length = std::max(length, t.end);
	return length;
}

ChoreoPose choreoActorPose(const MapDocument& doc, const ChoreoScene& scene, const std::vector<ChoreoTiming>& timeline, std::string_view actor, double time) {
	const ActorState start = startState(doc, actor);
	ChoreoPose pose;
	pose.position = start.position;
	pose.yaw = start.yaw;
	for (std::size_t i = 0; i < scene.steps.size() && i < timeline.size(); ++i) {
		const auto& step = scene.steps[i];
		if (!iequals(step.actor, actor)) continue;
		const auto& t = timeline[i];
		if (time >= t.end) {
			pose.position = t.to;
			pose.yaw = t.yaw;
			continue;
		}
		if (time < t.start) break;
		pose.step = static_cast<int>(i);
		pose.stepTime = time - t.start;
		const double length = std::max(1e-6, t.end - t.start);
		const double f = std::clamp((time - t.start) / length, 0.0, 1.0);
		if (step.kind == ChoreoStep::Kind::Move) {
			pose.position = t.from + (t.to - t.from) * f;
			pose.yaw = t.yaw;
		} else if (step.kind == ChoreoStep::Kind::Face) {
			// Turn over the step (the shorter way round).
			double delta = std::fmod(t.yaw - pose.yaw + 540.0, 360.0) - 180.0;
			pose.yaw += delta * f;
		}
		break;
	}
	return pose;
}

std::string writeVcd(const MapDocument& doc, const ChoreoScene& scene) {
	const auto timeline = choreoTimeline(doc, scene);
	std::vector<std::string> actorOrder;
	for (const auto& step : scene.steps) {
		if (trim(step.actor).empty()) continue;
		if (std::none_of(actorOrder.begin(), actorOrder.end(), [&step](const std::string& a) { return iequals(a, step.actor); })) actorOrder.push_back(step.actor);
	}
	std::string out = "// Choreo version 1\n// Written by Mapsmith from the scene \"" + escapeField(scene.name) + "\" (edit it there)\n";
	// A pause at the planned end of each move: if the NPC is not there yet, the scene waits for it.
	for (std::size_t i = 0; i < scene.steps.size(); ++i) {
		if (scene.steps[i].kind != ChoreoStep::Kind::Move || trim(scene.steps[i].actor).empty()) continue;
		out += "event section \"wait_" + std::to_string(i + 1) + "\"\n{\n  time " + fmt(timeline[i].end) + " -1.000000\n  param \"noaction\"\n}\n";
	}
	// "Set variable" steps: the scene fires its OnTrigger<N> output (N = 1 for the first such step).
	{
		int number = 0;
		for (std::size_t i = 0; i < scene.steps.size() && number < 16; ++i) {
			if (scene.steps[i].kind != ChoreoStep::Kind::SetVariable) continue;
			++number;
			out += "event firetrigger \"set " + escapeField(scene.steps[i].variable) + " " + std::to_string(number) + "\"\n{\n  time " + fmt(timeline[i].start) +
			       " -1.000000\n  param \"" + std::to_string(number) + "\"\n}\n";
		}
	}
	const auto eventHeader = [](std::string& text, std::string_view type, std::size_t index, std::string_view what, double start, double end) {
		text += "    event " + std::string(type) + " \"" + std::to_string(index + 1) + " " + std::string(what) + "\"\n    {\n";
		text += "      time " + fmt(start) + " " + fmt(end) + "\n";
	};
	for (const auto& actor : actorOrder) {
		std::string move, anim, audio;
		for (std::size_t i = 0; i < scene.steps.size(); ++i) {
			const auto& step = scene.steps[i];
			if (!iequals(step.actor, actor)) continue;
			const auto& t = timeline[i];
			switch (step.kind) {
				case ChoreoStep::Kind::Move:
					eventHeader(move, "moveto", i, step.run ? "run" : "walk", t.start, t.end);
					move += "      param \"" + escapeField(choreoPointName(scene.name, i)) + "\"\n";
					move += step.run ? "      param2 \"Run\"\n" : "      param2 \"Walk\"\n";
					move += "      resumecondition\n    }\n";
					break;
				case ChoreoStep::Kind::Face:
					eventHeader(move, "face", i, "face", t.start, t.end);
					move += "      param \"" + escapeField(choreoPointName(scene.name, i)) + "\"\n      lockbodyfacing\n    }\n";
					break;
				case ChoreoStep::Kind::Animation:
					if (trim(step.sequence).empty()) break;
					eventHeader(anim, step.gesture ? "gesture" : "sequence", i, escapeField(step.sequence), t.start, t.end);
					anim += "      param \"" + escapeField(step.sequence) + "\"\n      fixedlength\n    }\n";
					break;
				case ChoreoStep::Kind::Say:
					if (trim(step.sound).empty()) break;
					eventHeader(audio, "speak", i, escapeField(step.sound), t.start, t.end);
					audio += "      param \"" + escapeField(step.sound) + "\"\n      fixedlength\n      cctype \"cc_master\"\n      cctoken \"\"\n    }\n";
					break;
				case ChoreoStep::Kind::Wait:
				case ChoreoStep::Kind::SetVariable: break;
			}
		}
		out += "actor \"" + escapeField(actor) + "\"\n{\n";
		const auto channel = [&out](std::string_view name, const std::string& events) {
			if (events.empty()) return;
			out += "  channel \"" + std::string(name) + "\"\n  {\n" + events + "  }\n";
		};
		channel("move", move);
		channel("animation", anim);
		channel("audio", audio);
		if (const Entity* entity = findNamedEntity(doc, actor)) {
			const auto model = normalizePath(entity->get("model"));
			if (!model.empty()) out += "  faceposermodel \"" + escapeField(model) + "\"\n";
		}
		out += "}\n";
	}
	out += "\nfps 60\nsnap off\nignorePhonemes off\n";
	return out;
}

// --- Reading .vcd --------------------------------------------------------------------------

namespace {

std::vector<std::string> vcdTokens(std::string_view text) {
	std::vector<std::string> tokens;
	std::size_t i = 0;
	while (i < text.size()) {
		const char c = text[i];
		if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
			while (i < text.size() && text[i] != '\n') ++i;
			continue;
		}
		if (c == '{' || c == '}') { tokens.emplace_back(1, c); ++i; continue; }
		if (c == '"') {
			const auto close = text.find('"', i + 1);
			const auto end = close == std::string_view::npos ? text.size() : close;
			tokens.emplace_back(text.substr(i + 1, end - i - 1));
			i = end + 1;
			continue;
		}
		std::size_t j = i;
		while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j])) && text[j] != '{' && text[j] != '}' && text[j] != '"') ++j;
		tokens.emplace_back(text.substr(i, j - i));
		i = j;
	}
	return tokens;
}

bool isNumber(const std::string& token) {
	if (token.empty()) return false;
	char* end = nullptr;
	std::strtod(token.c_str(), &end);
	return end && *end == '\0';
}

class VcdReader {
public:
	explicit VcdReader(std::string_view text) : m_tokens(vcdTokens(text)) {}

	std::vector<VcdEvent> run() {
		while (m_at < m_tokens.size()) {
			const std::string& t = next();
			if (iequals(t, "event")) event({}, {});
			else if (iequals(t, "actor")) actor();
			else if (peek() == "{") skipBlock();
			else if (m_at < m_tokens.size() && peek() != "{") next();   // fps 60, snap off ...
		}
		return std::move(m_events);
	}

private:
	std::vector<std::string> m_tokens;
	std::size_t m_at = 0;
	std::vector<VcdEvent> m_events;

	const std::string& next() {
		static const std::string empty;
		return m_at < m_tokens.size() ? m_tokens[m_at++] : empty;
	}
	const std::string& peek() const {
		static const std::string empty;
		return m_at < m_tokens.size() ? m_tokens[m_at] : empty;
	}
	void skipBlock() {
		if (peek() != "{") return;
		int depth = 0;
		while (m_at < m_tokens.size()) {
			const std::string& t = next();
			if (t == "{") ++depth;
			else if (t == "}" && --depth == 0) return;
		}
	}
	void actor() {
		const std::string name = next();
		if (next() != "{") return;
		while (m_at < m_tokens.size()) {
			const std::string& t = next();
			if (t == "}") return;
			if (iequals(t, "channel")) {
				const std::string channel = next();
				if (next() != "{") return;
				while (m_at < m_tokens.size()) {
					const std::string& u = next();
					if (u == "}") break;
					if (iequals(u, "event")) event(name, channel);
					else if (peek() == "{") skipBlock();
					else next();   // active "1"
				}
			} else if (peek() == "{") {
				skipBlock();
			} else {
				next();   // faceposermodel "...", active "1"
			}
		}
	}
	void event(const std::string& actorName, const std::string& channel) {
		VcdEvent e;
		e.actor = actorName;
		e.channel = channel;
		e.type = next();
		e.name = next();
		if (next() != "{") return;
		static const std::set<std::string> FLAGS = {"resumecondition", "fixedlength", "lockbodyfacing", "forceshortmovement", "synctofollowinggesture",
		                                            "playoverscript", "cc_usingcombinedfile", "cc_combinedusesgender", "cc_noattenuate"};
		while (m_at < m_tokens.size()) {
			const std::string key = toLower(next());
			if (key == "}") break;
			if (key == "time") {
				e.start = parseDouble(next());
				if (isNumber(peek())) e.end = parseDouble(next());
			} else if (key == "param") e.param = next();
			else if (key == "param2") e.param2 = next();
			else if (key == "param3") e.param3 = next();
			else if (key == "cctype") e.cctype = next();
			else if (key == "cctoken") e.cctoken = next();
			else if (FLAGS.contains(key)) e.flags.push_back(key);
			else if (peek() == "{") skipBlock();
			else if (key == "absolutetags") { next(); skipBlock(); }
			else if (!peek().empty() && peek() != "}") next();
		}
		m_events.push_back(std::move(e));
	}
};

} // namespace

std::vector<VcdEvent> parseVcd(std::string_view text) {
	return VcdReader(text).run();
}

// --- Checks --------------------------------------------------------------------------------

std::vector<ChoreoIssue> checkChoreoScene(const MapDocument& doc, const ChoreoScene& scene, const std::function<bool(std::string_view)>& soundKnown,
                                          const std::function<std::optional<bool>(std::string_view, std::string_view)>& sequenceKnown) {
	std::vector<ChoreoIssue> issues;
	std::vector<Vec3> nodes;
	for (const auto& entity : doc.entities) {
		if (iequals(entity.classname(), "info_node")) nodes.push_back(entity.origin());
	}
	bool noNodesSaid = false;
	for (std::size_t i = 0; i < scene.steps.size(); ++i) {
		const auto& step = scene.steps[i];
		const int n = static_cast<int>(i);
		const std::string label = "Step " + std::to_string(i + 1);
		if (step.kind == ChoreoStep::Kind::SetVariable) {
			if (trim(step.variable).empty()) issues.push_back({n, label + " sets no variable - pick one"});
			continue;
		}
		if (trim(step.actor).empty()) {
			issues.push_back({n, label + " has no NPC"});
			continue;
		}
		if (!findNamedEntity(doc, step.actor)) issues.push_back({n, label + ": no entity is named \"" + step.actor + "\""});
		switch (step.kind) {
			case ChoreoStep::Kind::Say:
				if (trim(step.sound).empty()) issues.push_back({n, label + " says nothing - pick or import a sound"});
				else if (soundKnown && !soundKnown(step.sound)) issues.push_back({n, label + ": the sound \"" + step.sound + "\" is not in the game's sound scripts"});
				break;
			case ChoreoStep::Kind::Animation:
				if (trim(step.sequence).empty()) issues.push_back({n, label + " has no animation"});
				else if (sequenceKnown) {
					if (const auto known = sequenceKnown(step.actor, step.sequence); known && !*known) {
						issues.push_back({n, label + ": the model of \"" + step.actor + "\" has no sequence \"" + step.sequence + "\""});
					}
				}
				break;
			case ChoreoStep::Kind::Move:
				if (nodes.empty()) {
					if (!noNodesSaid) issues.push_back({-1, "The map has no info_node: NPCs may not find their way to the points of \"" + scene.name + "\""});
					noNodesSaid = true;
				} else {
					double nearest = std::numeric_limits<double>::max();
					for (const auto& node : nodes) nearest = std::min(nearest, length(node - step.point));
					if (nearest > 384) issues.push_back({n, label + ": no info_node within 384 units of the point - the NPC may not find its way"});
				}
				break;
			case ChoreoStep::Kind::Wait:
			case ChoreoStep::Kind::Face:
			case ChoreoStep::Kind::SetVariable: break;
		}
	}
	return issues;
}

// --- Files ---------------------------------------------------------------------------------

std::vector<std::string> writeChoreoFiles(const MapDocument& doc, const std::filesystem::path& modDir, std::string* error) {
	std::vector<std::string> written;
	for (const auto& entity : doc.entities) {
		if (!isChoreoScene(entity)) continue;
		const std::string file = normalizePath(entity.get("SceneFile"));
		if (file.empty() || file.find("..") != std::string::npos || !startsWithI(file, "scenes/")) continue;
		const auto scene = readChoreoScene(doc, entity);
		const std::string text = writeVcd(doc, scene);
		const auto path = modDir / toNativePath(file);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (const auto old = readFileText(path); old && *old == text) {
			written.push_back(file);
			continue;
		}
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out) {
			if (error) *error = "cannot write " + displayPath(path);
			continue;
		}
		written.push_back(file);
	}
	return written;
}

std::string voiceLineName(std::string_view scene, std::string_view line) {
	const auto clean = [](std::string_view text) {
		std::string out;
		for (const char c : text) {
			const auto u = static_cast<unsigned char>(c);
			if (std::isalnum(u) || c == '_') out += static_cast<char>(std::tolower(u));
			else if (!out.empty() && out.back() != '_') out += '_';
		}
		while (!out.empty() && out.back() == '_') out.pop_back();
		return out.empty() ? std::string("line") : out;
	};
	return clean(scene) + "." + clean(line);
}

bool setVoiceCaption(const std::filesystem::path& modDir, std::string_view token, std::string_view text, std::string* error) {
	const auto path = localizationPath(modDir, "closecaption", "english");
	LocalizationFile file;
	if (std::filesystem::exists(path)) {
		auto loaded = loadLocalizationFile(path);
		if (!loaded) {
			if (error) *error = "cannot read " + displayPath(path);
			return false;
		}
		file = std::move(*loaded);
	}
	if (file.get(token) == text && file.find(token) >= 0 && std::filesystem::exists(path.parent_path() / "closecaption_english.dat")) return true;
	file.set(token, std::string(text));
	if (!saveLocalizationFile(path, file, error)) return false;
	return compileCaptionFile(path, error);
}

// --- Edits ---------------------------------------------------------------------------------

void writeChoreoPlanKeys(Entity& entity, const ChoreoScene& scene) {
	auto& kv = entity.keyValues;
	kv.erase(std::remove_if(kv.begin(), kv.end(),
	                        [](const auto& p) {
		                        return isStepKey(p.first) || isNumberedKey(p.first, CHOREO_START_KEY) || isNumberedKey(p.first, CHOREO_CONDITION_KEY) ||
		                               iequals(p.first, CHOREO_TRIGGER_KEY);
	                        }),
	         kv.end());
	for (std::size_t i = 0; i < scene.starts.size(); ++i) entity.set(numberedKey(CHOREO_START_KEY, i), encodeChoreoStart(scene.starts[i]));
	for (std::size_t i = 0; i < scene.conditions.size(); ++i) entity.set(numberedKey(CHOREO_CONDITION_KEY, i), encodeChoreoCondition(scene.conditions[i]));
	for (std::size_t i = 0; i < scene.steps.size(); ++i) entity.set(stepKey(i), encodeChoreoStep(scene.steps[i]));
}

namespace {

/// "scenes/map/old.vcd" -> "scenes/map/new.vcd" when the file was named after the scene.
std::string renamedSceneFile(const std::string& file, std::string_view oldName, std::string_view newName) {
	const std::string oldFile = choreoSceneFile("x", oldName);
	const std::string oldLeaf = oldFile.substr(oldFile.rfind('/'));
	const std::string normal = normalizePath(file);
	if (normal.size() < oldLeaf.size() || !iequals(std::string_view(normal).substr(normal.size() - oldLeaf.size()), oldLeaf)) return file;
	const std::string newFile = choreoSceneFile("x", newName);
	return normal.substr(0, normal.size() - oldLeaf.size()) + newFile.substr(newFile.rfind('/'));
}

/// Writes plan, name and points of one scene (see setChoreoScene) inside `t`; the logic is synced
/// by the caller. Returns whether anything changed.
bool writeScene(MapDocument& doc, Transaction& t, int sceneId, const ChoreoScene& scene) {
	const Entity* found = doc.findEntity(sceneId);
	const std::string oldName = found->get("targetname");
	bool dirty = false;

	// The scene entity: name and plan.
	{
		Entity changed = *found;
		if (changed.get("targetname") != scene.name) {
			changed.set("targetname", scene.name);
			if (!oldName.empty()) changed.set("SceneFile", renamedSceneFile(changed.get("SceneFile"), oldName, scene.name));
		}
		writeChoreoPlanKeys(changed, scene);
		if (changed.keyValues != found->keyValues) {
			t.touchEntity(sceneId);
			doc.findEntity(sceneId)->keyValues = std::move(changed.keyValues);
			dirty = true;
		}
	}

	// A new name: everything aimed at the old one or its helpers follows (outputs, helper names and
	// tags, other scenes' plans and the lists of linked entities).
	if (!oldName.empty() && !iequals(oldName, scene.name)) {
		std::map<std::string, std::string> renamed;   // lower old name -> new name
		renamed[toLower(oldName)] = scene.name;
		for (const auto& entity : doc.entities) {
			if (!isHelperOf(entity, oldName)) continue;
			const std::string name = entity.get("targetname");
			if (startsWithI(name, oldName + "_")) renamed[toLower(name)] = scene.name + name.substr(oldName.size());
		}
		for (auto& entity : doc.entities) {
			bool touched = false;
			const auto touch = [&] {
				if (!touched) t.touchEntity(entity.id);
				touched = true;
			};
			for (auto& c : entity.connections) {
				if (const auto to = renamed.find(toLower(c.target)); to != renamed.end()) {
					touch();
					c.target = to->second;
					c.sourceText.clear();
				}
			}
			if (isHelperOf(entity, oldName)) {
				touch();
				entity.set(CHOREO_SCENE_KEY, scene.name);
				if (const auto to = renamed.find(toLower(entity.get("targetname"))); to != renamed.end()) entity.set("targetname", to->second);
			}
			if (entity.id != sceneId && isChoreoScene(entity)) {
				ChoreoScene other = readChoreoScene(entity);
				bool changed = false;
				for (auto& start : other.starts) {
					if (start.kind == ChoreoStart::Kind::AfterScene && iequals(start.target, oldName)) {
						start.target = scene.name;
						changed = true;
					}
				}
				for (auto& condition : other.conditions) {
					const bool aboutScene = condition.kind == ChoreoCondition::Kind::ScenePlayed || condition.kind == ChoreoCondition::Kind::SceneNotPlayed;
					if (aboutScene && iequals(condition.name, oldName)) {
						condition.name = scene.name;
						changed = true;
					}
				}
				if (changed) {
					touch();
					writeChoreoPlanKeys(entity, other);
				}
			}
			if (isChoreoScene(entity)) {
				// The list of entities whose outputs Mapsmith set ("mapsmith_links").
				const std::string links = entity.get("mapsmith_links");
				std::string updated;
				for (const auto part : splitBars(links)) {
					const std::string name(trim(part));
					if (name.empty()) continue;
					updated += (updated.empty() ? "" : "|") + (iequals(name, oldName) ? scene.name : name);
				}
				if (!links.empty() && updated != links) {
					touch();
					entity.set("mapsmith_links", updated);
				}
			}
			dirty = dirty || touched;
		}
	}

	// Points: one info_target per Move/Face step, reused in order.
	std::vector<int> pointHelpers;
	for (const auto& entity : doc.entities) {
		if (isHelperOf(entity, scene.name) && iequals(entity.classname(), "info_target")) pointHelpers.push_back(entity.id);
	}
	std::size_t used = 0;
	for (std::size_t i = 0; i < scene.steps.size(); ++i) {
		const auto& step = scene.steps[i];
		if (!stepUsesPoint(step)) continue;
		const std::string name = choreoPointName(scene.name, i);
		const std::string origin = formatVec3(step.point);
		if (used < pointHelpers.size()) {
			const int id = pointHelpers[used++];
			const Entity* helper = doc.findEntity(id);
			if (helper->get("targetname") != name || helper->get("origin") != origin) {
				t.touchEntity(id);
				Entity* h = doc.findEntity(id);
				h->set("targetname", name);
				h->set("origin", origin);
				dirty = true;
			}
			continue;
		}
		Entity point;
		point.id = doc.newEntityId();
		t.touchEntity(point.id);
		point.set("classname", "info_target");
		point.set("targetname", name);
		point.set(CHOREO_SCENE_KEY, scene.name);
		point.set(CHOREO_ROLE_KEY, "point");
		point.set("origin", origin);
		doc.entities.push_back(std::move(point));
		dirty = true;
	}
	for (std::size_t k = used; k < pointHelpers.size(); ++k) {
		t.touchEntity(pointHelpers[k]);
		std::erase_if(doc.entities, [id = pointHelpers[k]](const Entity& e) { return e.id == id; });
		dirty = true;
	}
	return dirty;
}

/// A scene name without a trailing "_<number>" (for copies: alyx_greet_2 -> alyx_greet).
std::string withoutNumber(const std::string& name) {
	const auto bar = name.rfind('_');
	if (bar == std::string::npos || bar + 1 >= name.size() || bar == 0) return name;
	if (!std::all_of(name.begin() + static_cast<std::ptrdiff_t>(bar) + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; })) return name;
	return name.substr(0, bar);
}

} // namespace

namespace edit {

Change createChoreoScene(MapDocument& doc, int npcId, std::string_view mapName) {
	const Entity* found = doc.findEntity(npcId);
	if (!found || npcId == 0) return {};
	Transaction t(doc, "New scene");
	t.touchEntity(npcId);
	Entity* npc = doc.findEntity(npcId);
	std::string npcName = npc->get("targetname");
	if (npcName.empty()) {
		npcName = npc->classname() + "_" + std::to_string(npc->id);
		npc->set("targetname", npcName);
	}
	// Scenes of the NPC: with a step of it, or named after it and still empty.
	std::set<int> own;
	for (const int id : choreoScenesOfActor(doc, npcName)) own.insert(id);
	for (const auto& entity : doc.entities) {
		if (isChoreoScene(entity) && startsWithI(entity.get("targetname"), npcName + "_scene")) own.insert(entity.id);
	}
	const bool first = own.empty();
	const std::string name = uniqueChoreoName(doc, npcName + "_scene");
	const Vec3 origin = npc->origin();
	Entity scene;
	scene.id = doc.newEntityId();
	t.touchEntity(scene.id);
	scene.set("classname", "logic_choreographed_scene");
	scene.set("targetname", name);
	scene.set("SceneFile", choreoSceneFile(mapName, name));
	scene.set("busyactor", "1");   // Wait for the NPC to finish what it is doing
	scene.set("onplayerdeath", "0");
	scene.set(CHOREO_SCENE_KEY, "1");
	// The NPC's first scene plays at map start, the next ones when the player walks into an area.
	ChoreoScene plan;
	plan.name = name;
	ChoreoStart start;
	start.kind = first ? ChoreoStart::Kind::MapStart : ChoreoStart::Kind::Area;
	plan.starts.push_back(start);
	writeChoreoPlanKeys(scene, plan);
	// Above the NPC, one row per scene (its logic in a row to the right of it).
	scene.setOrigin(origin + Vec3(0, 0, 80 + 24.0 * static_cast<double>(own.size())));
	doc.entities.push_back(std::move(scene));
	syncChoreoLogic(doc, t);
	return t.finish();
}

Change setChoreoScene(MapDocument& doc, int sceneId, const ChoreoScene& scene, std::string_view undoName) {
	const Entity* found = doc.findEntity(sceneId);
	if (!found || sceneId == 0 || !isChoreoScene(*found) || trim(scene.name).empty()) return {};
	// Another entity has the name already: the scene keeps its own.
	const Entity* named = findNamedEntity(doc, scene.name);
	if (named && named->id != sceneId && !isHelperOf(*named, found->get("targetname"))) return {};
	Transaction t(doc, std::string(undoName));
	bool dirty = writeScene(doc, t, sceneId, scene);
	dirty = syncChoreoLogic(doc, t) || dirty;
	if (!dirty) return {};
	return t.finish();
}

Change deleteChoreoScene(MapDocument& doc, int sceneId) {
	const Entity* found = doc.findEntity(sceneId);
	if (!found || sceneId == 0 || !isChoreoScene(*found)) return {};
	const std::string name = found->get("targetname");
	Transaction t(doc, "Delete scene");
	std::vector<int> gone{sceneId};
	std::set<std::string> helperNames;
	for (const auto& entity : doc.entities) {
		if (!isHelperOf(entity, name)) continue;
		gone.push_back(entity.id);
		if (!entity.get("targetname").empty()) helperNames.insert(toLower(entity.get("targetname")));
	}
	std::set<std::string> linked;
	for (const auto part : splitBars(found->get("mapsmith_links"))) {
		if (!trim(part).empty()) linked.insert(toLower(trim(part)));
	}
	for (const int id : gone) t.touchEntity(id);
	std::erase_if(doc.entities, [&gone](const Entity& e) { return std::find(gone.begin(), gone.end(), e.id) != gone.end(); });
	// Outputs Mapsmith set on other entities to start it go too (also older maps' OnCompletion -> Start).
	for (auto& entity : doc.entities) {
		const bool isLinked = linked.contains(toLower(entity.get("targetname")));
		const auto aimed = [&](const Connection& c) {
			if (isLinked && helperNames.contains(toLower(c.target))) return true;
			return isChoreoScene(entity) && iequals(c.target, name) && iequals(c.output, "OnCompletion") && iequals(c.input, "Start");
		};
		if (std::none_of(entity.connections.begin(), entity.connections.end(), aimed)) continue;
		t.touchEntity(entity.id);
		std::erase_if(entity.connections, aimed);
	}
	syncChoreoLogic(doc, t);
	return t.finish();
}

Change duplicateChoreoScene(MapDocument& doc, int sceneId, std::string_view mapName) {
	const Entity* found = doc.findEntity(sceneId);
	if (!found || sceneId == 0 || !isChoreoScene(*found)) return {};
	ChoreoScene scene = readChoreoScene(doc, *found);
	const std::string original = scene.name;
	scene.name = uniqueChoreoName(doc, withoutNumber(original.empty() ? std::string("scene") : original));
	Transaction t(doc, "Duplicate scene");
	Entity copy;
	copy.id = doc.newEntityId();
	t.touchEntity(copy.id);
	for (const auto& [key, value] : found->keyValues) {
		if (!iequals(key, "mapsmith_links")) copy.keyValues.emplace_back(key, value);
	}
	copy.set("targetname", scene.name);
	copy.set("SceneFile", choreoSceneFile(mapName, scene.name));
	copy.setOrigin(found->origin() + Vec3(0, 0, 24));
	writeChoreoPlanKeys(copy, scene);
	const int copyId = copy.id;
	doc.entities.push_back(std::move(copy));
	writeScene(doc, t, copyId, scene);
	syncChoreoLogic(doc, t);
	// The copy's trigger boxes where the original's are (it is usually a variant of the same moment).
	std::map<std::string, Bounds> boxes;
	for (const auto& entity : doc.entities) {
		if (!isHelperOf(entity, original) || entity.solids.empty()) continue;
		Bounds box;
		for (const auto& solid : entity.solids) box.add(buildBrushGeometry(solid).bounds);
		if (!box.empty()) boxes[toLower(entity.get(CHOREO_ROLE_KEY))] = box;
	}
	std::vector<int> copies;
	for (const auto& entity : doc.entities) {
		if (isHelperOf(entity, scene.name) && !entity.solids.empty() && entity.get("parentname").empty() && boxes.contains(toLower(entity.get(CHOREO_ROLE_KEY)))) {
			copies.push_back(entity.id);
		}
	}
	for (const int id : copies) {
		const Bounds box = boxes[toLower(doc.findEntity(id)->get(CHOREO_ROLE_KEY))];
		Solid block = makeBlock(doc, box, "tools/toolstrigger");
		t.touchEntity(id);
		doc.findEntity(id)->solids = {std::move(block)};
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
