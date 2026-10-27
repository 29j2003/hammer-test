// shapes.h - Brush shapes of the block tool: block, wedge, cylinder, spike (pyramid / cone),
// sphere, arch and stairs, all fitted into the box the mapper dragged. Like Hammer's
// primitives, but with the settings kept so a shape can be changed right after creating it.

#pragma once

#include <string>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

struct ShapeOptions {
	enum class Kind { Block, Wedge, Cylinder, Spike, Sphere, Arch, Stairs };
	Kind kind = Kind::Block;
	int sides = 8;              ///< Cylinder, spike, sphere: sides around; arch: number of pieces
	double arc = 180;           ///< Arch: degrees the arch covers
	double startAngle = 0;      ///< Arch: where it starts (0 = +X, counter-clockwise seen from above)
	double wallWidth = 16;      ///< Arch: thickness of the ring
	double addHeight = 0;       ///< Arch: each piece this much higher than the one before (spiral)
	int steps = 8;              ///< Stairs: number of steps (rising towards +Y)
};

/// Readable name ("arch").
[[nodiscard]] std::string shapeName(ShapeOptions::Kind kind);

/// Brushes for the shape inside `box` (new ids from the document, world-aligned textures).
/// Empty if the shape does not fit (box too small for the settings).
[[nodiscard]] std::vector<Solid> makeShape(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& material);

} // namespace sme
