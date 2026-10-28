// checks.cpp - Map problem checks and autofixes (see checks.h).

#include <sme/checks.h>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/doors.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace sme {

namespace {

bool isPlayerStart(const std::string& classname) {
	return startsWithI(classname, "info_player_");
}

bool isLight(const std::string& classname) {
	return startsWithI(classname, "light");
}

/// Entities a target name refers to (supports trailing '*' wildcards, case-insensitive).
std::vector<const Entity*> resolveTarget(const MapDocument& doc, const std::string& target) {
	std::vector<const Entity*> out;
	const bool wildcard = !target.empty() && target.back() == '*';
	const auto prefix = wildcard ? std::string_view(target).substr(0, target.size() - 1) : std::string_view(target);
	for (const auto& entity : doc.entities) {
		const auto name = entity.get("targetname");
		const auto cls = entity.classname();
		const bool match = wildcard ? (startsWithI(name, prefix) || startsWithI(cls, prefix)) : (iequals(name, target) || iequals(cls, target));
		if (match) {
			out.push_back(&entity);
		}
	}
	return out;
}

std::vector<std::string> ioNames(const std::vector<EntityIo>& list) {
	std::vector<std::string> out;
	for (const auto& io : list) {
		out.push_back(io.name);
	}
	return out;
}

bool hasIo(const std::vector<EntityIo>& list, const std::string& name) {
	return std::any_of(list.begin(), list.end(), [&](const EntityIo& io) { return iequals(io.name, name); });
}

std::string describeEntity(const Entity& e) {
	const auto name = e.get("targetname");
	return e.classname() + (name.empty() ? "" : " \"" + name + "\"") + " (id " + std::to_string(e.id) + ")";
}

void forEachSolid(const MapDocument& doc, const std::function<void(const SolidRef&, const Solid&)>& fn) {
	for (const auto& solid : doc.world.solids) {
		fn({0, solid.id}, solid);
	}
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) {
			fn({entity.id, solid.id}, solid);
		}
	}
}

std::string formatPoint(const Vec3& p) {
	std::ostringstream s;
	s << static_cast<long long>(std::llround(p.x)) << " " << static_cast<long long>(std::llround(p.y)) << " " << static_cast<long long>(std::llround(p.z));
	return s.str();
}

} // namespace

std::vector<Problem> checkMap(const MapDocument& doc, const CheckContext& context) {
	std::vector<Problem> problems;

	// Brushes: geometry and texture scale.
	std::map<std::string, Problem> missingMaterials;
	std::set<std::string> checkedMaterials;
	Problem badScale;
	badScale.kind = Problem::Kind::BadTextureScale;
	badScale.severity = Problem::Severity::Error;
	forEachSolid(doc, [&](const SolidRef& ref, const Solid& solid) {
		if (!isConvexValid(solid)) {
			Problem p;
			p.kind = Problem::Kind::InvalidBrush;
			p.severity = Problem::Severity::Error;
			p.message = "Brush " + std::to_string(solid.id) + " is not a valid convex solid (VBSP will drop it or fail).";
			p.howToFix = "Delete it and build it again from blocks, or move its vertices so no face is bent inwards.";
			if (ref.entityId == 0) {
				p.objects.solids.push_back(ref);
			} else {
				p.objects.entities.push_back(ref.entityId);
			}
			p.from = std::to_string(ref.entityId) + ":" + std::to_string(solid.id);
			p.fixLabel = "Delete brush";
			problems.push_back(std::move(p));
		}
		for (const auto& side : solid.sides) {
			if (side.u.scale == 0 || side.v.scale == 0) {
				badScale.objects.faces.push_back({ref, side.id});
			}
			const auto name = normalizeMaterialName(side.material);
			if (!context.files || name.empty()) {
				continue;
			}
			if (checkedMaterials.insert(name).second) {
				if (!context.files->exists("materials/" + name + ".vmt")) {
					Problem p;
					p.kind = Problem::Kind::MissingMaterial;
					p.severity = Problem::Severity::Warning;
					p.from = side.material;
					p.message = "Material \"" + side.material + "\" does not exist in this game.";
					if (context.materialNames) {
						if (auto suggestion = closestMatch(name, *context.materialNames); !suggestion.empty()) {
							p.to = toLower(suggestion);
							std::transform(p.to.begin(), p.to.end(), p.to.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
							p.fixLabel = "Use " + p.to;
							p.howToFix = "Probably a typo - did you mean \"" + p.to + "\"?";
						}
					}
					if (p.howToFix.empty()) {
						p.howToFix = "Pick another material in the material browser, or add the missing VMT/VTF to the mod.";
					}
					missingMaterials.emplace(name, std::move(p));
				}
			}
			if (const auto it = missingMaterials.find(name); it != missingMaterials.end()) {
				it->second.objects.faces.push_back({ref, side.id});
			}
		}
	});
	for (auto& [name, p] : missingMaterials) {
		p.message += " Used on " + std::to_string(p.objects.faces.size()) + " face(s).";
		problems.push_back(std::move(p));
	}
	if (!badScale.objects.faces.empty()) {
		badScale.message = std::to_string(badScale.objects.faces.size()) + " face(s) have a texture scale of 0 (the compile fails).";
		badScale.howToFix = "Reset the texture scale to 0.25.";
		badScale.fixLabel = "Reset scale";
		problems.push_back(std::move(badScale));
	}

	// Entities.
	bool playerStart = false, lights = false;
	CheckCache localCache;
	CheckCache& cache = context.cache ? *context.cache : localCache;
	Selection skyCameras;
	std::vector<std::string> classNames;
	if (context.entities) {
		for (const auto* e : context.entities->placeable()) {
			classNames.push_back(e->name);
		}
	}
	for (const auto& entity : doc.entities) {
		const auto classname = entity.classname();
		playerStart |= isPlayerStart(classname);
		lights |= isLight(classname);
		const EntityClass* definition = context.entities ? context.entities->find(classname) : nullptr;
		if (context.entities && !definition) {
			Problem p;
			p.kind = Problem::Kind::UnknownClass;
			p.severity = Problem::Severity::Warning;
			p.entityId = entity.id;
			p.objects.entities.push_back(entity.id);
			p.from = classname;
			p.message = "Entity class \"" + classname + "\" is not in the loaded FGDs.";
			if (auto suggestion = closestMatch(classname, classNames); !suggestion.empty()) {
				p.to = suggestion;
				p.fixLabel = "Rename to " + suggestion;
				p.howToFix = "Probably a typo - did you mean \"" + suggestion + "\"?";
			} else {
				p.howToFix = "Check the spelling, or add the FGD of the mod that defines it (Game settings).";
			}
			problems.push_back(std::move(p));
		}
		if (definition && definition->isSolid() && entity.solids.empty()) {
			Problem p;
			p.kind = Problem::Kind::BrushEntityWithoutBrushes;
			p.severity = Problem::Severity::Error;
			p.entityId = entity.id;
			p.objects.entities.push_back(entity.id);
			p.message = describeEntity(entity) + " is a brush entity without brushes.";
			p.howToFix = "Delete it, or select brushes and tie them to a new " + classname + ".";
			p.fixLabel = "Delete entity";
			problems.push_back(std::move(p));
		}
		if (definition && !definition->isSolid() && !entity.solids.empty()) {
			Problem p;
			p.kind = Problem::Kind::PointClassWithBrushes;
			p.severity = Problem::Severity::Warning;
			p.entityId = entity.id;
			p.objects.entities.push_back(entity.id);
			p.message = describeEntity(entity) + " is a point entity but has brushes.";
			p.howToFix = "Move the brushes to the world (Ctrl+Shift+W) or use a brush entity class.";
			problems.push_back(std::move(p));
		}
		// Model keys.
		if (context.files) {
			const auto model = entity.get("model");
			if (!model.empty() && endsWithI(model, ".mdl") && !context.files->exists(model)) {
				Problem p;
				p.kind = Problem::Kind::MissingModel;
				p.severity = Problem::Severity::Warning;
				p.entityId = entity.id;
				p.objects.entities.push_back(entity.id);
				p.message = describeEntity(entity) + " uses the model \"" + model + "\" which does not exist.";
				p.howToFix = "Pick a model in the model browser (or add it to the mod).";
				problems.push_back(std::move(p));
			} else if (!model.empty() && startsWithI(classname, "prop_")) {
				const auto key = toLower(model);
				auto it = cache.models.find(key);
				if (it == cache.models.end()) {
					it = cache.models.emplace(key, readPropModelFlags(*context.files, model)).first;
				}
				if (it->second) {
					if (const auto better = betterPropClass(classname, *it->second); !better.empty()) {
						Problem p;
						p.kind = Problem::Kind::PropClassMismatch;
						p.severity = Problem::Severity::Error;
						p.entityId = entity.id;
						p.objects.entities.push_back(entity.id);
						p.from = classname;
						p.to = better;
						p.message = iequals(classname, "prop_static")
						                ? describeEntity(entity) + ": \"" + model + "\" is not a static prop model - VBSP deletes this prop_static."
						            : better == "prop_door_rotating"
						                ? describeEntity(entity) + ": \"" + model + "\" is a door model - as a prop_physics it falls over instead of opening."
						                : describeEntity(entity) + ": \"" + model + "\" has no collision model - the game removes this prop_physics at spawn.";
						p.howToFix = "Use " + better + " (the class this model is made for).";
						p.fixLabel = "Make " + better;
						problems.push_back(std::move(p));
					}
				}
			}
			// A looping WAV needs "Is NOT Looped" off and a one-shot WAV needs it on, else the
			// sound cannot be stopped or never plays again.
			if (iequals(classname, "ambient_generic")) {
				if (const auto sound = entity.get("message"); !sound.empty()) {
					const auto key = toLower(sound);
					auto it = cache.soundLoops.find(key);
					if (it == cache.soundLoops.end()) {
						if (!cache.soundScripts) {
							cache.soundScripts = soundScripts(*context.files);
						}
						it = cache.soundLoops.emplace(key, soundLoops(*context.files, sound, &*cache.soundScripts)).first;
					}
					const int flags = std::atoi(entity.get("spawnflags", "0").c_str());
					const bool notLooped = (flags & AMBIENT_NOT_LOOPED) != 0;
					if (it->second && *it->second == notLooped) {
						Problem p;
						p.kind = Problem::Kind::SoundLoopMismatch;
						p.severity = Problem::Severity::Warning;
						p.entityId = entity.id;
						p.objects.entities.push_back(entity.id);
						p.to = std::to_string(flags ^ AMBIENT_NOT_LOOPED);
						if (*it->second) {
							p.message = describeEntity(entity) + ": \"" + sound + "\" loops, but \"Is NOT Looped\" is on - the sound plays forever and cannot be stopped.";
							p.howToFix = "Turn \"Is NOT Looped\" off, or use a sound without a loop point.";
							p.fixLabel = "Turn \"Is NOT Looped\" off";
						} else {
							p.message = describeEntity(entity) + ": \"" + sound + "\" plays once, but \"Is NOT Looped\" is off - it plays only the first time.";
							p.howToFix = "Turn \"Is NOT Looped\" on, or use a sound with a loop point.";
							p.fixLabel = "Turn \"Is NOT Looped\" on";
						}
						problems.push_back(std::move(p));
					}
				}
			}
		}
		if (iequals(classname, "sky_camera")) {
			skyCameras.entities.push_back(entity.id);
		}
		// Outputs.
		for (const auto& c : entity.connections) {
			if (definition && !definition->outputs.empty() && !hasIo(definition->outputs, c.output)) {
				Problem p;
				p.kind = Problem::Kind::UnknownOutput;
				p.severity = Problem::Severity::Warning;
				p.entityId = entity.id;
				p.connection = c;
				p.objects.entities.push_back(entity.id);
				p.from = c.output;
				p.message = describeEntity(entity) + " has an output \"" + c.output + "\" its class does not know.";
				if (auto s = closestMatch(c.output, ioNames(definition->outputs)); !s.empty()) {
					p.to = s;
					p.fixLabel = "Use " + s;
					p.howToFix = "Probably a typo - did you mean \"" + s + "\"?";
				} else {
					p.howToFix = "Pick one of the class's outputs in the Outputs list.";
				}
				problems.push_back(std::move(p));
			}
			if (c.target.empty() || c.target.starts_with('!')) {
				continue; // !player, !self, !activator, ... are resolved at runtime
			}
			const auto targets = resolveTarget(doc, c.target);
			if (targets.empty()) {
				Problem p;
				p.kind = Problem::Kind::OutputTargetMissing;
				p.severity = Problem::Severity::Error;
				p.entityId = entity.id;
				p.connection = c;
				p.objects.entities.push_back(entity.id);
				p.message = describeEntity(entity) + ": output " + c.output + " targets \"" + c.target + "\", but no entity has that name.";
				p.howToFix = "Name the target entity \"" + c.target + "\", pick an existing target, or remove the output.";
				p.fixLabel = "Remove output";
				problems.push_back(std::move(p));
				continue;
			}
			if (!context.entities) {
				continue;
			}
			const auto* targetClass = context.entities->find(targets.front()->classname());
			if (targetClass && !targetClass->inputs.empty() && !hasIo(targetClass->inputs, c.input)) {
				Problem p;
				p.kind = Problem::Kind::UnknownInput;
				p.severity = Problem::Severity::Warning;
				p.entityId = entity.id;
				p.connection = c;
				p.objects.entities.push_back(entity.id);
				p.from = c.input;
				p.message = describeEntity(entity) + ": " + targets.front()->classname() + " \"" + c.target + "\" has no input \"" + c.input + "\".";
				if (auto s = closestMatch(c.input, ioNames(targetClass->inputs)); !s.empty()) {
					p.to = s;
					p.fixLabel = "Use " + s;
					p.howToFix = "Probably a typo - did you mean \"" + s + "\"?";
				} else {
					p.howToFix = "Pick one of the target's inputs in the Outputs editor.";
				}
				problems.push_back(std::move(p));
			}
		}
	}
	// A door in an areaportal must be linked to it, else the portal stays shut (or open) for good.
	for (const auto& entity : doc.entities) {
		if (!isDoor(entity)) {
			continue;
		}
		const Entity* portal = doorAreaportal(doc, entity);
		if (!portal || areaportalLinked(*portal, entity) || (!portal->get("target").empty() && !resolveTarget(doc, portal->get("target")).empty())) {
			continue;
		}
		Problem p;
		p.kind = Problem::Kind::DoorAreaportalNotLinked;
		p.severity = Problem::Severity::Warning;
		p.entityId = entity.id;
		p.objects.entities = {entity.id, portal->id};
		p.message = describeEntity(entity) + " sits in a func_areaportal that is not linked to it - the portal will not open and close with the door.";
		p.howToFix = "Set the areaportal's \"target\" to the door's name (the door's Door box links it with one click).";
		p.fixLabel = "Link areaportal";
		problems.push_back(std::move(p));
	}
	if (!playerStart && doc.solidCount() > 0) {
		Problem p;
		p.kind = Problem::Kind::NoPlayerStart;
		p.severity = Problem::Severity::Warning;
		p.message = "The map has no info_player_start - the player spawns at 0 0 0, often inside a wall.";
		p.howToFix = "Place an info_player_start (Shift+E) where the player should begin.";
		p.fixLabel = "Add player start";
		problems.push_back(std::move(p));
	}
	if (!lights && doc.solidCount() > 0) {
		Problem p;
		p.kind = Problem::Kind::NoLights;
		p.severity = Problem::Severity::Info;
		p.message = "The map has no light entities - after VRAD everything is fullbright or black.";
		p.howToFix = "Add a light_environment (for outdoor areas) or light entities.";
		problems.push_back(std::move(p));
	}
	if (skyCameras.entities.size() > 1) {
		Problem p;
		p.kind = Problem::Kind::MultipleSkyCameras;
		p.severity = Problem::Severity::Warning;
		p.objects = skyCameras;
		p.message = "The map has " + std::to_string(skyCameras.entities.size()) +
		            " sky_camera entities - the engine uses only one, the 3D skybox may show the wrong one.";
		p.howToFix = "Keep one sky_camera in the 3D skybox and delete the others.";
		problems.push_back(std::move(p));
	}
	return problems;
}

Change applyFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities) {
	switch (problem.kind) {
		case Problem::Kind::InvalidBrush: {
			const auto colon = problem.from.find(':');
			if (colon == std::string::npos) {
				return {};
			}
			const SolidRef ref{std::atoi(problem.from.substr(0, colon).c_str()), std::atoi(problem.from.substr(colon + 1).c_str())};
			Selection sel;
			sel.solids.push_back(ref);
			Transaction t(doc, "Delete invalid brush");
			t.touchSolid(ref);
			if (auto* entity = doc.findEntity(ref.entityId)) {
				std::erase_if(entity->solids, [&](const Solid& s) { return s.id == ref.solidId; });
			}
			return t.finish();
		}
		case Problem::Kind::MissingMaterial: {
			if (problem.to.empty()) {
				return {};
			}
			return edit::modifyFaces(doc, problem.objects.faces, "Fix material", [&](Side& side) {
				if (iequals(side.material, problem.from)) {
					side.material = problem.to;
				}
			});
		}
		case Problem::Kind::BadTextureScale:
			return edit::modifyFaces(doc, problem.objects.faces, "Reset texture scale", [](Side& side) {
				if (side.u.scale == 0) side.u.scale = 0.25;
				if (side.v.scale == 0) side.v.scale = 0.25;
			});
		case Problem::Kind::UnknownClass:
			if (problem.to.empty()) {
				return {};
			}
			return edit::setKeyValue(doc, {problem.entityId}, "classname", problem.to);
		case Problem::Kind::PropClassMismatch: {
			if (problem.to.empty()) {
				return {};
			}
			auto change = edit::setKeyValue(doc, {problem.entityId}, "classname", problem.to);
			change.name = "Make " + problem.to;
			return change;
		}
		case Problem::Kind::SoundLoopMismatch: {
			auto change = edit::setKeyValue(doc, {problem.entityId}, "spawnflags", problem.to);
			change.name = "Fix \"Is NOT Looped\"";
			return change;
		}
		case Problem::Kind::BrushEntityWithoutBrushes: {
			Selection sel;
			sel.entities.push_back(problem.entityId);
			return edit::remove(doc, sel);
		}
		case Problem::Kind::OutputTargetMissing:
		case Problem::Kind::UnknownOutput:
		case Problem::Kind::UnknownInput: {
			const auto* entity = doc.findEntity(problem.entityId);
			if (!entity || (problem.kind != Problem::Kind::OutputTargetMissing && problem.to.empty())) {
				return {};
			}
			auto connections = entity->connections;
			for (auto it = connections.begin(); it != connections.end(); ++it) {
				const auto& c = *it;
				if (c.output == problem.connection.output && c.target == problem.connection.target && c.input == problem.connection.input &&
				    c.parameter == problem.connection.parameter) {
					if (problem.kind == Problem::Kind::OutputTargetMissing) {
						connections.erase(it);
					} else if (problem.kind == Problem::Kind::UnknownOutput) {
						it->output = problem.to;
					} else {
						it->input = problem.to;
					}
					break;
				}
			}
			return edit::setConnections(doc, problem.entityId, connections);
		}
		case Problem::Kind::DoorAreaportalNotLinked: {
			if (problem.objects.entities.size() != 2) {
				return {};
			}
			return edit::linkAreaportal(doc, problem.objects.entities[0], problem.objects.entities[1]);
		}
		case Problem::Kind::NoPlayerStart: {
			// On top of the brush with the largest upward-facing area.
			Vec3 spot{0, 0, 16};
			double bestArea = 0;
			for (const auto& solid : doc.world.solids) {
				for (const auto& face : buildBrushGeometry(solid).faces) {
					if (face.normal.z < 0.9) {
						continue;
					}
					Vec3 areaVec, center;
					for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
						areaVec += cross(face.vertices[i] - face.vertices[0], face.vertices[i + 1] - face.vertices[0]);
					}
					for (const auto& v : face.vertices) {
						center += v;
					}
					const double area = length(areaVec) / 2;
					if (area > bestArea) {
						bestArea = area;
						spot = center / static_cast<double>(face.vertices.size());
					}
				}
			}
			spot = snap(spot, 1);
			const auto* definition = entities ? entities->find("info_player_start") : nullptr;
			return edit::placeEntity(doc, "info_player_start", spot, definition).first;
		}
		default:
			return {};
	}
}

std::vector<Vec3> readPointfile(std::string_view text) {
	std::vector<Vec3> points;
	std::size_t start = 0;
	while (start < text.size()) {
		auto end = text.find('\n', start);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		Vec3 p;
		if (parseVec3(trim(text.substr(start, end - start)), p)) {
			points.push_back(p);
		}
		start = end + 1;
	}
	return points;
}

Problem leakProblem(const MapDocument& doc, const std::vector<Vec3>& line, const EntityCatalog* entities) {
	Problem p;
	p.kind = Problem::Kind::Leak;
	p.severity = Problem::Severity::Error;
	p.line = line;
	if (line.empty()) {
		p.message = "The map leaks, but the pointfile is empty.";
		return p;
	}
	// The pointfile runs from an entity inside the map to the void; find that entity.
	const Entity* best = nullptr;
	double bestDistance = 1e30;
	Vec3 bestEnd;
	for (const auto& entity : doc.entities) {
		if (!entity.hasOrigin()) {
			continue;
		}
		for (const auto& end : {line.front(), line.back()}) {
			const double d = length(entity.origin() - end);
			if (d < bestDistance) {
				bestDistance = d;
				best = &entity;
				bestEnd = end;
			}
		}
	}
	const Vec3 outside = (best && bestEnd == line.front()) ? line.back() : line.front();
	p.message = "The map is not sealed (leak)";
	if (best) {
		p.entityId = best->id;
		p.objects.entities.push_back(best->id);
		p.message += ": " + describeEntity(*best) + " can see the void";
	}
	p.message += ". The red line leads from it to the hole near " + formatPoint(outside) + ".";
	p.howToFix = "Follow the red line to where it leaves the map and close that gap with a brush (nodraw or a skybox brush). "
	             "Entities outside the map also cause leaks - move them inside.";
	(void)entities;
	return p;
}

} // namespace sme
