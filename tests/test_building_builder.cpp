// test_building_builder.cpp - Building builder: lots from selected brushes become buildings.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/building_builder.h>
#include <sme/checks.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/edge_round.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/text.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <fstream>
#include <set>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

SolidRef addLot(MapDocument& doc, const Bounds& b, const std::string& material = "BRICK/BRICKWALL001A") {
	auto [change, ref] = edit::createBlock(doc, b, material);
	(void)change;
	return ref;
}

BuildingOptions baseOptions() {
	BuildingOptions o = defaultBuildingOptions();
	o.facadeMaterial = "BRICK/BRICKWALL001A";
	o.windowMaterial = "GLASS/WINDOW001A";
	o.doorMaterial = "DOORS/DOOR001A";
	o.trimMaterial = "CONCRETE/CONCRETEFLOOR001A";
	o.roofMaterial = "ROOF/ROOF001A";
	return o;
}

const TextureSizeFn SIZE = [](const std::string& material) {
	if (iequals(material, "GLASS/WINDOW001A")) return std::pair<int, int>{128, 256};
	return std::pair<int, int>{256, 256};
};

BuildingPlan plan(const MapDocument& doc, const std::vector<SolidRef>& lots, const BuildingOptions& o) {
	MapDocument ids;
	ids.setIdCounters(doc.idCounters());
	return planBuildings(doc, ids, lots, o, SIZE);
}

void checkClean(const BuildingPlan& p) {
	const auto solids = p.resultSolids();
	for (const auto& s : solids) {
		INFO("brush " << s.id);
		CHECK(brushProblem(s).empty());
		for (const auto& side : s.sides) {
			INFO("side material " << side.material);
			CHECK_FALSE(textureEdgeOn(side));
		}
	}
	// No two pieces overlap (Z fighting, doubled brushes).
	int overlaps = 0;
	for (std::size_t i = 0; i < solids.size(); ++i) {
		for (std::size_t j = i + 1; j < solids.size(); ++j) {
			if (sharedVolume(solids[i], solids[j]) > 0.5) {
				++overlaps;
				const auto a = buildBrushGeometry(solids[i]).bounds, b = buildBrushGeometry(solids[j]).bounds;
				MESSAGE("overlap " << a.min.x << " " << a.min.y << " " << a.min.z << " - " << a.max.x << " " << a.max.y << " " << a.max.z << " with " << b.min.x << " "
				                   << b.min.y << " " << b.min.z << " - " << b.max.x << " " << b.max.y << " " << b.max.z << " vol " << sharedVolume(solids[i], solids[j]));
			}
		}
	}
	CHECK(overlaps == 0);
}

int countSides(const BuildingPlan& p, SideState state) {
	return static_cast<int>(std::count_if(p.sides.begin(), p.sides.end(), [&](const BuildingSideInfo& s) { return s.state == state; }));
}

} // namespace

TEST_CASE("building builder: window columns by count and by spacing, patterns, steps") {
	BuildingOptions o = baseOptions();
	o.cornerMargin = 48;
	o.layout = WindowLayout::Spacing;
	o.windowGap = 64;
	double w = 0;
	const auto spaced = windowColumns(512, o, 48, 0, &w);
	REQUIRE(spaced.size() == 4);
	CHECK(w == doctest::Approx(48));
	CHECK(spaced[0] == doctest::Approx(88));
	CHECK(spaced[3] == doctest::Approx(424));
	CHECK(spaced[1] - spaced[0] == doctest::Approx(112));
	o.layout = WindowLayout::Count;
	const auto counted = windowColumns(512, o, 48, 6, &w);
	REQUIRE(counted.size() == 6);
	CHECK(w <= 48);
	CHECK(counted.front() + counted.back() == doctest::Approx(512).epsilon(0.01));
	// A count that does not fit gets narrower windows, then fewer.
	const auto tight = windowColumns(256, o, 48, 40, &w);
	CHECK(tight.size() < 40);
	CHECK(w >= 8);
	CHECK(windowColumns(40, o, 48, 4).empty());
	CHECK(patternHasWindow("x.", 0));
	CHECK_FALSE(patternHasWindow("x.", 1));
	CHECK(patternHasWindow("x.", 2));
	CHECK(patternHasWindow("", 5));
	CHECK_FALSE(patternHasWindow("x..x", 6));
	CHECK(stepsFor(48) == std::pair<int, double>{3, 16});
	CHECK(stepsFor(18).first == 1);
	CHECK(stepsFor(19).first == 2);
	CHECK(stepsFor(40).second <= 18);
	CHECK(stepsFor(0).first == 0);
}

TEST_CASE("building builder: a box becomes a block with floors and windows exactly as set, clean pieces, mass world, details func_detail") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 512}));
	BuildingOptions o = baseOptions();
	o.layout = WindowLayout::Count;
	o.windowsPerSide = 4;
	const auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	REQUIRE(p.buildings.size() == 1);
	const auto& b = p.buildings.front();
	// 512 high: ground floor 160, then 128 each, 24 above the top floor -> 3 floors.
	CHECK(b.floors == 3);
	CHECK(b.top == doctest::Approx(512));
	CHECK(b.world.size() == 1);
	CHECK(b.detail.size() > 20);
	REQUIRE(p.sides.size() == 4);
	CHECK(countSides(p, SideState::Facade) == 4);
	for (const auto& s : p.sides) CHECK(s.windowsPerFloor == 4);
	// One door on the front (the longest side).
	CHECK(p.doors == 1);
	const auto front = std::find_if(p.sides.begin(), p.sides.end(), [](const BuildingSideInfo& s) { return s.front; });
	REQUIRE(front != p.sides.end());
	CHECK(front->length == doctest::Approx(512));
	CHECK(front->doors == 1);
	// Upper floors: 2 x 4 sides x 4; ground floor: 4 x 4 minus the door.
	CHECK(p.windows == 2 * 4 * 4 + 4 * 4 - 1);
	checkClean(p);
	// The mass seals: a world brush up to the walls' top, inside the wall layer.
	const auto massBounds = buildBrushGeometry(b.world.front()).bounds;
	CHECK(massBounds.max.z == doctest::Approx(512));
	CHECK(massBounds.min.x == doctest::Approx(o.windowDepth));
	// Window panels show their texture once (fitted): u scale = width / texture width.
	bool fitted = false;
	for (const auto& s : b.detail) {
		for (const auto& side : s.sides) {
			if (!iequals(side.material, "GLASS/WINDOW001A")) continue;
			const auto g = buildBrushGeometry(s).bounds;
			const double width = std::max(g.size().x, g.size().y);
			CHECK(side.u.scale * 128 == doctest::Approx(width).epsilon(0.02));
			CHECK(side.v.scale * 256 == doctest::Approx(g.size().z).epsilon(0.02));
			fitted = true;
		}
	}
	CHECK(fitted);
}

TEST_CASE("building builder: spacing, patterns, door places, per-side settings, presets") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 416}));
	BuildingOptions o = baseOptions();
	o.layout = WindowLayout::Spacing;
	o.windowGap = 64;
	o.pattern = "x.";
	auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	for (const auto& s : p.sides) {
		if (s.length > 500) CHECK(s.windowsPerFloor == 2);   // 4 columns, every second
	}
	// Two doors at the start of the front.
	o.pattern = "x";
	o.doorsPerSide = 2;
	o.doorPlace = DoorPlace::Start;
	p = plan(doc, {lot}, o);
	CHECK(p.doors == 2);
	// Per side: 2 windows on one side, a plain wall, a nodraw side.
	o.doorsPerSide = 1;
	SideSettings two;
	two.windows = 2;
	o.sideSettings[{0, 1}] = two;
	SideSettings plain;
	plain.state = SideState::Plain;
	o.sideSettings[{0, 2}] = plain;
	SideSettings none;
	none.state = SideState::Nodraw;
	o.sideSettings[{0, 3}] = none;
	p = plan(doc, {lot}, o);
	REQUIRE(p.sides.size() == 4);
	CHECK(p.sides[1].windowsPerFloor == 2);
	CHECK(p.sides[2].state == SideState::Plain);
	CHECK(p.sides[2].windowsPerFloor == 0);
	CHECK(p.sides[3].state == SideState::Nodraw);
	checkClean(p);
	// Presets: only the front gets a facade, the rest plain walls.
	o.sideSettings.clear();
	o.sides = BuildingSides::Front;
	p = plan(doc, {lot}, o);
	CHECK(countSides(p, SideState::Facade) == 1);
	CHECK(countSides(p, SideState::Plain) == 3);
	o.sides = BuildingSides::FrontAndSides;
	p = plan(doc, {lot}, o);
	CHECK(countSides(p, SideState::Facade) == 3);
	checkClean(p);
}

TEST_CASE("building builder: two touching lots share no facade, a slanted side builds clean") {
	MapDocument doc;
	const SolidRef a = addLot(doc, box({0, 0, 0}, {256, 256, 416}));
	const SolidRef b = addLot(doc, box({256, 0, 0}, {512, 256, 544}));
	BuildingOptions o = baseOptions();
	const auto p = plan(doc, {a, b}, o);
	REQUIRE(p.buildings.size() == 2);
	int shared = 0;
	for (const auto& s : p.sides) {
		if (!s.shared) continue;
		++shared;
		CHECK(s.state == SideState::Nodraw);
		CHECK(s.windowsPerFloor == 0);
	}
	CHECK(shared == 2);
	// Nothing sticks out between the two (no cornice or sill on the shared wall).
	for (const auto& s : p.resultSolids()) {
		const auto g = buildBrushGeometry(s).bounds;
		CHECK_FALSE((g.min.x > 250 && g.max.x < 262 && g.max.y > 1 && g.min.y < 255));
	}
	checkClean(p);

	// A lot with a slanted side (a trapezoid seen from above).
	MapDocument slanted;
	const auto solid = solidFromPoints(slanted, {{0, 0, 0}, {512, 0, 0}, {512, 256, 0}, {160, 256, 0}, {0, 0, 448}, {512, 0, 448}, {512, 256, 448}, {160, 256, 448}},
	                                   "BRICK/BRICKWALL001A");
	REQUIRE(solid);
	slanted.world.solids.push_back(*solid);
	const auto ps = plan(slanted, {SolidRef{0, solid->id}}, o);
	REQUIRE(ps.ok());
	CHECK(ps.sides.size() == 4);
	CHECK(ps.windows > 10);
	checkClean(ps);
}

TEST_CASE("building builder: facade only - any subset of sides, mitred corners, returns at open ends") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 384, 416}));
	BuildingOptions o = baseOptions();
	o.mode = BuildingMode::Facade;
	o.facadeThickness = 32;
	// Only the front.
	o.sides = BuildingSides::Front;
	auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	CHECK(countSides(p, SideState::Facade) == 1);
	CHECK(countSides(p, SideState::Open) == 3);
	REQUIRE(p.buildings.front().world.size() == 1);
	{
		const auto slab = buildBrushGeometry(p.buildings.front().world.front()).bounds;
		CHECK(std::min(slab.size().x, slab.size().y) == doctest::Approx(32 - o.windowDepth));
	}
	checkClean(p);
	// A U of three walls, open at the back, with returns of 64 at both open ends.
	o.sides = BuildingSides::FrontAndSides;
	o.returnDepth = 64;
	p = plan(doc, {lot}, o);
	CHECK(countSides(p, SideState::Facade) == 3);
	CHECK(countSides(p, SideState::Open) == 1);
	CHECK(p.buildings.front().world.size() == 5);   // three slabs, two returns
	checkClean(p);
	// The parapet sits on the walls only.
	for (const auto& s : p.buildings.front().detail) {
		const auto g = buildBrushGeometry(s).bounds;
		if (g.min.z >= 416 - 0.01) CHECK(std::min(g.size().x, g.size().y) <= o.parapetThickness + 0.01);
	}
}

TEST_CASE("building builder: height, floors and variation stay consistent; raised ground floor with walkable steps; half sunk") {
	MapDocument doc;
	std::vector<SolidRef> lots;
	for (int i = 0; i < 3; ++i) lots.push_back(addLot(doc, box({i * 384.0, 0, 0}, {i * 384.0 + 320, 256, 256})));
	BuildingOptions o = baseOptions();
	o.heightMode = HeightMode::Floors;
	o.floors = 5;
	auto p = plan(doc, lots, o);
	for (const auto& b : p.buildings) {
		CHECK(b.floors == 5);
		CHECK(b.top == doctest::Approx(o.groundFloorHeight + 4 * o.floorHeight + o.topWall));
	}
	o.heightMode = HeightMode::Height;
	o.height = 700;
	p = plan(doc, lots, o);
	for (const auto& b : p.buildings) {
		CHECK(b.top == doctest::Approx(700));
		CHECK(b.floors == 1 + static_cast<int>((700 - o.groundFloorHeight - o.topWall) / o.floorHeight));
	}
	// Variation: reproducible with the same seed, different with another.
	o.heightMode = HeightMode::Floors;
	o.floorVariation = 2;
	o.seed = 5;
	const auto v1 = describeBuildingPlan(plan(doc, lots, o));
	CHECK(v1 == describeBuildingPlan(plan(doc, lots, o)));
	bool differs = false;
	for (std::uint32_t seed = 6; seed < 16 && !differs; ++seed) {
		o.seed = seed;
		differs = describeBuildingPlan(plan(doc, lots, o)) != v1;
	}
	CHECK(differs);
	// Per lot: 2 floors on the middle one.
	o.floorVariation = 0;
	o.lotSettings[1].floors = 2;
	p = plan(doc, lots, o);
	CHECK(p.buildings[1].floors == 2);
	CHECK(p.buildings[0].floors == 5);
	o.lotSettings.clear();

	// Raised ground floor: 48 up, three steps of 16 in front of the door.
	o.groundLevel = GroundLevel::Raised;
	o.groundRaise = 48;
	o.doorsPerSide = 1;
	p = plan(doc, {lots.front()}, o);
	CHECK(p.doors == 1);
	CHECK(p.steps == 3);
	CHECK(p.buildings.front().top == doctest::Approx(48 + o.groundFloorHeight + 4 * o.floorHeight + o.topWall));
	// The steps: tops 16 apart, the highest at the door's floor.
	std::vector<double> tops;
	for (const auto& s : p.buildings.front().detail) {
		const auto g = buildBrushGeometry(s).bounds;
		if (g.max.z <= 48.01 && g.max.z > 1 && std::abs(std::min(g.size().x, g.size().y) - o.stepDepth) < 0.01) tops.push_back(g.max.z);
	}
	std::sort(tops.begin(), tops.end());
	REQUIRE(tops.size() == 3);
	CHECK(tops[0] == doctest::Approx(16));
	CHECK(tops[2] == doctest::Approx(48));
	checkClean(p);
	// Half sunk: the mass goes below the street.
	o.groundLevel = GroundLevel::HalfSunk;
	p = plan(doc, {lots.front()}, o);
	CHECK(p.buildings.front().foundation < -40);
	checkClean(p);
}

TEST_CASE("building builder: roofs - flat with parapet, gable, hip - each clean") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 416}));
	BuildingOptions o = baseOptions();
	for (const auto roof : {BuildingRoof::Flat, BuildingRoof::Gable, BuildingRoof::Hip}) {
		o.roof = roof;
		const auto p = plan(doc, {lot}, o);
		REQUIRE(p.ok());
		double highest = 0;
		int roofFaces = 0;
		for (const auto& s : p.resultSolids()) {
			highest = std::max(highest, buildBrushGeometry(s).bounds.max.z);
			for (const auto& side : s.sides) roofFaces += iequals(side.material, "ROOF/ROOF001A") ? 1 : 0;
		}
		if (roof == BuildingRoof::Flat) {
			CHECK(highest == doctest::Approx(416 + o.parapetHeight));
		} else {
			CHECK(highest > 416 + 50);
			CHECK(roofFaces >= (roof == BuildingRoof::Gable ? 2 : 4));
		}
		checkClean(p);
	}
}

TEST_CASE("building builder: props on the facade grid - turned outward, moved out by their box, counted as set") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 512}));
	BuildingOptions o = baseOptions();
	o.layout = WindowLayout::Count;
	o.windowsPerSide = 4;
	FacadeProp ac;
	ac.model = "models/props_c17/air_conditioner01.mdl";
	ac.anchor = FacadePropAnchor::BelowWindow;
	o.props.push_back(ac);
	FacadeProp roof;
	roof.model = "models/props_roof/vent01.mdl";
	roof.anchor = FacadePropAnchor::Roof;
	roof.spacing = 128;
	o.props.push_back(roof);
	o.modelBounds = [](const std::string& model) -> std::optional<Bounds> {
		if (model.find("air_conditioner") != std::string::npos) return box({-8, -16, -12}, {8, 16, 12});
		return box({-16, -16, 0}, {16, 16, 32});
	};
	const auto p = plan(doc, {lot}, o);
	int below = 0, onRoof = 0;
	for (const auto& prop : p.props) {
		if (prop.model.find("air_conditioner") != std::string::npos) {
			++below;
			// Facing out, the back touching the wall: 8 units out.
			const double yaw = prop.placement.yaw * std::numbers::pi / 180.0;
			const Vec3 n{std::cos(yaw), std::sin(yaw), 0};
			const Vec3 at = prop.placement.origin;
			if (n.x > 0.5) CHECK(at.x == doctest::Approx(512 + 8));
			if (n.x < -0.5) CHECK(at.x == doctest::Approx(-8));
			if (n.y > 0.5) CHECK(at.y == doctest::Approx(256 + 8));
			if (n.y < -0.5) CHECK(at.y == doctest::Approx(-8));
		} else {
			++onRoof;
			CHECK(prop.placement.origin.z == doctest::Approx(512));
		}
	}
	CHECK(below == 2 * 4 * 4);   // under each upper window
	CHECK(onRoof > 0);
	// Every second with half the chance: fewer, and the same again with the same seed.
	o.props[0].every = 2;
	o.props[0].chance = 50;
	const auto q = plan(doc, {lot}, o);
	const auto count = std::count_if(q.props.begin(), q.props.end(), [](const BuildingProp& b) { return b.model.find("air_conditioner") != std::string::npos; });
	CHECK(count < 16);
	CHECK(describeBuildingPlan(q) == describeBuildingPlan(plan(doc, {lot}, o)));
	// A whole lot as one model.
	o.mode = BuildingMode::Model;
	o.lotModel = "models/props_buildings/row_res_1.mdl";
	const auto m = plan(doc, {lot}, o);
	REQUIRE(m.props.size() == 1);
	CHECK(m.brushCount() == 0);
	CHECK(m.replacedRefs().size() == 1);
}

TEST_CASE("building builder: damage - lost floors, jagged top, broken bays with models, interior behind; clean") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 672}));
	BuildingOptions o = baseOptions();
	o.interiorMaterial = "PLASTER/PLASTERWALL001A";
	o.damage = 1;
	o.damageSeed = 3;
	FacadeProp broken;
	broken.model = "models/props_debris/building_brokenexterior001a.mdl";
	broken.anchor = FacadePropAnchor::Bay;
	broken.when = PropCondition::Damaged;
	broken.chance = 60;
	o.props.push_back(broken);
	o.modelBounds = [](const std::string&) -> std::optional<Bounds> { return box({-13, -64, 0}, {13, 64, 128}); };
	const auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	const auto& b = p.buildings.front();
	const auto massTop = buildBrushGeometry(b.world.front()).bounds.max.z;
	CHECK(massTop < b.top - 100);
	bool interior = false;
	for (const auto& side : b.world.front().sides) interior = interior || iequals(side.material, "PLASTER/PLASTERWALL001A");
	CHECK(interior);
	CHECK_FALSE(p.props.empty());
	// Broken panels stand on a floor.
	for (const auto& prop : p.props) CHECK(std::fmod(prop.placement.origin.z - o.groundFloorHeight, o.floorHeight) == doctest::Approx(0));
	checkClean(p);
	// Intact: none of that.
	o.damage = 0;
	const auto q = plan(doc, {lot}, o);
	CHECK(q.props.empty());
	CHECK(buildBrushGeometry(q.buildings.front().world.front()).bounds.max.z == doctest::Approx(672));
}

TEST_CASE("building builder: displacements - foundation down to the terrain, bulging walls and a sagging roof on their own four-cornered brushes") {
	MapDocument doc;
	History history;
	// Terrain: a displacement floor under the lot, sunk to -40.
	auto [ground, groundRef] = edit::createBlock(doc, box({-256, -256, -64}, {768, 512, 0}), "NATURE/GRASS01");
	history.push(std::move(ground));
	FaceRef top{};
	for (const auto& face : buildBrushGeometry(*doc.findSolid(groundRef)).faces) {
		if (face.normal.z > 0.9) top = {groundRef, doc.findSolid(groundRef)->sides[static_cast<std::size_t>(face.sideIndex)].id};
	}
	history.push(edit::createDisplacements(doc, {top}, 2));
	{
		Transaction t(doc, "Sculpt");
		t.touchSolid(groundRef);
		for (auto& side : doc.findSolid(groundRef)->sides) {
			if (side.id != top.sideId) continue;
			auto d = readDisplacement(side);
			REQUIRE(d);
			for (auto& dist : d->distances) dist = -40;
			storeDisplacement(side, *d);
		}
		history.push(t.finish());
	}
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 416}));
	const auto terrainBrushes = displacementBrushesWithHiddenFaces(doc).size();   // the terrain itself (grass sides)
	BuildingOptions o = baseOptions();
	o.sides = BuildingSides::Front;
	o.wallDisplacement = true;
	o.roofDisplacement = true;
	o.displacementStrength = 6;
	const auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	CHECK(p.buildings.front().foundation <= -40);
	CHECK(p.displacements >= 4);   // three plain walls (between the bands) and the roof
	checkClean(p);
	// Applied: no displacement problem the checks know.
	auto [change, selection] = edit::applyBuildings(doc, p);
	CHECK_FALSE(change.empty());
	CHECK(nonQuadDisplacements(doc).empty());
	CHECK(oversizedDisplacements(doc).empty());
	CHECK(displacementBrushesWithHiddenFaces(doc).size() == terrainBrushes);
}

TEST_CASE("building builder: applied as one undo step; the lot goes, a func_detail per building, props as prop_static; checks stay quiet") {
	MapDocument doc;
	History history;
	auto [c1, lot] = edit::createBlock(doc, box({0, 0, 0}, {512, 256, 416}), "BRICK/BRICKWALL001A");
	history.push(std::move(c1));
	const std::string before = doc.serialize();
	BuildingOptions o = baseOptions();
	FacadeProp ac;
	ac.model = "models/props_c17/air_conditioner01.mdl";
	o.props.push_back(ac);
	MapDocument ids;
	ids.setIdCounters(doc.idCounters());
	const auto p = planBuildings(doc, ids, {lot}, o, SIZE);
	REQUIRE(p.ok());
	auto [change, selection] = edit::applyBuildings(doc, p);
	history.push(std::move(change));
	CHECK_FALSE(doc.findSolid(lot));
	int details = 0, props = 0;
	for (const auto& e : doc.entities) {
		details += iequals(e.classname(), "func_detail") ? 1 : 0;
		props += iequals(e.classname(), "prop_static") ? 1 : 0;
	}
	CHECK(details == 1);
	CHECK(props == static_cast<int>(p.props.size()));
	CHECK(selection.solids.size() == 1);
	// Ids are unique.
	std::set<int> ids2;
	for (const auto& s : doc.world.solids) CHECK(ids2.insert(s.id).second);
	for (const auto& e : doc.entities) {
		CHECK(ids2.insert(e.id).second);
		for (const auto& s : e.solids) CHECK(ids2.insert(s.id).second);
	}
	CheckContext context;
	for (const auto& problem : checkMap(doc, context)) {
		CHECK(problem.kind != Problem::Kind::EdgeOnTexture);
		CHECK(problem.kind != Problem::Kind::FlatDisplacementBrush);
		CHECK(problem.kind != Problem::Kind::DisplacementHiddenFaces);
		CHECK(problem.kind != Problem::Kind::InvalidBrush);
		CHECK(problem.kind != Problem::Kind::DuplicateBrush);
	}
	history.undo(doc);
	CHECK(doc.serialize() == before);
	history.redo(doc);
	const std::string text = doc.serialize();
	CHECK(MapDocument::parse(text).serialize() == text);
}

TEST_CASE("building builder: a brush of the map in the way cuts the piece, never the other way round") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 416}));
	// A sidewalk the plinth and steps would reach into.
	const SolidRef walk = addLot(doc, box({-128, -128, 0}, {640, 0, 8}), "CONCRETE/CONCRETEFLOOR001A");
	BuildingOptions o = baseOptions();
	o.groundLevel = GroundLevel::Raised;
	const auto p = plan(doc, {lot}, o);
	REQUIRE(p.ok());
	const Solid* sidewalk = doc.findSolid(walk);
	REQUIRE(sidewalk);
	for (const auto& s : p.resultSolids()) CHECK(sharedVolume(s, *sidewalk) <= 1.0);
	CHECK(p.clipped > 0);
	checkClean(p);
}

TEST_CASE("building builder: material and model suggestions by name") {
	const std::vector<std::string> materials{"brick/brickwall001a", "glass/window001a", "tools/toolsnodraw", "roof/roofshingle01", "doors/door_metal01",
	                                         "concrete/concretefloor001a", "plaster/plasterwall001a", "glass/window_broken01", "decals/brick"};
	CHECK(buildingMaterialSuggestions(materials, BuildingMaterialRole::Facade).front() == "brick/brickwall001a");
	CHECK(buildingMaterialSuggestions(materials, BuildingMaterialRole::Window).front() == "glass/window001a");
	CHECK(buildingMaterialSuggestions(materials, BuildingMaterialRole::BrokenWindow).front() == "glass/window_broken01");
	CHECK(buildingMaterialSuggestions(materials, BuildingMaterialRole::Door).front() == "doors/door_metal01");
	CHECK(buildingMaterialSuggestions(materials, BuildingMaterialRole::Roof).front() == "roof/roofshingle01");
	for (const auto& name : buildingMaterialSuggestions(materials, BuildingMaterialRole::Facade, 20)) CHECK_FALSE(startsWithI(name, "tools/"));
	const std::vector<std::string> models{"models/props_debris/building_brokenexterior001a.mdl", "models/props_c17/air_conditioner01.mdl",
	                                      "models/props_buildings/row_res_1.mdl", "models/props_debris/building_brokenpillar001c.mdl",
	                                      "models/props_debris/building_brokenwindow001a.mdl"};
	CHECK(buildingModelSuggestions(models, FacadePropAnchor::Bay, true).front() == "models/props_debris/building_brokenexterior001a.mdl");
	CHECK(buildingModelSuggestions(models, FacadePropAnchor::BelowWindow).front() == "models/props_c17/air_conditioner01.mdl");
	CHECK(buildingModelSuggestions(models, FacadePropAnchor::Corner, true).front() == "models/props_debris/building_brokenpillar001c.mdl");
	CHECK(buildingModelSuggestions(models, FacadePropAnchor::Window, true).front() == "models/props_debris/building_brokenwindow001a.mdl");
	CHECK(buildingLotModelSuggestions(models).front() == "models/props_buildings/row_res_1.mdl");
}

TEST_CASE("building builder: a side is found from a click near it") {
	MapDocument doc;
	const SolidRef lot = addLot(doc, box({0, 0, 0}, {512, 256, 416}));
	const auto p = plan(doc, {lot}, baseOptions());
	const int i = p.sideAt({256, -10, 100});
	REQUIRE(i >= 0);
	CHECK(p.sides[static_cast<std::size_t>(i)].normal.y < -0.9);
	CHECK(p.sideAt({256, 128, 100}, 64) == -1);   // the middle of the roof is no side
}

TEST_CASE("building builder: a block of buildings plans quickly") {
	MapDocument doc;
	std::vector<SolidRef> lots;
	for (int i = 0; i < 8; ++i) lots.push_back(addLot(doc, box({i * 320.0, 0, 0}, {i * 320.0 + 320, 384, 544})));
	BuildingOptions o = baseOptions();
	o.layout = WindowLayout::Count;
	o.windowsPerSide = 4;
	const auto t0 = std::chrono::steady_clock::now();
	const auto p = plan(doc, lots, o);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	REQUIRE(p.buildings.size() == 8);
#ifdef NDEBUG
	constexpr double bound = 3000;
#else
	constexpr double bound = 20000;
#endif
	CHECK(ms < bound);
	// Neighbours share their side walls.
	int shared = 0;
	for (const auto& s : p.sides) shared += s.shared ? 1 : 0;
	CHECK(shared == 14);
	checkClean(p);
}

// Writes a demo street for screenshots (skipped: run with --no-skip, SME_BUILDING_DEMO=<file>
// and optionally SME_BUILDING_ROOF / _LEVEL / _DAMAGE / _MODE).
TEST_CASE("building builder: demo street for screenshots" * doctest::skip()) {
	const char* out = std::getenv("SME_BUILDING_DEMO");
	if (!out) return;
	const auto env = [](const char* name, double fallback) {
		const char* v = std::getenv(name);
		return v ? std::atof(v) : fallback;
	};
	MapDocument doc;
	(void)edit::createBlock(doc, box({-2048, -2048, -64}, {3072, 2048, 0}), "DEMO/CONCRETEWALL001");
	std::vector<SolidRef> row;
	double x = 0;
	for (double w : {384.0, 320.0, 448.0, 352.0}) {
		row.push_back(addLot(doc, box({x, 0, 0}, {x + w, 384, 544}), "DEMO/BRICKWALL001"));
		x += w;
	}
	// A curved corner: wedge lots round a quarter circle.
	std::vector<SolidRef> curve;
	const double r0 = 256, r1 = 640;
	const int n = 6;
	for (int i = 0; i < n; ++i) {
		const double a0 = std::numbers::pi * i / (2 * n), a1 = std::numbers::pi * (i + 1) / (2 * n);
		const Vec3 c{-200, -400, 0};
		std::vector<Vec3> pts;
		for (double z : {0.0, 416.0}) {
			pts.push_back(c + Vec3{-std::cos(a0) * r0, -std::sin(a0) * r0, z});
			pts.push_back(c + Vec3{-std::cos(a1) * r0, -std::sin(a1) * r0, z});
			pts.push_back(c + Vec3{-std::cos(a1) * r1, -std::sin(a1) * r1, z});
			pts.push_back(c + Vec3{-std::cos(a0) * r1, -std::sin(a0) * r1, z});
		}
		auto s = solidFromPoints(doc, pts, "DEMO/BRICKWALL001");
		REQUIRE(s);
		doc.world.solids.push_back(*s);
		curve.push_back({0, s->id});
	}
	BuildingOptions o = defaultBuildingOptions();
	o.facadeMaterial = "DEMO/BRICKWALL001";
	o.facadeVariants = {"DEMO/CONCRETEWALL001"};
	o.windowMaterial = "DEMO/GLASS001";
	o.doorMaterial = "DEMO/WOOD001";
	o.trimMaterial = "DEMO/CONCRETEWALL001";
	o.roofMaterial = "DEMO/METAL001";
	o.groundMaterial = "DEMO/CONCRETEWALL001";
	o.interiorMaterial = "DEMO/WOOD001";
	o.groundWindows = GroundWindows::ShopWindows;
	o.sides = BuildingSides::Street;
	o.floorVariation = 1;
	o.seed = 3;
	o.mode = static_cast<BuildingMode>(static_cast<int>(env("SME_BUILDING_MODE", 0)));
	o.roof = static_cast<BuildingRoof>(static_cast<int>(env("SME_BUILDING_ROOF", 0)));
	o.groundLevel = static_cast<GroundLevel>(static_cast<int>(env("SME_BUILDING_LEVEL", 0)));
	o.damage = env("SME_BUILDING_DAMAGE", 0);
	const TextureSizeFn size = [](const std::string&) { return std::pair<int, int>{128, 128}; };
	for (auto* lots : {&row, &curve}) {
		BuildingOptions use = o;
		if (lots == &curve) {
			use.groundWindows = GroundWindows::SameAsUpper;
			use.floorVariation = 0;
			use.roof = BuildingRoof::Flat;
		}
		MapDocument ids;
		ids.setIdCounters(doc.idCounters());
		const auto p = planBuildings(doc, ids, *lots, use, size);
		MESSAGE(describeBuildingPlan(p));
		(void)edit::applyBuildings(doc, p);
		doc.updateIdCounters();
	}
	std::ofstream(out, std::ios::binary) << doc.serialize();
}

TEST_CASE("building builder: settings and outline stay with the building - found again after save and load, rebuilt with new settings and size, unlinked") {
	MapDocument doc;
	History history;
	auto [c1, lot] = edit::createBlock(doc, box({0, 0, 0}, {512, 256, 416}), "BRICK/BRICKWALL001A");
	history.push(std::move(c1));
	BuildingOptions o = baseOptions();
	o.layout = WindowLayout::Count;
	o.windowsPerSide = 3;
	o.pattern = "x.x";
	FacadeProp ac;
	ac.model = "models/props_c17/air_conditioner01.mdl";
	ac.every = 2;
	o.props.push_back(ac);
	SideSettings side;
	side.windows = 5;
	o.sideSettings[{0, 1}] = side;
	o.textures[static_cast<std::size_t>(BuildingPart::Facade)].fit = TextureFit::Floor;
	o.textures[static_cast<std::size_t>(BuildingPart::Facade)].rotation = 90;
	o.facadeVariants = {"BRICK/A;B", "CONCRETE/X"};
	// The settings as text and back: every field.
	CHECK(encodeBuildingOptions(decodeBuildingOptions(encodeBuildingOptions(o))) == encodeBuildingOptions(o));
	CHECK(decodeBuildingOptions("buildingWindowCount=7&unknown=1").windowsPerSide == 7);
	o.facadeVariants.clear();
	{
		MapDocument ids;
		ids.setIdCounters(doc.idCounters());
		auto [change, selection] = edit::applyBuildings(doc, planBuildings(doc, ids, {lot}, o, SIZE));
		history.push(std::move(change));
	}
	// Found again, also after saving and loading.
	const auto reloaded = MapDocument::parse(doc.serialize());
	for (const MapDocument* d : std::initializer_list<const MapDocument*>{&doc, &reloaded}) {
		const auto records = buildingsInMap(*d);
		REQUIRE(records.size() == 1);
		const auto& r = records.front();
		CHECK(r.outline.size() == 4);
		CHECK(r.bottom == doctest::Approx(0));
		CHECK(r.top == doctest::Approx(416));
		CHECK(r.options.windowsPerSide == 3);
		CHECK(r.options.pattern == "x.x");
		CHECK(r.options.sideSettings.at({0, 1}).windows == 5);
		CHECK(r.options.props.size() == 1);
		CHECK(r.members.solids.size() == 1);    // the mass
		CHECK(r.members.entities.size() >= 2);  // the func_detail and the props
		// Any of its pieces finds the whole building.
		Selection one;
		one.solids.push_back(r.members.solids.front());
		const auto id = buildingIdsOf(*d, one);
		REQUIRE(id.size() == 1);
		CHECK(buildingMembers(*d, id).entities.size() == r.members.entities.size());
	}
	// Edited: more windows, longer, lower - one undo step, the old pieces gone.
	auto records = buildingsInMap(doc);
	const auto before = doc.serialize();
	const auto [length, depth] = outlineSize(records.front().outline);
	CHECK(length == doctest::Approx(512));
	CHECK(depth == doctest::Approx(256));
	BuildingOptions edited = records.front().options;
	edited.windowsPerSide = 6;
	edited.heightMode = HeightMode::Height;
	edited.height = 300;
	records.front().options = mergeBuildingOptions(records.front().options, edited, {"buildingWindowCount", "buildingHeightMode", "buildingHeight"});
	CHECK(records.front().options.pattern == "x.x");   // untouched keys stay
	records.front().outline = resizeOutline(records.front().outline, 768, 256);
	CHECK(outlineSize(records.front().outline).first == doctest::Approx(768));
	MapDocument ids;
	ids.setIdCounters(doc.idCounters());
	const auto rebuilt = planRebuild(doc, ids, records, SIZE);
	REQUIRE(rebuilt.ok());
	CHECK(rebuilt.buildings.front().top == doctest::Approx(300));
	CHECK_FALSE(rebuilt.removeSolids.empty());
	auto [change, selection] = edit::applyBuildings(doc, rebuilt);
	history.push(std::move(change));
	const auto after = buildingsInMap(doc);
	REQUIRE(after.size() == 1);
	CHECK(after.front().options.windowsPerSide == 6);
	CHECK(outlineSize(after.front().outline).first == doctest::Approx(768));
	// No piece of the old building is left.
	std::set<int> detailCount;
	for (const auto& e : doc.entities) {
		if (iequals(e.classname(), "func_detail")) detailCount.insert(e.id);
	}
	CHECK(detailCount.size() == 1);
	CHECK(doc.world.solids.size() == 1);
	history.undo(doc);
	CHECK(doc.serialize() == before);
	history.redo(doc);
	// Unlinked: plain brushes and props, no record.
	history.push(edit::unlinkBuildings(doc, {after.front().id}));
	CHECK(buildingsInMap(doc).empty());
	CHECK(doc.world.solids.size() == 1);
}

TEST_CASE("building builder: touching lots as one building (a curve) - one material, the doors on one front, the texture running on round the curve") {
	MapDocument doc;
	std::vector<SolidRef> lots;
	const double r0 = 256, r1 = 640;
	const int n = 6;
	for (int i = 0; i < n; ++i) {
		const double a0 = std::numbers::pi * i / (2 * n), a1 = std::numbers::pi * (i + 1) / (2 * n);
		std::vector<Vec3> pts;
		for (double z : {0.0, 416.0}) {
			pts.push_back({-std::cos(a0) * r0, -std::sin(a0) * r0, z});
			pts.push_back({-std::cos(a1) * r0, -std::sin(a1) * r0, z});
			pts.push_back({-std::cos(a1) * r1, -std::sin(a1) * r1, z});
			pts.push_back({-std::cos(a0) * r1, -std::sin(a0) * r1, z});
		}
		auto s = solidFromPoints(doc, pts, "BRICK/BRICKWALL001A");
		REQUIRE(s);
		doc.world.solids.push_back(*s);
		lots.push_back({0, s->id});
	}
	BuildingOptions o = baseOptions();
	o.facadeVariants = {"CONCRETE/WALL01", "PLASTER/WALL02"};
	o.touchingIsOne = true;
	auto p = plan(doc, lots, o);
	REQUIRE(p.buildings.size() == 6);
	CHECK(p.doors == 1);
	std::set<std::string> materials;
	for (const auto& b : p.buildings) materials.insert(b.facadeMaterial);
	CHECK(materials.size() == 1);
	checkClean(p);
	// The outer facade's texture: at the joint of two pieces both have the same u there.
	int joints = 0;
	for (std::size_t i = 0; i + 1 < p.buildings.size(); ++i) {
		const double a = std::numbers::pi * (i + 1) / (2 * n);
		const Vec3 joint{-std::cos(a) * r1, -std::sin(a) * r1, 200};
		std::vector<double> us;
		for (std::size_t k : {i, i + 1}) {
			for (const auto& s : p.buildings[k].detail) {
				for (const auto& side : s.sides) {
					if (!iequals(side.material, p.buildings[k].facadeMaterial)) continue;
					const Plane plane = side.plane();
					if (std::abs(plane.distanceTo(joint)) > 0.01 || plane.normal.z != doctest::Approx(0)) continue;
					if (dot(plane.normal, normalize(Vec3{joint.x, joint.y, 0})) < 0.9) continue;
					us.push_back(textureCoordinate(side, joint).u);
					break;
				}
				if (us.size() == (k == i ? 1u : 2u)) break;
			}
		}
		if (us.size() == 2) {
			++joints;
			const double r = std::fmod(std::abs(us[0] - us[1]), 256.0);
			CHECK(std::min(r, 256.0 - r) < 0.01);
		}
	}
	CHECK(joints >= 3);
	// Separate houses (a row): each its own front door.
	o.touchingIsOne = false;
	p = plan(doc, lots, o);
	CHECK(p.doors == 6);
}
