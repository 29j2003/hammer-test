// math.h - Small double-precision vector math for map geometry.
//
// Map coordinates are kept in double so repeated edits do not drift; the renderer converts to float.

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace sme {

/// 3D vector in Source units (x east, y north, z up).
struct Vec3 {
	double x = 0, y = 0, z = 0;

	/// Zero vector / the given components.
	constexpr Vec3() = default;
	constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

	constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
	constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
	constexpr Vec3 operator-() const { return {-x, -y, -z}; }
	constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
	constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
	constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
	constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	constexpr bool operator==(const Vec3&) const = default;

	constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
	constexpr double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

/// Dot product, cross product, length, unit vector (zero stays zero), per-component min/max.
[[nodiscard]] constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
[[nodiscard]] inline Vec3 normalize(const Vec3& v) {
	const double len = length(v);
	return len > 0 ? v / len : Vec3{};
}
[[nodiscard]] constexpr Vec3 componentMin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
[[nodiscard]] constexpr Vec3 componentMax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

/// Rounds to the nearest multiple of `grid` (no-op for grid <= 0).
[[nodiscard]] inline double snap(double value, double grid) { return grid > 0 ? std::round(value / grid) * grid : value; }
[[nodiscard]] inline Vec3 snap(const Vec3& v, double grid) { return {snap(v.x, grid), snap(v.y, grid), snap(v.z, grid)}; }

/// Row-major 3x3 matrix (rotations).
struct Mat3 {
	double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

	[[nodiscard]] Vec3 operator*(const Vec3& v) const {
		return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
		        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
		        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
	}
	[[nodiscard]] Mat3 operator*(const Mat3& o) const {
		Mat3 r;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
			}
		}
		return r;
	}
	/// Rotation around a world axis (0 = X, 1 = Y, 2 = Z), angle in degrees, right-handed.
	[[nodiscard]] static Mat3 rotation(int axis, double degrees);
};

inline Mat3 Mat3::rotation(int axis, double degrees) {
	const double r = degrees * 3.14159265358979323846 / 180.0;
	// Snap exact quarter turns so 90 degree rotations stay on the grid.
	double c = std::cos(r), s = std::sin(r);
	if (std::abs(c) < 1e-12) c = 0;
	if (std::abs(s) < 1e-12) s = 0;
	Mat3 out;
	const int a = (axis + 1) % 3, b = (axis + 2) % 3;
	out.m[a][a] = c;
	out.m[a][b] = -s;
	out.m[b][a] = s;
	out.m[b][b] = c;
	return out;
}

/// A plane: points p with dot(normal, p) == dist.
struct Plane {
	Vec3 normal;    ///< Unit length, points out of the brush
	double dist = 0;

	/// Signed distance (positive in front, outside the brush).
	[[nodiscard]] double distanceTo(const Vec3& p) const { return dot(normal, p) - dist; }
};

/// Axis-aligned box. Empty while min > max.
struct Bounds {
	Vec3 min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
	Vec3 max{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};

	/// Nothing added yet.
	[[nodiscard]] bool empty() const { return min.x > max.x; }
	/// Grows the box to include a point or another box.
	void add(const Vec3& p) { min = componentMin(min, p); max = componentMax(max, p); }
	void add(const Bounds& b) { if (!b.empty()) { add(b.min); add(b.max); } }
	/// Middle and size of the box.
	[[nodiscard]] Vec3 center() const { return (min + max) * 0.5; }
	[[nodiscard]] Vec3 size() const { return max - min; }
};

/// A ray for picking: start point and direction.
struct Ray {
	Vec3 origin;
	Vec3 direction;   ///< Unit length
};

/// Ray against box; distance along the ray or a negative value if missed.
[[nodiscard]] inline double intersect(const Ray& ray, const Bounds& box) {
	double tMin = 0, tMax = std::numeric_limits<double>::max();
	for (int i = 0; i < 3; ++i) {
		if (std::abs(ray.direction[i]) < 1e-12) {
			if (ray.origin[i] < box.min[i] || ray.origin[i] > box.max[i]) return -1;
			continue;
		}
		double t1 = (box.min[i] - ray.origin[i]) / ray.direction[i];
		double t2 = (box.max[i] - ray.origin[i]) / ray.direction[i];
		if (t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if (tMin > tMax) return -1;
	}
	return tMin;
}

} // namespace sme
