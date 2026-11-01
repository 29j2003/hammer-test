// editor_report.cpp - The editor report and its replay (see editor_report.h).

#include <sme/editor_report.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/shipping.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace sme {

std::string reportNumber(double value) {
	char text[48];
	std::snprintf(text, sizeof text, "%.9g", std::abs(value) < 1e-9 ? 0.0 : value);
	return text;
}

namespace {

std::string number(double value) {
	return reportNumber(value);
}

std::string vec(const Vec3& v) {
	return number(v.x) + " " + number(v.y) + " " + number(v.z);
}

std::string solidName(const SolidRef& ref) {
	return std::to_string(ref.entityId) + "/" + std::to_string(ref.solidId);
}

std::optional<Vec3> parseVec(const std::string& text) {
	std::istringstream in(text);
	Vec3 v;
	if (in >> v.x >> v.y >> v.z) return v;
	return std::nullopt;
}

std::string axisText(const TextureAxis& axis) {
	return "[" + vec(axis.axis) + " " + number(axis.shift) + "] " + number(axis.scale);
}

const char* flowModeName(FlowMode mode) {
	switch (mode) {
		case FlowMode::Auto: return "auto";
		case FlowMode::Wrap: return "wrap";
		case FlowMode::Path: return "path";
		case FlowMode::Curve: return "curve";
		case FlowMode::Match: return "match";
	}
	return "auto";
}

const char* flowPresetName(FlowPreset preset) {
	switch (preset) {
		case FlowPreset::Road: return "road";
		case FlowPreset::Kerb: return "kerb";
		case FlowPreset::River: return "river";
		case FlowPreset::Trim: return "trim";
		case FlowPreset::Fence: return "fence";
	}
	return "road";
}

bool boxesTouch(const Bounds& a, const Bounds& b) {
	if (a.empty() || b.empty()) return false;
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] < b.min[i] || b.max[i] < a.min[i]) return false;
	}
	return true;
}

Bounds solidBox(const Solid& solid) {
	Bounds box = buildBrushGeometry(solid).bounds;
	if (box.empty()) {
		for (const auto& side : solid.sides) {
			for (const auto& p : side.planePoints) box.add(p);
		}
	}
	return box;
}

} // namespace

std::string formatPickTrace(const PickTrace& trace, int number) {
	std::string out = "pick #" + std::to_string(number) + " view=" + trace.view + " size=" + std::to_string(trace.viewWidth) + "x" +
	                  std::to_string(trace.viewHeight) + " at=" + sme::number(trace.screenX) + "," + sme::number(trace.screenY) + " tool=" + trace.tool +
	                  " mode=" + (trace.faceMode ? "faces" : "objects") + " modifiers=" + (trace.modifiers.empty() ? "-" : trace.modifiers) +
	                  " cycle=" + std::to_string(trace.cycle) + "\n";
	out += "  ray origin=" + vec(trace.ray.origin) + " dir=" + vec(trace.ray.direction) + "\n";
	for (const auto& c : trace.candidates) {
		out += "  hit " + c.kind + " ";
		out += c.kind == "brush" ? solidName({c.entityId, c.solidId}) + " side " + std::to_string(c.sideId) : "entity " + std::to_string(c.entityId);
		out += " at " + sme::number(c.distance);
		if (!c.note.empty()) out += " (" + c.note + ")";
		out += "\n";
	}
	for (const auto& s : trace.skipped) out += "  skipped " + s + "\n";
	if (trace.ties > 1) out += "  ties " + std::to_string(trace.ties) + " (" + trace.order + ")\n";
	out += "  result " + (trace.result.empty() ? std::string("nothing") : trace.result) + "\n";
	if (!trace.selection.empty()) out += "  selection " + trace.selection + "\n";
	return out;
}

std::vector<LoggedPick> parsePickLog(std::string_view text) {
	std::vector<LoggedPick> picks;
	std::istringstream in{std::string(text)};
	std::string line;
	static const std::regex head(R"(^pick #(\d+) view=(\S*) .* mode=(\S+))");
	static const std::regex ray(R"(^\s+ray origin=(\S+ \S+ \S+) dir=(\S+ \S+ \S+))");
	static const std::regex result(R"(^\s+result (.*)$)");
	std::smatch m;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (std::regex_search(line, m, head)) {
			LoggedPick pick;
			pick.number = std::atoi(m[1].str().c_str());
			pick.view = m[2].str();
			pick.faceMode = m[3].str() == "faces";
			picks.push_back(std::move(pick));
		} else if (!picks.empty() && std::regex_search(line, m, ray)) {
			if (const auto o = parseVec(m[1].str())) picks.back().ray.origin = *o;
			if (const auto d = parseVec(m[2].str())) picks.back().ray.direction = *d;
		} else if (!picks.empty() && std::regex_search(line, m, result)) {
			picks.back().result = m[1].str();
		}
	}
	return picks;
}

std::vector<RayHit> brushesAlongRay(const MapDocument& doc, const Ray& ray, const Selection* hidden) {
	std::vector<RayHit> hits;
	const auto test = [&](const Solid& solid, int entityId) {
		const SolidRef ref{entityId, solid.id};
		if (hidden && ((entityId == 0 && hidden->contains(ref)) || (entityId != 0 && hidden->containsEntity(entityId)))) return;
		bool inside = false;
		if (const auto hit = intersectSeen(ray, solid, &inside)) {
			const int sideId = solid.sides[static_cast<std::size_t>(hit->sideIndex)].id;
			// A hidden face (tool textures hidden) is not seen: the ray goes on behind it.
			if (hidden && hidden->contains(FaceRef{ref, sideId})) return;
			hits.push_back({ref, sideId, hit->distance, inside});
		}
	};
	for (const auto& solid : doc.world.solids) test(solid, 0);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) test(solid, entity.id);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.distance < b.distance; });
	return hits;
}

std::string describeSolid(const Solid& solid) {
	const Bounds box = solidBox(solid);
	const BrushGeometry geometry = buildBrushGeometry(solid);
	std::string out = "  brush " + std::to_string(solid.id) + ": box (" + vec(box.min) + ") - (" + vec(box.max) + ")" +
	                  (geometry.valid ? "" : " INVALID") + ", " + std::to_string(geometry.faces.size()) + " of " + std::to_string(solid.sides.size()) +
	                  " sides have a face\n";
	const Vec3 centre = box.empty() ? Vec3{} : box.center();
	for (const auto& side : solid.sides) {
		const Plane plane = side.plane();
		const bool outward = plane.distanceTo(centre) < 0;
		out += "    side " + std::to_string(side.id) + ": (" + vec(side.planePoints[0]) + ") (" + vec(side.planePoints[1]) + ") (" +
		       vec(side.planePoints[2]) + ") normal " + vec(plane.normal) + (outward ? "" : " POINTS INWARD") + " " + side.material + "\n";
	}
	return out;
}

std::string describeChange(const Change& change, std::size_t maxBrushes) {
	std::string out = "edit \"" + change.name + "\": " + std::to_string(change.solids.size()) + " brush(es), " + std::to_string(change.entities.size()) +
	                  " entit(ies)\n";
	std::size_t described = 0;
	for (const auto& state : change.solids) {
		if (described++ >= maxBrushes) {
			out += " ... " + std::to_string(change.solids.size() - maxBrushes) + " more brush(es)\n";
			break;
		}
		out += " brush " + solidName(state.ref) + (state.before ? "" : " (new)") + (state.after ? "" : " (removed)") + "\n";
		if (state.before) out += " before:\n" + describeSolid(*state.before);
		if (state.after) out += " after:\n" + describeSolid(*state.after);
	}
	for (const auto& state : change.entities) {
		out += " entity " + std::to_string(state.entityId) + (state.before ? "" : " (new)") + (state.after ? "" : " (removed)");
		const Entity* e = state.after ? &*state.after : state.before ? &*state.before : nullptr;
		if (e) out += " " + e->classname() + ", " + std::to_string(e->solids.size()) + " brush(es)";
		out += "\n";
		if (state.after && described < maxBrushes) {
			for (const auto& solid : state.after->solids) {
				if (described++ >= maxBrushes) break;
				out += describeSolid(solid);
			}
		}
	}
	return out;
}

std::string describeRoadPlan(const RoadPlan& plan, const RoadOptions& options) {
	std::string out = "road plan: material \"" + plan.material + "\", along " + (plan.alongV ? "V" : "U") + ", " + std::to_string(plan.chains) +
	                  " road(s), " + std::to_string(plan.faceCount()) + " face(s), left out " + std::to_string(plan.ignored) + ", kerbs " +
	                  std::to_string(plan.kerbs) + ", edge pieces " + std::to_string(plan.edgePieces) + ", curve corners " +
	                  std::to_string(plan.corners.size()) + ", square corners " + std::to_string(plan.squareCorners) + "\n";
	out += "settings: material \"" + options.material + "\" along " +
	       (options.along == RoadAlong::Auto ? "auto" : options.along == RoadAlong::U ? "U" : "V") + " flip " + (options.flip ? "yes" : "no") +
	       " repeats " + std::to_string(options.repeatsAcross) + " width " + number(options.roadWidth) + " curve corners " +
	       (options.curveCorners ? "yes" : "no") + " kerb material \"" + options.sideMaterial + "\"\n";
	for (std::size_t i = 0; i < plan.widths.size(); ++i) out += "road " + std::to_string(i) + " width " + number(plan.widths[i]) + "\n";
	for (const auto& note : plan.notes) out += "note: " + note + "\n";
	for (const auto& change : plan.faces) {
		out += "face " + solidName(change.face.solid) + " side " + std::to_string(change.face.sideId) + " road " + std::to_string(change.chain) +
		       ": u " + axisText(change.side.u) + " v " + axisText(change.side.v) + "\n";
	}
	for (const auto& corner : plan.corners) {
		out += "corner " + solidName(corner.original) + " cut into " + std::to_string(corner.pieces.size()) + " piece(s)\n";
		for (const auto& piece : corner.pieces) out += describeSolid(piece);
	}
	return out;
}

std::string describeFlowPlan(const FlowPlan& plan, const FlowOptions& options) {
	std::string out = "texture flow: mode " + std::string(flowModeName(plan.mode)) + " (asked " + flowModeName(options.mode) + "), preset " +
	                  flowPresetName(plan.preset) + ", seams " + std::to_string(plan.seams) + ", unreached " + std::to_string(plan.unreached) +
	                  (plan.stretched ? ", stretched" : "") + (plan.unevenStrips ? ", uneven strips" : "") + ", strip size " + number(plan.stripSize) + "\n";
	if (plan.seed) out += "seed " + solidName(plan.seed->solid) + " side " + std::to_string(plan.seed->sideId) + "\n";
	RoadOptions road;
	road.material = options.material;
	road.flip = options.flip;
	road.repeatsAcross = options.repeatsAcross;
	road.curveCorners = options.curveCorners;
	return out + describeRoadPlan(plan.plan, road);
}

MapDocument reportScene(const MapDocument& doc, const Bounds& area, double margin, std::size_t wholeMapBytes, std::size_t* keptSolids,
                        std::size_t* keptEntities) {
	const std::string whole = doc.serialize();
	if (whole.size() <= wholeMapBytes || area.empty()) {
		MapDocument copy = MapDocument::parse(whole);
		if (keptSolids) *keptSolids = copy.solidCount();
		if (keptEntities) *keptEntities = copy.entities.size();
		return copy;
	}
	Bounds around = area;
	around.min -= Vec3{margin, margin, margin};
	around.max += Vec3{margin, margin, margin};
	MapDocument scene;
	scene.headerBlocks = doc.headerBlocks;
	scene.world = doc.world;
	scene.world.solids.clear();
	scene.world.source = nullptr;
	std::size_t solids = 0;
	for (const auto& solid : doc.world.solids) {
		if (boxesTouch(solidBox(solid), around)) {
			scene.world.solids.push_back(solid);
			++solids;
		}
	}
	for (const auto& entity : doc.entities) {
		Bounds box;
		for (const auto& solid : entity.solids) box.add(solidBox(solid));
		if (entity.solids.empty() && entity.hasOrigin()) {
			const Vec3 o = entity.origin();
			box.add(o - Vec3{16, 16, 16});
			box.add(o + Vec3{16, 16, 16});
		}
		if (boxesTouch(box, around)) {
			scene.entities.push_back(entity);
			solids += entity.solids.size();
		}
	}
	scene.updateIdCounters();
	if (keptSolids) *keptSolids = solids;
	if (keptEntities) *keptEntities = scene.entities.size();
	return scene;
}

std::string homeFolder() {
#ifdef _WIN32
	if (const char* profile = std::getenv("USERPROFILE")) return profile;
#endif
	if (const char* home = std::getenv("HOME")) return home;
	return {};
}

std::string maskPersonalText(std::string text, std::string_view home) {
	if (home.size() > 1) {
		std::string slashes(home), backslashes(home);
		std::replace(slashes.begin(), slashes.end(), '\\', '/');
		std::replace(backslashes.begin(), backslashes.end(), '/', '\\');
		const std::string variants[] = {std::string(home), slashes, backslashes};
		for (const auto& h : variants) {
			for (auto pos = text.find(h); pos != std::string::npos; pos = text.find(h, pos + 1)) text.replace(pos, h.size(), "~");
		}
	}
	static const std::regex users(R"(([\\/])(Users|home)([\\/])[^\\/"\s]+)", std::regex::icase);
	return std::regex_replace(text, users, "$1$2$3<user>");
}

std::string formatSettings(const std::vector<std::pair<std::string, std::string>>& settings) {
	std::string out;
	for (auto [key, value] : settings) {
		std::replace(value.begin(), value.end(), '\n', ' ');
		std::replace(value.begin(), value.end(), '\r', ' ');
		out += key + "=" + value + "\n";
	}
	return out;
}

std::map<std::string, std::string> parseSettings(std::string_view text) {
	std::map<std::string, std::string> out;
	std::istringstream in{std::string(text)};
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto eq = line.find('=');
		if (eq == std::string::npos || eq == 0) continue;
		out[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return out;
}

std::string formatSolids(const std::vector<SolidRef>& solids) {
	std::string out;
	for (const auto& ref : solids) out += (out.empty() ? "" : ",") + solidName(ref);
	return out;
}

std::string formatFaces(const std::vector<FaceRef>& faces) {
	std::string out;
	for (const auto& face : faces) out += (out.empty() ? "" : ",") + solidName(face.solid) + "/" + std::to_string(face.sideId);
	return out;
}

std::vector<SolidRef> parseSolids(std::string_view text) {
	std::vector<SolidRef> out;
	for (const auto& part : splitTrimmed(text, ',')) {
		int entity = 0, solid = 0;
		if (std::sscanf(part.c_str(), "%d/%d", &entity, &solid) == 2) out.push_back({entity, solid});
	}
	return out;
}

std::vector<FaceRef> parseFaces(std::string_view text) {
	std::vector<FaceRef> out;
	for (const auto& part : splitTrimmed(text, ',')) {
		int entity = 0, solid = 0, side = 0;
		if (std::sscanf(part.c_str(), "%d/%d/%d", &entity, &solid, &side) == 3) out.push_back({{entity, solid}, side});
	}
	return out;
}

void addRoadSettings(std::vector<std::pair<std::string, std::string>>& out, const RoadOptions& options, std::pair<int, int> textureSize) {
	out.emplace_back("road.material", options.material);
	out.emplace_back("road.along", options.along == RoadAlong::Auto ? "auto" : options.along == RoadAlong::U ? "U" : "V");
	out.emplace_back("road.flip", options.flip ? "1" : "0");
	out.emplace_back("road.repeats", std::to_string(options.repeatsAcross));
	out.emplace_back("road.kerbMaterial", options.sideMaterial);
	out.emplace_back("road.width", number(options.roadWidth));
	out.emplace_back("road.curveCorners", options.curveCorners ? "1" : "0");
	out.emplace_back("road.texture", std::to_string(textureSize.first) + "x" + std::to_string(textureSize.second));
}

void addFlowSettings(std::vector<std::pair<std::string, std::string>>& out, const FlowOptions& options, std::pair<int, int> textureSize) {
	out.emplace_back("flow.mode", flowModeName(options.mode));
	out.emplace_back("flow.preset", flowPresetName(options.preset));
	out.emplace_back("flow.material", options.material);
	out.emplace_back("flow.flip", options.flip ? "1" : "0");
	out.emplace_back("flow.repeats", std::to_string(options.repeatsAcross));
	out.emplace_back("flow.curveCorners", options.curveCorners ? "1" : "0");
	if (options.seed) out.emplace_back("flow.seed", formatFaces({*options.seed}));
	out.emplace_back("flow.texture", std::to_string(textureSize.first) + "x" + std::to_string(textureSize.second));
}

namespace {

std::string setting(const std::map<std::string, std::string>& settings, const std::string& key) {
	const auto it = settings.find(key);
	return it == settings.end() ? std::string() : it->second;
}

std::pair<int, int> parseSize(const std::string& text) {
	int w = 512, h = 512;
	if (std::sscanf(text.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return {512, 512};
	return {w, h};
}

} // namespace

RoadOptions roadSettings(const std::map<std::string, std::string>& settings, std::pair<int, int>* textureSize) {
	RoadOptions options;
	options.material = setting(settings, "road.material");
	const std::string along = setting(settings, "road.along");
	options.along = along == "U" ? RoadAlong::U : along == "V" ? RoadAlong::V : RoadAlong::Auto;
	options.flip = setting(settings, "road.flip") == "1";
	options.repeatsAcross = std::max(1, std::atoi(setting(settings, "road.repeats").c_str()));
	options.sideMaterial = setting(settings, "road.kerbMaterial");
	options.roadWidth = std::atof(setting(settings, "road.width").c_str());
	options.curveCorners = setting(settings, "road.curveCorners") != "0";
	if (textureSize) *textureSize = parseSize(setting(settings, "road.texture"));
	return options;
}

FlowOptions flowSettings(const std::map<std::string, std::string>& settings, std::pair<int, int>* textureSize) {
	FlowOptions options;
	const std::string mode = setting(settings, "flow.mode");
	for (const FlowMode m : {FlowMode::Auto, FlowMode::Wrap, FlowMode::Path, FlowMode::Curve, FlowMode::Match}) {
		if (mode == flowModeName(m)) options.mode = m;
	}
	const std::string preset = setting(settings, "flow.preset");
	for (const FlowPreset p : {FlowPreset::Road, FlowPreset::Kerb, FlowPreset::River, FlowPreset::Trim, FlowPreset::Fence}) {
		if (preset == flowPresetName(p)) options.preset = p;
	}
	options.material = setting(settings, "flow.material");
	options.flip = setting(settings, "flow.flip") == "1";
	options.repeatsAcross = std::max(1, std::atoi(setting(settings, "flow.repeats").c_str()));
	options.curveCorners = setting(settings, "flow.curveCorners") != "0";
	if (const auto seed = parseFaces(setting(settings, "flow.seed")); !seed.empty()) options.seed = seed.front();
	if (textureSize) *textureSize = parseSize(setting(settings, "flow.texture"));
	return options;
}

std::optional<std::string> replayEditorReport(const std::filesystem::path& zip, std::string* error) {
	const auto vmf = readZipEntry(zip, "scene.vmf");
	if (!vmf) {
		if (error) *error = "no scene.vmf in " + displayPath(zip);
		return std::nullopt;
	}
	MapDocument doc;
	try {
		doc = MapDocument::parse(*vmf);
	} catch (const std::exception& e) {
		if (error) *error = std::string("scene.vmf cannot be read: ") + e.what();
		return std::nullopt;
	}
	const auto settings = parseSettings(readZipEntry(zip, "replay.txt").value_or(""));
	std::string out = "scene: " + std::to_string(doc.solidCount()) + " brush(es), " + std::to_string(doc.entities.size()) + " entit(ies)\n";
	for (const auto& [key, value] : settings) {
		if (key.rfind("road.", 0) == 0 || key.rfind("flow.", 0) == 0) continue;
		out += key + ": " + (value.size() > 200 ? value.substr(0, 200) + " ..." : value) + "\n";
	}
	Selection hidden;
	hidden.solids = parseSolids(setting(settings, "hidden.solids"));
	for (const auto& part : splitTrimmed(setting(settings, "hidden.entities"), ',')) hidden.entities.push_back(std::atoi(part.c_str()));
	hidden.faces = parseFaces(setting(settings, "hidden.faces"));

	const auto picks = parsePickLog(readZipEntry(zip, "picks.log").value_or(""));
	out += "\n" + std::to_string(picks.size()) + " click(s) replayed against the scene (plain geometry; hidden objects left out):\n";
	for (const auto& pick : picks) {
		out += "pick #" + std::to_string(pick.number) + " (" + pick.view + "): recorded " + (pick.result.empty() ? "nothing" : pick.result) + "\n";
		const auto hits = brushesAlongRay(doc, pick.ray, &hidden);
		if (hits.empty()) out += "  now: no brush on this ray\n";
		for (std::size_t i = 0; i < hits.size() && i < 6; ++i) {
			const auto& h = hits[i];
			out += "  now: brush " + solidName(h.solid) + " side " + std::to_string(h.sideId) + " at " + number(h.distance) + (h.inside ? " (from inside)" : "");
			if (i > 0 && std::abs(h.distance - hits[0].distance) <= 0.05) out += " (same surface as the first)";
			out += "\n";
		}
	}

	// The selection each plan was made for (the one at the time of the report otherwise).
	const auto selectionFor = [&settings](const std::string& prefix) {
		Selection selection;
		const bool own = settings.contains(prefix + "faces") || settings.contains(prefix + "solids");
		selection.faces = parseFaces(setting(settings, own ? prefix + "faces" : "selection.faces"));
		selection.solids = parseSolids(setting(settings, own ? prefix + "solids" : "selection.solids"));
		return selection;
	};
	if (settings.contains("road.material")) {
		std::pair<int, int> size{512, 512};
		const RoadOptions options = roadSettings(settings, &size);
		const Selection selection = selectionFor("road.");
		const auto plan = planRoad(doc, roadFaces(doc, selection), options, [size](const std::string&) { return size; });
		out += "\nroad replayed with the recorded settings and selection:\n" + describeRoadPlan(plan, options);
	}
	if (settings.contains("flow.mode")) {
		std::pair<int, int> size{512, 512};
		const FlowOptions options = flowSettings(settings, &size);
		const Selection selection = selectionFor("flow.");
		const auto plan = planTextureFlow(doc, flowFaces(doc, selection), options, [size](const std::string&) { return size; });
		out += "\ntexture flow replayed with the recorded settings and selection:\n" + describeFlowPlan(plan, options);
	}
	return out;
}

} // namespace sme
