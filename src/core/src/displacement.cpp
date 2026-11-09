// displacement.cpp - Displacements: dispinfo read/write, creation, sculpting, sewing (see displacement.h).

#include <sme/displacement.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numbers>
#include <tuple>

#include <sme/text.h>

namespace sme {

namespace {

constexpr int TAG_WALKABLE = 1 << 0;    // Hammer's COREDISPTRI_TAG_WALKABLE
constexpr int TAG_BUILDABLE = 1 << 3;   // COREDISPTRI_TAG_BUILDABLE
constexpr double WALKABLE_Z = 0.7;      // like the player's ground check
constexpr double BUILDABLE_Z = 0.8;

/// Numbers as Hammer writes them: short, no noise beyond a thousandth.
std::string number(double value) {
	const double rounded = std::round(value * 1000.0) / 1000.0;
	return formatNumber(std::abs(rounded) < 0.0005 ? 0.0 : rounded);
}

std::vector<std::string> splitWords(std::string_view text) {
	std::vector<std::string> out;
	std::size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
		const std::size_t start = i;
		while (i < text.size() && text[i] != ' ' && text[i] != '\t') ++i;
		if (i > start) out.emplace_back(text.substr(start, i - start));
	}
	return out;
}

/// Row `r` of a block ("normals" -> "row3"), split into numbers; empty if missing.
std::vector<double> rowNumbers(const KvNode& info, std::string_view block, int row) {
	std::vector<double> out;
	if (const auto* node = info.find(block)) {
		for (const auto& word : splitWords(node->get("row" + std::to_string(row)))) out.push_back(std::atof(word.c_str()));
	}
	return out;
}

/// Replaces (or adds) a block of rows: `row0` .. `row{n-1}`.
void setRows(KvNode& info, std::string_view block, const std::vector<std::string>& rows) {
	KvNode* node = info.find(block);
	if (!node) {
		info.children.push_back(KvNode::makeBlock(std::string(block)));
		node = &info.children.back();
	}
	std::vector<KvNode> kept;
	for (auto& child : node->children) {
		if (!(child.key.starts_with("row") && !child.block)) kept.push_back(std::move(child));
	}
	node->children.clear();
	for (std::size_t i = 0; i < rows.size(); ++i) node->children.push_back(KvNode::makeValue("row" + std::to_string(i), rows[i]));
	for (auto& child : kept) node->children.push_back(std::move(child));
}

std::string vectorRow(const std::vector<Vec3>& values, int row, int size) {
	std::string out;
	for (int j = 0; j < size; ++j) {
		const Vec3& v = values[static_cast<std::size_t>(row) * size + j];
		if (j) out += ' ';
		out += number(v.x) + ' ' + number(v.y) + ' ' + number(v.z);
	}
	return out;
}

std::string numberRow(const std::vector<double>& values, int row, int size) {
	std::string out;
	for (int j = 0; j < size; ++j) {
		if (j) out += ' ';
		out += number(values[static_cast<std::size_t>(row) * size + j]);
	}
	return out;
}

/// The face plane's point and outward normal.
Vec3 planeHeightAxis(const FaceGeometry& face) {
	return normalize(face.normal);
}

/// Height of a vertex's displacement above the face plane.
double heightOf(const Displacement& d, std::size_t i, const Vec3& n) {
	return dot(d.vector(i) + d.offsets[i], n);
}

/// Pseudo-random value in -1..1 for a vertex (repeatable).
double noiseValue(std::size_t index, unsigned seed) {
	unsigned x = static_cast<unsigned>(index) * 2654435761u ^ (seed * 2246822519u + 0x9E3779B9u);
	x ^= x >> 15;
	x *= 2246822519u;
	x ^= x >> 13;
	x *= 3266489917u;
	x ^= x >> 16;
	return (x & 0xFFFFFF) / double(0xFFFFFF) * 2.0 - 1.0;
}

/// The face of `solid` that belongs to the side with this id.
const FaceGeometry* faceOfSide(const BrushGeometry& geometry, const Solid& solid, int sideId) {
	for (const auto& face : geometry.faces) {
		if (solid.sides[static_cast<std::size_t>(face.sideIndex)].id == sideId) return &face;
	}
	return nullptr;
}

} // namespace

void Displacement::setVector(std::size_t index, const Vec3& v) {
	const double len = length(v);
	if (len < 1e-6) {
		distances[index] = 0;   // keep the direction: an untouched grid writes back the same normals
		return;
	}
	normals[index] = v / len;
	distances[index] = len;
}

std::optional<Displacement> readDisplacement(const Side& side) {
	const KvNode* info = side.dispInfo();
	if (!info) return std::nullopt;
	Displacement d;
	d.power = std::atoi(info->get("power", "0").c_str());
	if (d.power < 2 || d.power > 4) return std::nullopt;
	std::string start = info->get("startposition");
	if (start.size() > 2 && start.front() == '[') start = start.substr(1, start.size() - 2);
	if (!parseVec3(start, d.startPosition)) return std::nullopt;
	d.flags = std::atoi(info->get("flags", "0").c_str());
	d.elevation = std::atof(info->get("elevation", "0").c_str());
	d.subdiv = info->get("subdiv", "0") != "0";
	const int size = d.size();
	const std::size_t count = static_cast<std::size_t>(size) * size;
	d.normals.assign(count, {});
	d.distances.assign(count, 0);
	d.offsets.assign(count, {});
	d.offsetNormals.assign(count, {});
	d.alphas.assign(count, 0);
	for (int r = 0; r < size; ++r) {
		const auto normals = rowNumbers(*info, "normals", r);
		const auto distances = rowNumbers(*info, "distances", r);
		const auto offsets = rowNumbers(*info, "offsets", r);
		const auto offsetNormals = rowNumbers(*info, "offset_normals", r);
		const auto alphas = rowNumbers(*info, "alphas", r);
		for (int j = 0; j < size; ++j) {
			const std::size_t i = static_cast<std::size_t>(r) * size + j;
			const auto j3 = static_cast<std::size_t>(j) * 3;
			if (j3 + 2 < normals.size()) d.normals[i] = {normals[j3], normals[j3 + 1], normals[j3 + 2]};
			if (static_cast<std::size_t>(j) < distances.size()) d.distances[i] = distances[static_cast<std::size_t>(j)];
			if (j3 + 2 < offsets.size()) d.offsets[i] = {offsets[j3], offsets[j3 + 1], offsets[j3 + 2]};
			if (j3 + 2 < offsetNormals.size()) d.offsetNormals[i] = {offsetNormals[j3], offsetNormals[j3 + 1], offsetNormals[j3 + 2]};
			if (static_cast<std::size_t>(j) < alphas.size()) d.alphas[i] = alphas[static_cast<std::size_t>(j)];
		}
	}
	const std::size_t tags = static_cast<std::size_t>(size - 1) * (size - 1) * 2;
	d.triangleTags.assign(tags, 0);
	for (int r = 0; r + 1 < size; ++r) {
		const auto row = rowNumbers(*info, "triangle_tags", r);
		for (int k = 0; k < (size - 1) * 2 && static_cast<std::size_t>(k) < row.size(); ++k) {
			d.triangleTags[static_cast<std::size_t>(r) * (size - 1) * 2 + k] = static_cast<int>(row[static_cast<std::size_t>(k)]);
		}
	}
	d.allowedVerts.assign(10, -1);
	if (const auto* allowed = info->find("allowed_verts")) {
		const auto words = splitWords(allowed->get("10"));
		for (std::size_t k = 0; k < words.size() && k < 10; ++k) d.allowedVerts[k] = std::atoi(words[k].c_str());
	}
	return d;
}

void storeDisplacement(Side& side, const Displacement& d) {
	KvNode* info = nullptr;
	for (auto& node : side.extra) {
		if (node.block && iequals(node.key, "dispinfo")) info = &node;
	}
	if (!info) {
		side.extra.push_back(KvNode::makeBlock("dispinfo"));
		info = &side.extra.back();
	}
	info->set("power", std::to_string(d.power));
	info->set("startposition", "[" + formatVec3(d.startPosition) + "]");
	info->set("flags", std::to_string(d.flags));
	info->set("elevation", number(d.elevation));
	info->set("subdiv", d.subdiv ? "1" : "0");
	const int size = d.size();
	std::vector<std::string> normals, distances, offsets, offsetNormals, alphas, tags;
	for (int r = 0; r < size; ++r) {
		normals.push_back(vectorRow(d.normals, r, size));
		distances.push_back(numberRow(d.distances, r, size));
		offsets.push_back(vectorRow(d.offsets, r, size));
		offsetNormals.push_back(vectorRow(d.offsetNormals, r, size));
		alphas.push_back(numberRow(d.alphas, r, size));
	}
	for (int r = 0; r + 1 < size; ++r) {
		std::string row;
		for (int k = 0; k < (size - 1) * 2; ++k) {
			if (k) row += ' ';
			row += std::to_string(d.triangleTags[static_cast<std::size_t>(r) * (size - 1) * 2 + k]);
		}
		tags.push_back(row);
	}
	setRows(*info, "normals", normals);
	setRows(*info, "distances", distances);
	setRows(*info, "offsets", offsets);
	setRows(*info, "offset_normals", offsetNormals);
	setRows(*info, "alphas", alphas);
	setRows(*info, "triangle_tags", tags);
	KvNode* allowed = info->find("allowed_verts");
	if (!allowed) {
		info->children.push_back(KvNode::makeBlock("allowed_verts"));
		allowed = &info->children.back();
	}
	std::string verts;
	for (std::size_t k = 0; k < 10; ++k) {
		if (k) verts += ' ';
		verts += std::to_string(k < d.allowedVerts.size() ? d.allowedVerts[k] : -1);
	}
	allowed->set("10", verts);
}

bool removeDisplacement(Side& side) {
	const auto before = side.extra.size();
	std::erase_if(side.extra, [](const KvNode& node) { return node.block && iequals(node.key, "dispinfo"); });
	return side.extra.size() != before;
}

std::vector<Vec3> displacementCorners(const FaceGeometry& face, const Vec3& start) {
	if (face.vertices.size() != 4) return {};
	// Clockwise seen from outside (the face's vertices run counter-clockwise), from the corner
	// nearest to startposition - CCoreDispInfo::GenerateDispSurf.
	std::vector<Vec3> corners(face.vertices.rbegin(), face.vertices.rend());
	std::size_t first = 0;
	double best = std::numeric_limits<double>::max();
	for (std::size_t i = 0; i < 4; ++i) {
		const double d = length(corners[i] - start);
		if (d < best) {
			best = d;
			first = i;
		}
	}
	std::rotate(corners.begin(), corners.begin() + static_cast<std::ptrdiff_t>(first), corners.end());
	return corners;
}

std::vector<Vec3> displacementBase(const Displacement& d, const FaceGeometry& face) {
	const auto corners = displacementCorners(face, d.startPosition);
	if (corners.empty()) return {};
	const int size = d.size();
	const double step = 1.0 / (size - 1);
	std::vector<Vec3> out(static_cast<std::size_t>(size) * size);
	for (int i = 0; i < size; ++i) {
		const Vec3 a = corners[0] + (corners[1] - corners[0]) * (i * step);
		const Vec3 b = corners[3] + (corners[2] - corners[3]) * (i * step);
		for (int j = 0; j < size; ++j) out[static_cast<std::size_t>(i) * size + j] = a + (b - a) * (j * step);
	}
	return out;
}

std::vector<Vec3> displacementPositions(const Displacement& d, const FaceGeometry& face) {
	auto out = displacementBase(d, face);
	for (std::size_t i = 0; i < out.size(); ++i) out[i] += d.vector(i) + d.offsets[i];
	return out;
}

std::optional<Displacement> makeDisplacement(const FaceGeometry& face, int power, double elevation) {
	if (face.vertices.size() != 4 || power < 2 || power > 4) return std::nullopt;
	Displacement d;
	d.power = power;
	// Start at the lowest corner (smallest x, then y, then z) - any corner works, this one is stable.
	d.startPosition = *std::min_element(face.vertices.begin(), face.vertices.end(), [](const Vec3& a, const Vec3& b) {
		if (std::abs(a.x - b.x) > 0.001) return a.x < b.x;
		if (std::abs(a.y - b.y) > 0.001) return a.y < b.y;
		return a.z < b.z;
	});
	const int size = d.size();
	const std::size_t count = static_cast<std::size_t>(size) * size;
	const Vec3 n = normalize(face.normal);
	d.normals.assign(count, n);
	d.distances.assign(count, 0);
	d.offsets.assign(count, {});
	d.offsetNormals.assign(count, n);
	d.alphas.assign(count, 0);
	d.allowedVerts.assign(10, -1);
	// Elevation is written into the distances: VBSP does not read the "elevation" key.
	if (std::abs(elevation) > 1e-9) {
		for (std::size_t i = 0; i < count; ++i) d.setVector(i, n * elevation);
	}
	updateTriangleTags(d, face);
	return d;
}

void updateTriangleTags(Displacement& d, const FaceGeometry& face) {
	const auto p = displacementPositions(d, face);
	const int size = d.size();
	d.triangleTags.assign(static_cast<std::size_t>(size - 1) * (size - 1) * 2, 0);
	if (p.empty()) return;
	const Vec3 faceNormal = normalize(face.normal);
	for (int i = 0; i + 1 < size; ++i) {
		for (int j = 0; j + 1 < size; ++j) {
			const std::size_t a = static_cast<std::size_t>(i) * size + j, b = a + 1, c = a + size, e = c + 1;
			const std::array<std::size_t, 3> tris[2] = {{a, c, b}, {b, c, e}};
			for (int t = 0; t < 2; ++t) {
				Vec3 tn = normalize(cross(p[tris[t][1]] - p[tris[t][0]], p[tris[t][2]] - p[tris[t][0]]));
				if (dot(tn, faceNormal) < 0) tn = -tn;
				int tag = 0;
				if (tn.z >= WALKABLE_Z) tag |= TAG_WALKABLE;
				if (tn.z >= BUILDABLE_Z) tag |= TAG_BUILDABLE;
				d.triangleTags[(static_cast<std::size_t>(i) * (size - 1) + j) * 2 + t] = tag;
			}
		}
	}
}

double sculptFalloff(double distance, double radius) {
	if (radius <= 0 || distance >= radius) return 0;
	return 0.5 * (1.0 + std::cos(std::numbers::pi * distance / radius));
}

bool sculpt(Displacement& d, const FaceGeometry& face, SculptMode mode, const SculptBrush& brush) {
	const auto positions = displacementPositions(d, face);
	if (positions.empty()) return false;
	const Vec3 n = planeHeightAxis(face);
	// Raise, Noise and Flatten move along the chosen axis; the brush circle stays in the face plane.
	const bool ownAxis = length(brush.axis) > 1e-9;
	const Vec3 axis = ownAxis ? normalize(brush.axis) : n;
	const int size = d.size();
	const double sign = brush.invert ? -1.0 : 1.0;
	// Weights first: smoothing reads the neighbours' old values.
	std::vector<double> weight(positions.size(), 0);
	bool any = false;
	for (std::size_t i = 0; i < positions.size(); ++i) {
		Vec3 offset = positions[i] - brush.center;
		offset -= n * dot(offset, n);   // measured in the face plane
		weight[i] = sculptFalloff(length(offset), brush.radius);
		any = any || weight[i] > 0;
	}
	if (!any) return false;
	const Displacement before = d;
	bool changed = false;
	for (std::size_t i = 0; i < positions.size(); ++i) {
		const double w = weight[i];
		if (w <= 0) continue;
		switch (mode) {
			case SculptMode::Raise:
				d.setVector(i, before.vector(i) + axis * (sign * brush.strength * w));
				break;
			case SculptMode::Noise:
				d.setVector(i, before.vector(i) + axis * (noiseValue(i, brush.seed) * brush.strength * w));
				break;
			case SculptMode::Flatten: {
				const double f = std::clamp(brush.strength, 0.0, 1.0) * w;
				if (ownAxis) {
					// Along another axis: towards the stroke start's level on that axis (a wall of a cut, a terrace).
					const double h = dot(positions[i], axis), target = dot(brush.flattenPoint, axis);
					d.setVector(i, before.vector(i) + axis * ((target - h) * f));
				} else {
					const double h = heightOf(before, i, n);
					d.setVector(i, before.vector(i) + n * ((brush.flattenHeight - h) * f));
				}
				break;
			}
			case SculptMode::Smooth: {
				const int row = static_cast<int>(i) / size, col = static_cast<int>(i) % size;
				Vec3 sum;
				int count = 0;
				for (int dr = -1; dr <= 1; ++dr) {
					for (int dc = -1; dc <= 1; ++dc) {
						const int r = row + dr, c = col + dc;
						if ((dr || dc) && r >= 0 && r < size && c >= 0 && c < size) {
							sum += before.vector(static_cast<std::size_t>(r) * size + c);
							++count;
						}
					}
				}
				if (!count) break;
				const double f = std::clamp(brush.strength, 0.0, 1.0) * w;
				const Vec3 v = before.vector(i);
				d.setVector(i, v + (sum / double(count) - v) * f);
				break;
			}
			case SculptMode::PaintAlpha:
				d.alphas[i] = std::clamp(before.alphas[i] + sign * brush.strength * w, 0.0, 255.0);
				break;
		}
		changed = changed || std::abs(d.distances[i] - before.distances[i]) > 1e-9 || length(d.normals[i] - before.normals[i]) > 1e-9 ||
		          std::abs(d.alphas[i] - before.alphas[i]) > 1e-9;
	}
	if (changed && mode != SculptMode::PaintAlpha) updateTriangleTags(d, face);
	return changed;
}

namespace {

/// Distance from `p` to the segment a-b.
double segmentDistance(const Vec3& p, const Vec3& a, const Vec3& b) {
	const Vec3 ab = b - a;
	const double len2 = dot(ab, ab);
	const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
	return length(p - (a + ab * t));
}

/// Smooth value noise in -1..1 over the plane (repeatable, about one wave per 64 units), so a
/// frayed blend border has bays and tongues instead of single-vertex spikes.
double smoothNoise(const Vec3& p, unsigned seed) {
	constexpr double CELL = 64;
	const double x = p.x / CELL, y = p.y / CELL + p.z / CELL * 0.37;
	const double fx = std::floor(x), fy = std::floor(y);
	const auto lattice = [seed](long long ix, long long iy) {
		return noiseValue(static_cast<std::size_t>(ix * 73856093LL ^ iy * 19349663LL), seed);
	};
	const double tx = x - fx, ty = y - fy;
	const double sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
	const auto ix = static_cast<long long>(fx), iy = static_cast<long long>(fy);
	const double a = lattice(ix, iy) + (lattice(ix + 1, iy) - lattice(ix, iy)) * sx;
	const double b = lattice(ix, iy + 1) + (lattice(ix + 1, iy + 1) - lattice(ix, iy + 1)) * sx;
	return a + (b - a) * sy;
}

} // namespace

double distanceToBrush(const BrushGeometry& brush, const Vec3& point) {
	if (brush.faces.empty()) return std::numeric_limits<double>::infinity();
	// Inside every face plane: inside the brush.
	bool inside = true;
	for (const auto& face : brush.faces) {
		if (face.vertices.empty()) continue;
		if (dot(point - face.vertices.front(), normalize(face.normal)) > 1e-6) {
			inside = false;
			break;
		}
	}
	if (inside) return 0;
	double best = std::numeric_limits<double>::infinity();
	for (const auto& face : brush.faces) {
		const auto& v = face.vertices;
		if (v.size() < 3) continue;
		const Vec3 n = normalize(face.normal);
		const double h = dot(point - v.front(), n);
		const Vec3 onPlane = point - n * h;
		// Projected point inside the (convex, counter-clockwise) polygon: the plane distance.
		bool within = true;
		for (std::size_t i = 0; i < v.size() && within; ++i) {
			within = dot(cross(v[(i + 1) % v.size()] - v[i], onPlane - v[i]), n) >= -1e-9;
		}
		if (within) {
			best = std::min(best, std::abs(h));
			continue;
		}
		for (std::size_t i = 0; i < v.size(); ++i) best = std::min(best, segmentDistance(point, v[i], v[(i + 1) % v.size()]));
	}
	return best;
}

double blendNearAlpha(double distance, const BlendNearOptions& options) {
	const double width = std::max(0.0, options.width), soft = std::max(0.0, options.softness);
	double share = 1;
	if (distance > width) {
		if (soft <= 1e-9) {
			share = 0;
		} else {
			const double t = std::clamp((distance - width) / soft, 0.0, 1.0);
			share = 1 - t * t * (3 - 2 * t);   // smoothstep: no visible ring where the fade starts or ends
		}
	}
	return options.secondMaterialNear ? 255.0 * share : 255.0 * (1 - share);
}

bool blendNear(Displacement& d, const FaceGeometry& face, const std::vector<BrushGeometry>& targets, const BlendNearOptions& options) {
	const auto positions = displacementPositions(d, face);
	if (positions.empty() || targets.empty()) return false;
	bool changed = false;
	for (std::size_t i = 0; i < positions.size(); ++i) {
		double distance = std::numeric_limits<double>::infinity();
		for (const auto& target : targets) distance = std::min(distance, distanceToBrush(target, positions[i]));
		if (!std::isfinite(distance)) continue;
		if (options.noise > 0) distance = std::max(0.0, distance + smoothNoise(positions[i], options.seed) * options.noise);
		const double alpha = blendNearAlpha(distance, options);
		// The stronger paint toward the near material wins: hand-painted patches stay.
		const double next = options.secondMaterialNear ? std::max(d.alphas[i], alpha) : std::min(d.alphas[i], alpha);
		if (std::abs(next - d.alphas[i]) > 1e-6) {
			d.alphas[i] = next;
			changed = true;
		}
	}
	return changed;
}

int sewDisplacements(const std::vector<SewItem>& items) {
	// Group the edge vertices of all displacements by their flat position.
	struct Member {
		std::size_t item, index;
	};
	struct Key {
		long long x, y, z;
		bool operator<(const Key& o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
	};
	const auto keyOf = [](const Vec3& p) { return Key{std::llround(p.x * 10), std::llround(p.y * 10), std::llround(p.z * 10)}; };
	std::map<Key, std::vector<Member>> groups;
	std::vector<std::vector<Vec3>> bases(items.size()), finals(items.size());
	for (std::size_t k = 0; k < items.size(); ++k) {
		const auto& d = *items[k].displacement;
		bases[k] = displacementBase(d, *items[k].face);
		finals[k] = displacementPositions(d, *items[k].face);
		const int size = d.size();
		for (int i = 0; i < size; ++i) {
			for (int j = 0; j < size; ++j) {
				if (i != 0 && j != 0 && i != size - 1 && j != size - 1) continue;   // edges only
				const std::size_t index = static_cast<std::size_t>(i) * size + j;
				if (!bases[k].empty()) groups[keyOf(bases[k][index])].push_back({k, index});
			}
		}
	}
	int moved = 0;
	std::vector<bool> touched(items.size(), false);
	for (const auto& [key, members] : groups) {
		bool shared = false;
		for (const auto& m : members) shared = shared || m.item != members.front().item;
		if (!shared) continue;
		Vec3 average;
		for (const auto& m : members) average += finals[m.item][m.index];
		average = average / double(members.size());
		for (const auto& m : members) {
			if (length(finals[m.item][m.index] - average) < 0.001) continue;
			auto& d = *items[m.item].displacement;
			d.setVector(m.index, average - bases[m.item][m.index] - d.offsets[m.index]);
			touched[m.item] = true;
			++moved;
		}
	}
	for (std::size_t k = 0; k < items.size(); ++k) {
		if (touched[k]) updateTriangleTags(*items[k].displacement, *items[k].face);
	}
	return moved;
}

namespace edit {

Change createDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces, int power, double elevation, int* skipped) {
	Transaction t(doc, "Create displacement");
	int skip = 0;
	for (const auto& ref : faces) {
		Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* face = faceOfSide(geometry, *solid, ref.sideId);
		const auto it = std::find_if(solid->sides.begin(), solid->sides.end(), [&](const Side& s) { return s.id == ref.sideId; });
		if (!face || it == solid->sides.end() || it->dispInfo()) {
			++skip;
			continue;
		}
		auto made = makeDisplacement(*face, power, elevation);
		if (!made) {
			++skip;
			continue;
		}
		t.touchSolid(ref.solid);
		storeDisplacement(*it, *made);
	}
	if (skipped) *skipped = skip;
	return t.finish();
}

Change destroyDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces) {
	Transaction t(doc, "Destroy displacement");
	for (const auto& ref : faces) {
		Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		for (auto& side : solid->sides) {
			if (side.id == ref.sideId && side.dispInfo()) {
				t.touchSolid(ref.solid);
				removeDisplacement(side);
			}
		}
	}
	return t.finish();
}

Change sewDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces, int* moved) {
	Transaction t(doc, "Sew displacements");
	struct Entry {
		FaceRef ref;
		Displacement displacement;
		FaceGeometry face;
	};
	std::vector<Entry> entries;
	for (const auto& ref : faces) {
		const Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* face = faceOfSide(geometry, *solid, ref.sideId);
		const auto it = std::find_if(solid->sides.begin(), solid->sides.end(), [&](const Side& s) { return s.id == ref.sideId; });
		if (!face || it == solid->sides.end()) continue;
		if (auto d = readDisplacement(*it)) entries.push_back({ref, std::move(*d), *face});
	}
	std::vector<SewItem> items;
	for (auto& e : entries) items.push_back({&e.displacement, &e.face});
	const int count = sme::sewDisplacements(items);
	if (moved) *moved = count;
	if (count > 0) {
		for (auto& e : entries) {
			Solid* solid = doc.findSolid(e.ref.solid);
			t.touchSolid(e.ref.solid);
			for (auto& side : solid->sides) {
				if (side.id == e.ref.sideId) storeDisplacement(side, e.displacement);
			}
		}
	}
	return t.finish();
}

Change blendAlongSelection(MapDocument& doc, const Selection& selection, const BlendNearOptions& options, int* count) {
	Transaction t(doc, "Blend along selection");
	if (count) *count = 0;
	// Targets: the selected brushes without displacements (the road, the path).
	std::vector<BrushGeometry> targets;
	Bounds reach;
	const auto addTarget = [&](const Solid& solid) {
		if (std::any_of(solid.sides.begin(), solid.sides.end(), [](const Side& s) { return s.dispInfo() != nullptr; })) return;
		auto geometry = buildBrushGeometry(solid);
		if (!geometry.valid) return;
		reach.add(geometry.bounds.min);
		reach.add(geometry.bounds.max);
		targets.push_back(std::move(geometry));
	};
	for (const auto& ref : selection.solids) {
		if (const Solid* solid = doc.findSolid(ref)) addTarget(*solid);
	}
	for (const int id : selection.entities) {
		if (const Entity* entity = doc.findEntity(id)) {
			for (const auto& solid : entity->solids) addTarget(solid);
		}
	}
	if (targets.empty()) return t.finish();
	// The displacements: the selected ones, else every one near enough to be touched.
	auto faces = displacementFaces(doc, selection);
	if (faces.empty()) {
		const double margin = std::max(0.0, options.width) + std::max(0.0, options.softness) + std::max(0.0, options.noise) + 1;
		const Vec3 m{margin, margin, margin};
		const Bounds area{reach.min - m, reach.max + m};
		const auto consider = [&](const Solid& solid, const SolidRef& ref) {
			for (const auto& side : solid.sides) {
				if (!side.dispInfo()) continue;
				const auto geometry = buildBrushGeometry(solid);
				const Bounds& b = geometry.bounds;
				// The displacement may stand out of its brush: allow for it (sculpting rarely goes beyond 512).
				const double up = 512;
				if (b.max.x + up < area.min.x || b.min.x - up > area.max.x || b.max.y + up < area.min.y || b.min.y - up > area.max.y ||
				    b.max.z + up < area.min.z || b.min.z - up > area.max.z) {
					return;
				}
				faces.push_back({ref, side.id});
			}
		};
		for (const auto& solid : doc.world.solids) consider(solid, {0, solid.id});
		for (const auto& entity : doc.entities) {
			for (const auto& solid : entity.solids) consider(solid, {entity.id, solid.id});
		}
	}
	int changed = 0;
	for (const auto& ref : faces) {
		Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* face = faceOfSide(geometry, *solid, ref.sideId);
		const auto it = std::find_if(solid->sides.begin(), solid->sides.end(), [&](const Side& s) { return s.id == ref.sideId; });
		if (!face || it == solid->sides.end()) continue;
		auto d = readDisplacement(*it);
		if (!d || !blendNear(*d, *face, targets, options)) continue;
		t.touchSolid(ref.solid);
		// touchSolid may copy: find the side again.
		for (auto& side : doc.findSolid(ref.solid)->sides) {
			if (side.id == ref.sideId) storeDisplacement(side, *d);
		}
		++changed;
	}
	if (count) *count = changed;
	return t.finish();
}

std::vector<FaceRef> displacementFaces(const MapDocument& doc, const Selection& selection) {
	std::vector<FaceRef> out;
	const auto addSolid = [&](const SolidRef& ref) {
		if (const Solid* solid = doc.findSolid(ref)) {
			for (const auto& side : solid->sides) {
				if (side.dispInfo()) out.push_back({ref, side.id});
			}
		}
	};
	for (const auto& ref : selection.solids) addSolid(ref);
	for (const int id : selection.entities) {
		if (const Entity* entity = doc.findEntity(id)) {
			for (const auto& solid : entity->solids) addSolid({id, solid.id});
		}
	}
	for (const auto& face : selection.faces) {
		const Solid* solid = doc.findSolid(face.solid);
		if (!solid) continue;
		for (const auto& side : solid->sides) {
			if (side.id == face.sideId && side.dispInfo() && std::find(out.begin(), out.end(), face) == out.end()) out.push_back(face);
		}
	}
	return out;
}

} // namespace edit

} // namespace sme
