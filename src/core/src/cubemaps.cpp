// cubemaps.cpp - env_cubemap probes for the reflection preview (see cubemaps.h).

#include <sme/cubemaps.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include <sme/brush.h>
#include <sme/map_tools.h>
#include <sme/text.h>

namespace sme {

std::vector<CubemapProbe> collectCubemaps(const MapDocument& doc) {
	std::vector<CubemapProbe> probes;
	for (const auto& entity : doc.entities) {
		if (!iequals(entity.classname(), "env_cubemap")) continue;
		CubemapProbe probe;
		probe.entityId = entity.id;
		probe.origin = entity.origin();
		std::istringstream sides(entity.get("sides"));
		for (int id = 0; sides >> id;) probe.sides.push_back(id);
		if (const Entity* obb = cubemapRoomBox(doc, entity)) {
			Bounds box;
			for (const auto& solid : obb->solids) box.add(buildBrushGeometry(solid).bounds);
			if (!box.empty()) probe.box = box;
		}
		probes.push_back(std::move(probe));
	}
	return probes;
}

int nearestProbe(const std::vector<CubemapProbe>& probes, const Vec3& point) {
	int best = -1;
	double bestDistance = std::numeric_limits<double>::max();
	for (std::size_t i = 0; i < probes.size(); ++i) {
		const Vec3 d = probes[i].origin - point;
		const double distance = dot(d, d);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = static_cast<int>(i);
		}
	}
	return best;
}

int probeForFace(const std::vector<CubemapProbe>& probes, int sideId, const Vec3& faceCenter) {
	if (sideId != 0) {
		for (std::size_t i = 0; i < probes.size(); ++i) {
			if (std::find(probes[i].sides.begin(), probes[i].sides.end(), sideId) != probes[i].sides.end()) {
				return static_cast<int>(i);
			}
		}
	}
	return nearestProbe(probes, faceCenter);
}

Vec3 boxProjectedDirection(const Vec3& position, const Vec3& direction, const Vec3& probeOrigin, const Bounds& box) {
	// Distance along the ray to the box's far side on each axis; the nearest of those is where
	// the ray leaves the box.
	double leave = std::numeric_limits<double>::max();
	for (int axis = 0; axis < 3; ++axis) {
		const double d = direction[axis];
		if (std::abs(d) < 1e-9) continue;
		const double far = d > 0 ? box.max[axis] : box.min[axis];
		leave = std::min(leave, (far - position[axis]) / d);
	}
	if (leave <= 0 || leave == std::numeric_limits<double>::max()) {
		return direction;
	}
	const Vec3 hit = position + direction * leave;
	const Vec3 corrected = hit - probeOrigin;
	return dot(corrected, corrected) > 1e-12 ? normalize(corrected) : direction;
}

Vec3 cubeFaceForward(int face) {
	static const Vec3 FORWARD[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	return FORWARD[std::clamp(face, 0, 5)];
}

Vec3 cubeFaceUp(int face) {
	// Z is up in the map; the floor and ceiling faces use +Y as their up.
	return face >= 4 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

CubeTexel cubeFaceCoordinates(const Vec3& direction) {
	const double ax = std::abs(direction.x), ay = std::abs(direction.y), az = std::abs(direction.z);
	CubeTexel out;
	if (ax >= ay && ax >= az) {
		out.face = direction.x >= 0 ? 0 : 1;
	} else if (ay >= az) {
		out.face = direction.y >= 0 ? 2 : 3;
	} else {
		out.face = direction.z >= 0 ? 4 : 5;
	}
	const Vec3 forward = cubeFaceForward(out.face), up = cubeFaceUp(out.face);
	const Vec3 right = cross(forward, up);
	const double depth = dot(direction, forward);
	if (depth <= 0) return out;
	out.s = (dot(direction, right) / depth + 1) * 0.5;
	out.t = (dot(direction, up) / depth + 1) * 0.5;
	return out;
}

} // namespace sme
