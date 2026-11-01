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
	}
	return "wait";
}

std::optional<ChoreoStep::Kind> kindFromName(std::string_view name) {
	for (const auto kind : {ChoreoStep::Kind::Move, ChoreoStep::Kind::Animation, ChoreoStep::Kind::Say, ChoreoStep::Kind::Wait, ChoreoStep::Kind::Face}) {
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

std::string encodeTrigger(const ChoreoScene& scene) {
	switch (scene.trigger) {
		case ChoreoTrigger::MapStart: return "mapstart";
		case ChoreoTrigger::Trigger: return "trigger";
		case ChoreoTrigger::Input: return "input";
		case ChoreoTrigger::AfterScene: return "after:" + scene.afterScene;
	}
	return "mapstart";
}

void decodeTrigger(std::string_view text, ChoreoScene& scene) {
	const auto t = trim(text);
	if (iequals(t, "trigger")) scene.trigger = ChoreoTrigger::Trigger;
	else if (iequals(t, "input")) scene.trigger = ChoreoTrigger::Input;
	else if (startsWithI(t, "after:")) {
		scene.trigger = ChoreoTrigger::AfterScene;
		scene.afterScene = std::string(trim(t.substr(6)));
	} else scene.trigger = ChoreoTrigger::MapStart;
}

bool isStepKey(std::string_view key) {
	return startsWithI(key, CHOREO_STEP_KEY);
}

std::string stepKey(std::size_t index) {
	std::string number = std::to_string(index + 1);
	if (number.size() < 2) number.insert(0, 1, '0');
	return std::string(CHOREO_STEP_KEY) + number;
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

bool sameConnection(const Connection& a, const Connection& b) {
	return iequals(a.output, b.output) && iequals(a.target, b.target) && iequals(a.input, b.input) && a.parameter == b.parameter && a.delay == b.delay &&
	       a.timesToFire == b.timesToFire;
}

Connection startConnection(std::string_view output, std::string_view scene) {
	Connection c;
	c.output = std::string(output);
	c.target = std::string(scene);
	c.input = "Start";
	c.timesToFire = 1;
	return c;
}

bool isHelperOf(const Entity& entity, std::string_view scene) {
	return !scene.empty() && iequals(entity.get(CHOREO_SCENE_KEY), scene) && !iequals(entity.classname(), "logic_choreographed_scene");
}

/// Where a new trigger box goes: 128 x 128 x 112 on the floor of the first actor, in front of it.
Bounds triggerBoxFor(const MapDocument& doc, const ChoreoScene& scene, const Vec3& fallback) {
	Vec3 origin = fallback;
	double yaw = 0;
	for (const auto& step : scene.steps) {
		if (const Entity* actor = findNamedEntity(doc, step.actor)) {
			origin = actor->origin();
			yaw = yawOf(*actor);
			break;
		}
	}
	const double rad = yaw * std::numbers::pi / 180.0;
	const Vec3 centre = origin + Vec3(std::cos(rad), std::sin(rad), 0) * 192.0;
	const auto snap = [](double v) { return std::round(v / 16.0) * 16.0; };
	Bounds box;
	box.add(Vec3(snap(centre.x) - 64, snap(centre.y) - 64, snap(origin.z)));
	box.add(Vec3(snap(centre.x) + 64, snap(centre.y) + 64, snap(origin.z) + 112));
	return box;
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
	return step;
}

bool isChoreoScene(const Entity& entity) {
	return iequals(entity.classname(), "logic_choreographed_scene") && !entity.get(CHOREO_SCENE_KEY).empty();
}

ChoreoScene readChoreoScene(const Entity& entity) {
	ChoreoScene scene;
	scene.name = entity.get("targetname");
	decodeTrigger(entity.get(CHOREO_TRIGGER_KEY), scene);
	// Steps by their number (the keys may come in any order).
	std::map<int, ChoreoStep> numbered;
	for (const auto& [key, value] : entity.keyValues) {
		if (!isStepKey(key)) continue;
		const int number = static_cast<int>(parseDouble(std::string_view(key).substr(CHOREO_STEP_KEY.size()), -1));
		if (number < 0) continue;
		if (auto step = decodeChoreoStep(value)) numbered.emplace(number, std::move(*step));
	}
	for (auto& [number, step] : numbered) scene.steps.push_back(std::move(step));
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
				case ChoreoStep::Kind::Wait: break;
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
			case ChoreoStep::Kind::Face: break;
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
	std::string name = npcName + "_scene";
	for (int n = 2; findNamedEntity(doc, name); ++n) name = npcName + "_scene" + std::to_string(n);
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
	scene.set(CHOREO_TRIGGER_KEY, "mapstart");
	scene.setOrigin(origin + Vec3(0, 0, 80));
	doc.entities.push_back(std::move(scene));
	// The map-start helper right away, so the new scene plays as soon as it has steps.
	Entity automatic;
	automatic.id = doc.newEntityId();
	t.touchEntity(automatic.id);
	automatic.set("classname", "logic_auto");
	automatic.set("spawnflags", "1");
	automatic.set(CHOREO_SCENE_KEY, name);
	automatic.setOrigin(origin + Vec3(0, 0, 96));
	automatic.connections.push_back(startConnection("OnMapSpawn", name));
	doc.entities.push_back(std::move(automatic));
	return t.finish();
}

Change setChoreoScene(MapDocument& doc, int sceneId, const ChoreoScene& scene, std::string_view undoName) {
	const Entity* found = doc.findEntity(sceneId);
	if (!found || sceneId == 0 || !isChoreoScene(*found) || trim(scene.name).empty()) return {};
	const std::string oldName = found->get("targetname");
	const ChoreoScene before = readChoreoScene(*found);
	const Vec3 sceneOrigin = found->origin();
	bool dirty = false;
	Transaction t(doc, std::string(undoName));

	// The scene entity: name and plan.
	{
		Entity changed = *found;
		if (changed.get("targetname") != scene.name) changed.set("targetname", scene.name);
		auto& kv = changed.keyValues;
		kv.erase(std::remove_if(kv.begin(), kv.end(), [](const auto& p) { return isStepKey(p.first); }), kv.end());
		changed.set(CHOREO_TRIGGER_KEY, encodeTrigger(scene));
		for (std::size_t i = 0; i < scene.steps.size(); ++i) changed.set(stepKey(i), encodeChoreoStep(scene.steps[i]));
		if (changed.keyValues != found->keyValues) {
			t.touchEntity(sceneId);
			doc.findEntity(sceneId)->keyValues = std::move(changed.keyValues);
			dirty = true;
		}
	}

	// A new name: everything aimed at the old one follows (outputs, helper tags).
	if (!oldName.empty() && !iequals(oldName, scene.name)) {
		for (auto& entity : doc.entities) {
			bool touched = false;
			for (auto& c : entity.connections) {
				if (iequals(c.target, oldName)) {
					if (!touched) t.touchEntity(entity.id);
					touched = true;
					c.target = scene.name;
					c.sourceText.clear();
				}
			}
			if (isHelperOf(entity, oldName)) {
				if (!touched) t.touchEntity(entity.id);
				touched = true;
				entity.set(CHOREO_SCENE_KEY, scene.name);
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
		point.set("origin", origin);
		doc.entities.push_back(std::move(point));
		dirty = true;
	}
	for (std::size_t k = used; k < pointHelpers.size(); ++k) {
		t.touchEntity(pointHelpers[k]);
		std::erase_if(doc.entities, [id = pointHelpers[k]](const Entity& e) { return e.id == id; });
		dirty = true;
	}

	// The trigger helpers.
	const auto helperOfClass = [&doc, &scene](std::string_view classname) -> int {
		for (const auto& entity : doc.entities) {
			if (isHelperOf(entity, scene.name) && iequals(entity.classname(), classname)) return entity.id;
		}
		return 0;
	};
	const auto removeHelper = [&](std::string_view classname) {
		while (const int id = helperOfClass(classname)) {
			t.touchEntity(id);
			std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
			dirty = true;
		}
	};
	const auto setOnlyConnection = [&](int id, const Connection& wanted) {
		const Entity* e = doc.findEntity(id);
		if (e->connections.size() == 1 && sameConnection(e->connections.front(), wanted)) return;
		t.touchEntity(id);
		doc.findEntity(id)->connections = {wanted};
		dirty = true;
	};
	if (scene.trigger == ChoreoTrigger::MapStart) {
		if (const int id = helperOfClass("logic_auto")) {
			setOnlyConnection(id, startConnection("OnMapSpawn", scene.name));
		} else {
			Entity automatic;
			automatic.id = doc.newEntityId();
			t.touchEntity(automatic.id);
			automatic.set("classname", "logic_auto");
			automatic.set("spawnflags", "1");
			automatic.set(CHOREO_SCENE_KEY, scene.name);
			automatic.setOrigin(sceneOrigin + Vec3(0, 0, 16));
			automatic.connections.push_back(startConnection("OnMapSpawn", scene.name));
			doc.entities.push_back(std::move(automatic));
			dirty = true;
		}
	} else {
		removeHelper("logic_auto");
	}
	if (scene.trigger == ChoreoTrigger::Trigger) {
		if (const int id = helperOfClass("trigger_once")) {
			setOnlyConnection(id, startConnection("OnStartTouch", scene.name));
			if (doc.findEntity(id)->get("targetname") != scene.name + "_trigger") {
				t.touchEntity(id);
				doc.findEntity(id)->set("targetname", scene.name + "_trigger");
				dirty = true;
			}
		} else {
			Entity trigger;
			trigger.id = doc.newEntityId();
			t.touchEntity(trigger.id);
			trigger.set("classname", "trigger_once");
			trigger.set("targetname", scene.name + "_trigger");
			trigger.set("spawnflags", "1");   // Clients (the player)
			trigger.set("StartDisabled", "0");
			trigger.set(CHOREO_SCENE_KEY, scene.name);
			trigger.connections.push_back(startConnection("OnStartTouch", scene.name));
			trigger.solids.push_back(makeBlock(doc, triggerBoxFor(doc, scene, sceneOrigin), "tools/toolstrigger"));
			doc.entities.push_back(std::move(trigger));
			dirty = true;
		}
	} else {
		removeHelper("trigger_once");
	}
	// After another scene: its OnCompletion starts this one (an old link from a previous choice goes).
	const auto linkFrom = [&](std::string_view other, bool wanted) {
		for (auto& entity : doc.entities) {
			if (entity.id == sceneId || !iequals(entity.get("targetname"), other) || !iequals(entity.classname(), "logic_choreographed_scene")) continue;
			const Connection link = startConnection("OnCompletion", scene.name);
			const auto has = std::find_if(entity.connections.begin(), entity.connections.end(), [&link](const Connection& c) { return sameConnection(c, link); });
			if (wanted && has == entity.connections.end()) {
				t.touchEntity(entity.id);
				entity.connections.push_back(link);
				dirty = true;
			} else if (!wanted && has != entity.connections.end()) {
				t.touchEntity(entity.id);
				entity.connections.erase(has);
				dirty = true;
			}
		}
	};
	if (before.trigger == ChoreoTrigger::AfterScene && !(scene.trigger == ChoreoTrigger::AfterScene && iequals(before.afterScene, scene.afterScene))) {
		linkFrom(before.afterScene, false);
	}
	if (scene.trigger == ChoreoTrigger::AfterScene && !trim(scene.afterScene).empty()) linkFrom(scene.afterScene, true);

	if (!dirty) return {};
	return t.finish();
}

Change deleteChoreoScene(MapDocument& doc, int sceneId) {
	const Entity* found = doc.findEntity(sceneId);
	if (!found || sceneId == 0 || !isChoreoScene(*found)) return {};
	const std::string name = found->get("targetname");
	Transaction t(doc, "Delete scene");
	std::vector<int> gone{sceneId};
	for (const auto& entity : doc.entities) {
		if (isHelperOf(entity, name)) gone.push_back(entity.id);
	}
	for (const int id : gone) t.touchEntity(id);
	std::erase_if(doc.entities, [&gone](const Entity& e) { return std::find(gone.begin(), gone.end(), e.id) != gone.end(); });
	// Outputs of other scenes that started this one go too.
	for (auto& entity : doc.entities) {
		const auto before = entity.connections.size();
		const bool aimed = std::any_of(entity.connections.begin(), entity.connections.end(), [&name](const Connection& c) {
			return iequals(c.target, name) && iequals(c.output, "OnCompletion") && iequals(c.input, "Start");
		});
		if (!aimed) continue;
		t.touchEntity(entity.id);
		std::erase_if(entity.connections, [&name](const Connection& c) {
			return iequals(c.target, name) && iequals(c.output, "OnCompletion") && iequals(c.input, "Start");
		});
		(void)before;
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
