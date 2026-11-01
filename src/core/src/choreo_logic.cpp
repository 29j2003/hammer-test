// choreo_logic.cpp - What starts the scenes made here and under which conditions: the generated
// logic (relay, start helpers, condition chain), the variables and the checks (see choreography.h).

#include <sme/choreography.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <numbers>
#include <set>

namespace sme {

namespace {

std::string num(double v) {
	return formatNumber(v);
}

Connection link(std::string_view output, std::string_view target, std::string_view input, std::string_view parameter = {}, double delay = 0, int times = -1) {
	Connection c;
	c.output = std::string(output);
	c.target = std::string(target);
	c.input = std::string(input);
	c.parameter = std::string(parameter);
	c.delay = delay;
	c.timesToFire = times;
	return c;
}

bool sameLink(const Connection& a, const Connection& b) {
	return iequals(a.output, b.output) && iequals(a.target, b.target) && iequals(a.input, b.input) && a.parameter == b.parameter &&
	       std::abs(a.delay - b.delay) < 1e-6 && a.timesToFire == b.timesToFire;
}

bool sameLinks(const std::vector<Connection>& a, const std::vector<Connection>& b) {
	if (a.size() != b.size()) return false;
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (!sameLink(a[i], b[i])) return false;
	}
	return true;
}

/// A point a step walks to or faces (managed per scene by setChoreoScene, not by the sync).
bool isPointHelper(const Entity& entity) {
	const std::string role = entity.get(CHOREO_ROLE_KEY);
	return iequals(entity.classname(), "info_target") && (role.empty() || iequals(role, "point"));
}

bool isBrushTrigger(std::string_view classname) {
	return iequals(classname, "trigger_once") || iequals(classname, "trigger_multiple") || iequals(classname, "trigger_look");
}

double yawOfEntity(const Entity& entity) {
	Vec3 angles;
	if (!parseVec3(entity.get("angles", "0 0 0"), angles)) return 0;
	return angles.y;
}

/// Where a new area box goes: 128 x 128 x 112 on the floor of the first actor, in front of it
/// (the n-th box of a scene a little further to the side).
Bounds areaBoxFor(const MapDocument& doc, const ChoreoScene& scene, const Vec3& fallback, int index) {
	Vec3 origin = fallback;
	double yaw = 0;
	for (const auto& actor : choreoActors(scene)) {
		if (const Entity* entity = findNamedEntity(doc, actor)) {
			origin = entity->origin();
			yaw = yawOfEntity(*entity);
			break;
		}
	}
	const double rad = yaw * std::numbers::pi / 180.0;
	const Vec3 forward(std::cos(rad), std::sin(rad), 0);
	const Vec3 side(-forward.y, forward.x, 0);
	const Vec3 centre = origin + forward * 192.0 + side * (160.0 * index);
	const auto snap = [](double v) { return std::round(v / 16.0) * 16.0; };
	Bounds box;
	box.add(Vec3(snap(centre.x) - 64, snap(centre.y) - 64, snap(origin.z)));
	box.add(Vec3(snap(centre.x) + 64, snap(centre.y) + 64, snap(origin.z) + 112));
	return box;
}

Bounds solidsBounds(const Entity& entity) {
	Bounds box;
	for (const auto& solid : entity.solids) box.add(buildBrushGeometry(solid).bounds);
	return box;
}

std::string compareOutput(ChoreoCondition::Compare compare) {
	switch (compare) {
		case ChoreoCondition::Compare::Equal: return "OnEqualTo";
		case ChoreoCondition::Compare::NotEqual: return "OnNotEqualTo";
		case ChoreoCondition::Compare::Greater: return "OnGreaterThan";
		case ChoreoCondition::Compare::Less: return "OnLessThan";
	}
	return "OnEqualTo";
}

bool compareHolds(ChoreoCondition::Compare compare, double value, double with) {
	switch (compare) {
		case ChoreoCondition::Compare::Equal: return std::abs(value - with) < 1e-6;
		case ChoreoCondition::Compare::NotEqual: return std::abs(value - with) >= 1e-6;
		case ChoreoCondition::Compare::Greater: return value > with;
		case ChoreoCondition::Compare::Less: return value < with;
	}
	return false;
}

/// The names of the entities whose outputs aim at a scene's helpers ("mapsmith_links", '|'-separated).
inline constexpr std::string_view LINKS_KEY = "mapsmith_links";

std::vector<std::string> splitLinks(std::string_view text) {
	std::vector<std::string> names;
	std::size_t from = 0;
	while (from <= text.size()) {
		const auto bar = text.find('|', from);
		const auto part = trim(text.substr(from, bar == std::string_view::npos ? std::string_view::npos : bar - from));
		if (!part.empty()) names.emplace_back(part);
		if (bar == std::string_view::npos) break;
		from = bar + 1;
	}
	return names;
}

/// One entity the logic needs.
struct Wanted {
	std::string scene, role, classname, targetname;
	std::vector<std::pair<std::string, std::string>> keys;
	std::vector<Connection> connections;
	Vec3 origin;
	bool brush = false;
	Bounds box;             ///< Brush: where a new one goes
	std::string parent;     ///< Brush: the NPC it follows (a new box when that changes)
};

/// An output Mapsmith puts on an entity it did not make (or on a scene entity).
struct WantedLink {
	std::string source;     ///< The entity's targetname
	std::string owner;      ///< The scene whose helpers it aims at
	Connection connection;
};

struct SceneInfo {
	int id = 0;
	std::string name;
	ChoreoScene plan;
	Vec3 origin;
};

struct Planner {
	const MapDocument& doc;
	const std::vector<SceneInfo>& scenes;
	const std::map<std::string, ChoreoVariable>& variables;   ///< by lower-case name
	std::vector<Wanted> wanted;
	std::vector<WantedLink> links;
	std::map<int, std::vector<Connection>> variableLinks;      ///< scene id -> OnTrigger<N> outputs to variables

	std::size_t add(const SceneInfo& s, std::string role, std::string classname, std::string targetname, int& slot) {
		Wanted w;
		w.scene = s.name;
		w.role = std::move(role);
		w.classname = std::move(classname);
		w.targetname = std::move(targetname);
		w.origin = s.origin + Vec3(20.0 * (++slot), 0, 0);   // a row to the right of the scene's marker
		wanted.push_back(std::move(w));
		return wanted.size() - 1;
	}

	void addLink(std::string_view source, const SceneInfo& owner, Connection c) {
		if (trim(source).empty()) return;
		links.push_back({std::string(source), owner.name, std::move(c)});
	}

	const ChoreoVariable* variable(std::string_view name) const {
		const auto found = variables.find(toLower(trim(name)));
		return found == variables.end() ? nullptr : &found->second;
	}

	void plan(const SceneInfo& s) {
		const std::string& n = s.name;
		if (trim(n).empty()) return;
		const ChoreoScene& scene = s.plan;
		const auto actors = choreoActors(scene);
		const std::string firstActor = actors.empty() ? std::string() : actors.front();
		int slot = 0;

		// The relay every start fires.
		const std::size_t relay = add(s, "relay", "logic_relay", n + "_start", slot);
		wanted[relay].keys = {{"spawnflags", "0"}};

		// The conditions, in a chain after the relay: each one's success outputs fire the next.
		struct Step {
			std::size_t emitter;              ///< The entity whose outputs go on
			std::vector<std::string> outputs; ///< Its outputs that mean "go on" (empty: never)
			std::string target, input;        ///< How the chain enters it
		};
		std::vector<Step> chain;
		for (std::size_t k = 0; k < scene.conditions.size(); ++k) {
			const auto& condition = scene.conditions[k];
			const std::string base = n + "_if" + std::to_string(k + 1);
			const std::string role = "if" + std::to_string(k + 1);
			switch (condition.kind) {
				case ChoreoCondition::Kind::ScenePlayed:
				case ChoreoCondition::Kind::SceneNotPlayed: {
					// A private logic_branch the other scene's OnCompletion sets (a shared one would fire
					// every scene's chain when tested).
					const std::size_t mirror = add(s, role, "logic_branch", base, slot);
					wanted[mirror].keys = {{"InitialValue", "0"}};
					if (!trim(condition.name).empty()) addLink(condition.name, s, link("OnCompletion", base, "SetValue", "1"));
					chain.push_back({mirror, {condition.kind == ChoreoCondition::Kind::ScenePlayed ? "OnTrue" : "OnFalse"}, base, "Test"});
					break;
				}
				case ChoreoCondition::Kind::Variable: {
					const ChoreoVariable* var = variable(condition.name);
					if (!var) {
						// A variable that is gone: the condition never holds (checkChoreoLogic says so).
						const std::size_t never = add(s, role, "logic_branch", base, slot);
						wanted[never].keys = {{"InitialValue", "0"}};
						chain.push_back({never, {}, base, "Test"});
					} else if (!var->flag) {
						// A logic_compare kept up to date by the math_counter.
						const std::size_t compare = add(s, role, "logic_compare", base, slot);
						wanted[compare].keys = {{"InitialValue", num(var->initial)}, {"CompareValue", num(condition.value)}};
						addLink(var->name, s, link("OutValue", base, "SetValue"));
						chain.push_back({compare, {compareOutput(condition.compare)}, base, "Compare"});
					} else {
						const bool whenSet = compareHolds(condition.compare, 1, condition.value);
						const bool whenClear = compareHolds(condition.compare, 0, condition.value);
						if (whenSet && whenClear) break;   // always true
						// A private logic_branch, set by a listener on the variable whenever it changes.
						const std::size_t mirror = add(s, role, "logic_branch", base, slot);
						wanted[mirror].keys = {{"InitialValue", var->initial != 0 ? "1" : "0"}};
						const std::size_t watcher = add(s, role + "w", "logic_branch_listener", base + "w", slot);
						wanted[watcher].keys = {{"Branch01", var->name}};
						wanted[watcher].connections = {link("OnAllTrue", base, "SetValue", "1"), link("OnAllFalse", base, "SetValue", "0")};
						std::vector<std::string> outputs;
						if (whenSet) outputs.push_back("OnTrue");
						if (whenClear) outputs.push_back("OnFalse");
						chain.push_back({mirror, outputs, base, "Test"});
					}
					break;
				}
				case ChoreoCondition::Kind::Global: {
					// env_global hands its counter to a logic_compare (SetValueCompare tests it at once).
					const std::size_t compare = add(s, role, "logic_compare", base, slot);
					wanted[compare].keys = {{"InitialValue", "0"}, {"CompareValue", num(condition.value)}};
					if (trim(condition.name).empty()) {
						chain.push_back({compare, {}, base, "Compare"});
						break;
					}
					const std::size_t global = add(s, role + "g", "env_global", base + "g", slot);
					wanted[global].keys = {{"globalstate", std::string(trim(condition.name))},
					                       {"initialstate", "0"},
					                       {"counter", "0"},
					                       {"spawnflags", "0"}};
					wanted[global].connections = {link("OutCounter", base, "SetValueCompare")};
					chain.push_back({compare, {compareOutput(condition.compare)}, base + "g", "GetCounter"});
					break;
				}
				case ChoreoCondition::Kind::Random: {
					const int cases = std::clamp(static_cast<int>(std::lround(condition.value / 10.0)), 0, 10);
					if (cases >= 10) break;   // always
					// Ten cases, PickRandom fires one of those with an output: `cases` go on, the others
					// only reset the shuffle (harmless, but they count as connected).
					const std::size_t pick = add(s, role, "logic_case", base, slot);
					std::vector<std::string> outputs;
					for (int c = 1; c <= 10; ++c) {
						const std::string number = (c < 10 ? "0" : "") + std::to_string(c);
						wanted[pick].keys.emplace_back("Case" + number, std::to_string(c));
						if (c <= cases) outputs.push_back("OnCase" + number);
						else wanted[pick].connections.push_back(link("OnCase" + number, base, "ResetShuffle"));
					}
					chain.push_back({pick, outputs, base, "PickRandom"});
					break;
				}
			}
		}
		const bool hasConditions = !chain.empty();
		{
			std::size_t from = relay;
			std::vector<std::string> outputs = {"OnTrigger"};
			for (const auto& step : chain) {
				for (const auto& output : outputs) wanted[from].connections.push_back(link(output, step.target, step.input));
				from = step.emitter;
				outputs = step.outputs;
			}
			for (const auto& output : outputs) wanted[from].connections.push_back(link(output, n, "Start"));
		}

		// The starts.
		int areas = 0;
		bool everyTime = false;
		const Connection toRelay = link("OnTrigger", n + "_start", "Trigger");
		for (std::size_t i = 0; i < scene.starts.size(); ++i) {
			const auto& start = scene.starts[i];
			const std::string number = std::to_string(i + 1);
			const std::string role = "start" + number;
			const std::string target = trim(start.target).empty() ? firstActor : std::string(trim(start.target));
			const auto fire = [&](std::string_view output) { return link(output, n + "_start", "Trigger", {}, start.delay); };
			std::size_t killable = SIZE_MAX;
			switch (start.kind) {
				case ChoreoStart::Kind::MapStart: {
					const std::size_t automatic = add(s, role, "logic_auto", n + "_auto" + number, slot);
					wanted[automatic].keys = {{"spawnflags", "1"}};
					wanted[automatic].connections = {fire("OnMapSpawn")};
					break;
				}
				case ChoreoStart::Kind::Area:
				case ChoreoStart::Kind::Near:
				case ChoreoStart::Kind::LookAt: {
					const bool look = start.kind == ChoreoStart::Kind::LookAt;
					const bool near = start.kind == ChoreoStart::Kind::Near;
					// trigger_once is used up on the first touch: with conditions that may fail it must
					// keep firing (trigger_multiple) until the scene has started.
					const std::string classname = look ? "trigger_look" : (start.once && !hasConditions) ? "trigger_once" : "trigger_multiple";
					const std::string name = n + (look ? "_look" : near ? "_near" : "_area") + number;
					const std::size_t trigger = add(s, role, classname, name, slot);
					Wanted& w = wanted[trigger];
					w.brush = true;
					w.keys = {{"spawnflags", "1"}, {"StartDisabled", "0"}};
					if (classname == "trigger_multiple") w.keys.emplace_back("wait", "1");
					if (look) {
						w.keys.emplace_back("target", target);
						w.keys.emplace_back("LookTime", "0.5");
						w.keys.emplace_back("FieldOfView", "0.9");
						w.keys.emplace_back("Timeout", "0");
					}
					if (near) {
						const double radius = start.a > 0 ? start.a : 128;
						const Entity* npc = findNamedEntity(doc, target);
						const Vec3 at = npc ? npc->origin() : s.origin;
						w.box.add(Vec3(at.x - radius, at.y - radius, at.z - 16));
						w.box.add(Vec3(at.x + radius, at.y + radius, at.z + 96));
						w.parent = target;
						w.keys.emplace_back("parentname", target);
					} else {
						w.box = areaBoxFor(doc, scene, s.origin, areas++);
					}
					// Once: OnTrigger fires again while the player stays inside, until the conditions let the
					// scene start. Every time: each time the player comes in (OnStartTouch).
					w.connections = {fire(start.once || look ? "OnTrigger" : "OnStartTouch")};
					if (start.once) killable = trigger;
					break;
				}
				case ChoreoStart::Kind::Timer: {
					const std::size_t timer = add(s, role, "logic_timer", n + "_timer" + number, slot);
					Wanted& w = wanted[timer];
					if (start.b > start.a) {
						w.keys = {{"UseRandomTime", "1"}, {"LowerRandomBound", num(start.a)}, {"UpperRandomBound", num(start.b)}, {"StartDisabled", "0"}};
					} else {
						w.keys = {{"UseRandomTime", "0"}, {"RefireTime", num(std::max(0.1, start.a))}, {"StartDisabled", "0"}};
					}
					w.connections = {fire("OnTimer")};
					if (start.once) killable = timer;
					break;
				}
				case ChoreoStart::Kind::Use:
				case ChoreoStart::Kind::Output:
				case ChoreoStart::Kind::AfterScene: {
					std::string source = target, output = start.output;
					if (start.kind == ChoreoStart::Kind::Use && trim(output).empty()) {
						const Entity* entity = findNamedEntity(doc, source);
						output = defaultUseOutput(entity ? entity->classname() : std::string());
					}
					if (start.kind == ChoreoStart::Kind::AfterScene) {
						source = std::string(trim(start.target));
						output = "OnCompletion";
					}
					if (trim(source).empty() || trim(output).empty()) break;
					if (start.once && (hasConditions || scene.starts.size() > 1)) {
						// "Once" with conditions or other starts: a gate the scene removes when it starts.
						const std::size_t gate = add(s, role, "logic_relay", n + "_gate" + number, slot);
						wanted[gate].keys = {{"spawnflags", "0"}};
						wanted[gate].connections = {toRelay};
						addLink(source, s, link(output, n + "_gate" + number, "Trigger", {}, start.delay));
						killable = gate;
					} else {
						addLink(source, s, link(output, n + "_start", "Trigger", {}, start.delay, start.once ? 1 : -1));
					}
					break;
				}
				case ChoreoStart::Kind::Input: break;
			}
			if (killable != SIZE_MAX) addLink(n, s, link("OnStart", wanted[killable].targetname, "Kill"));
			if (!start.once && start.kind != ChoreoStart::Kind::MapStart && start.kind != ChoreoStart::Kind::Input) everyTime = true;
		}
		// Starts that fire again must not restart the scene while it plays.
		if (everyTime) {
			addLink(n, s, link("OnStart", n + "_start", "Disable"));
			addLink(n, s, link("OnCompletion", n + "_start", "Enable"));
			addLink(n, s, link("OnCanceled", n + "_start", "Enable"));
		}

		// "Set variable" steps: the VCD fires the scene's OnTrigger<N>.
		int triggerNumber = 0;
		for (const auto& step : scene.steps) {
			if (step.kind != ChoreoStep::Kind::SetVariable) continue;
			if (++triggerNumber > 16) break;
			const ChoreoVariable* var = variable(step.variable);
			if (!var) continue;
			const std::string output = "OnTrigger" + std::to_string(triggerNumber);
			Connection c;
			if (var->flag) {
				c = step.op == ChoreoStep::VariableOp::Toggle ? link(output, var->name, "Toggle")
				                                              : link(output, var->name, "SetValue", step.value != 0 ? "1" : "0");
			} else if (step.op == ChoreoStep::VariableOp::Add) {
				c = link(output, var->name, "Add", num(step.value));
			} else if (step.op == ChoreoStep::VariableOp::Set) {
				c = link(output, var->name, "SetValue", num(step.value));
			} else {
				continue;   // toggling a number (checkChoreoLogic says so)
			}
			variableLinks[s.id].push_back(std::move(c));
		}
	}
};

std::vector<SceneInfo> sceneInfos(const MapDocument& doc) {
	std::vector<SceneInfo> scenes;
	for (const auto& entity : doc.entities) {
		if (isChoreoScene(entity)) scenes.push_back({entity.id, entity.get("targetname"), readChoreoScene(doc, entity), entity.origin()});
	}
	return scenes;
}

std::map<std::string, ChoreoVariable> variablesByName(const MapDocument& doc) {
	std::map<std::string, ChoreoVariable> variables;
	for (auto& v : choreoVariables(doc)) variables.emplace(toLower(v.name), v);
	return variables;
}

} // namespace

std::vector<std::string> choreoActors(const ChoreoScene& scene) {
	std::vector<std::string> actors;
	for (const auto& step : scene.steps) {
		if (step.kind == ChoreoStep::Kind::SetVariable || trim(step.actor).empty()) continue;
		if (std::none_of(actors.begin(), actors.end(), [&step](const std::string& a) { return iequals(a, step.actor); })) actors.push_back(step.actor);
	}
	return actors;
}

std::string uniqueChoreoName(const MapDocument& doc, std::string_view base) {
	std::string clean;
	for (const char c : trim(base)) {
		const auto u = static_cast<unsigned char>(c);
		clean += (std::isalnum(u) || c == '_' || c == '-') ? c : '_';
	}
	if (clean.empty()) clean = "scene";
	if (!findNamedEntity(doc, clean)) return clean;
	for (int n = 2;; ++n) {
		const std::string name = clean + "_" + std::to_string(n);
		if (!findNamedEntity(doc, name)) return name;
	}
}

std::vector<ChoreoVariable> choreoVariables(const MapDocument& doc) {
	std::vector<ChoreoVariable> variables;
	for (const auto& entity : doc.entities) {
		if (entity.get(CHOREO_VARIABLE_KEY).empty() || entity.get("targetname").empty()) continue;
		ChoreoVariable v;
		v.name = entity.get("targetname");
		v.entityId = entity.id;
		v.flag = iequals(entity.classname(), "logic_branch");
		const std::string initial = v.flag ? entity.get("InitialValue", "0") : entity.get("startvalue", "0");
		v.initial = std::atof(initial.c_str());
		if (!std::isfinite(v.initial)) v.initial = 0;
		variables.push_back(std::move(v));
	}
	return variables;
}

bool isChoreoHelper(const Entity& entity) {
	return !entity.get(CHOREO_SCENE_KEY).empty() && !iequals(entity.classname(), "logic_choreographed_scene");
}

namespace edit {

bool syncChoreoLogic(MapDocument& doc, Transaction& t) {
	bool dirty = false;
	const auto scenes = sceneInfos(doc);
	const auto variables = variablesByName(doc);
	Planner planner{doc, scenes, variables, {}, {}, {}};
	for (const auto& s : scenes) planner.plan(s);
	auto& wanted = planner.wanted;

	// The helpers there are now (points are the scenes' own business).
	struct Existing {
		int id = 0;
		std::string scene, role, classname, targetname;
		bool used = false;
	};
	std::vector<Existing> existing;
	for (const auto& entity : doc.entities) {
		if (!isChoreoHelper(entity) || isPointHelper(entity)) continue;
		existing.push_back({entity.id, entity.get(CHOREO_SCENE_KEY), entity.get(CHOREO_ROLE_KEY), entity.classname(), entity.get("targetname")});
	}
	// Helpers of older maps have no role: the logic_auto / trigger_once was the scene's one start.
	for (auto& ex : existing) {
		if (!ex.role.empty()) continue;
		const auto scene = std::find_if(scenes.begin(), scenes.end(), [&ex](const SceneInfo& s) { return iequals(s.name, ex.scene); });
		if (scene == scenes.end()) continue;
		for (std::size_t i = 0; i < scene->plan.starts.size(); ++i) {
			const auto kind = scene->plan.starts[i].kind;
			const bool fits = (iequals(ex.classname, "logic_auto") && kind == ChoreoStart::Kind::MapStart) ||
			                  (iequals(ex.classname, "trigger_once") && kind == ChoreoStart::Kind::Area);
			const std::string role = "start" + std::to_string(i + 1);
			const bool taken = std::any_of(existing.begin(), existing.end(), [&](const Existing& o) { return iequals(o.scene, ex.scene) && iequals(o.role, role); });
			if (fits && !taken) {
				ex.role = role;
				break;
			}
		}
	}

	// Which scene each helper name belongs to (now or after this sync): outputs aimed at them are Mapsmith's.
	std::map<std::string, std::string> helperOwner;
	for (const auto& ex : existing) {
		if (!ex.targetname.empty()) helperOwner[toLower(ex.targetname)] = toLower(ex.scene);
	}
	for (const auto& w : wanted) helperOwner[toLower(w.targetname)] = toLower(w.scene);
	std::map<std::string, std::set<std::string>> linkSources;   // lower scene -> lower source names (old and new)
	for (const auto& s : scenes) {
		const Entity* entity = doc.findEntity(s.id);
		for (const auto& name : splitLinks(entity->get(LINKS_KEY))) linkSources[toLower(s.name)].insert(toLower(name));
	}
	for (const auto& l : planner.links) linkSources[toLower(l.owner)].insert(toLower(l.source));

	// Helpers: reuse by (scene, role) when the class fits, keeping where the mapper moved them.
	for (const auto& w : wanted) {
		const auto found = std::find_if(existing.begin(), existing.end(), [&w](const Existing& ex) {
			return !ex.used && iequals(ex.scene, w.scene) && iequals(ex.role, w.role) &&
			       (iequals(ex.classname, w.classname) || (w.brush && isBrushTrigger(ex.classname)));
		});
		if (found != existing.end()) {
			found->used = true;
			const Entity* before = doc.findEntity(found->id);
			Entity copy = *before;
			bool newBox = false;
			if (w.brush && (copy.solids.empty() || !iequals(copy.get("parentname"), w.parent))) newBox = true;
			copy.set("classname", w.classname);
			copy.set("targetname", w.targetname);
			for (const auto& [key, value] : w.keys) copy.set(key, value);
			if (w.parent.empty() && w.brush) copy.erase("parentname");
			copy.set(CHOREO_SCENE_KEY, w.scene);
			copy.set(CHOREO_ROLE_KEY, w.role);
			if (!w.brush && !copy.hasOrigin()) copy.setOrigin(w.origin);
			const bool linksChanged = !sameLinks(copy.connections, w.connections);
			if (copy.keyValues == before->keyValues && !linksChanged && !newBox) continue;
			if (linksChanged) copy.connections = w.connections;
			if (newBox) copy.solids = {makeBlock(doc, w.box, "tools/toolstrigger")};
			t.touchEntity(found->id);
			*doc.findEntity(found->id) = std::move(copy);
			dirty = true;
			continue;
		}
		Entity entity;
		entity.id = doc.newEntityId();
		t.touchEntity(entity.id);
		entity.set("classname", w.classname);
		entity.set("targetname", w.targetname);
		for (const auto& [key, value] : w.keys) entity.set(key, value);
		entity.set(CHOREO_SCENE_KEY, w.scene);
		entity.set(CHOREO_ROLE_KEY, w.role);
		if (w.brush) entity.solids.push_back(makeBlock(doc, w.box, "tools/toolstrigger"));
		else entity.setOrigin(w.origin);
		entity.connections = w.connections;
		doc.entities.push_back(std::move(entity));
		dirty = true;
	}
	for (const auto& ex : existing) {
		if (ex.used) continue;
		t.touchEntity(ex.id);
		std::erase_if(doc.entities, [id = ex.id](const Entity& e) { return e.id == id; });
		dirty = true;
	}

	// Outputs on other entities (and on the scenes): only Mapsmith's are replaced, in place of the old ones.
	std::map<std::string, std::vector<Connection>> wantedBySource;
	for (const auto& l : planner.links) wantedBySource[toLower(l.source)].push_back(l.connection);
	std::map<std::string, const SceneInfo*> sceneByName;
	for (const auto& s : scenes) sceneByName[toLower(s.name)] = &s;
	for (auto& entity : doc.entities) {
		if (isChoreoHelper(entity)) continue;
		const std::string name = toLower(entity.get("targetname"));
		const bool scene = isChoreoScene(entity);
		std::vector<Connection> want;
		if (!name.empty()) {
			if (const auto found = wantedBySource.find(name); found != wantedBySource.end()) want = found->second;
		}
		if (scene) {
			if (const auto found = planner.variableLinks.find(entity.id); found != planner.variableLinks.end()) want.insert(want.end(), found->second.begin(), found->second.end());
		}
		const auto owned = [&](const Connection& c) {
			const std::string target = toLower(c.target);
			if (const auto owner = helperOwner.find(target); owner != helperOwner.end() && !name.empty()) {
				if (const auto sources = linkSources.find(owner->second); sources != linkSources.end() && sources->second.contains(name)) return true;
			}
			if (!scene) return false;
			if (startsWithI(c.output, "OnTrigger") && variables.contains(target)) return true;
			// Older maps started "after scene X" with X's OnCompletion -> Start (now through the relay).
			if (iequals(c.output, "OnCompletion") && iequals(c.input, "Start") && c.timesToFire == 1) {
				if (const auto other = sceneByName.find(target); other != sceneByName.end()) {
					const auto& starts = other->second->plan.starts;
					return std::any_of(starts.begin(), starts.end(), [&name](const ChoreoStart& st) {
						return st.kind == ChoreoStart::Kind::AfterScene && iequals(st.target, name);
					});
				}
			}
			return false;
		};
		std::vector<Connection> kept, mine;
		for (const auto& c : entity.connections) (owned(c) ? mine : kept).push_back(c);
		if (sameLinks(mine, want)) continue;
		t.touchEntity(entity.id);
		kept.insert(kept.end(), want.begin(), want.end());
		entity.connections = std::move(kept);
		dirty = true;
	}

	// Remember whose outputs Mapsmith set, so the next sync can take them back.
	std::map<std::string, std::vector<std::string>> newSources;
	for (const auto& l : planner.links) {
		auto& list = newSources[toLower(l.owner)];
		if (std::none_of(list.begin(), list.end(), [&l](const std::string& n) { return iequals(n, l.source); })) list.push_back(l.source);
	}
	for (const auto& s : scenes) {
		std::string value;
		if (const auto found = newSources.find(toLower(s.name)); found != newSources.end()) {
			auto list = found->second;
			std::sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
			for (const auto& n : list) value += (value.empty() ? "" : "|") + n;
		}
		Entity* entity = doc.findEntity(s.id);
		const bool hasKey = std::any_of(entity->keyValues.begin(), entity->keyValues.end(), [](const auto& p) { return iequals(p.first, LINKS_KEY); });
		if (value.empty() ? !hasKey : entity->get(LINKS_KEY) == value) continue;
		t.touchEntity(s.id);
		entity = doc.findEntity(s.id);
		if (value.empty()) entity->erase(LINKS_KEY);
		else entity->set(LINKS_KEY, value);
		dirty = true;
	}
	return dirty;
}

Change createChoreoVariable(MapDocument& doc, std::string_view name, bool flag, double initial, const Vec3& origin) {
	if (trim(name).empty()) return {};
	Transaction t(doc, "New variable");
	Entity variable;
	variable.id = doc.newEntityId();
	t.touchEntity(variable.id);
	variable.set("classname", flag ? "logic_branch" : "math_counter");
	variable.set("targetname", uniqueChoreoName(doc, name));
	if (flag) {
		variable.set("InitialValue", initial != 0 ? "1" : "0");
	} else {
		variable.set("startvalue", num(initial));
		variable.set("min", "0");
		variable.set("max", "0");   // min = max = 0: no limits
	}
	variable.set(CHOREO_VARIABLE_KEY, "1");
	variable.setOrigin(origin);
	doc.entities.push_back(std::move(variable));
	syncChoreoLogic(doc, t);
	return t.finish();
}

namespace {

/// The plan keys of every scene, rewritten through `change` (returns whether any scene changed).
bool rewritePlans(MapDocument& doc, Transaction& t, const std::function<bool(ChoreoScene&)>& change);

} // namespace

Change renameChoreoVariable(MapDocument& doc, std::string_view oldName, std::string_view newName) {
	const auto variables = choreoVariables(doc);
	const auto found = std::find_if(variables.begin(), variables.end(), [oldName](const ChoreoVariable& v) { return iequals(v.name, oldName); });
	const std::string wantedName(trim(newName));
	if (found == variables.end() || wantedName.empty() || wantedName == found->name) return {};
	if (!iequals(wantedName, found->name) && findNamedEntity(doc, wantedName)) return {};   // taken
	Transaction t(doc, "Rename variable");
	t.touchEntity(found->entityId);
	doc.findEntity(found->entityId)->set("targetname", wantedName);
	// Everything aimed at it follows: outputs, listeners, the scenes' conditions and steps.
	for (auto& entity : doc.entities) {
		bool touched = false;
		const auto touch = [&] {
			if (!touched) t.touchEntity(entity.id);
			touched = true;
		};
		for (auto& c : entity.connections) {
			if (iequals(c.target, found->name)) {
				touch();
				c.target = wantedName;
				c.sourceText.clear();
			}
		}
		if (iequals(entity.classname(), "logic_branch_listener")) {
			for (auto& [key, value] : entity.keyValues) {
				if (startsWithI(key, "Branch") && iequals(value, found->name)) {
					touch();
					value = wantedName;
				}
			}
		}
	}
	rewritePlans(doc, t, [&](ChoreoScene& scene) {
		bool changed = false;
		for (auto& c : scene.conditions) {
			if (c.kind == ChoreoCondition::Kind::Variable && iequals(c.name, found->name)) {
				c.name = wantedName;
				changed = true;
			}
		}
		for (auto& s : scene.steps) {
			if (s.kind == ChoreoStep::Kind::SetVariable && iequals(s.variable, found->name)) {
				s.variable = wantedName;
				changed = true;
			}
		}
		return changed;
	});
	syncChoreoLogic(doc, t);
	return t.finish();
}

Change setChoreoVariableInitial(MapDocument& doc, std::string_view name, double initial) {
	const auto variables = choreoVariables(doc);
	const auto found = std::find_if(variables.begin(), variables.end(), [name](const ChoreoVariable& v) { return iequals(v.name, name); });
	if (found == variables.end() || found->initial == initial) return {};
	Transaction t(doc, "Variable value");
	t.touchEntity(found->entityId);
	Entity* entity = doc.findEntity(found->entityId);
	if (found->flag) entity->set("InitialValue", initial != 0 ? "1" : "0");
	else entity->set("startvalue", num(initial));
	syncChoreoLogic(doc, t);
	return t.finish();
}

Change deleteChoreoVariable(MapDocument& doc, std::string_view name) {
	const auto variables = choreoVariables(doc);
	const auto found = std::find_if(variables.begin(), variables.end(), [name](const ChoreoVariable& v) { return iequals(v.name, name); });
	if (found == variables.end()) return {};
	Transaction t(doc, "Delete variable");
	t.touchEntity(found->entityId);
	std::erase_if(doc.entities, [id = found->entityId](const Entity& e) { return e.id == id; });
	// The scenes' outputs to it go (their steps stay and are reported).
	for (auto& entity : doc.entities) {
		if (!isChoreoScene(entity)) continue;
		const auto aimed = [&found](const Connection& c) { return startsWithI(c.output, "OnTrigger") && iequals(c.target, found->name); };
		if (std::none_of(entity.connections.begin(), entity.connections.end(), aimed)) continue;
		t.touchEntity(entity.id);
		std::erase_if(entity.connections, aimed);
	}
	syncChoreoLogic(doc, t);
	return t.finish();
}

namespace {

bool rewritePlans(MapDocument& doc, Transaction& t, const std::function<bool(ChoreoScene&)>& change) {
	bool any = false;
	for (const int id : choreoScenes(doc)) {
		ChoreoScene scene = readChoreoScene(*doc.findEntity(id));
		if (!change(scene)) continue;
		t.touchEntity(id);
		writeChoreoPlanKeys(*doc.findEntity(id), scene);
		any = true;
	}
	return any;
}

} // namespace

} // namespace edit

// --- Checks --------------------------------------------------------------------------------

std::vector<ChoreoLogicIssue> checkChoreoLogic(const MapDocument& doc) {
	std::vector<ChoreoLogicIssue> issues;
	const auto scenes = sceneInfos(doc);
	const auto variables = variablesByName(doc);
	Bounds world;
	for (const auto& solid : doc.world.solids) world.add(buildBrushGeometry(solid).bounds);
	const auto outside = [&world](const Vec3& p) {
		return !world.empty() && (p.x < world.min.x - 1 || p.y < world.min.y - 1 || p.z < world.min.z - 1 || p.x > world.max.x + 1 || p.y > world.max.y + 1 ||
		                          p.z > world.max.z + 1);
	};
	const auto isScene = [&doc](std::string_view name) {
		const Entity* e = findNamedEntity(doc, name);
		return e && iequals(e->classname(), "logic_choreographed_scene");
	};
	for (const auto& s : scenes) {
		const auto say = [&](std::string message, bool warning = true) { issues.push_back({s.id, s.name, std::move(message), warning}); };
		const auto actors = choreoActors(s.plan);
		for (std::size_t i = 0; i < s.plan.starts.size(); ++i) {
			const auto& start = s.plan.starts[i];
			const std::string label = "start " + std::to_string(i + 1) + " (" + describeChoreoStart(start) + ")";
			switch (start.kind) {
				case ChoreoStart::Kind::Use:
				case ChoreoStart::Kind::LookAt:
				case ChoreoStart::Kind::Near:
				case ChoreoStart::Kind::Output:
					if (trim(start.target).empty() && actors.empty()) say(label + " is aimed at the scene's NPC, but the scene has no NPC yet");
					else if (!trim(start.target).empty() && !findNamedEntity(doc, start.target)) say(label + ": no entity is named \"" + start.target + "\"", false);
					if (start.kind == ChoreoStart::Kind::Output && trim(start.output).empty()) say(label + " names no output", false);
					break;
				case ChoreoStart::Kind::AfterScene:
					if (trim(start.target).empty()) say(label + " names no scene", false);
					else if (!isScene(start.target)) say(label + ": there is no scene \"" + start.target + "\"", false);
					else if (iequals(start.target, s.name)) say(label + " waits for itself");
					break;
				default: break;
			}
		}
		// Area and look boxes in the void never see the player.
		for (const auto& entity : doc.entities) {
			if (!iequals(entity.get(CHOREO_SCENE_KEY), s.name) || entity.solids.empty() || entity.get("parentname").size()) continue;
			const Bounds box = solidsBounds(entity);
			if (!box.empty() && outside(box.center())) say("the trigger \"" + entity.get("targetname") + "\" is outside the map - the player can never touch it");
		}
		for (std::size_t k = 0; k < s.plan.conditions.size(); ++k) {
			const auto& c = s.plan.conditions[k];
			const std::string label = "condition " + std::to_string(k + 1) + " (" + describeChoreoCondition(c) + ")";
			switch (c.kind) {
				case ChoreoCondition::Kind::ScenePlayed:
				case ChoreoCondition::Kind::SceneNotPlayed:
					if (trim(c.name).empty() || !isScene(c.name)) say(label + ": there is no scene \"" + c.name + "\" (deleted or renamed?)", false);
					break;
				case ChoreoCondition::Kind::Variable:
					if (!variables.contains(toLower(trim(c.name)))) say(label + ": there is no variable \"" + c.name + "\"", false);
					break;
				case ChoreoCondition::Kind::Global:
					if (trim(c.name).empty()) say(label + " names no global state", false);
					break;
				case ChoreoCondition::Kind::Random:
					if (c.value <= 0) say(label + " never lets the scene start");
					break;
			}
		}
		int sets = 0;
		for (std::size_t i = 0; i < s.plan.steps.size(); ++i) {
			const auto& step = s.plan.steps[i];
			if (step.kind != ChoreoStep::Kind::SetVariable) continue;
			++sets;
			const std::string label = "step " + std::to_string(i + 1);
			const auto var = variables.find(toLower(trim(step.variable)));
			if (var == variables.end()) say(label + " sets \"" + step.variable + "\", but there is no such variable", false);
			else if (!var->second.flag && step.op == ChoreoStep::VariableOp::Toggle) say(label + " toggles \"" + step.variable + "\", but only true/false variables can be toggled", false);
		}
		if (sets > 16) say("more than 16 \"Set variable\" steps - a scene has only 16 trigger outputs; the ones after the 16th do nothing", false);
	}
	// Two scenes of one NPC that start the same way with no condition: both start at once.
	const auto sameStart = [](const ChoreoStart& a, const ChoreoStart& b) {
		if (a.kind != b.kind) return false;
		switch (a.kind) {
			case ChoreoStart::Kind::MapStart: return true;
			case ChoreoStart::Kind::Use:
			case ChoreoStart::Kind::AfterScene:
			case ChoreoStart::Kind::LookAt: return iequals(a.target, b.target);
			case ChoreoStart::Kind::Output: return iequals(a.target, b.target) && iequals(a.output, b.output);
			default: return false;
		}
	};
	for (std::size_t i = 0; i < scenes.size(); ++i) {
		for (std::size_t j = i + 1; j < scenes.size(); ++j) {
			const auto& a = scenes[i].plan;
			const auto& b = scenes[j].plan;
			if (!a.conditions.empty() || !b.conditions.empty()) continue;
			std::string actor;
			for (const auto& x : choreoActors(a)) {
				const auto bActors = choreoActors(b);
				if (std::any_of(bActors.begin(), bActors.end(), [&x](const std::string& y) { return iequals(x, y); })) {
					actor = x;
					break;
				}
			}
			if (actor.empty()) continue;
			const bool clash = std::any_of(a.starts.begin(), a.starts.end(), [&](const ChoreoStart& sa) {
				return std::any_of(b.starts.begin(), b.starts.end(), [&](const ChoreoStart& sb) { return sameStart(sa, sb); });
			});
			if (!clash) continue;
			issues.push_back({scenes[j].id, scenes[j].name,
			                  "starts together with \"" + scenes[i].name + "\" (" + actor + " is in both, same start, no condition) - give one of them a condition or another start",
			                  true});
		}
	}
	return issues;
}

} // namespace sme
