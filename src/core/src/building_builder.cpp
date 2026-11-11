// building_builder.cpp - Building builder (see building_builder.h).

#include <sme/building_builder.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/edge_round.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <sstream>

namespace sme {

namespace {

constexpr double EPS = 1e-6;
constexpr double PANEL = 2;          ///< Window / door panel thickness
constexpr double STEP_MAX = 18;      ///< Player step height
const std::string NODRAW = "TOOLS/TOOLSNODRAW";

// --- Random numbers from a seed (the same settings give the same buildings) ------------------

std::uint64_t mix(std::uint64_t x) {
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

/// 0..1 from a seed and up to four numbers.
double rand01(std::uint32_t seed, long long a, long long b = 0, long long c = 0, long long d = 0) {
	std::uint64_t h = mix(seed);
	for (const long long v : {a, b, c, d}) h = mix(h ^ static_cast<std::uint64_t>(v));
	return static_cast<double>(h >> 11) / static_cast<double>(1ULL << 53);
}

// --- Outline -------------------------------------------------------------------------------

/// A side of the outline: from `a` to `b` (counter-clockwise from above), outward normal `n`,
/// inside where dot(n, p) <= c.
struct Edge {
	Vec3 a, b, e, n;
	double c = 0;
	double length = 0;
	/// Inward distance of a point from the side's line.
	[[nodiscard]] double dist(const Vec3& p) const { return c - dot(n, p); }
	/// Position along the side (0 at `a`).
	[[nodiscard]] double along(const Vec3& p) const { return dot(e, p - a); }
	[[nodiscard]] Vec3 at(double s) const { return a + e * s; }
};

double cross2(const Vec3& o, const Vec3& a, const Vec3& b) {
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/// Convex hull of the points seen from above, counter-clockwise, without points on a straight line.
std::vector<Vec3> hull2d(std::vector<Vec3> points) {
	for (auto& p : points) p.z = 0;
	std::sort(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
	points.erase(std::unique(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) { return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01; }),
	             points.end());
	if (points.size() < 3) return {};
	std::vector<Vec3> hull(points.size() * 2);
	std::size_t k = 0;
	for (const auto& p : points) {
		while (k >= 2 && cross2(hull[k - 2], hull[k - 1], p) <= 1e-6) --k;
		hull[k++] = p;
	}
	for (std::size_t i = points.size() - 1, t = k + 1; i-- > 0;) {
		while (k >= t && cross2(hull[k - 2], hull[k - 1], points[i]) <= 1e-6) --k;
		hull[k++] = points[i];
	}
	hull.resize(k - 1);
	// Corners closer than a unit, or almost straight, are one corner.
	bool changed = true;
	while (changed && hull.size() > 3) {
		changed = false;
		for (std::size_t i = 0; i < hull.size(); ++i) {
			const Vec3& prev = hull[(i + hull.size() - 1) % hull.size()];
			const Vec3& next = hull[(i + 1) % hull.size()];
			const double len = length(next - prev);
			if (length(hull[i] - prev) < 1 || (len > 0 && std::abs(cross2(prev, hull[i], next)) / len < 0.05)) {
				hull.erase(hull.begin() + static_cast<long>(i));
				changed = true;
				break;
			}
		}
	}
	return hull;
}

std::vector<Edge> edgesOf(const std::vector<Vec3>& poly, double z) {
	std::vector<Edge> edges;
	for (std::size_t i = 0; i < poly.size(); ++i) {
		Edge edge;
		edge.a = {poly[i].x, poly[i].y, z};
		edge.b = {poly[(i + 1) % poly.size()].x, poly[(i + 1) % poly.size()].y, z};
		edge.length = length(edge.b - edge.a);
		edge.e = normalize(edge.b - edge.a);
		edge.n = {edge.e.y, -edge.e.x, 0};
		edge.c = dot(edge.n, edge.a);
		edges.push_back(edge);
	}
	return edges;
}

bool insidePolygon(const std::vector<Edge>& edges, const Vec3& p, double margin) {
	return std::all_of(edges.begin(), edges.end(), [&](const Edge& e) { return e.dist(p) > margin; });
}

double polygonArea(const std::vector<Vec3>& poly) {
	double area = 0;
	for (std::size_t i = 0; i < poly.size(); ++i) {
		const Vec3& a = poly[i];
		const Vec3& b = poly[(i + 1) % poly.size()];
		area += a.x * b.y - b.x * a.y;
	}
	return area / 2;
}

// --- Textures ------------------------------------------------------------------------------

/// What a face of a piece is.
enum class Role { Nodraw, Facade, Ground, Window, BrokenWindow, Door, Trim, Cornice, Roof, Interior };

/// Where a face's texture starts: a point on its side (the side's start at the reference height),
/// the side's direction, the floor height and the bay width (for the fitted settings).
struct TexRef {
	Vec3 origin;
	Vec3 along{1, 0, 0};
	double floor = 128;
	double bay = 128;
};

/// An opening a panel fits its texture to: its left top corner, width and height.
struct Fit {
	Vec3 leftTop;
	double width = 0, height = 0;
};

struct Materials {
	std::string facade, ground, window, broken, door, trim, cornice, roof, interior;
	[[nodiscard]] const std::string& of(Role role) const {
		switch (role) {
			case Role::Facade: return facade;
			case Role::Ground: return ground;
			case Role::Window: return window;
			case Role::BrokenWindow: return broken;
			case Role::Door: return door;
			case Role::Trim: return trim;
			case Role::Cornice: return cornice;
			case Role::Roof: return roof;
			case Role::Interior: return interior;
			case Role::Nodraw: break;
		}
		return NODRAW;
	}
};

BuildingPart partOf(Role role) {
	switch (role) {
		case Role::Facade: return BuildingPart::Facade;
		case Role::Ground: return BuildingPart::Ground;
		case Role::Window:
		case Role::BrokenWindow: return BuildingPart::Window;
		case Role::Door: return BuildingPart::Door;
		case Role::Trim: return BuildingPart::Trim;
		case Role::Cornice: return BuildingPart::Cornice;
		case Role::Roof: return BuildingPart::Roof;
		case Role::Interior: return BuildingPart::Interior;
		case Role::Nodraw: break;
	}
	return BuildingPart::Trim;
}

double wrap(double value, double size) {
	if (size <= 0) return value;
	double r = std::fmod(value, size);
	if (r < 0) r += size;
	return std::round(r * 1000) / 1000;
}

/// Lays a face's texture: axes along its side (vertical faces), the world (flat faces) or the slope
/// (roofs), then the part's scale / fit, rotation and shift; with `fit`, once over the opening.
void layTexture(Side& side, const std::string& material, const PartTexture& part, const TexRef& ref, const TextureSizeFn& size, const Fit* fit) {
	side.material = material;
	side.rotation = 0;
	const Vec3 n = side.plane().normal;
	Vec3 u, v;
	if (std::abs(n.z) < 0.1) {
		u = normalize(Vec3{-n.y, n.x, 0});
		v = {0, 0, -1};
	} else if (std::abs(n.z) > 0.95) {
		const Vec3 along = std::abs(ref.along.z) < 0.9 ? normalize(Vec3{ref.along.x, ref.along.y, 0}) : Vec3{1, 0, 0};
		u = along;
		v = normalize(cross(n, u));
	} else {
		u = normalize(cross(Vec3{0, 0, 1}, n));
		v = normalize(cross(n, u));
		if (v.z > 0) v = -v;
	}
	const auto [texW, texH] = size ? size(material) : std::pair<int, int>{512, 512};
	const double tw = std::max(1, texW), th = std::max(1, texH);
	if (fit && part.fit == TextureFit::Opening && std::abs(n.z) < 0.1) {
		// Once over the opening: 0..1 from its left top corner.
		const double su = fit->width / tw, sv = fit->height / th;
		side.u = TextureAxis{u, 0, su};
		side.v = TextureAxis{v, 0, sv};
		side.u.shift = wrap(-dot(fit->leftTop, u) / su, tw);
		side.v.shift = wrap(-dot(fit->leftTop, v) / sv, th);
		return;
	}
	double su = part.scale > 0 ? part.scale : 0.25, sv = su;
	if (part.fit == TextureFit::Floor && ref.floor > 1) {
		sv = ref.floor / th;
		su = sv;
	} else if (part.fit == TextureFit::Bay && ref.floor > 1 && ref.bay > 1) {
		sv = ref.floor / th;
		su = ref.bay / tw;
	}
	if (std::abs(part.rotation) > 1e-6) {
		const double r = part.rotation * std::numbers::pi / 180.0;
		const Vec3 ru = u * std::cos(r) + v * std::sin(r);
		const Vec3 rv = v * std::cos(r) - u * std::sin(r);
		u = ru;
		v = rv;
		side.rotation = part.rotation;
	}
	side.u = TextureAxis{u, wrap(-dot(ref.origin, u) / su + part.shiftU, tw), su};
	side.v = TextureAxis{v, wrap(-dot(ref.origin, v) / sv + part.shiftV, th), sv};
}

// --- Pieces from planes --------------------------------------------------------------------

/// A plane of a piece and what its face is (if it ends up a face).
struct HalfSpace {
	Plane plane;   ///< Normal out of the piece
	Role role = Role::Nodraw;
	const Fit* fit = nullptr;
};

HalfSpace hs(const Vec3& normal, double distance, Role role, const Fit* fit = nullptr) {
	const double len = length(normal);
	return {Plane{normal / len, distance / len}, role, fit};
}
/// Inside where dot(n, p) <= d.
HalfSpace below(const Vec3& n, double d, Role role, const Fit* fit = nullptr) { return hs(n, d, role, fit); }
/// Inside where dot(n, p) >= d.
HalfSpace above(const Vec3& n, double d, Role role, const Fit* fit = nullptr) { return hs(-n, -d, role, fit); }

void addEditorBlock(Solid& solid) {
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "0 175 236");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	solid.extra.push_back(std::move(editor));
}

/// Everything a lot's pieces need.
struct Builder {
	const BuildingOptions& o;
	MapDocument& ids;
	const TextureSizeFn& size;
	Materials materials;
	BuildingPlan& plan;
	int skipped = 0;

	/// The piece the planes enclose (nullopt if it is broken or empty). Faces get their role's
	/// material and texture; `ref` tells where the textures start.
	std::optional<Solid> piece(const std::vector<HalfSpace>& planes, const TexRef& ref) {
		Solid solid;
		solid.id = ids.newSolidId();
		std::vector<const HalfSpace*> roles;
		for (const auto& h : planes) {
			// The same plane twice: keep the first.
			const bool twice = std::any_of(roles.begin(), roles.end(), [&](const HalfSpace* other) {
				return dot(other->plane.normal, h.plane.normal) > 1 - 1e-9 && std::abs(other->plane.dist - h.plane.dist) < 1e-6;
			});
			if (twice) continue;
			solid.sides.push_back(makeSide(ids, h.plane, NODRAW));
			roles.push_back(&h);
		}
		const auto geometry = buildBrushGeometry(solid);
		if (!geometry.valid) return std::nullopt;
		std::vector<bool> used(solid.sides.size(), false);
		for (const auto& face : geometry.faces) used[static_cast<std::size_t>(face.sideIndex)] = true;
		Solid out;
		out.id = solid.id;
		for (std::size_t i = 0; i < solid.sides.size(); ++i) {
			if (!used[i]) continue;
			Side side = solid.sides[i];
			const HalfSpace& h = *roles[i];
			if (h.role == Role::Nodraw) {
				side.material = NODRAW;
				alignToWorld(side);
			} else {
				layTexture(side, materials.of(h.role), o.texture(partOf(h.role)), ref, size, h.fit);
			}
			out.sides.push_back(std::move(side));
		}
		tidyPlanePoints(out);
		if (!brushProblem(out).empty()) {
			++skipped;
			return std::nullopt;
		}
		addEditorBlock(out);
		return out;
	}
};

// --- Openings and the wall layer -----------------------------------------------------------

enum class OpeningKind { Window, Shop, Basement, Door, Gap, Hole };

struct Opening {
	double s0 = 0, s1 = 0, z0 = 0, z1 = 0;
	OpeningKind kind = OpeningKind::Window;
	int floor = 0;
	int column = 0;
	bool broken = false;
	[[nodiscard]] bool panel() const { return kind != OpeningKind::Gap && kind != OpeningKind::Hole; }
};

} // namespace

// --- Public helpers ------------------------------------------------------------------------

BuildingOptions defaultBuildingOptions() {
	BuildingOptions o;
	o.texture(BuildingPart::Window).fit = TextureFit::Opening;
	o.texture(BuildingPart::Door).fit = TextureFit::Opening;
	return o;
}

std::pair<int, double> stepsFor(double raise) {
	if (raise <= 0.5) return {0, 0};
	const int count = static_cast<int>(std::ceil(raise / STEP_MAX - 1e-9));
	return {count, raise / count};
}

bool patternHasWindow(const std::string& pattern, int index) {
	std::string p;
	for (const char ch : pattern) {
		if (ch == 'x' || ch == 'X' || ch == '.' || ch == '-' || ch == '_' || ch == 'o' || ch == 'O') p += ch;
	}
	if (p.empty()) return true;
	const char ch = p[static_cast<std::size_t>(index) % p.size()];
	return ch == 'x' || ch == 'X' || ch == 'o' || ch == 'O';
}

std::vector<double> windowColumns(double length, const BuildingOptions& options, double width, int count, double* usedWidth) {
	std::vector<double> out;
	double w = std::max(8.0, width);
	const double margin = std::max(0.0, options.cornerMargin);
	const double usable = length - 2 * margin;
	if (usable < w) {
		if (length >= w + 16 && count != 0) {
			out.push_back(length / 2);
		}
		if (usedWidth) *usedWidth = w;
		return out;
	}
	if (options.layout == WindowLayout::Count) {
		int n = std::max(0, count);
		if (n == 0) {
			if (usedWidth) *usedWidth = w;
			return out;
		}
		double pitch = usable / n;
		// Narrower windows when the count does not fit (at least 8 units of wall between two).
		while (n > 1 && pitch - 8 < 16) {
			--n;
			pitch = usable / n;
		}
		w = std::min(w, std::max(8.0, pitch - 8));
		for (int k = 0; k < n; ++k) out.push_back(margin + pitch * (k + 0.5));
	} else {
		const double gap = std::max(8.0, options.windowGap);
		const int n = std::max(1, static_cast<int>(std::floor((usable + gap) / (w + gap) + 1e-9)));
		const double total = n * w + (n - 1) * gap;
		const double start = margin + (usable - total) / 2;
		for (int k = 0; k < n; ++k) out.push_back(start + w / 2 + k * (w + gap));
	}
	// Openings on whole units.
	for (auto& c : out) c = std::round(c - w / 2) + w / 2;
	if (usedWidth) *usedWidth = w;
	return out;
}

int BuildingPlan::brushCount() const {
	int n = 0;
	for (const auto& b : buildings) n += static_cast<int>(b.world.size() + b.detail.size());
	return n;
}

std::vector<Solid> BuildingPlan::resultSolids() const {
	std::vector<Solid> out;
	for (const auto& b : buildings) {
		out.insert(out.end(), b.world.begin(), b.world.end());
		out.insert(out.end(), b.detail.begin(), b.detail.end());
	}
	return out;
}

std::vector<SolidRef> BuildingPlan::replacedRefs() const {
	std::vector<SolidRef> out = removeSolids;
	for (const auto& b : buildings) {
		if (b.lot.entityId >= 0) out.push_back(b.lot);
	}
	return out;
}

int BuildingPlan::sideAt(const Vec3& point, double tolerance) const {
	int best = -1;
	double bestDistance = tolerance;
	for (std::size_t i = 0; i < sides.size(); ++i) {
		const auto& s = sides[i];
		const Vec3 d = s.b - s.a;
		const double len2 = d.x * d.x + d.y * d.y;
		if (len2 < 1e-9) continue;
		const double t = std::clamp(((point.x - s.a.x) * d.x + (point.y - s.a.y) * d.y) / len2, 0.0, 1.0);
		const double dx = point.x - (s.a.x + d.x * t), dy = point.y - (s.a.y + d.y * t);
		const double distance = std::hypot(dx, dy);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = static_cast<int>(i);
		}
	}
	return best;
}

namespace {

// --- Name suggestions ----------------------------------------------------------------------

struct Keyword {
	const char* word;
	int score;
};

std::vector<std::string> suggest(const std::vector<std::string>& names, const std::vector<Keyword>& wanted, const std::vector<const char*>& unwanted,
                                 std::size_t count, bool materials) {
	std::vector<std::pair<int, std::string>> scored;
	for (const auto& name : names) {
		const std::string lower = toLower(name);
		if (materials && (lower.starts_with("tools/") || lower.find("decal") != std::string::npos || lower.find("overlay") != std::string::npos ||
		                  lower.find("blend") != std::string::npos)) {
			continue;
		}
		int score = 0;
		for (const auto& k : wanted) {
			if (lower.find(k.word) != std::string::npos) score += k.score;
		}
		if (score <= 0) continue;
		for (const char* word : unwanted) {
			if (lower.find(word) != std::string::npos) score -= 6;
		}
		if (score <= 0) continue;
		scored.emplace_back(score, name);
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
	std::vector<std::string> out;
	for (const auto& [score, name] : scored) {
		if (out.size() >= count) break;
		out.push_back(name);
	}
	return out;
}

} // namespace

std::vector<std::string> buildingMaterialSuggestions(const std::vector<std::string>& names, BuildingMaterialRole role, std::size_t count) {
	switch (role) {
		case BuildingMaterialRole::Facade:
			return suggest(names, {{"brickwall", 6}, {"brick", 4}, {"plaster", 5}, {"stucco", 5}, {"building", 3}, {"facade", 6}, {"wall", 2}, {"concrete", 1}, {"siding", 4}},
			               {"floor", "ground", "roof", "glass", "window", "door", "road", "grass", "dirt", "sky", "water", "trim", "metal"}, count, true);
		case BuildingMaterialRole::Ground:
			return suggest(names, {{"stone", 4}, {"concrete", 4}, {"brick", 3}, {"shop", 5}, {"store", 5}, {"tile", 2}, {"wall", 2}, {"plaster", 2}},
			               {"floor", "road", "roof", "glass", "window", "door", "grass", "dirt", "sky", "water"}, count, true);
		case BuildingMaterialRole::Window:
			return suggest(names, {{"window", 6}, {"glass", 4}, {"pane", 2}}, {"broken", "floor", "frame", "decal", "shatter"}, count, true);
		case BuildingMaterialRole::BrokenWindow:
			return suggest(names, {{"broken", 5}, {"shatter", 4}, {"boarded", 6}, {"board", 3}, {"plywood", 4}, {"window", 3}, {"glass", 2}}, {"floor"}, count, true);
		case BuildingMaterialRole::Door:
			return suggest(names, {{"door", 6}, {"gate", 3}, {"shutter", 2}}, {"floor", "frame", "decal"}, count, true);
		case BuildingMaterialRole::Trim:
			return suggest(names, {{"trim", 6}, {"ledge", 5}, {"cornice", 6}, {"concrete", 3}, {"stone", 3}, {"frame", 3}, {"metal", 1}},
			               {"floor", "road", "grass", "glass", "window", "door", "sky", "water"}, count, true);
		case BuildingMaterialRole::Roof:
			return suggest(names, {{"roof", 6}, {"shingle", 6}, {"tar", 3}, {"gravel", 3}, {"tile", 2}, {"metal", 1}}, {"glass", "window", "door", "sky", "water"}, count,
			               true);
		case BuildingMaterialRole::Interior:
			return suggest(names, {{"interior", 6}, {"plaster", 4}, {"wallpaper", 5}, {"concrete", 2}, {"wall", 1}}, {"floor", "glass", "window", "door", "sky", "water"},
			               count, true);
	}
	return {};
}

std::vector<std::string> buildingModelSuggestions(const std::vector<std::string>& names, FacadePropAnchor anchor, bool damaged, std::size_t count) {
	std::vector<Keyword> wanted;
	switch (anchor) {
		case FacadePropAnchor::Window: wanted = {{"brokenwindow", 7}, {"shutter", 6}, {"window", 4}, {"frame", 2}, {"curtain", 3}}; break;
		case FacadePropAnchor::BelowWindow: wanted = {{"air_cond", 6}, {"aircon", 6}, {"ac_unit", 6}, {"flowerbox", 6}, {"planter", 4}, {"vent", 2}}; break;
		case FacadePropAnchor::AboveWindow: wanted = {{"awning", 6}, {"lamp", 3}, {"light", 2}, {"sign", 2}}; break;
		case FacadePropAnchor::Door: wanted = {{"door", 6}, {"sign", 2}, {"mailbox", 3}, {"intercom", 3}}; break;
		case FacadePropAnchor::Corner: wanted = {{"brokenpillar", 7}, {"pillar", 5}, {"drainpipe", 6}, {"pipe", 3}, {"column", 4}}; break;
		case FacadePropAnchor::Bay: wanted = {{"brokenexterior", 8}, {"brokeninterior", 3}, {"facade", 4}, {"panel", 2}}; break;
		case FacadePropAnchor::Ground: wanted = {{"rubble", 6}, {"debris", 4}, {"trash", 4}, {"bin", 3}, {"crate", 2}, {"barrel", 2}, {"concrete_chunk", 5}}; break;
		case FacadePropAnchor::Roof: wanted = {{"vent", 5}, {"chimney", 6}, {"antenna", 6}, {"air_cond", 5}, {"ac_unit", 5}, {"fan", 3}, {"watertank", 5}}; break;
	}
	if (damaged) {
		for (const char* word : {"broken", "debris", "destroyed", "rubble", "damaged", "burnt", "gib"}) wanted.push_back({word, 4});
	}
	return suggest(names, wanted, {"_gib", "skybox"}, count, false);
}

std::vector<std::string> buildingLotModelSuggestions(const std::vector<std::string>& names, std::size_t count) {
	return suggest(names, {{"props_buildings/", 6}, {"building", 3}, {"row_res", 4}, {"row_corner", 4}, {"project_building", 5}, {"house", 2}},
	               {"broken", "debris", "gib", "interior"}, count, false);
}

namespace {

// --- One lot ---------------------------------------------------------------------------------

struct Lot {
	SolidRef ref;
	int index = 0;
	std::vector<Vec3> poly;
	std::vector<Edge> edges;
	double bottom = 0;        ///< The lot brush's bottom: street level
	double brushTop = 0;
	double ground = 0;        ///< Lowest terrain under it (foundation)
	bool terrain = false;
	bool varied = false;      ///< The floor variation changed its floors
	// Decided while planning
	double top = 0;           ///< The walls' top
	int floors = 1;
	double groundBase = 0;    ///< The ground floor's floor
	std::string facade;
	std::string pattern;
	std::vector<SideState> states;
	std::vector<bool> shared, blocked;
	int front = 0;
	std::vector<int> neighbour;   ///< Per side: the lot it is shared with (-1: none)
	std::vector<double> texOffset;   ///< Per side: where the facade texture is along a chain of sides (curves)
	bool doorLot = true;          ///< Gets the doors of DoorSides::Longest (one building of touching lots: only one)
	int group = 0;                ///< Touching lots: the lowest lot index of the group
};

/// The lowest point of displacements (of the map, and the lot's own) under the outline.
std::optional<double> terrainUnder(const MapDocument& doc, const Lot& lot, const Solid* own) {
	std::optional<double> lowest;
	Bounds area;
	for (const auto& p : lot.poly) area.add(Vec3{p.x, p.y, lot.bottom});
	area.min.z = lot.bottom - 1024;
	area.max.z = lot.bottom + 64;
	const auto visit = [&](const Solid& solid) {
		if (std::none_of(solid.sides.begin(), solid.sides.end(), [](const Side& s) { return s.dispInfo() != nullptr; })) return;
		const auto geometry = buildBrushGeometry(solid);
		if (!geometry.valid) return;
		for (int i = 0; i < 3; ++i) {
			if (geometry.bounds.max[i] < area.min[i] || geometry.bounds.min[i] > area.max[i]) return;
		}
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			const auto mesh = buildDisplacement(side, face);
			if (!mesh) continue;
			for (const auto& p : mesh->positions) {
				if (p.z > area.max.z || p.z < area.min.z) continue;
				if (!insidePolygon(lot.edges, Vec3{p.x, p.y, 0}, -32)) continue;
				lowest = lowest ? std::min(*lowest, p.z) : p.z;
			}
		}
	};
	if (own) visit(*own);
	for (const auto& solid : doc.world.solids) visit(solid);
	for (const auto& entity : doc.entities) {
		if (!iequals(entity.classname(), "func_detail")) continue;
		for (const auto& solid : entity.solids) visit(solid);
	}
	return lowest;
}

/// Brushes of the map that count as standing in front of a side or being in a piece's way: world
/// and func_detail, not the lots, not displacements (terrain is reached down to, never cut).
std::vector<NearbySolid> blockers(const MapDocument& doc, const Bounds& around, double margin, const std::vector<SolidRef>& lots) {
	auto found = solidsNear(doc, around, margin, lots);
	std::erase_if(found, [&](const NearbySolid& n) {
		if (n.ref.entityId != 0) {
			const Entity* entity = doc.findEntity(n.ref.entityId);
			if (!entity || !iequals(entity->classname(), "func_detail")) return true;
		}
		const Solid& s = *n.solid;
		if (std::any_of(s.sides.begin(), s.sides.end(), [](const Side& side) { return side.dispInfo() != nullptr; })) return true;
		// Tool-only brushes (clips, triggers drawn as world, skybox walls) do not stand in the way.
		return std::all_of(s.sides.begin(), s.sides.end(), [](const Side& side) { return startsWithI(side.material, "TOOLS/"); });
	});
	return found;
}

struct LotBuilder {
	LotBuilder(const MapDocument& d, const BuildingOptions& options, Builder& builder, Lot& l, const std::vector<Lot>& all, std::vector<SolidRef> refs)
	    : doc(d), o(options), b(builder), lot(l), lots(all), lotRefs(std::move(refs)) {}
	const MapDocument& doc;
	const BuildingOptions& o;
	Builder& b;
	Lot& lot;
	const std::vector<Lot>& lots;
	std::vector<SolidRef> lotRefs;
	BuildingPlan::Building building;
	// Derived
	double depth = 0;             ///< Wall layer of facade sides
	double plainDepth = 0;        ///< Wall layer of plain sides (displacement walls)
	double massTop = 0;           ///< Top of the mass (lower than the walls when damaged)
	double foundation = 0;
	double damage = 0;
	bool roofDisp = false;        ///< The flat roof is a displacement (its own brush)
	std::vector<double> layer;    ///< Per side: its wall layer
	std::vector<bool> wall;       ///< Per side: has a wall (facade mode: a slab)
	std::vector<bool> banded;     ///< Per side: cornices / plinth
	std::vector<std::vector<std::pair<double, double>>> returns;   ///< Facade only, per side: the parts of an open side that are returns (s0, s1)
	std::vector<std::vector<Opening>> openings;   ///< Per side
	std::vector<std::vector<double>> columns;     ///< Per side: window columns (upper floors)
	std::vector<double> bays;                     ///< Per side: bay width

	[[nodiscard]] bool facadeMode() const { return o.mode == BuildingMode::Facade; }
	[[nodiscard]] double floorBase(int floor) const {
		return floor == 0 ? lot.groundBase : lot.groundBase + o.groundFloorHeight + (floor - 1) * o.floorHeight;
	}
	[[nodiscard]] double floorTop(int floor) const { return floor + 1 < lot.floors ? floorBase(floor + 1) : lot.top; }
	[[nodiscard]] TexRef ref(int side, double z) const {
		const Edge& e = lot.edges[static_cast<std::size_t>(side)];
		TexRef r;
		const double offset = side < static_cast<int>(lot.texOffset.size()) ? lot.texOffset[static_cast<std::size_t>(side)] : 0.0;
		r.origin = Vec3{e.a.x, e.a.y, z} - e.e * offset;
		r.along = e.e;
		r.floor = o.floorHeight;
		r.bay = side < static_cast<int>(bays.size()) && bays[static_cast<std::size_t>(side)] > 1 ? bays[static_cast<std::size_t>(side)] : o.windowWidth + o.windowGap;
		return r;
	}
	[[nodiscard]] double refZ(bool groundPart) const { return groundPart ? lot.groundBase : lot.groundBase + o.groundFloorHeight; }
	bool stop() const { return o.cancelled && o.cancelled(); }

	/// The bounds every piece of a side shares: mitred against the sides in `mitre` (closer to this
	/// side than to that one - inside the outline, and farther out than from that one - outside),
	/// stopped at the outer line of the others (`endRole`), end caps at an open neighbour (facade
	/// only), and the part of the side in `range` (s from, to).
	void sideBounds(std::vector<HalfSpace>& out, int i, const std::vector<bool>& mitre, Role endRole, const std::pair<double, double>* range = nullptr) const {
		const Edge& ei = lot.edges[static_cast<std::size_t>(i)];
		const int count = static_cast<int>(lot.edges.size());
		for (int j = 0; j < count; ++j) {
			if (j == i) continue;
			const Edge& ej = lot.edges[static_cast<std::size_t>(j)];
			if (mitre[static_cast<std::size_t>(j)]) {
				const Vec3 normal = ej.n - ei.n;
				if (length(normal) < 1e-6) continue;
				out.push_back(below(normal, ej.c - ei.c, Role::Nodraw));
			} else {
				out.push_back(below(ej.n, ej.c, endRole));
			}
		}
		if (facadeMode()) {
			const int prev = (i + count - 1) % count, next = (i + 1) % count;
			if (!mitre[static_cast<std::size_t>(prev)]) out.push_back(above(ei.e, dot(ei.e, ei.a), endRole));
			if (!mitre[static_cast<std::size_t>(next)]) out.push_back(below(ei.e, dot(ei.e, ei.b), endRole));
		}
		if (range) {
			out.push_back(above(ei.e, dot(ei.e, ei.a) + range->first, endRole));
			out.push_back(below(ei.e, dot(ei.e, ei.a) + range->second, endRole));
		}
	}

	void addDetail(std::optional<Solid> s) {
		if (s) building.detail.push_back(std::move(*s));
	}
	void addWorld(std::optional<Solid> s) {
		if (s) building.world.push_back(std::move(*s));
	}

	/// What the wall layer of a side is mitred against: the other layers (block), every wall (facade
	/// only: the slabs reach the outer line too).
	[[nodiscard]] std::vector<bool> layerMitre() const {
		std::vector<bool> m(lot.edges.size(), false);
		for (std::size_t j = 0; j < lot.edges.size(); ++j) {
			m[j] = facadeMode() ? (wall[j] || !returns[j].empty()) : layer[j] > 0;
		}
		return m;
	}
	Role facadeRole(double zCenter) const { return zCenter < lot.groundBase + o.groundFloorHeight - 1 ? Role::Ground : Role::Facade; }

	void decideSides();
	void decideOpenings();
	void addDamage();
	void buildMass();
	void buildWallLayer(int side);
	void buildPanels(int side);
	void buildBands();
	void buildSills();
	void buildSteps();
	void buildRoof();
	void buildWallDisplacements();
	void placeProps();
	void build();
};

void LotBuilder::decideSides() {
	const int count = static_cast<int>(lot.edges.size());
	layer.assign(static_cast<std::size_t>(count), 0);
	wall.assign(static_cast<std::size_t>(count), false);
	banded.assign(static_cast<std::size_t>(count), false);
	returns.assign(static_cast<std::size_t>(count), {});
	const bool recessed = o.windowStyle == WindowStyle::Recessed && o.windowDepth >= PANEL + 1;
	depth = recessed ? o.windowDepth : 0;
	plainDepth = 0;
	for (int i = 0; i < count; ++i) {
		const SideState state = lot.states[static_cast<std::size_t>(i)];
		const bool facade = state == SideState::Facade;
		const bool plain = state == SideState::Plain;
		wall[static_cast<std::size_t>(i)] = facadeMode() ? (facade || plain) : (facade || plain);
		banded[static_cast<std::size_t>(i)] = facade || plain;
		if (facade) layer[static_cast<std::size_t>(i)] = depth;
	}
	if (facadeMode() && o.returnDepth > 0) {
		// An open side next to a wall turns the corner for `returnDepth` (a plain wall).
		for (int i = 0; i < count; ++i) {
			if (wall[static_cast<std::size_t>(i)] || lot.shared[static_cast<std::size_t>(i)]) continue;
			const int prev = (i + count - 1) % count, next = (i + 1) % count;
			const Edge& e = lot.edges[static_cast<std::size_t>(i)];
			const double r = std::min(o.returnDepth, e.length);
			const bool atStart = wall[static_cast<std::size_t>(prev)] && lot.states[static_cast<std::size_t>(prev)] != SideState::Open;
			const bool atEnd = wall[static_cast<std::size_t>(next)] && lot.states[static_cast<std::size_t>(next)] != SideState::Open;
			auto& parts = returns[static_cast<std::size_t>(i)];
			if (atStart && atEnd && 2 * r >= e.length - 1) {
				parts.emplace_back(0, e.length);   // the whole side
			} else {
				if (atStart) parts.emplace_back(0, r);
				if (atEnd) parts.emplace_back(e.length - r, e.length);
			}
		}
	}
}

void LotBuilder::decideOpenings() {
	const int count = static_cast<int>(lot.edges.size());
	openings.assign(static_cast<std::size_t>(count), {});
	columns.assign(static_cast<std::size_t>(count), {});
	bays.assign(static_cast<std::size_t>(count), 0);
	// Doors: on the front, on every facade side, or none; a side's own count wins.
	for (int i = 0; i < count; ++i) {
		if (lot.states[static_cast<std::size_t>(i)] != SideState::Facade) continue;
		const Edge& e = lot.edges[static_cast<std::size_t>(i)];
		const auto overrideIt = o.sideSettings.find({lot.index, i});
		const SideSettings* own = overrideIt != o.sideSettings.end() ? &overrideIt->second : nullptr;
		const int windowCount = own && own->windows >= 0 ? own->windows : o.windowsPerSide;
		const std::string pattern = own && !own->pattern.empty() ? own->pattern : lot.pattern;
		BuildingOptions layout = o;
		if (own && own->windows >= 0) layout.layout = WindowLayout::Count;
		double width = o.windowWidth;
		auto cols = o.windowStyle == WindowStyle::None ? std::vector<double>{} : windowColumns(e.length, layout, o.windowWidth, windowCount, &width);
		if (own && own->windows == 0) cols.clear();
		columns[static_cast<std::size_t>(i)] = cols;
		bays[static_cast<std::size_t>(i)] = cols.size() > 1 ? cols[1] - cols[0] : (cols.empty() ? e.length : std::min(e.length, width + o.windowGap));
		// Ground floor slots.
		double groundWidth = width;
		std::vector<double> groundCols = cols;
		if (o.groundWindows == GroundWindows::ShopWindows && o.windowStyle != WindowStyle::None) {
			groundCols = windowColumns(e.length, layout, o.shopWidth, windowCount, &groundWidth);
			if (own && own->windows == 0) groundCols.clear();
		}
		int doors = 0;
		if (own && own->doors >= 0) {
			doors = own->doors;
		} else if (o.doorSides == DoorSides::Every || (o.doorSides == DoorSides::Longest && i == lot.front && lot.doorLot)) {
			doors = o.doorsPerSide;
		}
		std::vector<double> doorSlots = groundCols.empty() ? cols : groundCols;
		if (doorSlots.empty() && doors > 0 && e.length >= o.doorWidth + 16) doorSlots.push_back(std::round(e.length / 2));
		doors = std::min<int>(doors, static_cast<int>(doorSlots.size()));
		std::set<int> doorIndex;
		const int slots = static_cast<int>(doorSlots.size());
		for (int k = 0; k < doors; ++k) {
			int index = 0;
			switch (o.doorPlace) {
				case DoorPlace::Middle: index = (slots - doors) / 2 + k; break;
				case DoorPlace::Spread: index = static_cast<int>(std::floor((k + 0.5) * slots / doors)); break;
				case DoorPlace::Start: index = k; break;
				case DoorPlace::End: index = slots - doors + k; break;
			}
			doorIndex.insert(std::clamp(index, 0, slots - 1));
		}
		auto& list = openings[static_cast<std::size_t>(i)];
		const double cornice = o.cornices ? o.corniceHeight : 0;
		const double topBand = o.topCornice ? o.topCorniceHeight : 0;
		const double plinthTop = o.plinth ? lot.bottom + o.plinthHeight : lot.bottom;
		const auto ceiling = [&](int floor) {
			const double t = floorTop(floor);
			return floor + 1 < lot.floors ? t - cornice - 4 : t - topBand - 4;
		};
		const double pitch = slots > 1 ? doorSlots[1] - doorSlots[0] : e.length;
		// Doors.
		for (const int k : doorIndex) {
			const double w = std::min(o.doorWidth, std::max(16.0, pitch - 16));
			const double c = std::round(doorSlots[static_cast<std::size_t>(k)] - w / 2) + w / 2;
			Opening d;
			d.kind = OpeningKind::Door;
			d.s0 = c - w / 2;
			d.s1 = c + w / 2;
			d.z0 = o.groundLevel == GroundLevel::Raised ? lot.groundBase : lot.bottom;
			d.z1 = std::min(d.z0 + o.doorHeight, ceiling(0));
			d.column = k;
			if (d.z1 - d.z0 >= 48) {
				list.push_back(d);
				++b.plan.doors;
			}
		}
		const auto doorAt = [&](double s0, double s1) {
			return std::any_of(list.begin(), list.end(), [&](const Opening& d) { return d.kind == OpeningKind::Door && s0 < d.s1 + 4 && s1 > d.s0 - 4; });
		};
		// Ground floor windows.
		if (o.groundWindows != GroundWindows::None && o.windowStyle != WindowStyle::None) {
			const bool shop = o.groundWindows == GroundWindows::ShopWindows;
			const double w = groundWidth;
			for (std::size_t k = 0; k < groundCols.size(); ++k) {
				if (!patternHasWindow(pattern, static_cast<int>(k)) && !shop) continue;
				Opening win;
				win.kind = shop ? OpeningKind::Shop : OpeningKind::Window;
				win.s0 = groundCols[k] - w / 2;
				win.s1 = groundCols[k] + w / 2;
				win.z0 = lot.groundBase + (shop ? o.shopSill : o.sillHeight);
				// Never below the street (half sunk) or into the plinth.
				win.z0 = std::max(win.z0, plinthTop + 4);
				win.z1 = std::min(win.z0 + (shop ? o.shopHeight : o.windowHeight), ceiling(0));
				win.column = static_cast<int>(k);
				if (win.z1 - win.z0 < 16 || doorAt(win.s0, win.s1)) continue;
				list.push_back(win);
			}
		}
		// Basement windows in a raised ground floor's socle.
		if (o.groundLevel == GroundLevel::Raised && o.basementWindows && o.windowStyle != WindowStyle::None) {
			const double z0 = plinthTop + 4, z1 = lot.groundBase - 6;
			if (z1 - z0 >= 12) {
				const double w = std::min(o.windowWidth, 40.0);
				for (std::size_t k = 0; k < cols.size(); ++k) {
					if (!patternHasWindow(pattern, static_cast<int>(k))) continue;
					Opening win;
					win.kind = OpeningKind::Basement;
					win.s0 = std::round(cols[k] - w / 2);
					win.s1 = win.s0 + w;
					win.z0 = z0;
					win.z1 = z1;
					win.column = static_cast<int>(k);
					if (doorAt(win.s0 - 24, win.s1 + 24)) continue;   // the steps stand there
					list.push_back(win);
				}
			}
		}
		// Upper floors.
		for (int floor = 1; floor < lot.floors; ++floor) {
			for (std::size_t k = 0; k < cols.size(); ++k) {
				if (!patternHasWindow(pattern, static_cast<int>(k))) continue;
				Opening win;
				win.kind = OpeningKind::Window;
				win.s0 = cols[k] - width / 2;
				win.s1 = cols[k] + width / 2;
				win.z0 = floorBase(floor) + o.sillHeight;
				win.z1 = std::min(win.z0 + o.windowHeight, ceiling(floor));
				win.floor = floor;
				win.column = static_cast<int>(k);
				if (win.z1 - win.z0 < 16) continue;
				list.push_back(win);
			}
		}
		// Damage: broken windows; the jagged top (bays that end lower) comes with the mass.
		for (auto& win : list) {
			if (win.kind == OpeningKind::Door) continue;
			if (damage > 0 && rand01(o.damageSeed, lot.index, i, win.floor * 1000 + win.column, 11) < damage) win.broken = true;
		}
	}
}

/// The mass: world brush(es) inside the wall layers (block) or the slabs behind each wall (facade).
void LotBuilder::buildMass() {
	const int count = static_cast<int>(lot.edges.size());
	const bool flatRoof = o.roof == BuildingRoof::Flat || damage >= 0.3;
	if (!facadeMode()) {
		std::vector<HalfSpace> planes;
		for (int i = 0; i < count; ++i) {
			const Edge& e = lot.edges[static_cast<std::size_t>(i)];
			const SideState state = lot.states[static_cast<std::size_t>(i)];
			Role role = Role::Nodraw;
			if (state == SideState::Facade) {
				role = damage > 0 ? Role::Interior : Role::Nodraw;
			} else if (state == SideState::Plain) {
				role = Role::Facade;
			} else if (lot.shared[static_cast<std::size_t>(i)]) {
				// Seen above a lower neighbour: the plain wall.
				double neighbourTop = massTop;
				for (const auto& other : lots) {
					if (other.index == lot.index) continue;
					const Vec3 mid = (e.a + e.b) * 0.5 + e.n * 2;
					if (insidePolygon(other.edges, Vec3{mid.x, mid.y, 0}, 0.5)) neighbourTop = std::min(neighbourTop, other.top);
				}
				role = neighbourTop < massTop - 1 ? Role::Facade : Role::Nodraw;
			}
			planes.push_back(below(e.n, e.c - layer[static_cast<std::size_t>(i)], role));
		}
		planes.push_back(above(Vec3{0, 0, 1}, foundation, Role::Nodraw));
		const bool roofOnMass = flatRoof && !roofDisp;
		planes.push_back(below(Vec3{0, 0, 1}, massTop, roofOnMass ? Role::Roof : Role::Nodraw));
		addWorld(b.piece(planes, ref(lot.front, refZ(false))));
		return;
	}
	// Facade only: a slab behind each wall, mitred with the walls beside it; returns round open ends.
	const std::vector<bool> mitre = layerMitre();
	const double thickness = std::max(o.facadeThickness, depth + 4);
	for (int i = 0; i < count; ++i) {
		const std::size_t k = static_cast<std::size_t>(i);
		if (!mitre[k]) continue;
		const Edge& e = lot.edges[k];
		const double d = layer[k];
		const Role front = d > 0 ? (damage > 0 ? Role::Interior : Role::Nodraw) : Role::Facade;
		const auto slab = [&](const std::pair<double, double>* range) {
			std::vector<HalfSpace> planes;
			planes.push_back(below(e.n, e.c - d, front));
			planes.push_back(above(e.n, e.c - thickness, Role::Nodraw));
			planes.push_back(above(Vec3{0, 0, 1}, foundation, Role::Nodraw));
			planes.push_back(below(Vec3{0, 0, 1}, massTop, Role::Trim));
			sideBounds(planes, i, mitre, Role::Facade, range);
			addWorld(b.piece(planes, ref(i, refZ(false))));
		};
		if (wall[k]) {
			slab(nullptr);
		} else {
			for (const auto& range : returns[k]) slab(&range);
		}
	}
}

/// The wall layer of a facade side: few clean pieces around the openings.
void LotBuilder::buildWallLayer(int i) {
	const double d = layer[static_cast<std::size_t>(i)];
	if (d <= 0) return;
	const Edge& e = lot.edges[static_cast<std::size_t>(i)];
	const auto& list = openings[static_cast<std::size_t>(i)];
	const double zBot = foundation, zTop = lot.top;
	std::vector<double> zs{zBot, zTop};
	for (const auto& op : list) {
		zs.push_back(std::clamp(op.z0, zBot, zTop));
		zs.push_back(std::clamp(op.z1, zBot, zTop));
	}
	if (massTop > zBot && massTop < zTop) zs.push_back(massTop);
	const double groundTop = lot.groundBase + o.groundFloorHeight;
	if (groundTop > zBot && groundTop < zTop) zs.push_back(groundTop);
	std::sort(zs.begin(), zs.end());
	zs.erase(std::unique(zs.begin(), zs.end(), [](double a, double b) { return std::abs(a - b) < 0.01; }), zs.end());
	const std::vector<bool> mitre = layerMitre();
	const double sLo = 0, sHi = e.length;
	struct Open {
		double a, b, z0;
	};
	std::vector<Open> open;   // pieces still growing upwards
	struct Done {
		double a, b, z0, z1;
	};
	std::vector<Done> done;
	for (std::size_t k = 0; k + 1 < zs.size(); ++k) {
		const double za = zs[k], zb = zs[k + 1];
		// The openings this band runs through.
		std::vector<std::pair<double, double>> holes;
		for (const auto& op : list) {
			if (op.z0 <= za + 0.01 && op.z1 >= zb - 0.01) holes.emplace_back(op.s0, op.s1);
		}
		std::sort(holes.begin(), holes.end());
		std::vector<std::pair<double, double>> intervals;
		double cursor = sLo;
		for (const auto& [h0, h1] : holes) {
			if (h0 > cursor + 0.01) intervals.emplace_back(cursor, h0);
			cursor = std::max(cursor, h1);
		}
		if (sHi > cursor + 0.01) intervals.emplace_back(cursor, sHi);
		// A piece above the mass's top is its own piece (its back is seen).
		const bool splitHere = std::abs(za - massTop) < 0.01 || std::abs(za - groundTop) < 0.01;
		std::vector<Open> next;
		for (const auto& [a, bb] : intervals) {
			auto it = std::find_if(open.begin(), open.end(), [&](const Open& p) { return std::abs(p.a - a) < 0.01 && std::abs(p.b - bb) < 0.01; });
			if (it != open.end() && !splitHere) {
				next.push_back(*it);
				open.erase(it);
			} else {
				next.push_back({a, bb, za});
			}
		}
		for (const auto& p : open) done.push_back({p.a, p.b, p.z0, za});
		open = std::move(next);
	}
	for (const auto& p : open) done.push_back({p.a, p.b, p.z0, zTop});
	const bool pitched = o.roof != BuildingRoof::Flat && damage < 0.3 && !facadeMode();
	const bool parapetOnTop = o.roof == BuildingRoof::Flat && o.parapetHeight > 0 && o.parapetThickness >= d - 0.01 && damage < 0.3;
	for (const auto& p : done) {
		if (stop()) return;
		std::vector<HalfSpace> planes;
		const double zc = (p.z0 + p.z1) / 2;
		const bool aboveMass = p.z0 >= massTop - 0.01 && massTop < zTop - 0.01;
		planes.push_back(below(e.n, e.c, facadeRole(zc)));
		planes.push_back(above(e.n, e.c - d, aboveMass ? Role::Facade : Role::Nodraw));
		planes.push_back(above(Vec3{0, 0, 1}, p.z0, p.z0 <= zBot + 0.01 ? Role::Nodraw : Role::Trim));
		Role topRole = Role::Trim;
		if (p.z1 >= zTop - 0.01) topRole = pitched || parapetOnTop ? Role::Nodraw : (damage > 0 ? Role::Trim : Role::Roof);
		planes.push_back(below(Vec3{0, 0, 1}, p.z1, topRole));
		if (p.a > sLo + 0.01) planes.push_back(above(e.e, dot(e.e, e.a) + p.a, Role::Trim));
		if (p.b < sHi - 0.01) planes.push_back(below(e.e, dot(e.e, e.a) + p.b, Role::Trim));
		sideBounds(planes, i, mitre, Role::Facade);
		const bool groundPart = zc < groundTop - 1;
		addDetail(b.piece(planes, ref(i, refZ(groundPart))));
	}
}


/// The window / door panels at the back of each opening (or on the wall: panels style).
void LotBuilder::buildPanels(int i) {
	const Edge& e = lot.edges[static_cast<std::size_t>(i)];
	const double d = layer[static_cast<std::size_t>(i)];
	const bool recessed = d > 0;
	const std::vector<bool> mitre = layerMitre();
	for (const auto& op : openings[static_cast<std::size_t>(i)]) {
		if (!op.panel()) continue;
		if (stop()) return;
		Fit fit{e.at(op.s0) + Vec3{0, 0, op.z1}, op.s1 - op.s0, op.z1 - op.z0};
		Role role = op.kind == OpeningKind::Door ? Role::Door : (op.broken ? Role::BrokenWindow : Role::Window);
		std::vector<HalfSpace> planes;
		const bool backing = recessed && o.windowBacking && op.kind != OpeningKind::Door;
		if (backing) {
			// The glass in front, a plate with the interior behind it.
			std::vector<HalfSpace> plate;
			plate.push_back(below(e.n, e.c - d + PANEL / 2, Role::Interior));
			plate.push_back(above(e.n, e.c - d, Role::Nodraw));
			plate.push_back(above(e.e, dot(e.e, e.a) + op.s0, Role::Nodraw));
			plate.push_back(below(e.e, dot(e.e, e.a) + op.s1, Role::Nodraw));
			plate.push_back(above(Vec3{0, 0, 1}, op.z0, Role::Nodraw));
			plate.push_back(below(Vec3{0, 0, 1}, op.z1, Role::Nodraw));
			sideBounds(plate, i, mitre, Role::Nodraw);
			addDetail(b.piece(plate, ref(i, refZ(op.floor == 0))));
			planes.push_back(below(e.n, e.c - d + PANEL, role, &fit));
			planes.push_back(above(e.n, e.c - d + PANEL / 2, Role::Nodraw));
		} else if (recessed) {
			planes.push_back(below(e.n, e.c - d + PANEL, role, &fit));
			planes.push_back(above(e.n, e.c - d, Role::Nodraw));
		} else {
			planes.push_back(below(e.n, e.c + PANEL, role, &fit));
			planes.push_back(above(e.n, e.c, Role::Nodraw));
		}
		const Role sides = recessed ? Role::Nodraw : Role::Trim;
		planes.push_back(above(e.e, dot(e.e, e.a) + op.s0, sides));
		planes.push_back(below(e.e, dot(e.e, e.a) + op.s1, sides));
		planes.push_back(above(Vec3{0, 0, 1}, op.z0, sides));
		planes.push_back(below(Vec3{0, 0, 1}, op.z1, sides));
		if (recessed) sideBounds(planes, i, mitre, Role::Nodraw);
		const auto piece = b.piece(planes, ref(i, refZ(op.floor == 0)));
		if (piece) {
			building.detail.push_back(*piece);
			if (op.kind != OpeningKind::Door) ++b.plan.windows;
		}
	}
}

/// Plinth, cornices between the floors and the top cornice on the walls that have them.
void LotBuilder::buildBands() {
	struct Band {
		double z0, z1, depth;
		bool splitAtDoors;
	};
	std::vector<Band> bands;
	const double plinthTop = lot.bottom + o.plinthHeight;
	if (o.plinth && o.plinthHeight > 0 && o.plinthDepth > 0) bands.push_back({foundation, plinthTop, o.plinthDepth, true});
	if (o.cornices && o.corniceHeight > 0 && o.corniceDepth > 0) {
		for (int floor = 1; floor < lot.floors; ++floor) {
			const double z1 = floorBase(floor);
			if (damage > 0 && z1 > massTop - 0.01) continue;
			bands.push_back({z1 - o.corniceHeight, z1, o.corniceDepth, false});
		}
	}
	if (o.topCornice && damage <= 0 && o.topCorniceHeight > 0 && o.topCorniceDepth > 0) bands.push_back({lot.top - o.topCorniceHeight, lot.top, o.topCorniceDepth, false});
	const int count = static_cast<int>(lot.edges.size());
	for (const auto& band : bands) {
		if (band.z1 - band.z0 < 1) continue;
		for (int i = 0; i < count; ++i) {
			if (!banded[static_cast<std::size_t>(i)]) continue;
			if (stop()) return;
			const Edge& e = lot.edges[static_cast<std::size_t>(i)];
			// Around the doors (and the steps in front of them).
			std::vector<std::pair<double, double>> parts{{-1e9, 1e9}};
			if (band.splitAtDoors) {
				const double extra = o.groundLevel == GroundLevel::Raised ? 8 : 0;
				for (const auto& op : openings[static_cast<std::size_t>(i)]) {
					// A raised door's steps stand in front of the plinth too.
					if (op.kind != OpeningKind::Door || (op.z0 > band.z1 && o.groundLevel != GroundLevel::Raised)) continue;
					std::vector<std::pair<double, double>> next;
					for (const auto& [a, bb] : parts) {
						const double h0 = op.s0 - extra, h1 = op.s1 + extra;
						if (h1 <= a || h0 >= bb) {
							next.emplace_back(a, bb);
							continue;
						}
						if (h0 > a) next.emplace_back(a, h0);
						if (h1 < bb) next.emplace_back(h1, bb);
					}
					parts = std::move(next);
				}
			}
			for (const auto& [a, bb] : parts) {
				std::vector<HalfSpace> planes;
				planes.push_back(above(e.n, e.c, Role::Nodraw));
				planes.push_back(below(e.n, e.c + band.depth, Role::Cornice));
				planes.push_back(above(Vec3{0, 0, 1}, band.z0, band.z0 <= foundation + 0.01 ? Role::Nodraw : Role::Cornice));
				planes.push_back(below(Vec3{0, 0, 1}, band.z1, Role::Cornice));
				if (a > -1e8) planes.push_back(above(e.e, dot(e.e, e.a) + a, Role::Cornice));
				if (bb < 1e8) planes.push_back(below(e.e, dot(e.e, e.a) + bb, Role::Cornice));
				sideBounds(planes, i, banded, Role::Cornice);
				addDetail(b.piece(planes, ref(i, band.z1)));
			}
		}
	}
}

/// A sill under each window.
void LotBuilder::buildSills() {
	if (!o.sills || o.sillDepth <= 0 || o.sillThickness <= 0) return;
	const double plinthTop = o.plinth ? lot.bottom + o.plinthHeight : lot.bottom;
	for (std::size_t i = 0; i < lot.edges.size(); ++i) {
		const Edge& e = lot.edges[i];
		for (const auto& op : openings[i]) {
			if (op.kind != OpeningKind::Window && op.kind != OpeningKind::Shop) continue;
			const double z0 = op.z0 - o.sillThickness;
			if (z0 < plinthTop + 0.5) continue;
			// Not into a cornice.
			bool clash = false;
			if (o.cornices) {
				for (int floor = 1; floor < lot.floors; ++floor) {
					const double c1 = floorBase(floor), c0 = c1 - o.corniceHeight;
					if (z0 < c1 && op.z0 > c0) clash = true;
				}
			}
			if (clash) continue;
			std::vector<HalfSpace> planes;
			planes.push_back(above(e.n, e.c, Role::Nodraw));
			planes.push_back(below(e.n, e.c + o.sillDepth, Role::Trim));
			planes.push_back(above(e.e, dot(e.e, e.a) + op.s0 - 4, Role::Trim));
			planes.push_back(below(e.e, dot(e.e, e.a) + op.s1 + 4, Role::Trim));
			planes.push_back(above(Vec3{0, 0, 1}, z0, Role::Trim));
			planes.push_back(below(Vec3{0, 0, 1}, op.z0, Role::Trim));
			addDetail(b.piece(planes, ref(static_cast<int>(i), op.z0)));
		}
	}
}

/// Raised ground floor: steps up to each door (each at most 18 units high).
void LotBuilder::buildSteps() {
	if (o.groundLevel != GroundLevel::Raised) return;
	const auto [count, height] = stepsFor(lot.groundBase - lot.bottom);
	if (count <= 0) return;
	const double depthEach = std::max(8.0, o.stepDepth);
	const double stepBottom = std::min(lot.bottom, foundation);
	for (std::size_t i = 0; i < lot.edges.size(); ++i) {
		const Edge& e = lot.edges[i];
		for (const auto& op : openings[i]) {
			if (op.kind != OpeningKind::Door) continue;
			for (int k = 1; k <= count; ++k) {
				std::vector<HalfSpace> planes;
				planes.push_back(above(e.n, e.c + (k - 1) * depthEach, Role::Nodraw));
				planes.push_back(below(e.n, e.c + k * depthEach, Role::Trim));
				planes.push_back(above(e.e, dot(e.e, e.a) + op.s0 - 8, Role::Trim));
				planes.push_back(below(e.e, dot(e.e, e.a) + op.s1 + 8, Role::Trim));
				planes.push_back(above(Vec3{0, 0, 1}, stepBottom, Role::Nodraw));
				planes.push_back(below(Vec3{0, 0, 1}, lot.groundBase - (k - 1) * height, Role::Trim));
				auto piece = b.piece(planes, ref(static_cast<int>(i), lot.groundBase));
				if (piece) {
					building.detail.push_back(std::move(*piece));
					++b.plan.steps;
				}
			}
		}
	}
}

namespace {

/// Where two lines (point + direction, seen from above) meet.
std::optional<Vec3> meet(const Vec3& p, const Vec3& d, const Vec3& q, const Vec3& f) {
	const double den = d.x * f.y - d.y * f.x;
	if (std::abs(den) < 1e-9) return std::nullopt;
	const double t = ((q.x - p.x) * f.y - (q.y - p.y) * f.x) / den;
	return p + d * t;
}

/// Smooth bumps from the world position (the same at a shared edge: neighbouring pieces meet).
double bumps(const Vec3& p, std::uint32_t seed) {
	const double a = rand01(seed, 1) * 6.283, bb = rand01(seed, 2) * 6.283, c = rand01(seed, 3) * 6.283;
	const double v = std::sin(p.x * 0.021 + a) * std::sin(p.z * 0.027 + bb) + 0.6 * std::sin(p.y * 0.017 + c) * std::sin(p.z * 0.013 + a) +
	                 0.4 * std::sin((p.x + p.y) * 0.031 + c);
	return v / 2.0;
}

double smoothstep(double x) {
	x = std::clamp(x, 0.0, 1.0);
	return x * x * (3 - 2 * x);
}

} // namespace

/// Flat roof: parapet (and the sagging displacement roof); gable / hip: one roof brush.
void LotBuilder::buildRoof() {
	const int count = static_cast<int>(lot.edges.size());
	const bool damaged = damage >= 0.3;
	if (o.roof == BuildingRoof::Flat || damaged || facadeMode()) {
		// Parapet: on the walls (facade only) or round the roof except where a lot touches.
		if (o.parapetHeight > 0 && o.parapetThickness > 0 && !damaged) {
			std::vector<bool> sides(lot.edges.size(), false);
			for (int i = 0; i < count; ++i) {
				const bool hasWall = facadeMode() ? (wall[static_cast<std::size_t>(i)] || !returns[static_cast<std::size_t>(i)].empty())
				                                  : !lot.shared[static_cast<std::size_t>(i)];
				sides[static_cast<std::size_t>(i)] = hasWall;
			}
			const double thickness = facadeMode() ? std::min(o.parapetThickness, std::max(o.facadeThickness, 1.0)) : o.parapetThickness;
			for (int i = 0; i < count; ++i) {
				if (!sides[static_cast<std::size_t>(i)]) continue;
				const Edge& e = lot.edges[static_cast<std::size_t>(i)];
				const auto parapet = [&](const std::pair<double, double>* range) {
					std::vector<HalfSpace> planes;
					planes.push_back(below(e.n, e.c, Role::Facade));
					planes.push_back(above(e.n, e.c - thickness, Role::Facade));
					planes.push_back(above(Vec3{0, 0, 1}, lot.top, Role::Nodraw));
					planes.push_back(below(Vec3{0, 0, 1}, lot.top + o.parapetHeight, Role::Trim));
					sideBounds(planes, i, sides, Role::Facade, range);
					addDetail(b.piece(planes, ref(i, lot.top)));
				};
				if (facadeMode() && !wall[static_cast<std::size_t>(i)]) {
					for (const auto& range : returns[static_cast<std::size_t>(i)]) parapet(&range);
				} else {
					parapet(nullptr);
				}
			}
		}
		// Sagging roof: a displacement over the mass's top on its own thin brush.
		if (roofDisp) {
			const double thick = std::max(4.0, o.displacementStrength + 2);
			const double inset = o.parapetHeight > 0 ? o.parapetThickness : 0;
			std::vector<HalfSpace> planes;
			for (int i = 0; i < count; ++i) {
				const Edge& e = lot.edges[static_cast<std::size_t>(i)];
				const double d = std::max(layer[static_cast<std::size_t>(i)], lot.shared[static_cast<std::size_t>(i)] ? layer[static_cast<std::size_t>(i)] : inset);
				planes.push_back(below(e.n, e.c - d, Role::Nodraw));
			}
			planes.push_back(above(Vec3{0, 0, 1}, massTop - thick, Role::Nodraw));
			planes.push_back(below(Vec3{0, 0, 1}, massTop, Role::Roof));
			auto piece = b.piece(planes, ref(lot.front, massTop));
			if (piece) {
				const auto geometry = buildBrushGeometry(*piece);
				for (const auto& face : geometry.faces) {
					if (face.normal.z < 0.9 || face.vertices.size() != 4) continue;
					Side& side = piece->sides[static_cast<std::size_t>(face.sideIndex)];
					auto disp = makeDisplacement(face, std::clamp(o.displacementPower, 2, 4));
					if (!disp) break;
					storeDisplacement(side, *disp);
					const auto mesh = buildDisplacement(side, face);
					if (!mesh) break;
					for (std::size_t k = 0; k < mesh->positions.size() && k < disp->distances.size(); ++k) {
						const Vec3& p = mesh->positions[k];
						double edge = 1e9;
						for (const auto& e : lot.edges) edge = std::min(edge, e.dist(p) - inset);
						const double w = smoothstep(edge / 96.0);
						disp->distances[k] = -o.displacementStrength * w * (0.75 + 0.25 * bumps(p, o.seed + 7));
					}
					storeDisplacement(side, *disp);
					side.lightmapScale = fittingLightmapScale(face, side);
					++b.plan.displacements;
					break;
				}
				building.detail.push_back(std::move(*piece));
			}
		}
		return;
	}
	// Gable / hip: the outline with the overhang (not over a shared side), planes rising from the eaves.
	const double t = std::tan(std::clamp(o.roofAngle, 5.0, 75.0) * std::numbers::pi / 180.0);
	const double eave = lot.top + std::max(1.0, o.eaveThickness);
	std::vector<double> over(lot.edges.size());
	for (int i = 0; i < count; ++i) over[static_cast<std::size_t>(i)] = lot.shared[static_cast<std::size_t>(i)] ? 0 : std::max(0.0, o.roofOverhang);
	std::vector<HalfSpace> planes;
	for (int i = 0; i < count; ++i) {
		const Edge& e = lot.edges[static_cast<std::size_t>(i)];
		planes.push_back(below(e.n, e.c + over[static_cast<std::size_t>(i)], Role::Trim));
	}
	planes.push_back(above(Vec3{0, 0, 1}, lot.top, Role::Trim));
	if (o.roof == BuildingRoof::Hip) {
		for (int i = 0; i < count; ++i) {
			if (lot.shared[static_cast<std::size_t>(i)]) continue;
			const Edge& e = lot.edges[static_cast<std::size_t>(i)];
			planes.push_back(below(Vec3{t * e.n.x, t * e.n.y, 1}, eave + t * (e.c + over[static_cast<std::size_t>(i)]), Role::Roof));
		}
	} else {
		// Ridge along the front, sloping down to the front and the back.
		const Edge& front = lot.edges[static_cast<std::size_t>(lot.front)];
		const Vec3 m = front.n;
		double wMin = 1e18, wMax = -1e18;
		for (int i = 0; i < count; ++i) {
			const int j = (i + 1) % count;
			const Edge& ei = lot.edges[static_cast<std::size_t>(i)];
			const Edge& ej = lot.edges[static_cast<std::size_t>(j)];
			const auto corner = meet(ei.a + ei.n * over[static_cast<std::size_t>(i)], ei.e, ej.a + ej.n * over[static_cast<std::size_t>(j)], ej.e);
			if (!corner) continue;
			wMin = std::min(wMin, dot(m, *corner));
			wMax = std::max(wMax, dot(m, *corner));
		}
		if (wMax > wMin) {
			planes.push_back(below(Vec3{t * m.x, t * m.y, 1}, eave + t * wMax, Role::Roof));
			planes.push_back(below(Vec3{-t * m.x, -t * m.y, 1}, eave - t * wMin, Role::Roof));
		}
	}
	auto piece = b.piece(planes, ref(lot.front, lot.top));
	if (!piece) return;
	// Tall vertical faces are gable ends (the wall goes on up), low ones the eaves' fascia.
	const auto geometry = buildBrushGeometry(*piece);
	for (const auto& face : geometry.faces) {
		if (std::abs(face.normal.z) > 0.1) continue;
		double lo = 1e18, hi = -1e18;
		for (const auto& v : face.vertices) {
			lo = std::min(lo, v.z);
			hi = std::max(hi, v.z);
		}
		if (hi - lo <= std::max(1.0, o.eaveThickness) + 1) continue;
		Side& side = piece->sides[static_cast<std::size_t>(face.sideIndex)];
		int nearest = lot.front;
		double best = -2;
		for (int i = 0; i < count; ++i) {
			const double d = dot(lot.edges[static_cast<std::size_t>(i)].n, face.normal);
			if (d > best) {
				best = d;
				nearest = i;
			}
		}
		layTexture(side, b.materials.facade, o.texture(BuildingPart::Facade), ref(nearest, refZ(false)), b.size, nullptr);
	}
	building.detail.push_back(std::move(*piece));
}

/// Plain walls with a bulging displacement: thin brushes in front of the wall between the bands.
void LotBuilder::buildWallDisplacements() {
	if (!o.wallDisplacement || o.displacementStrength <= 0) return;
	const int count = static_cast<int>(lot.edges.size());
	for (int i = 0; i < count; ++i) {
		if (lot.states[static_cast<std::size_t>(i)] != SideState::Plain) continue;
		const Edge& e = lot.edges[static_cast<std::size_t>(i)];
		if (e.length < 32) continue;
		std::vector<std::pair<double, double>> taken;
		if (o.plinth) taken.emplace_back(foundation, lot.bottom + o.plinthHeight);
		if (o.cornices) {
			for (int floor = 1; floor < lot.floors; ++floor) {
				if (damage > 0 && floorBase(floor) > massTop - 0.01) continue;
				taken.emplace_back(floorBase(floor) - o.corniceHeight, floorBase(floor));
			}
		}
		const double wallTop = damage > 0 ? massTop : lot.top;
		if (o.topCornice && damage <= 0) taken.emplace_back(lot.top - o.topCorniceHeight, lot.top);
		std::sort(taken.begin(), taken.end());
		std::vector<std::pair<double, double>> free;
		double cursor = std::max(foundation, lot.bottom);
		for (const auto& [a, bb] : taken) {
			if (a > cursor + 16) free.emplace_back(cursor, a);
			cursor = std::max(cursor, bb);
		}
		if (wallTop > cursor + 16) free.emplace_back(cursor, wallTop);
		for (const auto& [z0, z1] : free) {
			std::vector<HalfSpace> planes;
			planes.push_back(below(e.n, e.c + 2, Role::Facade));
			planes.push_back(above(e.n, e.c, Role::Nodraw));
			planes.push_back(above(e.e, dot(e.e, e.a), Role::Nodraw));
			planes.push_back(below(e.e, dot(e.e, e.b), Role::Nodraw));
			planes.push_back(above(Vec3{0, 0, 1}, z0, Role::Nodraw));
			planes.push_back(below(Vec3{0, 0, 1}, z1, Role::Nodraw));
			auto piece = b.piece(planes, ref(i, refZ(false)));
			if (!piece) continue;
			bool done = false;
			const auto geometry = buildBrushGeometry(*piece);
			for (const auto& face : geometry.faces) {
				if (dot(face.normal, e.n) < 0.99 || face.vertices.size() != 4) continue;
				Side& side = piece->sides[static_cast<std::size_t>(face.sideIndex)];
				auto disp = makeDisplacement(face, std::clamp(o.displacementPower, 2, 4));
				if (!disp) break;
				storeDisplacement(side, *disp);
				const auto mesh = buildDisplacement(side, face);
				if (!mesh) break;
				for (std::size_t k = 0; k < mesh->positions.size() && k < disp->distances.size(); ++k) {
					const Vec3& p = mesh->positions[k];
					const double s = e.along(p);
					const double w = smoothstep(std::min(s, e.length - s) / 24.0) * smoothstep(std::min(p.z - z0, z1 - p.z) / 24.0);
					disp->distances[k] = o.displacementStrength * w * bumps(p, o.seed + 13);
				}
				storeDisplacement(side, *disp);
				side.lightmapScale = fittingLightmapScale(face, side);
				done = true;
				break;
			}
			if (!done) continue;
			++b.plan.displacements;
			building.detail.push_back(std::move(*piece));
		}
	}
}

namespace {

double yawOf(const Vec3& n) {
	return std::atan2(n.y, n.x) * 180.0 / std::numbers::pi;
}

/// Whether a prop goes at this place: its condition, every N-th, its chance (from the seed).
bool propHere(const FacadeProp& prop, std::uint32_t seed, double damage, bool damagedSpot, int counter, long long salt) {
	if (prop.model.empty()) return false;
	double chance = std::clamp(prop.chance, 0.0, 100.0) / 100.0;
	switch (prop.when) {
		case PropCondition::Always: break;
		case PropCondition::Intact:
			if (damagedSpot) return false;
			chance *= 1 - damage;
			break;
		case PropCondition::Damaged:
			if (damage <= 0) return false;
			if (!damagedSpot) chance *= damage;
			break;
	}
	if (prop.every > 1 && counter % prop.every != 0) return false;
	return rand01(seed, salt, counter, 977) < chance;
}

} // namespace

/// Damage: a jagged edge above the mass (each bay of a facade ends at its own height) and bays
/// left open for broken facade models (Bay props); windows there go.
void LotBuilder::addDamage() {
	const int count = static_cast<int>(lot.edges.size());
	for (int i = 0; i < count; ++i) {
		if (lot.states[static_cast<std::size_t>(i)] != SideState::Facade || layer[static_cast<std::size_t>(i)] <= 0) continue;
		const Edge& e = lot.edges[static_cast<std::size_t>(i)];
		auto& list = openings[static_cast<std::size_t>(i)];
		const auto& cols = columns[static_cast<std::size_t>(i)];
		// Bays: from the middle between two columns to the next (the ends: the side's ends).
		std::vector<double> cuts{0};
		for (std::size_t k = 0; k + 1 < cols.size(); ++k) cuts.push_back(std::round((cols[k] + cols[k + 1]) / 2));
		cuts.push_back(e.length);
		if (cols.empty()) {
			cuts = {0};
			const int pieces = std::max(1, static_cast<int>(e.length / 128));
			for (int k = 1; k < pieces; ++k) cuts.push_back(std::round(e.length * k / pieces));
			cuts.push_back(e.length);
		}
		const auto dropWindows = [&](double s0, double s1, double z0, double z1) {
			std::erase_if(list, [&](const Opening& op) {
				return op.kind != OpeningKind::Door && op.kind != OpeningKind::Gap && op.kind != OpeningKind::Hole && op.s0 < s1 && op.s1 > s0 && op.z0 < z1 && op.z1 > z0;
			});
		};
		if (massTop < lot.top - 1) {
			for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
				const double r = rand01(o.damageSeed, lot.index, i, static_cast<long long>(k), 3);
				const double h = massTop + std::floor(r * (lot.top - massTop) / 8) * 8;
				if (h >= lot.top - 1) continue;
				Opening gap;
				gap.kind = OpeningKind::Gap;
				gap.s0 = cuts[k];
				gap.s1 = cuts[k + 1];
				gap.z0 = h;
				gap.z1 = lot.top;
				dropWindows(gap.s0, gap.s1, gap.z0, gap.z1);
				list.push_back(gap);
			}
		}
		// Broken facade models take a bay of a floor (or more, by their box).
		int counter = 0;
		for (std::size_t p = 0; p < o.props.size(); ++p) {
			const FacadeProp& prop = o.props[p];
			if (prop.anchor != FacadePropAnchor::Bay || prop.model.empty()) continue;
			const auto box = o.modelBounds ? o.modelBounds(prop.model) : std::nullopt;
			for (int floor = 1; floor < lot.floors; ++floor) {
				const double base = floorBase(floor);
				if (base >= massTop - 1) break;
				for (std::size_t k = 0; k + 1 < cuts.size(); ++k, ++counter) {
					if (!propHere(prop, o.damageSeed + static_cast<std::uint32_t>(p), damage, false, counter, lot.index * 131 + i)) continue;
					const double bayWidth = cuts[k + 1] - cuts[k];
					const double width = box ? std::max(16.0, box->size().y) : bayWidth;
					const double height = std::min(box ? std::max(16.0, box->size().z) : o.floorHeight, floorTop(floor) - base);
					const double centre = (cuts[k] + cuts[k + 1]) / 2;
					Opening hole;
					hole.kind = OpeningKind::Hole;
					hole.s0 = std::max(layer[static_cast<std::size_t>(i)] + 8, std::round(centre - width / 2));
					hole.s1 = std::min(e.length - layer[static_cast<std::size_t>(i)] - 8, hole.s0 + width);
					hole.z0 = base;
					hole.z1 = base + height;
					hole.floor = floor;
					if (hole.s1 - hole.s0 < 16) continue;
					const bool overlaps = std::any_of(list.begin(), list.end(), [&](const Opening& op) {
						return (op.kind == OpeningKind::Hole || op.kind == OpeningKind::Gap || op.kind == OpeningKind::Door) && op.s0 < hole.s1 && op.s1 > hole.s0 &&
						       op.z0 < hole.z1 && op.z1 > hole.z0;
					});
					if (overlaps) continue;
					dropWindows(hole.s0, hole.s1, hole.z0, hole.z1);
					list.push_back(hole);
					// The model: its back against the mass, standing on the floor.
					PropPlacement place;
					const double out = (box ? -box->min.x : 0) + prop.wallOffset;
					place.origin = e.at((hole.s0 + hole.s1) / 2) + e.n * (out - layer[static_cast<std::size_t>(i)]);
					place.origin.z = base - (box ? box->min.z : 0) + prop.up;
					place.yaw = yawOf(e.n) + prop.yaw;
					b.plan.props.push_back({prop.model, place, lot.index});
				}
			}
		}
	}
}

/// Props on the facade grid (Bay props come with the damage).
void LotBuilder::placeProps() {
	const int count = static_cast<int>(lot.edges.size());
	for (std::size_t p = 0; p < o.props.size(); ++p) {
		const FacadeProp& prop = o.props[p];
		if (prop.model.empty() || prop.anchor == FacadePropAnchor::Bay) continue;
		const auto box = o.modelBounds ? o.modelBounds(prop.model) : std::nullopt;
		const double out = (box ? -box->min.x : 0) + prop.wallOffset;
		const std::uint32_t seed = o.seed + static_cast<std::uint32_t>(p) * 7919u;
		int counter = 0;
		const auto add = [&](const Vec3& origin, double yaw) {
			PropPlacement place;
			place.origin = origin + Vec3{0, 0, prop.up};
			place.yaw = yaw + prop.yaw;
			b.plan.props.push_back({prop.model, place, lot.index});
		};
		if (prop.anchor == FacadePropAnchor::Roof) {
			if (facadeMode() || (o.roof != BuildingRoof::Flat && damage < 0.3)) continue;
			const double inset = std::max(depth, o.parapetHeight > 0 ? o.parapetThickness : 0.0) + 32;
			Bounds area;
			for (const auto& v : lot.poly) area.add(v);
			const double step = std::max(32.0, prop.spacing);
			for (double x = area.min.x + inset; x <= area.max.x - inset; x += step) {
				for (double y = area.min.y + inset; y <= area.max.y - inset; y += step, ++counter) {
					const Vec3 at{x, y, 0};
					if (!insidePolygon(lot.edges, at, inset)) continue;
					if (!propHere(prop, seed, damage, false, counter, lot.index)) continue;
					const double yaw = std::floor(rand01(seed, lot.index, counter, 5) * 4) * 90;
					add(Vec3{x, y, massTop - (box ? box->min.z : 0)}, yaw);
				}
			}
			continue;
		}
		if (prop.anchor == FacadePropAnchor::Corner) {
			for (int i = 0; i < count; ++i) {
				const int prev = (i + count - 1) % count;
				const bool here = banded[static_cast<std::size_t>(i)] || banded[static_cast<std::size_t>(prev)];
				if (!here) continue;
				const Edge& e = lot.edges[static_cast<std::size_t>(i)];
				const Edge& ep = lot.edges[static_cast<std::size_t>(prev)];
				const Vec3 bisector = normalize(e.n + ep.n);
				if (!propHere(prop, seed, damage, false, counter++, lot.index)) continue;
				add(Vec3{e.a.x, e.a.y, lot.bottom - (box ? box->min.z : 0)} + bisector * out, yawOf(bisector));
			}
			continue;
		}
		for (int i = 0; i < count; ++i) {
			const Edge& e = lot.edges[static_cast<std::size_t>(i)];
			const double d = layer[static_cast<std::size_t>(i)];
			const double inRecess = d > 0 ? -(d - PANEL) : PANEL;   // the panel's front, from the outer line
			const auto& list = openings[static_cast<std::size_t>(i)];
			if (prop.anchor == FacadePropAnchor::Ground) {
				if (!banded[static_cast<std::size_t>(i)]) continue;
				const double step = std::max(32.0, prop.spacing);
				const double front = o.plinth ? o.plinthDepth : 0;
				for (double s = std::min(e.length / 2, o.cornerMargin / 2 + 16); s <= e.length - 16; s += step, ++counter) {
					const bool atDoor = std::any_of(list.begin(), list.end(), [&](const Opening& op) { return op.kind == OpeningKind::Door && s > op.s0 - 32 && s < op.s1 + 32; });
					if (atDoor) continue;
					if (!propHere(prop, seed, damage, false, counter, lot.index * 131 + i)) continue;
					Vec3 at = e.at(s) + e.n * (front + out);
					at.z = lot.bottom - (box ? box->min.z : 0);
					add(at, yawOf(e.n));
				}
				continue;
			}
			for (const auto& op : list) {
				if (!op.panel()) continue;
				const bool door = op.kind == OpeningKind::Door;
				if ((prop.anchor == FacadePropAnchor::Door) != door) continue;
				if (!door) {
					const bool groundFloor = op.floor == 0;
					if ((groundFloor && !prop.groundFloor) || (!groundFloor && !prop.upperFloors)) continue;
				}
				const int here = counter++;
				if (!propHere(prop, seed, damage, op.broken, here, lot.index * 131 + i)) continue;
				Vec3 at = e.at((op.s0 + op.s1) / 2);
				switch (prop.anchor) {
					case FacadePropAnchor::Window:
						at = at + e.n * (inRecess + out);
						at.z = (op.z0 + op.z1) / 2 - (box ? (box->min.z + box->max.z) / 2 : 0);
						break;
					case FacadePropAnchor::BelowWindow:
						at = at + e.n * out;
						at.z = op.z0 - (o.sills ? o.sillThickness : 0) - 2 - (box ? box->max.z : 0);
						break;
					case FacadePropAnchor::AboveWindow:
						at = at + e.n * out;
						at.z = op.z1 + 2 - (box ? box->min.z : 0);
						break;
					case FacadePropAnchor::Door:
						at = at + e.n * (inRecess + out);
						at.z = op.z0 - (box ? box->min.z : 0);
						break;
					default: continue;
				}
				add(at, yawOf(e.n));
			}
		}
	}
}

void LotBuilder::build() {
	damage = std::clamp(o.damage, 0.0, 1.0);
	foundation = lot.bottom;
	if (o.groundLevel == GroundLevel::HalfSunk) foundation = std::min(foundation, lot.groundBase - 8);
	if (o.foundation && lot.terrain) foundation = std::min(foundation, lot.ground - 8);
	// Floors lost from the top.
	massTop = lot.top;
	if (damage > 0 && lot.floors > 1) {
		const int lost = static_cast<int>(std::floor(damage * (lot.floors - 1) * (0.5 + 0.5 * rand01(o.damageSeed, lot.index, 1)) + 1e-9));
		if (lost > 0) massTop = std::max(floorBase(lot.floors - lost), lot.groundBase + o.groundFloorHeight);
	}
	decideSides();
	decideOpenings();
	if (damage > 0) addDamage();
	if (stop()) return;
	roofDisp = !facadeMode() && o.roofDisplacement && damage < 0.3 && o.roof == BuildingRoof::Flat && lot.edges.size() == 4;
	const double realMassTop = massTop;
	if (roofDisp) massTop = realMassTop - std::max(4.0, o.displacementStrength + 2);
	buildMass();
	massTop = realMassTop;
	for (std::size_t i = 0; i < lot.edges.size(); ++i) {
		if (stop()) return;
		if (layer[i] > 0) buildWallLayer(static_cast<int>(i));
		buildPanels(static_cast<int>(i));
	}
	buildBands();
	buildSills();
	buildSteps();
	buildRoof();
	buildWallDisplacements();
	placeProps();
}

/// Cuts a new piece around the brushes of the map it would reach into (never a displacement or a
/// brush entity other than func_detail). Faces made by the cut are nodraw (they lie on the brush).
std::vector<Solid> clipToMap(const MapDocument& doc, MapDocument& ids, const Solid& piece, const std::vector<SolidRef>& lots, int& clipped) {
	const auto bounds = buildBrushGeometry(piece).bounds;
	std::vector<Solid> pieces{piece};
	for (const auto& n : blockers(doc, bounds, 0, lots)) {
		std::vector<Solid> next;
		for (const auto& p : pieces) {
			if (sharedVolume(p, *n.solid) <= 1.0) {
				next.push_back(p);
				continue;
			}
			Solid plain = p;
			for (auto& side : plain.sides) std::erase_if(side.extra, [](const KvNode& node) { return node.block && iequals(node.key, "dispinfo"); });
			std::set<int> before;
			for (const auto& side : plain.sides) before.insert(side.id);
			for (auto cut : subtractSolid(ids, plain, *n.solid)) {
				for (auto& side : cut.sides) {
					if (!before.contains(side.id)) {
						side.material = NODRAW;
						alignToWorld(side);
					}
				}
				if (brushProblem(cut).empty()) next.push_back(std::move(cut));
			}
			++clipped;
		}
		pieces = std::move(next);
	}
	return pieces;
}

} // namespace

namespace {

/// The settings a lot was built with, as lot 0 on its own: what variation chose is fixed, its shared
/// sides keep their state (built again alone they would face the next building's wall), the doors
/// of a group stay where they were.
BuildingOptions resolvedSettings(const BuildingOptions& o, const Lot& lot) {
	BuildingOptions s = o;
	s.cancelled = nullptr;
	s.modelBounds = nullptr;
	s.sideSettings.clear();
	for (const auto& [key, side] : o.sideSettings) {
		if (key.first == lot.index) s.sideSettings[{0, key.second}] = side;
	}
	for (std::size_t i = 0; i < lot.shared.size(); ++i) {
		if (!lot.shared[i]) continue;
		auto& side = s.sideSettings[{0, static_cast<int>(i)}];
		if (side.state == SideState::Auto) side.state = lot.states[i];
	}
	if (!lot.doorLot) {
		for (std::size_t i = 0; i < lot.edges.size(); ++i) {
			auto& side = s.sideSettings[{0, static_cast<int>(i)}];
			if (side.doors < 0) side.doors = 0;
		}
	}
	s.lotSettings.clear();
	if (const auto it = o.lotSettings.find(lot.index); it != o.lotSettings.end()) s.lotSettings[0] = it->second;
	s.floorVariation = 0;
	s.facadeVariants.clear();
	s.facadeMaterial = lot.facade;
	s.pattern = lot.pattern;
	s.touchingIsOne = false;
	if (lot.varied) {
		s.heightMode = HeightMode::Floors;
		s.floors = lot.floors;
	}
	return s;
}

} // namespace

BuildingPlan planBuildings(const MapDocument& doc, MapDocument& ids, const std::vector<SolidRef>& lotRefs, const BuildingOptions& o, const TextureSizeFn& textureSize) {
	BuildingPlan plan;
	// The lots: outline from above, bottom and top.
	std::vector<Lot> lots;
	std::vector<const Solid*> lotSolids;
	for (const auto& ref : lotRefs) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) {
			plan.notes.push_back("Brush " + std::to_string(ref.solidId) + " is broken - left alone.");
			continue;
		}
		std::vector<Vec3> points;
		for (const auto& face : geometry.faces) points.insert(points.end(), face.vertices.begin(), face.vertices.end());
		Lot lot;
		lot.ref = ref;
		lot.index = static_cast<int>(lots.size());
		lot.poly = hull2d(points);
		lot.bottom = geometry.bounds.min.z;
		lot.brushTop = geometry.bounds.max.z;
		if (lot.poly.size() < 3 || polygonArea(lot.poly) < 32 * 32 || lot.brushTop - lot.bottom < 8) {
			plan.notes.push_back("Brush " + std::to_string(ref.solidId) + " is too small for a building (at least 32 x 32 units) - left alone.");
			continue;
		}
		lot.edges = edgesOf(lot.poly, lot.bottom);
		// The lot's most common visible material: the facade if none is set.
		std::map<std::string, int> materials;
		for (const auto& side : solid->sides) {
			if (!startsWithI(side.material, "TOOLS/")) ++materials[side.material];
		}
		int best = 0;
		for (const auto& [name, n] : materials) {
			if (n > best) {
				best = n;
				lot.facade = name;
			}
		}
		lots.push_back(std::move(lot));
		lotSolids.push_back(solid);
	}
	if (lots.empty()) return plan;
	// Model mode: one model per lot, facing its longest side.
	if (o.mode == BuildingMode::Model) {
		if (o.lotModel.empty()) {
			plan.notes.push_back("Choose a model for the lots (Shape: Model).");
			return plan;
		}
		const auto box = o.modelBounds ? o.modelBounds(o.lotModel) : std::nullopt;
		for (auto& lot : lots) {
			Vec3 centre;
			for (const auto& p : lot.poly) centre = centre + p;
			centre = centre / static_cast<double>(lot.poly.size());
			int longest = 0;
			for (std::size_t i = 0; i < lot.edges.size(); ++i) {
				if (lot.edges[i].length > lot.edges[static_cast<std::size_t>(longest)].length) longest = static_cast<int>(i);
			}
			PropPlacement place;
			place.origin = {centre.x, centre.y, lot.bottom - (box ? box->min.z : 0)};
			place.yaw = yawOf(lot.edges[static_cast<std::size_t>(longest)].n);
			plan.props.push_back({o.lotModel, place, static_cast<int>(plan.buildings.size())});
			BuildingPlan::Building building;
			building.lot = lot.ref;
			building.bottom = lot.bottom;
			building.top = lot.brushTop;
			building.foundation = lot.bottom;
			building.outline = lot.poly;
			building.lotTop = lot.brushTop;
			building.settings = resolvedSettings(o, lot);
			plan.buildings.push_back(std::move(building));
		}
		return plan;
	}
	// Sides: shared with another lot, blocked by a brush of the map; the front; their states.
	bool openInBlock = false;
	for (auto& lot : lots) {
		if (o.cancelled && o.cancelled()) return {};
		const std::size_t count = lot.edges.size();
		lot.shared.assign(count, false);
		lot.blocked.assign(count, false);
		lot.neighbour.assign(count, -1);
		Bounds around;
		for (const auto& p : lot.poly) {
			around.add(Vec3{p.x, p.y, lot.bottom});
			around.add(Vec3{p.x, p.y, lot.brushTop});
		}
		const auto nearby = blockers(doc, around, 48, lotRefs);
		for (std::size_t i = 0; i < count; ++i) {
			const Edge& e = lot.edges[i];
			int shared = 0, blocked = 0;
			std::map<int, int> sharedWith;
			constexpr int SAMPLES = 5;
			for (int k = 0; k < SAMPLES; ++k) {
				const Vec3 at = e.at(e.length * (k + 0.5) / SAMPLES);
				const Vec3 out2 = at + e.n * 2;
				for (const auto& other : lots) {
					if (other.index == lot.index) continue;
					if (other.bottom > lot.brushTop || other.brushTop < lot.bottom) continue;
					if (insidePolygon(other.edges, Vec3{out2.x, out2.y, 0}, 0.5)) {
						++shared;
						++sharedWith[other.index];
						break;
					}
				}
				const Vec3 out8 = at + e.n * 8;
				const double h = std::min(64.0, (lot.brushTop - lot.bottom) / 2);
				for (const auto& n : nearby) {
					if (containsPoint(*n.solid, Vec3{out8.x, out8.y, lot.bottom + std::max(8.0, h)}, 0.5)) {
						++blocked;
						break;
					}
				}
			}
			lot.shared[i] = shared * 2 > SAMPLES;
			for (const auto& [other, n] : sharedWith) {
				if (lot.neighbour[i] < 0 || n > sharedWith[lot.neighbour[i]]) lot.neighbour[i] = other;
			}
			if (!lot.shared[i]) lot.neighbour[i] = -1;
			lot.blocked[i] = !lot.shared[i] && blocked * 5 >= SAMPLES * 3;
		}
		// A side set to no facade by hand is never the front.
		const auto setAway = [&](std::size_t i) {
			const auto it = o.sideSettings.find({lot.index, static_cast<int>(i)});
			return it != o.sideSettings.end() && it->second.state != SideState::Auto && it->second.state != SideState::Facade;
		};
		int front = -1;
		for (int pass = 0; pass < 3 && front < 0; ++pass) {
			for (std::size_t i = 0; i < count; ++i) {
				if (lot.shared[i] || (pass == 0 && lot.blocked[i]) || (pass < 2 && setAway(i))) continue;
				if (front < 0 || lot.edges[i].length > lot.edges[static_cast<std::size_t>(front)].length + 0.5) front = static_cast<int>(i);
			}
		}
		lot.front = std::max(0, front);
		lot.states.assign(count, SideState::Nodraw);
		for (std::size_t i = 0; i < count; ++i) {
			const int prev = static_cast<int>((i + count - 1) % count), next = static_cast<int>((i + 1) % count);
			const bool besideFront = prev == lot.front || next == lot.front;
			const bool facadeMode = o.mode == BuildingMode::Facade;
			const SideState away = facadeMode ? SideState::Open : SideState::Nodraw;
			const SideState rest = facadeMode ? SideState::Open : SideState::Plain;
			SideState state = SideState::Facade;
			if (lot.shared[i]) {
				state = away;
			} else {
				switch (o.sides) {
					case BuildingSides::AllOuter: state = SideState::Facade; break;
					case BuildingSides::Street: state = lot.blocked[i] ? away : SideState::Facade; break;
					case BuildingSides::Front: state = static_cast<int>(i) == lot.front ? SideState::Facade : (lot.blocked[i] ? away : rest); break;
					case BuildingSides::FrontAndSides:
						state = static_cast<int>(i) == lot.front || besideFront ? SideState::Facade : (lot.blocked[i] ? away : rest);
						break;
				}
			}
			const auto it = o.sideSettings.find({lot.index, static_cast<int>(i)});
			if (it != o.sideSettings.end() && it->second.state != SideState::Auto) state = it->second.state;
			if (state == SideState::Open && !facadeMode) {
				state = SideState::Nodraw;
				openInBlock = true;
			}
			lot.states[i] = state;
		}
	}
	if (openInBlock) plan.notes.push_back("Open sides of a block are nodraw (only a facade-only building can be open).");
	// Touching lots: groups (one building when touchingIsOne: one material, pattern, the doors on
	// the group's longest front only).
	{
		std::vector<int> parent(lots.size());
		for (std::size_t i = 0; i < lots.size(); ++i) parent[i] = static_cast<int>(i);
		const std::function<int(int)> find = [&](int x) { return parent[static_cast<std::size_t>(x)] == x ? x : parent[static_cast<std::size_t>(x)] = find(parent[static_cast<std::size_t>(x)]); };
		for (const auto& lot : lots) {
			for (const int other : lot.neighbour) {
				if (other >= 0) parent[static_cast<std::size_t>(find(lot.index))] = find(other);
			}
		}
		std::map<int, int> best;   // group -> lot with the longest front
		for (auto& lot : lots) {
			const int root = find(lot.index);
			lot.group = lot.index;
			if (!o.touchingIsOne) continue;
			for (const auto& other : lots) {
				if (find(other.index) == root) lot.group = std::min(lot.group, other.index);
			}
			const auto it = best.find(root);
			const auto frontLength = [&](const Lot& l) { return l.states[static_cast<std::size_t>(l.front)] == SideState::Facade ? l.edges[static_cast<std::size_t>(l.front)].length : 0.0; };
			if (it == best.end() || frontLength(lot) > frontLength(lots[static_cast<std::size_t>(it->second)]) + 0.5) best[root] = lot.index;
		}
		if (o.touchingIsOne) {
			for (auto& lot : lots) lot.doorLot = best[find(lot.index)] == lot.index;
		}
	}
	// The facade texture runs on from one lot's side to the next one's (a curve, a row): each side
	// starts where the side before it ends.
	{
		struct SideKey {
			int lot, side;
		};
		std::vector<SideKey> walls;
		for (auto& lot : lots) {
			lot.texOffset.assign(lot.edges.size(), 0);
			for (std::size_t i = 0; i < lot.edges.size(); ++i) {
				if (lot.states[i] == SideState::Facade || lot.states[i] == SideState::Plain) walls.push_back({lot.index, static_cast<int>(i)});
			}
		}
		const auto edgeOf = [&](const SideKey& k) -> const Edge& { return lots[static_cast<std::size_t>(k.lot)].edges[static_cast<std::size_t>(k.side)]; };
		const auto before = [&](const SideKey& k) -> std::optional<SideKey> {
			const Edge& e = edgeOf(k);
			for (const auto& w : walls) {
				if (w.lot == k.lot) continue;
				const Edge& f = edgeOf(w);
				if (length(Vec3{f.b.x - e.a.x, f.b.y - e.a.y, 0}) < 0.5 && dot(f.e, e.e) > 0.7) return w;
			}
			return std::nullopt;
		};
		for (const auto& w : walls) {
			// From the start of its chain.
			std::vector<SideKey> chain{w};
			std::set<std::pair<int, int>> seen{{w.lot, w.side}};
			while (auto prev = before(chain.back())) {
				if (!seen.insert({prev->lot, prev->side}).second) break;
				chain.push_back(*prev);
			}
			double offset = 0;
			for (std::size_t k = chain.size(); k-- > 1;) offset += edgeOf(chain[k]).length;
			lots[static_cast<std::size_t>(w.lot)].texOffset[static_cast<std::size_t>(w.side)] = offset;
		}
	}
	// Heights and floors, variation, material and pattern per lot.
	std::vector<std::string> facades;
	if (!o.facadeMaterial.empty()) facades.push_back(o.facadeMaterial);
	for (const auto& m : o.facadeVariants) {
		if (!m.empty()) facades.push_back(m);
	}
	std::vector<std::string> patterns;
	{
		std::string current;
		for (const char ch : o.pattern + ";") {
			if (ch == ';') {
				if (!current.empty()) patterns.push_back(current);
				current.clear();
			} else if (ch != ' ') {
				current += ch;
			}
		}
		if (patterns.empty()) patterns.push_back("x");
	}
	const double g = std::max(32.0, o.groundFloorHeight), f = std::max(32.0, o.floorHeight);
	for (auto& lot : lots) {
		const auto settingsIt = o.lotSettings.find(lot.index);
		const LotSettings* own = settingsIt != o.lotSettings.end() ? &settingsIt->second : nullptr;
		double offset = 0;
		if (o.groundLevel == GroundLevel::Raised) offset = std::max(0.0, o.groundRaise);
		if (o.groundLevel == GroundLevel::HalfSunk) offset = -std::max(0.0, o.groundRaise);
		lot.groundBase = lot.bottom + offset;
		double height = lot.brushTop - lot.bottom;
		if (o.heightMode == HeightMode::Height) height = o.height;
		if (own && own->height > 0) height = own->height;
		int floors = 0;
		bool fromFloors = o.heightMode == HeightMode::Floors && !(own && own->height > 0);
		if (fromFloors) floors = std::max(1, o.floors);
		if (own && own->floors > 0) {
			floors = own->floors;
			fromFloors = true;
		}
		if (!fromFloors) {
			const double available = lot.bottom + height - lot.groundBase - g - std::max(0.0, o.topWall);
			floors = 1 + std::max(0, static_cast<int>(std::floor(available / f + 1e-9)));
		}
		if (o.floorVariation > 0) {
			const int dn = static_cast<int>(std::lround((rand01(o.seed, lot.group, 21) * 2 - 1) * o.floorVariation));
			if (dn != 0) {
				floors = std::max(1, floors + dn);
				fromFloors = true;
				lot.varied = true;
			}
		}
		lot.floors = floors;
		lot.top = fromFloors ? lot.groundBase + g + (floors - 1) * f + std::max(0.0, o.topWall) : lot.bottom + height;
		lot.top = std::max(lot.top, lot.groundBase + 16);
		if (!facades.empty()) lot.facade = facades[static_cast<std::size_t>(rand01(o.seed, lot.group, 31) * static_cast<double>(facades.size())) % facades.size()];
		if (own && !own->facadeMaterial.empty()) lot.facade = own->facadeMaterial;
		if (lot.facade.empty()) lot.facade = "BRICK/BRICKWALL001A";
		lot.pattern = patterns[static_cast<std::size_t>(rand01(o.seed, lot.group, 41) * static_cast<double>(patterns.size())) % patterns.size()];
		if (o.foundation) {
			const auto low = terrainUnder(doc, lot, lotSolids[static_cast<std::size_t>(lot.index)]);
			if (low && *low < lot.bottom + 0.5) {
				lot.terrain = true;
				lot.ground = *low;
			}
		}
	}
	// Build each lot.
	for (auto& lot : lots) {
		if (o.cancelled && o.cancelled()) return {};
		Materials m;
		m.facade = lot.facade;
		m.ground = o.groundMaterial.empty() ? m.facade : o.groundMaterial;
		m.trim = o.trimMaterial.empty() ? m.facade : o.trimMaterial;
		m.window = o.windowMaterial.empty() ? m.trim : o.windowMaterial;
		m.broken = o.brokenWindowMaterial.empty() ? m.window : o.brokenWindowMaterial;
		m.door = o.doorMaterial.empty() ? m.window : o.doorMaterial;
		m.cornice = o.corniceMaterial.empty() ? m.trim : o.corniceMaterial;
		m.roof = o.roofMaterial.empty() ? m.trim : o.roofMaterial;
		m.interior = o.interiorMaterial.empty() ? m.trim : o.interiorMaterial;
		Builder b{o, ids, textureSize, m, plan};
		LotBuilder lb(doc, o, b, lot, lots, lotRefs);
		lb.building.lot = lot.ref;
		lb.build();
		if (o.cancelled && o.cancelled()) return {};
		auto& building = lb.building;
		building.floors = lot.floors;
		building.bottom = lot.bottom;
		building.top = lot.top;
		building.foundation = lb.foundation;
		building.facadeMaterial = lot.facade;
		building.pattern = lot.pattern;
		building.outline = lot.poly;
		building.lotTop = lot.brushTop;
		building.settings = resolvedSettings(o, lot);
		// Cut round the brushes of the map.
		for (auto* list : {&building.world, &building.detail}) {
			std::vector<Solid> kept;
			for (const auto& piece : *list) {
				auto parts = clipToMap(doc, ids, piece, lotRefs, plan.clipped);
				kept.insert(kept.end(), parts.begin(), parts.end());
			}
			*list = std::move(kept);
		}
		plan.skipped += b.skipped;
		for (std::size_t i = 0; i < lot.edges.size(); ++i) {
			BuildingSideInfo info;
			info.lot = lot.index;
			info.side = static_cast<int>(i);
			info.a = lot.edges[i].a;
			info.b = lot.edges[i].b;
			info.normal = lot.edges[i].n;
			info.length = lot.edges[i].length;
			info.shared = lot.shared[i];
			info.blocked = lot.blocked[i];
			info.front = static_cast<int>(i) == lot.front;
			info.state = lot.states[i];
			const auto& list = lb.openings[i];
			const int floorShown = lot.floors > 1 ? 1 : 0;
			info.windowsPerFloor = static_cast<int>(std::count_if(list.begin(), list.end(), [&](const Opening& op) {
				return op.floor == floorShown && (op.kind == OpeningKind::Window || op.kind == OpeningKind::Shop);
			}));
			info.doors = static_cast<int>(std::count_if(list.begin(), list.end(), [](const Opening& op) { return op.kind == OpeningKind::Door; }));
			plan.sides.push_back(info);
		}
		plan.buildings.push_back(std::move(building));
	}
	if (plan.clipped > 0) plan.notes.push_back(std::to_string(plan.clipped) + " piece(s) cut round brushes of the map they reached into.");
	if (plan.skipped > 0) plan.notes.push_back(std::to_string(plan.skipped) + " piece(s) left out (they would have been too thin).");
	const int brushes = plan.brushCount();
	if (brushes > 1500) {
		plan.notes.push_back(std::to_string(brushes) +
		                     " brushes - a lot for one map (8192 at most). Fewer windows, Windows: Panels, or models for details keep it lower.");
	}
	const auto total = static_cast<long long>(doc.solidCount()) + brushes;
	if (total > 8192 * 85 / 100) plan.notes.push_back("The map would have " + std::to_string(total) + " brushes - near the engine's limit of 8192.");
	if (o.groundLevel == GroundLevel::Raised && plan.steps > 0) {
		const auto [n, h] = stepsFor(o.groundRaise);
		plan.notes.push_back(std::to_string(n) + " step(s) of " + formatNumber(std::round(h * 10) / 10) + " units to each door (players walk up to 18).");
	}
	return plan;
}

std::string describeBuildingOptions(const BuildingOptions& o) {
	std::ostringstream out;
	const auto num = [](double v) { return formatNumber(std::round(v * 1000) / 1000); };
	out << "mode = " << static_cast<int>(o.mode) << "\nfacadeThickness = " << num(o.facadeThickness) << "\nreturnDepth = " << num(o.returnDepth)
	    << "\nsides = " << static_cast<int>(o.sides) << "\n";
	for (const auto& [key, s] : o.sideSettings) {
		out << "side " << key.first << "." << key.second << " = " << static_cast<int>(s.state) << " " << s.windows << " " << s.pattern << " " << s.doors << "\n";
	}
	for (const auto& [lot, s] : o.lotSettings) out << "lot " << lot << " = " << num(s.height) << " " << s.floors << " " << s.facadeMaterial << "\n";
	out << "heightMode = " << static_cast<int>(o.heightMode) << "\nheight = " << num(o.height) << "\nfloors = " << o.floors << "\nfloorHeight = " << num(o.floorHeight)
	    << "\ngroundFloorHeight = " << num(o.groundFloorHeight) << "\nfloorVariation = " << o.floorVariation << "\ntopWall = " << num(o.topWall)
	    << "\ngroundLevel = " << static_cast<int>(o.groundLevel) << "\ngroundRaise = " << num(o.groundRaise) << "\nbasementWindows = " << o.basementWindows
	    << "\ngroundWindows = " << static_cast<int>(o.groundWindows) << "\nshop = " << num(o.shopWidth) << " " << num(o.shopHeight) << " " << num(o.shopSill)
	    << "\ndoors = " << static_cast<int>(o.doorSides) << " " << o.doorsPerSide << " " << static_cast<int>(o.doorPlace) << " " << num(o.doorWidth) << " "
	    << num(o.doorHeight) << " " << num(o.stepDepth) << "\nwindows = " << static_cast<int>(o.windowStyle) << " " << static_cast<int>(o.layout) << " "
	    << o.windowsPerSide << " " << num(o.windowWidth) << " " << num(o.windowHeight) << " " << num(o.sillHeight) << " " << num(o.windowGap) << " "
	    << num(o.cornerMargin) << " " << num(o.windowDepth) << "\npattern = " << o.pattern << "\nsills = " << o.sills << " " << num(o.sillDepth) << " "
	    << num(o.sillThickness) << "\nbands = " << o.cornices << " " << num(o.corniceHeight) << " " << num(o.corniceDepth) << " " << o.topCornice << " "
	    << num(o.topCorniceHeight) << " " << num(o.topCorniceDepth) << " " << o.plinth << " " << num(o.plinthHeight) << " " << num(o.plinthDepth)
	    << "\nroof = " << static_cast<int>(o.roof) << " " << num(o.parapetHeight) << " " << num(o.parapetThickness) << " " << num(o.roofAngle) << " "
	    << num(o.roofOverhang) << " " << num(o.eaveThickness) << "\ndisplacements = " << o.foundation << " " << o.wallDisplacement << " " << o.roofDisplacement << " "
	    << o.displacementPower << " " << num(o.displacementStrength) << "\ndamage = " << num(o.damage) << " " << o.damageSeed << "\nlotModel = " << o.lotModel
	    << "\nmaterials = " << o.facadeMaterial << " | " << o.groundMaterial << " | " << o.windowMaterial << " | " << o.brokenWindowMaterial << " | " << o.doorMaterial
	    << " | " << o.trimMaterial << " | " << o.corniceMaterial << " | " << o.roofMaterial << " | " << o.interiorMaterial << "\nvariants =";
	for (const auto& v : o.facadeVariants) out << " " << v;
	out << "\n";
	for (std::size_t i = 0; i < o.textures.size(); ++i) {
		const auto& t = o.textures[i];
		out << "texture " << i << " = " << static_cast<int>(t.fit) << " " << num(t.scale) << " " << num(t.shiftU) << " " << num(t.shiftV) << " " << num(t.rotation)
		    << "\n";
	}
	for (const auto& p : o.props) {
		out << "prop = " << p.model << " " << static_cast<int>(p.anchor) << " " << p.every << " " << num(p.chance) << " " << static_cast<int>(p.when) << " "
		    << p.groundFloor << p.upperFloors << " " << num(p.wallOffset) << " " << num(p.up) << " " << num(p.yaw) << " " << num(p.spacing) << "\n";
	}
	out << "seed = " << o.seed << "\n";
	return out.str();
}

std::string describeBuildingPlan(const BuildingPlan& plan) {
	std::ostringstream out;
	out << "buildings: " << plan.buildings.size() << ", brushes " << plan.brushCount() << ", windows " << plan.windows << ", doors " << plan.doors << ", steps "
	    << plan.steps << ", props " << plan.props.size() << ", displacements " << plan.displacements << ", cut " << plan.clipped << ", left out " << plan.skipped
	    << "\n";
	for (const auto& b : plan.buildings) {
		out << "  lot " << b.lot.entityId << "/" << b.lot.solidId << ": floors " << b.floors << ", bottom " << formatNumber(b.bottom) << ", top "
		    << formatNumber(b.top) << ", foundation " << formatNumber(b.foundation) << ", world " << b.world.size() << ", detail " << b.detail.size() << ", facade "
		    << b.facadeMaterial << ", pattern " << b.pattern << "\n";
	}
	static const char* stateNames[] = {"auto", "facade", "plain", "open", "nodraw"};
	for (const auto& s : plan.sides) {
		out << "  side " << s.lot << "." << s.side << ": " << stateNames[static_cast<int>(s.state)] << ", length " << formatNumber(std::round(s.length))
		    << (s.shared ? ", shared" : "") << (s.blocked ? ", blocked" : "") << (s.front ? ", front" : "") << ", windows/floor " << s.windowsPerFloor << ", doors "
		    << s.doors << "\n";
	}
	for (const auto& note : plan.notes) out << "  note: " << note << "\n";
	return out.str();
}


// --- Kept with the building: settings, outline ------------------------------------------------

namespace {

/// Percent-escapes what separates keys and values (and what a VMF value cannot hold).
std::string escape(std::string_view text) {
	std::string out;
	for (const char ch : text) {
		if (ch == '%' || ch == '&' || ch == '=' || ch == '"' || ch == ';' || ch == ',' || ch == '|' || ch == ':' || static_cast<unsigned char>(ch) < 32) {
			static const char* hex = "0123456789ABCDEF";
			out += '%';
			out += hex[(static_cast<unsigned char>(ch) >> 4) & 15];
			out += hex[static_cast<unsigned char>(ch) & 15];
		} else {
			out += ch;
		}
	}
	return out;
}

std::string unescape(std::string_view text) {
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '%' && i + 2 < text.size()) {
			const auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0)); };
			out += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
			i += 2;
		} else {
			out += text[i];
		}
	}
	return out;
}

std::vector<std::string> splitOn(std::string_view text, char separator) {
	std::vector<std::string> out;
	std::string current;
	for (const char ch : text) {
		if (ch == separator) {
			out.push_back(current);
			current.clear();
		} else {
			current += ch;
		}
	}
	if (!current.empty() || !out.empty()) out.push_back(current);
	return out;
}

std::string num(double v) {
	return formatNumber(std::round(v * 10000) / 10000);
}

double toDouble(const std::string& text, double fallback) {
	try {
		return text.empty() ? fallback : std::stod(text);
	} catch (...) {
		return fallback;
	}
}

int toInt(const std::string& text, int fallback) {
	try {
		return text.empty() ? fallback : std::stoi(text);
	} catch (...) {
		return fallback;
	}
}

/// One setting: its key (the dialog's field name) and how it is read and written.
struct Field {
	std::string key;
	std::function<std::string(const BuildingOptions&)> get;
	std::function<void(BuildingOptions&, const std::string&)> set;
};

template<typename T>
Field member(const char* key, T BuildingOptions::*m) {
	Field f;
	f.key = key;
	if constexpr (std::is_same_v<T, double>) {
		f.get = [m](const BuildingOptions& o) { return num(o.*m); };
		f.set = [m](BuildingOptions& o, const std::string& v) { o.*m = toDouble(v, o.*m); };
	} else if constexpr (std::is_same_v<T, bool>) {
		f.get = [m](const BuildingOptions& o) { return std::string(o.*m ? "1" : "0"); };
		f.set = [m](BuildingOptions& o, const std::string& v) { o.*m = v == "1"; };
	} else if constexpr (std::is_same_v<T, std::string>) {
		f.get = [m](const BuildingOptions& o) { return o.*m; };
		f.set = [m](BuildingOptions& o, const std::string& v) { o.*m = v; };
	} else if constexpr (std::is_enum_v<T>) {
		f.get = [m](const BuildingOptions& o) { return std::to_string(static_cast<int>(o.*m)); };
		f.set = [m](BuildingOptions& o, const std::string& v) { o.*m = static_cast<T>(toInt(v, static_cast<int>(o.*m))); };
	} else {
		f.get = [m](const BuildingOptions& o) { return std::to_string(o.*m); };
		f.set = [m](BuildingOptions& o, const std::string& v) { o.*m = static_cast<T>(toInt(v, static_cast<int>(o.*m))); };
	}
	return f;
}

const std::vector<Field>& fields() {
	static const std::vector<Field> list = [] {
		std::vector<Field> f{
		    member("buildingMode", &BuildingOptions::mode),
		    member("buildingFacadeThickness", &BuildingOptions::facadeThickness),
		    member("buildingReturn", &BuildingOptions::returnDepth),
		    member("buildingSidesPreset", &BuildingOptions::sides),
		    member("buildingHeightMode", &BuildingOptions::heightMode),
		    member("buildingHeight", &BuildingOptions::height),
		    member("buildingFloors", &BuildingOptions::floors),
		    member("buildingFloorHeight", &BuildingOptions::floorHeight),
		    member("buildingGroundHeight", &BuildingOptions::groundFloorHeight),
		    member("buildingFloorVariation", &BuildingOptions::floorVariation),
		    member("buildingTopWall", &BuildingOptions::topWall),
		    member("buildingGroundLevel", &BuildingOptions::groundLevel),
		    member("buildingRaise", &BuildingOptions::groundRaise),
		    member("buildingBasement", &BuildingOptions::basementWindows),
		    member("buildingGroundWindows", &BuildingOptions::groundWindows),
		    member("buildingShopWidth", &BuildingOptions::shopWidth),
		    member("buildingShopHeight", &BuildingOptions::shopHeight),
		    member("shopSill", &BuildingOptions::shopSill),
		    member("buildingDoorSides", &BuildingOptions::doorSides),
		    member("buildingDoorCount", &BuildingOptions::doorsPerSide),
		    member("buildingDoorPlace", &BuildingOptions::doorPlace),
		    member("buildingDoorWidth", &BuildingOptions::doorWidth),
		    member("buildingDoorHeight", &BuildingOptions::doorHeight),
		    member("buildingStepDepth", &BuildingOptions::stepDepth),
		    member("buildingWindowStyle", &BuildingOptions::windowStyle),
		    member("buildingLayout", &BuildingOptions::layout),
		    member("buildingWindowCount", &BuildingOptions::windowsPerSide),
		    member("buildingWindowWidth", &BuildingOptions::windowWidth),
		    member("buildingWindowHeight", &BuildingOptions::windowHeight),
		    member("buildingSillHeight", &BuildingOptions::sillHeight),
		    member("buildingWindowGap", &BuildingOptions::windowGap),
		    member("buildingCornerMargin", &BuildingOptions::cornerMargin),
		    member("buildingWindowDepth", &BuildingOptions::windowDepth),
		    member("buildingPattern", &BuildingOptions::pattern),
		    member("buildingSills", &BuildingOptions::sills),
		    member("buildingBacking", &BuildingOptions::windowBacking),
		    member("sillDepth", &BuildingOptions::sillDepth),
		    member("sillThickness", &BuildingOptions::sillThickness),
		    member("buildingCornices", &BuildingOptions::cornices),
		    member("buildingCorniceHeight", &BuildingOptions::corniceHeight),
		    member("buildingCorniceDepth", &BuildingOptions::corniceDepth),
		    member("buildingTopCornice", &BuildingOptions::topCornice),
		    member("topCorniceHeight", &BuildingOptions::topCorniceHeight),
		    member("topCorniceDepth", &BuildingOptions::topCorniceDepth),
		    member("buildingPlinth", &BuildingOptions::plinth),
		    member("buildingPlinthHeight", &BuildingOptions::plinthHeight),
		    member("plinthDepth", &BuildingOptions::plinthDepth),
		    member("buildingRoof", &BuildingOptions::roof),
		    member("buildingParapet", &BuildingOptions::parapetHeight),
		    member("parapetThickness", &BuildingOptions::parapetThickness),
		    member("buildingRoofAngle", &BuildingOptions::roofAngle),
		    member("buildingOverhang", &BuildingOptions::roofOverhang),
		    member("eaveThickness", &BuildingOptions::eaveThickness),
		    member("buildingFoundation", &BuildingOptions::foundation),
		    member("buildingWallDisp", &BuildingOptions::wallDisplacement),
		    member("buildingRoofDisp", &BuildingOptions::roofDisplacement),
		    member("buildingDispPower", &BuildingOptions::displacementPower),
		    member("buildingDispStrength", &BuildingOptions::displacementStrength),
		    member("buildingDamage", &BuildingOptions::damage),
		    member("buildingDamageSeed", &BuildingOptions::damageSeed),
		    member("buildingLotModel", &BuildingOptions::lotModel),
		    member("buildingFacadeMaterial", &BuildingOptions::facadeMaterial),
		    member("buildingGroundMaterial", &BuildingOptions::groundMaterial),
		    member("buildingWindowMaterial", &BuildingOptions::windowMaterial),
		    member("buildingBrokenMaterial", &BuildingOptions::brokenWindowMaterial),
		    member("buildingDoorMaterial", &BuildingOptions::doorMaterial),
		    member("buildingTrimMaterial", &BuildingOptions::trimMaterial),
		    member("buildingCorniceMaterial", &BuildingOptions::corniceMaterial),
		    member("buildingRoofMaterial", &BuildingOptions::roofMaterial),
		    member("buildingInteriorMaterial", &BuildingOptions::interiorMaterial),
		    member("buildingTouching", &BuildingOptions::touchingIsOne),
		    member("buildingSeed", &BuildingOptions::seed),
		};
		f.push_back({"buildingVariants",
		             [](const BuildingOptions& o) {
			             std::string out;
			             for (const auto& v : o.facadeVariants) out += (out.empty() ? "" : ";") + escape(v);
			             return out;
		             },
		             [](BuildingOptions& o, const std::string& v) {
			             o.facadeVariants.clear();
			             for (const auto& part : splitOn(v, ';')) {
				             if (!part.empty()) o.facadeVariants.push_back(unescape(part));
			             }
		             }});
		for (std::size_t i = 0; i < static_cast<std::size_t>(BuildingPart::Count); ++i) {
			const std::string base = "buildingTexture" + std::to_string(i);
			f.push_back({base + "Fit", [i](const BuildingOptions& o) { return std::to_string(static_cast<int>(o.textures[i].fit)); },
			             [i](BuildingOptions& o, const std::string& v) { o.textures[i].fit = static_cast<TextureFit>(toInt(v, 0)); }});
			f.push_back({base + "Scale", [i](const BuildingOptions& o) { return num(o.textures[i].scale); },
			             [i](BuildingOptions& o, const std::string& v) { o.textures[i].scale = toDouble(v, 0.25); }});
			f.push_back({base + "ShiftU", [i](const BuildingOptions& o) { return num(o.textures[i].shiftU); },
			             [i](BuildingOptions& o, const std::string& v) { o.textures[i].shiftU = toDouble(v, 0); }});
			f.push_back({base + "ShiftV", [i](const BuildingOptions& o) { return num(o.textures[i].shiftV); },
			             [i](BuildingOptions& o, const std::string& v) { o.textures[i].shiftV = toDouble(v, 0); }});
			f.push_back({base + "Rotation", [i](const BuildingOptions& o) { return num(o.textures[i].rotation); },
			             [i](BuildingOptions& o, const std::string& v) { o.textures[i].rotation = toDouble(v, 0); }});
		}
		f.push_back({"buildingSides",
		             [](const BuildingOptions& o) {
			             std::string out;
			             for (const auto& [key, s] : o.sideSettings) {
				             out += (out.empty() ? "" : ";") + std::to_string(key.first) + "." + std::to_string(key.second) + ":" +
				                    std::to_string(static_cast<int>(s.state)) + "," + std::to_string(s.windows) + "," + escape(s.pattern) + "," + std::to_string(s.doors);
			             }
			             return out;
		             },
		             [](BuildingOptions& o, const std::string& v) {
			             o.sideSettings.clear();
			             for (const auto& entry : splitOn(v, ';')) {
				             const auto kv = splitOn(entry, ':');
				             if (kv.size() != 2) continue;
				             const auto key = splitOn(kv[0], '.');
				             const auto values = splitOn(kv[1], ',');
				             if (key.size() != 2 || values.size() != 4) continue;
				             SideSettings s;
				             s.state = static_cast<SideState>(std::clamp(toInt(values[0], 0), 0, 4));
				             s.windows = toInt(values[1], -1);
				             s.pattern = unescape(values[2]);
				             s.doors = toInt(values[3], -1);
				             o.sideSettings[{toInt(key[0], 0), toInt(key[1], 0)}] = s;
			             }
		             }});
		f.push_back({"buildingLots",
		             [](const BuildingOptions& o) {
			             std::string out;
			             for (const auto& [lot, s] : o.lotSettings) {
				             out += (out.empty() ? "" : ";") + std::to_string(lot) + ":" + num(s.height) + "," + std::to_string(s.floors) + "," + escape(s.facadeMaterial);
			             }
			             return out;
		             },
		             [](BuildingOptions& o, const std::string& v) {
			             o.lotSettings.clear();
			             for (const auto& entry : splitOn(v, ';')) {
				             const auto kv = splitOn(entry, ':');
				             if (kv.size() != 2) continue;
				             const auto values = splitOn(kv[1], ',');
				             if (values.size() != 3) continue;
				             LotSettings s;
				             s.height = toDouble(values[0], 0);
				             s.floors = toInt(values[1], 0);
				             s.facadeMaterial = unescape(values[2]);
				             o.lotSettings[toInt(kv[0], 0)] = s;
			             }
		             }});
		f.push_back({"buildingProps",
		             [](const BuildingOptions& o) {
			             std::string out;
			             for (const auto& p : o.props) {
				             out += (out.empty() ? "" : ";") + escape(p.model) + "," + std::to_string(static_cast<int>(p.anchor)) + "," + std::to_string(p.every) + "," +
				                    num(p.chance) + "," + std::to_string(static_cast<int>(p.when)) + "," + (p.groundFloor ? "1" : "0") + "," + (p.upperFloors ? "1" : "0") +
				                    "," + num(p.wallOffset) + "," + num(p.up) + "," + num(p.yaw) + "," + num(p.spacing);
			             }
			             return out;
		             },
		             [](BuildingOptions& o, const std::string& v) {
			             o.props.clear();
			             for (const auto& entry : splitOn(v, ';')) {
				             const auto values = splitOn(entry, ',');
				             if (values.size() != 11) continue;
				             FacadeProp p;
				             p.model = unescape(values[0]);
				             p.anchor = static_cast<FacadePropAnchor>(std::clamp(toInt(values[1], 0), 0, static_cast<int>(FacadePropAnchor::Roof)));
				             p.every = toInt(values[2], 1);
				             p.chance = toDouble(values[3], 100);
				             p.when = static_cast<PropCondition>(std::clamp(toInt(values[4], 0), 0, 2));
				             p.groundFloor = values[5] == "1";
				             p.upperFloors = values[6] == "1";
				             p.wallOffset = toDouble(values[7], 0);
				             p.up = toDouble(values[8], 0);
				             p.yaw = toDouble(values[9], 0);
				             p.spacing = toDouble(values[10], 128);
				             o.props.push_back(p);
			             }
		             }});
		return f;
	}();
	return list;
}


std::string encodeOutline(const std::vector<Vec3>& outline, double bottom, double top) {
	std::string out;
	for (const auto& p : outline) out += (out.empty() ? "" : " ") + num(p.x) + " " + num(p.y);
	return out + "|" + num(bottom) + "|" + num(top);
}

bool decodeOutline(const std::string& text, std::vector<Vec3>& outline, double& bottom, double& top) {
	const auto parts = splitOn(text, '|');
	if (parts.size() != 3) return false;
	std::istringstream in(parts[0]);
	double x = 0, y = 0;
	outline.clear();
	while (in >> x >> y) outline.push_back({x, y, 0});
	bottom = toDouble(parts[1], 0);
	top = toDouble(parts[2], 0);
	return outline.size() >= 3 && top > bottom;
}

const char* const KEY_ID = "sme_building";
const char* const KEY_SETTINGS = "sme_building_settings";
const char* const KEY_LOT = "sme_building_lot";

KvNode* editorOf(std::vector<KvNode>& extra) {
	for (auto& node : extra) {
		if (node.block && iequals(node.key, "editor")) return &node;
	}
	return nullptr;
}
const KvNode* editorOf(const std::vector<KvNode>& extra) {
	for (const auto& node : extra) {
		if (node.block && iequals(node.key, "editor")) return &node;
	}
	return nullptr;
}

int solidBuildingId(const Solid& solid) {
	const KvNode* editor = editorOf(solid.extra);
	return editor ? toInt(editor->get(KEY_ID), 0) : 0;
}

} // namespace

std::string encodeBuildingOptions(const BuildingOptions& options) {
	std::string out;
	for (const auto& f : fields()) out += (out.empty() ? "" : "&") + f.key + "=" + escape(f.get(options));
	return out;
}

BuildingOptions decodeBuildingOptions(std::string_view text) {
	BuildingOptions o = defaultBuildingOptions();
	std::map<std::string, std::string> values;
	for (const auto& pair : splitOn(text, '&')) {
		const auto at = pair.find('=');
		if (at == std::string::npos) continue;
		values[pair.substr(0, at)] = unescape(pair.substr(at + 1));
	}
	for (const auto& f : fields()) {
		if (const auto it = values.find(f.key); it != values.end()) f.set(o, it->second);
	}
	return o;
}

BuildingOptions mergeBuildingOptions(const BuildingOptions& base, const BuildingOptions& edited, const std::set<std::string>& changed) {
	BuildingOptions out = base;
	for (const auto& f : fields()) {
		if (changed.contains(f.key)) f.set(out, f.get(edited));
	}
	return out;
}

std::vector<BuildingRecord> buildingsInMap(const MapDocument& doc) {
	std::vector<BuildingRecord> out;
	for (const auto& entity : doc.entities) {
		const std::string settings = entity.get(KEY_SETTINGS);
		if (settings.empty()) continue;
		BuildingRecord record;
		record.id = toInt(entity.get(KEY_ID), 0);
		if (record.id == 0 || !decodeOutline(entity.get(KEY_LOT), record.outline, record.bottom, record.top)) continue;
		record.options = decodeBuildingOptions(settings);
		out.push_back(std::move(record));
	}
	if (out.empty()) return out;
	std::map<int, std::size_t> byId;
	for (std::size_t i = 0; i < out.size(); ++i) byId[out[i].id] = i;
	for (const auto& solid : doc.world.solids) {
		if (const auto it = byId.find(solidBuildingId(solid)); it != byId.end()) out[it->second].members.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		if (const auto it = byId.find(toInt(entity.get(KEY_ID), 0)); it != byId.end()) out[it->second].members.entities.push_back(entity.id);
	}
	return out;
}

std::set<int> buildingIdsOf(const MapDocument& doc, const Selection& selection) {
	std::set<int> ids;
	const auto ofSolid = [&](const SolidRef& ref) {
		if (ref.entityId != 0) {
			if (const Entity* e = doc.findEntity(ref.entityId)) {
				if (const int id = toInt(e->get(KEY_ID), 0)) ids.insert(id);
			}
			return;
		}
		if (const Solid* s = doc.findSolid(ref)) {
			if (const int id = solidBuildingId(*s)) ids.insert(id);
		}
	};
	for (const auto& ref : selection.solids) ofSolid(ref);
	for (const auto& face : selection.faces) ofSolid(face.solid);
	for (const int id : selection.entities) {
		if (const Entity* e = doc.findEntity(id)) {
			if (const int b = toInt(e->get(KEY_ID), 0)) ids.insert(b);
		}
	}
	return ids;
}

Selection buildingMembers(const MapDocument& doc, const std::set<int>& ids) {
	Selection out;
	if (ids.empty()) return out;
	for (const auto& solid : doc.world.solids) {
		if (const int id = solidBuildingId(solid); id && ids.contains(id)) out.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		if (const int id = toInt(entity.get(KEY_ID), 0); id && ids.contains(id)) out.entities.push_back(entity.id);
	}
	return out;
}

std::pair<double, double> outlineSize(const std::vector<Vec3>& outline) {
	if (outline.size() < 2) return {0, 0};
	Vec3 d{1, 0, 0};
	double longest = -1;
	for (std::size_t i = 0; i < outline.size(); ++i) {
		const Vec3 e = outline[(i + 1) % outline.size()] - outline[i];
		const double len = std::hypot(e.x, e.y);
		if (len > longest) {
			longest = len;
			d = Vec3{e.x, e.y, 0} / len;
		}
	}
	const Vec3 m{-d.y, d.x, 0};
	double a0 = 1e18, a1 = -1e18, b0 = 1e18, b1 = -1e18;
	for (const auto& p : outline) {
		a0 = std::min(a0, dot(p, d));
		a1 = std::max(a1, dot(p, d));
		b0 = std::min(b0, dot(p, m));
		b1 = std::max(b1, dot(p, m));
	}
	return {a1 - a0, b1 - b0};
}

std::vector<Vec3> resizeOutline(const std::vector<Vec3>& outline, double newLength, double newDepth) {
	if (outline.size() < 3) return outline;
	Vec3 d{1, 0, 0};
	double longest = -1;
	for (std::size_t i = 0; i < outline.size(); ++i) {
		const Vec3 e = outline[(i + 1) % outline.size()] - outline[i];
		const double len = std::hypot(e.x, e.y);
		if (len > longest) {
			longest = len;
			d = Vec3{e.x, e.y, 0} / len;
		}
	}
	const Vec3 m{-d.y, d.x, 0};
	double a0 = 1e18, a1 = -1e18, b0 = 1e18, b1 = -1e18;
	for (const auto& p : outline) {
		a0 = std::min(a0, dot(p, d));
		a1 = std::max(a1, dot(p, d));
		b0 = std::min(b0, dot(p, m));
		b1 = std::max(b1, dot(p, m));
	}
	const double sa = a1 - a0 > 1e-6 && newLength > 0 ? newLength / (a1 - a0) : 1;
	const double sb = b1 - b0 > 1e-6 && newDepth > 0 ? newDepth / (b1 - b0) : 1;
	const double ca = (a0 + a1) / 2, cb = (b0 + b1) / 2;
	std::vector<Vec3> out;
	for (const auto& p : outline) {
		const double a = ca + (dot(p, d) - ca) * sa, bb = cb + (dot(p, m) - cb) * sb;
		Vec3 q = d * a + m * bb;
		// Whole units stay whole units.
		q.x = std::abs(q.x - std::round(q.x)) < 1e-3 ? std::round(q.x) : q.x;
		q.y = std::abs(q.y - std::round(q.y)) < 1e-3 ? std::round(q.y) : q.y;
		out.push_back({q.x, q.y, 0});
	}
	return out;
}

BuildingPlan planRebuild(const MapDocument& doc, MapDocument& ids, const std::vector<BuildingRecord>& records, const TextureSizeFn& textureSize,
                         const std::function<std::optional<Bounds>(const std::string&)>& modelBounds, const std::function<bool()>& cancelled) {
	BuildingPlan out;
	// The map without the old pieces; the lots again where they were.
	MapDocument scratch = doc;
	std::set<int> idsToGo;
	for (const auto& r : records) idsToGo.insert(r.id);
	const Selection members = buildingMembers(doc, idsToGo);
	std::set<int> entitiesToGo(members.entities.begin(), members.entities.end());
	for (const auto& ref : members.solids) out.removeSolids.push_back(ref);
	for (const int id : members.entities) {
		out.removeEntities.push_back(id);
		if (const Entity* e = doc.findEntity(id)) {
			for (const auto& s : e->solids) out.removeSolids.push_back({id, s.id});
		}
	}
	std::erase_if(scratch.world.solids, [&](const Solid& s) { return solidBuildingId(s) && idsToGo.contains(solidBuildingId(s)); });
	std::erase_if(scratch.entities, [&](const Entity& e) { return entitiesToGo.contains(e.id); });
	std::vector<SolidRef> lots;
	for (const auto& r : records) {
		std::vector<Vec3> points;
		for (const auto& p : r.outline) {
			points.push_back({p.x, p.y, r.bottom});
			points.push_back({p.x, p.y, r.top});
		}
		auto lot = solidFromPoints(ids, points, r.options.facadeMaterial.empty() ? "BRICK/BRICKWALL001A" : r.options.facadeMaterial);
		if (!lot) {
			lots.push_back({0, 0});
			continue;
		}
		scratch.world.solids.push_back(*lot);
		lots.push_back({0, lot->id});
	}
	for (std::size_t i = 0; i < records.size(); ++i) {
		if (cancelled && cancelled()) return {};
		if (lots[i].solidId == 0) {
			out.notes.push_back("Building " + std::to_string(records[i].id) + ": its outline is broken - left as it is.");
			continue;
		}
		BuildingOptions o = records[i].options;
		o.modelBounds = modelBounds;
		o.cancelled = cancelled;
		auto p = planBuildings(scratch, ids, {lots[i]}, o, textureSize);
		const int base = static_cast<int>(out.buildings.size());
		for (auto& b : p.buildings) {
			b.lot = {-1, b.lot.solidId};   // not in the map: nothing to take out
			out.buildings.push_back(std::move(b));
		}
		for (auto& s : p.sides) {
			s.lot += base;
			out.sides.push_back(s);
		}
		for (auto& prop : p.props) {
			prop.building += base;
			out.props.push_back(std::move(prop));
		}
		for (auto& n : p.notes) out.notes.push_back(std::move(n));
		out.windows += p.windows;
		out.doors += p.doors;
		out.steps += p.steps;
		out.displacements += p.displacements;
		out.clipped += p.clipped;
		out.skipped += p.skipped;
	}
	return out;
}

namespace edit {

Change unlinkBuildings(MapDocument& doc, const std::set<int>& ids) {
	Transaction t(doc, "Unlink buildings");
	const Selection members = buildingMembers(doc, ids);
	for (const auto& ref : members.solids) {
		t.touchSolid(ref);
		if (Solid* s = doc.findSolid(ref)) {
			if (KvNode* editor = editorOf(s->extra)) std::erase_if(editor->children, [](const KvNode& n) { return !n.block && iequals(n.key, KEY_ID); });
		}
	}
	for (const int id : members.entities) {
		t.touchEntity(id);
		if (Entity* e = doc.findEntity(id)) {
			e->erase(KEY_ID);
			e->erase(KEY_SETTINGS);
			e->erase(KEY_LOT);
		}
	}
	return t.finish();
}

} // namespace edit

namespace edit {

std::pair<Change, Selection> applyBuildings(MapDocument& doc, const BuildingPlan& plan, const std::vector<std::pair<std::string, std::string>>& detailDefaults,
                                            const std::vector<std::pair<std::string, std::string>>& propDefaults) {
	Transaction t(doc, "Build buildings");
	Selection selection;
	if (!plan.ok() && plan.removeSolids.empty() && plan.removeEntities.empty()) return {t.finish(), selection};
	// The plan's ids may come from a copy of the counters: never hand one of them out again.
	auto counters = doc.idCounters();
	for (const auto& building : plan.buildings) {
		for (const auto* list : {&building.world, &building.detail}) {
			for (const auto& solid : *list) {
				counters.first = std::max(counters.first, solid.id);
				for (const auto& side : solid.sides) counters.second = std::max(counters.second, side.id);
			}
		}
	}
	doc.setIdCounters(counters);
	// Editing: the buildings' old pieces go.
	for (const auto& ref : plan.removeSolids) {
		if (ref.entityId != 0) continue;
		t.touchSolid(ref);
		std::erase_if(doc.world.solids, [&](const Solid& s) { return s.id == ref.solidId; });
	}
	for (const int id : plan.removeEntities) {
		t.touchEntity(id);
		std::erase_if(doc.entities, [&](const Entity& e) { return e.id == id; });
	}
	for (std::size_t bi = 0; bi < plan.buildings.size(); ++bi) {
		const auto& building = plan.buildings[bi];
		if (building.lot.entityId >= 0) {
			if (auto* entity = doc.findEntity(building.lot.entityId)) {
				t.touchSolid(building.lot);
				std::erase_if(entity->solids, [&](const Solid& s) { return s.id == building.lot.solidId; });
			}
		}
		int buildingId = 0;
		int holder = 0;   // the entity that keeps the settings (an id: the entity list grows below)
		if (!building.detail.empty()) {
			Entity detail;
			detail.id = doc.newEntityId();
			detail.set("classname", "func_detail");
			for (const auto& [key, value] : detailDefaults) {
				if (!value.empty() && !iequals(key, "classname")) detail.set(key, value);
			}
			detail.solids = building.detail;
			auto editor = KvNode::makeBlock("editor");
			editor.set("color", "0 180 0");
			editor.set("visgroupshown", "1");
			editor.set("visgroupautoshown", "1");
			detail.extra.push_back(std::move(editor));
			t.touchEntity(detail.id);
			selection.entities.push_back(detail.id);
			buildingId = detail.id;
			holder = detail.id;
			detail.set(KEY_ID, std::to_string(buildingId));
			doc.entities.push_back(std::move(detail));
		}
		// Its props, one entity each.
		std::map<std::string, std::vector<PropPlacement>> byModel;
		for (const auto& prop : plan.props) {
			if (prop.building == static_cast<int>(bi)) byModel[prop.model].push_back(prop.placement);
		}
		std::vector<int> propIds;
		for (const auto& [model, placements] : byModel) {
			for (auto& entity : makePropEntities(doc, placements, "prop_static", model, propDefaults)) {
				if (buildingId == 0) buildingId = entity.id;
				entity.set(KEY_ID, std::to_string(buildingId));
				t.touchEntity(entity.id);
				selection.entities.push_back(entity.id);
				propIds.push_back(entity.id);
				doc.entities.push_back(std::move(entity));
			}
		}
		if (holder == 0 && !propIds.empty()) holder = propIds.front();
		if (Entity* keeper = holder ? doc.findEntity(holder) : nullptr; keeper && !building.outline.empty()) {
			keeper->set(KEY_SETTINGS, encodeBuildingOptions(building.settings));
			keeper->set(KEY_LOT, encodeOutline(building.outline, building.bottom, building.lotTop));
		}
		for (auto solid : building.world) {
			if (buildingId != 0) {
				if (KvNode* editor = editorOf(solid.extra)) editor->set(KEY_ID, std::to_string(buildingId));
			}
			const SolidRef ref{0, solid.id};
			t.touchSolid(ref);
			doc.world.solids.push_back(std::move(solid));
			selection.solids.push_back(ref);
		}
	}
	return {t.finish(), selection};
}

} // namespace edit

} // namespace sme
