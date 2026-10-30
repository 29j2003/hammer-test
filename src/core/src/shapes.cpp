// shapes.cpp - Block tool shapes (see shapes.h).

#include <sme/shapes.h>

#include <sme/brush.h>
#include <sme/csg.h>

#include <algorithm>
#include <cmath>

namespace sme {

namespace {

constexpr double PI = 3.14159265358979323846;

/// Hammer keeps shape corners on whole units; very small shapes keep two decimals.
double tidy(double v, bool coarse) {
	return coarse ? std::round(v) : std::round(v * 100) / 100;
}

Vec3 tidy(const Vec3& v, bool coarse) {
	return {tidy(v.x, coarse), tidy(v.y, coarse), tidy(v.z, coarse)};
}

/// Corners of an n-sided ring fitted into the box (ellipse), a half step turned so that
/// shapes with 4, 8, 12 ... sides have faces along the axes.
std::vector<Vec3> ring(const Bounds& box, int sides, double z, bool coarse) {
	std::vector<Vec3> out;
	const Vec3 c = box.center();
	const double rx = box.size().x / 2, ry = box.size().y / 2;
	for (int i = 0; i < sides; ++i) {
		const double a = 2 * PI * (i + 0.5) / sides;
		out.push_back(tidy(Vec3{c.x + rx * std::cos(a), c.y + ry * std::sin(a), z}, coarse));
	}
	return out;
}

/// Where an arch's corners lie: shared by makeShape() and archOuterPoints().
struct ArchFrame {
	int pieces;
	double arc, start, rx, ry, ix, iy;
	Vec3 c;
	bool coarse;

	ArchFrame(const Bounds& box, const ShapeOptions& options) {
		const Vec3 size = box.size();
		pieces = std::clamp(options.sides, 1, 64);
		arc = std::clamp(options.arc, 1.0, 360.0);
		start = options.startAngle;
		const double width = std::clamp(options.wallWidth, 1.0, std::min(size.x, size.y) / 2);
		c = box.center();
		rx = size.x / 2;
		ry = size.y / 2;
		ix = rx - width;
		iy = ry - width;
		coarse = std::min({size.x, size.y}) >= 32;
	}
	[[nodiscard]] double angle(int i) const { return start + arc * i / pieces; }
	[[nodiscard]] Vec3 at(double rxx, double ryy, double degrees, double z) const {
		const double a = degrees * PI / 180;
		return tidy(Vec3{c.x + rxx * std::cos(a), c.y + ryy * std::sin(a), z}, coarse);
	}
	[[nodiscard]] Vec3 outer(double degrees, double z) const { return at(rx, ry, degrees, z); }
	[[nodiscard]] Vec3 inner(double degrees, double z) const { return at(ix, iy, degrees, z); }
};

} // namespace

std::vector<Vec3> archOuterPoints(const Bounds& box, const ShapeOptions& options) {
	std::vector<Vec3> out;
	if (box.empty()) return out;
	const ArchFrame frame(box, options);
	for (int i = 0; i <= frame.pieces; ++i) out.push_back(frame.outer(frame.angle(i), 0));
	return out;
}

std::string shapeName(ShapeOptions::Kind kind) {
	switch (kind) {
		case ShapeOptions::Kind::Block: return "block";
		case ShapeOptions::Kind::Wedge: return "wedge";
		case ShapeOptions::Kind::Cylinder: return "cylinder";
		case ShapeOptions::Kind::Spike: return "spike";
		case ShapeOptions::Kind::Sphere: return "sphere";
		case ShapeOptions::Kind::Arch: return "arch";
		case ShapeOptions::Kind::Stairs: return "stairs";
	}
	return "shape";
}

std::vector<Solid> makeShape(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& material) {
	std::vector<Solid> out;
	const Vec3 lo = box.min, hi = box.max, size = box.size();
	if (box.empty() || size.x < 1 || size.y < 1 || size.z < 1) {
		return out;
	}
	const bool coarse = std::min({size.x, size.y}) >= 32;
	const auto add = [&](const std::vector<Vec3>& points) {
		if (auto solid = solidFromPoints(doc, points, material)) {
			out.push_back(std::move(*solid));
			return true;
		}
		return false;
	};
	const int sides = std::clamp(options.sides, 3, 64);
	switch (options.kind) {
		case ShapeOptions::Kind::Block:
			out.push_back(makeBlock(doc, box, material));
			break;
		case ShapeOptions::Kind::Wedge:
			// Full bottom, top edge along the +Y side: the slope faces -Y.
			add({{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}});
			break;
		case ShapeOptions::Kind::Cylinder: {
			auto points = ring(box, sides, lo.z, coarse);
			const auto top = ring(box, sides, hi.z, coarse);
			points.insert(points.end(), top.begin(), top.end());
			add(points);
			break;
		}
		case ShapeOptions::Kind::Spike: {
			auto points = ring(box, sides, lo.z, coarse);
			points.push_back(tidy(Vec3{box.center().x, box.center().y, hi.z}, coarse));
			add(points);
			break;
		}
		case ShapeOptions::Kind::Sphere: {
			// Rings of latitude plus the two poles, one convex brush like Hammer's sphere.
			const int around = std::min(sides, 24);
			const int rings = std::max(2, around / 2);
			const Vec3 c = box.center();
			std::vector<Vec3> points{tidy(Vec3{c.x, c.y, lo.z}, coarse), tidy(Vec3{c.x, c.y, hi.z}, coarse)};
			for (int r = 1; r < rings; ++r) {
				const double phi = PI * r / rings - PI / 2;
				Bounds slice;
				slice.add({c.x - size.x / 2 * std::cos(phi), c.y - size.y / 2 * std::cos(phi), 0});
				slice.add({c.x + size.x / 2 * std::cos(phi), c.y + size.y / 2 * std::cos(phi), 0});
				const auto layer = ring(slice, around, c.z + size.z / 2 * std::sin(phi), coarse);
				points.insert(points.end(), layer.begin(), layer.end());
			}
			add(points);
			break;
		}
		case ShapeOptions::Kind::Arch: {
			// Pieces between an outer ellipse (the box) and an inner one `wallWidth` smaller.
			const ArchFrame frame(box, options);
			for (int i = 0; i < frame.pieces; ++i) {
				const double a0 = frame.angle(i), a1 = frame.angle(i + 1);
				const double z0 = lo.z + options.addHeight * i, z1 = hi.z + options.addHeight * i;
				add({frame.outer(a0, z0), frame.outer(a1, z0), frame.inner(a0, z0), frame.inner(a1, z0), frame.outer(a0, z1), frame.outer(a1, z1),
				     frame.inner(a0, z1), frame.inner(a1, z1)});
			}
			break;
		}
		case ShapeOptions::Kind::Stairs: {
			const int steps = std::clamp(options.steps, 1, 256);
			const double depth = size.y / steps, rise = size.z / steps;
			for (int i = 0; i < steps; ++i) {
				Bounds step;
				step.add(tidy(Vec3{lo.x, lo.y + depth * i, lo.z}, coarse));
				step.add(tidy(Vec3{hi.x, lo.y + depth * (i + 1), lo.z + rise * (i + 1)}, coarse));
				if (step.size().y >= 1 && step.size().z >= 1) {
					out.push_back(makeBlock(doc, step, material));
				}
			}
			break;
		}
	}
	return out;
}

} // namespace sme
