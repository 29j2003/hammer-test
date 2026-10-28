// cubemaps.h - env_cubemap probes for the editor's reflection preview: where the probes are,
// which probe a face reflects (as VBSP assigns them: the cubemap that lists the face in its
// "sides", else the nearest one), parallax correction with a Mapbase room box (parallax_obb)
// and the layout of the six cube faces the view renders and samples.

#pragma once

#include <optional>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

/// One env_cubemap of the map.
struct CubemapProbe {
	int entityId = 0;
	Vec3 origin;
	std::optional<Bounds> box;   ///< Room box (parallax_obb) for parallax-corrected reflections
	std::vector<int> sides;      ///< Faces given explicitly ("sides" key); others use the nearest probe
};

/// Every env_cubemap of the map, in file order, with its room box when it has one.
[[nodiscard]] std::vector<CubemapProbe> collectCubemaps(const MapDocument& doc);

/// The probe nearest to a point; -1 without probes.
[[nodiscard]] int nearestProbe(const std::vector<CubemapProbe>& probes, const Vec3& point);

/// The probe a face reflects: the one listing the face (side id) in its "sides", else the
/// nearest to the face's center; -1 without probes.
[[nodiscard]] int probeForFace(const std::vector<CubemapProbe>& probes, int sideId, const Vec3& faceCenter);

/// Parallax correction (box projection): the direction from the probe's origin to the point
/// where the reflected ray (from `position` along `direction`) leaves the room box. Without a hit
/// (the point is outside the box) the direction stays as it is.
[[nodiscard]] Vec3 boxProjectedDirection(const Vec3& position, const Vec3& direction, const Vec3& probeOrigin, const Bounds& box);

/// The six faces of a probe, in the order the view stores them: +X, -X, +Y, -Y, +Z, -Z. Each is
/// rendered with a 90 degree camera looking along `cubeFaceForward` with `cubeFaceUp` up; right
/// is forward x up (as QMatrix4x4::lookAt and OpenGL's view space have it).
[[nodiscard]] Vec3 cubeFaceForward(int face);
[[nodiscard]] Vec3 cubeFaceUp(int face);

/// Where a direction lands on the cube: the face, and s/t in 0..1 (s to the face's right, t up).
struct CubeTexel {
	int face = 0;
	double s = 0.5, t = 0.5;
};
[[nodiscard]] CubeTexel cubeFaceCoordinates(const Vec3& direction);

} // namespace sme
