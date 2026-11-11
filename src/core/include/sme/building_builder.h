// building_builder.h - Building builder: turns selected brushes into buildings (no interiors).
//
// Each selected brush is one lot: its outline seen from above (any convex shape, slanted sides
// too) and its height. Lots that touch form a row or a block: the sides they share get no facade.
// What is built, the way a mapper builds a facade by hand in Hammer:
// - Block: a solid mass (world brush: it seals and blocks vis) with the facades in front of it.
//   Facade only: a slab of `facadeThickness` behind each side that has a wall - any subset of
//   sides (one front, a U of three for a backdrop ...); corners between two walls are mitred, an
//   open end gets an end cap and, if wanted, a return (the wall turns the corner for a bit).
// - Every side has a state: Facade (windows, doors, bands), Plain wall (the facade material, no
//   openings), Open (no wall: facade only - nothing; block - the mass shows nodraw there), or
//   Nodraw. "Auto" decides by the preset: all outer sides, the sides facing the street, only the
//   front (the longest street side) or the front and the two beside it. Every side can also have
//   its own windows (count / spacing / pattern / doors).
// - A facade: a wall layer `windowDepth` thick in front of the mass, cut around the window and
//   door openings into few clean pieces (bands between the window rows, piers between the
//   windows; pieces of the same width stacked are one brush). At the back of each opening a thin
//   panel with the window (door) material, its texture fitted to that opening (0..1).
// - Ground floor: at street level, raised (Hochparterre: lifted by `groundRaise`, steps in front of
//   each door - never higher than 18 units each, so players walk up - optional small basement
//   windows in the socle) or half sunk (its floor below the street). Shop windows or none instead
//   of normal windows. Cornices between the floors, a top cornice, a plinth, sills (optional).
// - Roof: flat (the mass's top, optional parapet), gable or hip (one brush: the outline with
//   overhang, planes rising from the eaves at the roof angle).
// - Height: from the lot brush, set directly, or from the floors (height = ground floor + floors
//   above + the top); per lot overrides and random variation (seed) of floors and material.
// - Displacements: a lot standing on displacement terrain (or being one) gets a foundation down to
//   the lowest ground under it. Plain walls and a flat four-cornered roof can get a displacement
//   surface (bulge / sag), each on its own thin brush whose other faces are nodraw, four-cornered,
//   with a lightmap scale VBSP accepts.
// - Condition: intact ... destroyed. Damage removes floors and leaves a jagged top (each bay a
//   different height), breaks windows (panel material, props), and puts props at the base; bays
//   can be replaced by broken facade models (their box decides how many bays and floors they
//   take). Where the inside shows, the mass gets the interior material.
// - Props (prop_static) from the game's models on the facade grid: per window, below/above it, at
//   the doors, at the corners, in a bay, at the base or on a flat roof; every N-th, with a chance,
//   intact or damaged only; turned outward and moved out by the model's box.
// - Textures: every part (walls, ground floor, windows, doors, trim, cornices, roof, interior) has
//   its own alignment: scale and shift, rotation, or fitted to the floor height, to the window bay
//   or to the opening. Facade textures run on along each side; never edge-on.
// The mass is world; every other piece goes into one func_detail per building. Hidden faces are
// nodraw. Pieces that would reach into a brush of the map are cut around it.
// The plan is computed without changing the map (live preview); applying it is one undo step.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/prop_placement.h>
#include <sme/road_texture.h>

namespace sme {

enum class BuildingMode { Block, Facade, Model };
/// Which sides get a facade when a side is "Auto".
enum class BuildingSides { AllOuter, Street, Front, FrontAndSides };
/// What a side gets.
enum class SideState { Auto, Facade, Plain, Open, Nodraw };
enum class BuildingRoof { Flat, Gable, Hip };
/// How windows are made: recessed in a wall layer (panel at the back), as panels on the flat wall
/// (fewer brushes), or none (plain walls; props can still hang there).
enum class WindowStyle { Recessed, Panels, None };
/// Windows of a floor on a side: an exact count, or as many as fit at the spacing.
enum class WindowLayout { Count, Spacing };
/// The ground floor's level: at the street, raised (with steps to the doors), half sunk.
enum class GroundLevel { Street, Raised, HalfSunk };
/// The ground floor's openings.
enum class GroundWindows { SameAsUpper, ShopWindows, None };
/// Which facade sides get the doors.
enum class DoorSides { Longest, Every, None };
/// Where on the ground floor the doors go (among its window slots).
enum class DoorPlace { Middle, Spread, Start, End };
/// Where the height comes from.
enum class HeightMode { Lot, Height, Floors };

/// Where a facade prop goes.
enum class FacadePropAnchor {
	Window,        ///< In each window opening (shutters, frames, broken windows)
	BelowWindow,   ///< Hanging under each window (AC units, flower boxes)
	AboveWindow,   ///< Over each window (awnings, lamps)
	Door,          ///< At each door (door models, signs)
	Corner,        ///< At the bottom of each facade corner (drainpipes, pillars)
	Bay,           ///< Replaces a bay of a floor (broken facade panels): the wall there is left open
	Ground,        ///< Along the base of the facades (rubble, bins)
	Roof           ///< On a flat roof, spread out (vents, AC units)
};
/// When a prop is placed.
enum class PropCondition { Always, Intact, Damaged };

/// One kind of prop on the facades.
struct FacadeProp {
	std::string model;                      ///< models/....mdl
	FacadePropAnchor anchor = FacadePropAnchor::BelowWindow;
	int every = 1;                          ///< Every N-th place (1: all)
	double chance = 100;                    ///< Percent of those places, chosen by the seed
	PropCondition when = PropCondition::Always;   ///< Damaged: only broken windows / where damage is (chance x damage)
	bool groundFloor = false;               ///< Windows: also on the ground floor
	bool upperFloors = true;                ///< Windows: on the floors above
	double wallOffset = 0;                  ///< Extra units out from the wall (after the model's box)
	double up = 0;                          ///< Extra units up
	double yaw = 0;                         ///< Extra turn (a model facing sideways: 90)
	double spacing = 128;                   ///< Ground / roof: units between two places
};

/// How a part's texture is laid.
enum class TextureFit {
	Scale,     ///< Scale and shift as set (along the side, from its start)
	Floor,     ///< One tile per floor height (width keeps the texture's aspect)
	Bay,       ///< One tile per window bay and floor
	Opening    ///< Exactly once over each opening (windows, doors)
};
struct PartTexture {
	TextureFit fit = TextureFit::Scale;
	double scale = 0.25;                    ///< Units per texel (Scale)
	double shiftU = 0, shiftV = 0;          ///< Texels
	double rotation = 0;                    ///< Degrees in the face
};
/// The parts with their own texture settings.
enum class BuildingPart { Facade, Ground, Window, Door, Trim, Cornice, Roof, Interior, Count };

/// What one side gets (overrides; zero / empty: the building's setting).
struct SideSettings {
	SideState state = SideState::Auto;
	int windows = -1;                       ///< Windows per floor (-1: as the building; Count layout)
	std::string pattern;
	int doors = -1;                         ///< Doors on this side (-1: as the building)
};

/// Overrides for one lot (zero / empty: the selection's setting).
struct LotSettings {
	double height = 0;
	int floors = 0;
	std::string facadeMaterial;
};

/// Settings of the building builder.
struct BuildingOptions {
	BuildingMode mode = BuildingMode::Block;
	double facadeThickness = 32;            ///< Facade only: slab thickness behind the facade
	double returnDepth = 0;                 ///< Facade only: an open end turns the corner this far
	BuildingSides sides = BuildingSides::AllOuter;
	/// Per side (lot index, side index): state and own windows.
	std::map<std::pair<int, int>, SideSettings> sideSettings;
	std::map<int, LotSettings> lotSettings;

	// Height and floors
	HeightMode heightMode = HeightMode::Lot;
	double height = 512;                    ///< HeightMode::Height
	int floors = 4;                         ///< HeightMode::Floors: all floors incl. the ground floor
	double floorHeight = 128;
	double groundFloorHeight = 160;
	int floorVariation = 0;                 ///< Random +- floors per building (seed)
	double topWall = 24;                    ///< Wall above the top floor (Floors: part of the height)

	// Ground floor
	GroundLevel groundLevel = GroundLevel::Street;
	double groundRaise = 48;                ///< Raised: floor above the street; half sunk: below it
	bool basementWindows = true;            ///< Raised: small windows in the socle
	GroundWindows groundWindows = GroundWindows::SameAsUpper;
	double shopWidth = 112;
	double shopHeight = 96;
	double shopSill = 16;
	DoorSides doorSides = DoorSides::Longest;
	int doorsPerSide = 1;
	DoorPlace doorPlace = DoorPlace::Middle;
	double doorWidth = 56;
	double doorHeight = 112;
	double stepDepth = 16;                  ///< Raised: each step's depth (their height follows: <= 18)

	// Windows
	WindowStyle windowStyle = WindowStyle::Recessed;
	WindowLayout layout = WindowLayout::Spacing;
	int windowsPerSide = 4;                 ///< Count layout: windows per floor on each facade side
	double windowWidth = 48;
	double windowHeight = 72;
	double sillHeight = 32;                 ///< Floor to the window's bottom
	double windowGap = 64;                  ///< Spacing layout: wall between two windows
	double cornerMargin = 48;               ///< Wall left at each end of a side
	double windowDepth = 8;                 ///< Recess (the wall layer's thickness)
	std::string pattern = "x";              ///< Repeated over the columns: x window, . wall; ';' separates choices (seed)
	/// Recessed windows: a plate with the interior material behind the glass (see-through glass
	/// would show the void through the nodraw behind it in the game).
	bool windowBacking = true;
	bool sills = true;
	double sillDepth = 4;                   ///< Out from the wall
	double sillThickness = 4;

	// Bands
	bool cornices = true;                   ///< Between the floors
	double corniceHeight = 8;
	double corniceDepth = 8;
	bool topCornice = true;                 ///< At the top of the walls
	double topCorniceHeight = 16;
	double topCorniceDepth = 16;
	bool plinth = true;
	double plinthHeight = 16;
	double plinthDepth = 4;

	// Roof
	BuildingRoof roof = BuildingRoof::Flat;
	double parapetHeight = 32;              ///< Flat roof: 0 for none
	double parapetThickness = 8;
	double roofAngle = 30;                  ///< Gable/hip: degrees
	double roofOverhang = 16;
	double eaveThickness = 8;

	// Displacements
	bool foundation = true;                 ///< Reach down to displacement terrain under the lot
	bool wallDisplacement = false;          ///< Plain walls get a displacement surface (bulge)
	bool roofDisplacement = false;          ///< A flat four-cornered roof gets one (sag)
	int displacementPower = 3;
	double displacementStrength = 6;        ///< Units in and out

	// Condition
	double damage = 0;                      ///< 0 intact ... 1 destroyed
	std::uint32_t damageSeed = 1;

	// Whole lot as one model (BuildingMode::Model)
	std::string lotModel;

	// Materials (empty: see each)
	std::string facadeMaterial;             ///< Walls
	std::vector<std::string> facadeVariants;   ///< More facade materials: each building gets one at random (seed)
	std::string groundMaterial;             ///< Ground floor walls (empty: facade)
	std::string windowMaterial;             ///< Window panels
	std::string brokenWindowMaterial;       ///< Damaged windows (empty: the window material)
	std::string doorMaterial;               ///< Door panels (empty: window)
	std::string trimMaterial;               ///< Window reveals, sills, parapet tops, eaves, steps (empty: facade)
	std::string corniceMaterial;            ///< Cornices and plinth (empty: trim)
	std::string roofMaterial;               ///< Flat roof top / pitched roof (empty: trim)
	std::string interiorMaterial;           ///< What shows through damage (empty: trim)
	std::array<PartTexture, static_cast<std::size_t>(BuildingPart::Count)> textures{};

	// Props
	std::vector<FacadeProp> props;

	/// Touching lots are one building (a curve or an L made of several brushes): one material and
	/// pattern, doors only on its longest facade; else each lot is its own house (a row).
	bool touchingIsOne = false;

	std::uint32_t seed = 1;
	/// The model's box (local, Source: the model faces +X); nullopt: unknown (wallOffset only).
	std::function<std::optional<Bounds>(const std::string&)> modelBounds;
	/// Asked now and then (live preview on another thread): true stops, the plan comes back empty.
	std::function<bool()> cancelled;

	[[nodiscard]] const PartTexture& texture(BuildingPart part) const { return textures[static_cast<std::size_t>(part)]; }
	[[nodiscard]] PartTexture& texture(BuildingPart part) { return textures[static_cast<std::size_t>(part)]; }
};

/// The default texture setting of each part (fitted windows and doors, the rest by scale).
[[nodiscard]] BuildingOptions defaultBuildingOptions();

/// A side of a lot as the builder saw it.
struct BuildingSideInfo {
	int lot = 0;
	int side = 0;                           ///< Index of the edge in the lot's outline (counter-clockwise from above)
	Vec3 a, b;                              ///< Its ends at the bottom
	Vec3 normal;                            ///< Outward, horizontal
	double length = 0;
	bool shared = false;                    ///< Touches another selected lot
	bool blocked = false;                   ///< Another brush of the map stands in front of it
	bool front = false;                     ///< The lot's front (its longest side facing the street)
	SideState state = SideState::Nodraw;    ///< What it got (never Auto)
	int windowsPerFloor = 0;                ///< Openings on an upper floor
	int doors = 0;
};

/// One prop of the plan.
struct BuildingProp {
	std::string model;
	PropPlacement placement;
	int building = 0;                       ///< Index into BuildingPlan::buildings
};

/// What building would do.
struct BuildingPlan {
	struct Building {
		SolidRef lot;                       ///< The selected brush it replaces
		std::vector<Solid> world;           ///< Mass (seals)
		std::vector<Solid> detail;          ///< Facade pieces, panels, bands, roof (one func_detail)
		int floors = 0;
		double bottom = 0, top = 0;         ///< Street level and the walls' top
		double foundation = 0;              ///< Lowest point of the mass (below `bottom` on terrain)
		std::string facadeMaterial;
		std::string pattern;
		/// What it was built with, resolved (variation chosen, its shared sides fixed), for lot 0:
		/// kept with the building to edit or resize it later (BuildingRecord).
		BuildingOptions settings;
		std::vector<Vec3> outline;          ///< The lot seen from above (counter-clockwise)
		double lotTop = 0;                  ///< The lot brush's top
	};
	std::vector<Building> buildings;
	/// Editing buildings that are there: their pieces go (world brushes, brush-entity brushes, entities).
	std::vector<SolidRef> removeSolids;
	std::vector<int> removeEntities;
	std::vector<BuildingSideInfo> sides;
	std::vector<BuildingProp> props;
	std::vector<std::string> notes;
	int windows = 0;
	int doors = 0;
	int steps = 0;
	int displacements = 0;
	int clipped = 0;                        ///< Pieces cut around brushes of the map
	int skipped = 0;                        ///< Pieces left out (would be broken)

	[[nodiscard]] bool ok() const { return !buildings.empty() || !props.empty(); }
	[[nodiscard]] int brushCount() const;
	/// Every new brush, for a preview.
	[[nodiscard]] std::vector<Solid> resultSolids() const;
	/// The lots it replaces (hidden while the preview stands in for them).
	[[nodiscard]] std::vector<SolidRef> replacedRefs() const;
	/// The side nearest to a point (a click in the preview), or -1 (index into `sides`).
	[[nodiscard]] int sideAt(const Vec3& point, double tolerance = 64) const;
};

/// A building made by the builder, as kept in the map: its func_detail (or, a model lot, its prop)
/// holds the settings and the lot's outline ("sme_building", "sme_building_settings",
/// "sme_building_lot"); its world brushes (editor block) and props carry "sme_building" = its id.
/// VBSP and Hammer ignore those keys; Mapsmith edits, resizes and rebuilds the building with them.
struct BuildingRecord {
	int id = 0;
	BuildingOptions options;
	std::vector<Vec3> outline;              ///< Seen from above, counter-clockwise (z unused)
	double bottom = 0, top = 0;             ///< The lot brush's bottom and top
	Selection members;                      ///< World brushes and entities (func_detail, props)
};
/// Settings as one line of "key=value" pairs (the keys are the dialog's field names).
[[nodiscard]] std::string encodeBuildingOptions(const BuildingOptions& options);
/// Back from encodeBuildingOptions; missing keys keep the defaults, unknown ones are ignored.
[[nodiscard]] BuildingOptions decodeBuildingOptions(std::string_view text);
/// The settings of `edited` for the keys in `changed`, the rest from `base` (editing several
/// buildings: what was changed applies to all, the rest stays each one's own).
[[nodiscard]] BuildingOptions mergeBuildingOptions(const BuildingOptions& base, const BuildingOptions& edited, const std::set<std::string>& changed);
/// Every building of the map that was made by the builder.
[[nodiscard]] std::vector<BuildingRecord> buildingsInMap(const MapDocument& doc);
/// The building ids the selection's objects belong to.
[[nodiscard]] std::set<int> buildingIdsOf(const MapDocument& doc, const Selection& selection);
/// All objects of those buildings.
[[nodiscard]] Selection buildingMembers(const MapDocument& doc, const std::set<int>& ids);
/// An outline's size: length along its longest side, depth across it.
[[nodiscard]] std::pair<double, double> outlineSize(const std::vector<Vec3>& outline);
/// The outline stretched to a new length and depth (same directions, around its middle).
[[nodiscard]] std::vector<Vec3> resizeOutline(const std::vector<Vec3>& outline, double length, double depth);
/// Plans the buildings again with their records' (changed) settings and outlines; their old
/// pieces are taken out of the map the plan sees and go when it is applied.
[[nodiscard]] BuildingPlan planRebuild(const MapDocument& doc, MapDocument& ids, const std::vector<BuildingRecord>& records, const TextureSizeFn& textureSize,
                                       const std::function<std::optional<Bounds>(const std::string&)>& modelBounds = {},
                                       const std::function<bool()>& cancelled = {});

/// Plans buildings on the selected brushes (see the file comment). Reads `doc` only; new ids come
/// from `ids` (the document itself when applying, a scratch document for a preview). Brushes that
/// are not usable as a lot (broken, too small) are left alone with a note.
[[nodiscard]] BuildingPlan planBuildings(const MapDocument& doc, MapDocument& ids, const std::vector<SolidRef>& lots, const BuildingOptions& options,
                                         const TextureSizeFn& textureSize);

/// The settings as text, one "key = value" per line (two equal texts plan the same buildings; the
/// editor report shows them).
[[nodiscard]] std::string describeBuildingOptions(const BuildingOptions& options);
/// A plan as text (the editor report): buildings, sides, counts, notes.
[[nodiscard]] std::string describeBuildingPlan(const BuildingPlan& plan);

/// What a material is for in the building builder (its suggestions).
enum class BuildingMaterialRole { Facade, Ground, Window, BrokenWindow, Door, Trim, Roof, Interior };
/// Materials of the game that fit a role, best first (at most `count`), found by their names.
/// Tool textures, decals, overlays and blends are never suggested.
[[nodiscard]] std::vector<std::string> buildingMaterialSuggestions(const std::vector<std::string>& names, BuildingMaterialRole role, std::size_t count = 8);
/// Models of the game that fit an anchor, best first, found by their names (AC units, shutters,
/// awnings, doors, drainpipes, broken facade panels ...). `damaged`: broken / debris first.
[[nodiscard]] std::vector<std::string> buildingModelSuggestions(const std::vector<std::string>& names, FacadePropAnchor anchor, bool damaged = false,
                                                                std::size_t count = 8);
/// Whole-building models of the game (for BuildingMode::Model), best first.
[[nodiscard]] std::vector<std::string> buildingLotModelSuggestions(const std::vector<std::string>& names, std::size_t count = 12);

/// The columns of a side: centres of the window slots along a side of `length` (from its start)
/// for the layout, and the window width used (narrowed when the count does not fit). Exposed for tests.
[[nodiscard]] std::vector<double> windowColumns(double length, const BuildingOptions& options, double width, int count, double* usedWidth = nullptr);
/// Whether column `index` has a window in `pattern` ("x.x" repeats; empty: all). Exposed for tests.
[[nodiscard]] bool patternHasWindow(const std::string& pattern, int index);
/// Steps for a raise: count and height of each (never above 18 units). Exposed for tests.
[[nodiscard]] std::pair<int, double> stepsFor(double raise);

namespace edit {

/// Turns the buildings into plain brushes and props (their keys go; nothing else changes).
[[nodiscard]] Change unlinkBuildings(MapDocument& doc, const std::set<int>& ids);

/// Applies a plan (made from the current document) as one undo step "Build buildings": the lots
/// go, the masses become world brushes, each building's pieces one func_detail (`detailDefaults`),
/// the props prop_static entities (`propDefaults`). Returns the change and the new world brushes
/// and entities (to select).
[[nodiscard]] std::pair<Change, Selection> applyBuildings(MapDocument& doc, const BuildingPlan& plan,
                                                          const std::vector<std::pair<std::string, std::string>>& detailDefaults = {},
                                                          const std::vector<std::pair<std::string, std::string>>& propDefaults = {});

} // namespace edit
} // namespace sme
