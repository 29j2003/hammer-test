// editor_report.h - The editor report: what Julius sends when something in the editor misbehaves
// (a brush that cannot be clicked, a road laid wrong), and the replay that reads it back.
//
// The app writes a ZIP (MainWindow::writeEditorReport): the map around the selection
// (scene.vmf, ids kept), the editor's state (editor.json for people, replay.txt for the
// replay), the last clicks with every candidate the pick saw (picks.log), the last edits with the
// brushes' planes before and after (edits.log), the last road / texture flow plans with the
// planner's notes (road.log), the Log and Problems texts and a picture of each view. Paths are
// masked (home folder, user name) and nothing leaves the computer unless he sends the file.
//
// The replay (`sme-probe editor-report <zip>`) loads scene.vmf, sends every recorded click ray
// through the map again (brushesAlongRay: plain geometry, no editor state) and plans the road
// again with the recorded settings - what differs from the recording points at editor state
// (hidden objects, stale caches) rather than geometry.

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/math.h>
#include <sme/road_builder.h>
#include <sme/road_texture.h>
#include <sme/texture_flow.h>

namespace sme {

/// A number as the report writes it (enough digits for Hammer++'s decimals, no "-0").
[[nodiscard]] std::string reportNumber(double value);

/// One hit (or near miss) a click's pick considered.
struct PickCandidate {
	std::string kind;          ///< "brush", "box", "model", "stand-in"
	int entityId = 0;
	int solidId = 0;
	int sideId = 0;
	double distance = 0;
	std::string note;          ///< "inside", "same surface", ...
};

/// Everything one click's pick saw and decided.
struct PickTrace {
	std::string view;          ///< "3D", "Top", ...
	int viewWidth = 0, viewHeight = 0;
	double screenX = 0, screenY = 0;
	std::string tool;
	std::string modifiers;     ///< "ctrl", "alt+ctrl", or "-"
	bool faceMode = false;
	int cycle = 0;             ///< Clicks on the same spot before this one
	Ray ray;
	std::vector<PickCandidate> candidates;
	std::vector<std::string> skipped;   ///< Items whose box the ray hit that were left out, and why
	std::string order;                  ///< How brushes on one surface were ordered
	int ties = 0;
	std::string result;                 ///< "brush 0/12 side 40", "entity 5", "nothing"
	std::string selection;              ///< The selection after the click
};

/// The trace as lines for picks.log (the first starts with "pick").
[[nodiscard]] std::string formatPickTrace(const PickTrace& trace, int number);

/// A click read back from picks.log.
struct LoggedPick {
	int number = 0;
	std::string view;
	bool faceMode = false;
	Ray ray;
	std::string result;
};
[[nodiscard]] std::vector<LoggedPick> parsePickLog(std::string_view text);

/// A brush the ray goes into, by plain geometry.
struct RayHit {
	SolidRef solid;
	int sideId = 0;
	double distance = 0;
	bool inside = false;       ///< The ray starts inside the brush (the face it leaves through)
};
/// Every brush (world and brush entities) the ray goes into, nearest first; `hidden` brushes,
/// entities and faces are left out when given (a ray through a hidden face goes on behind it).
[[nodiscard]] std::vector<RayHit> brushesAlongRay(const MapDocument& doc, const Ray& ray, const Selection* hidden = nullptr);

/// A brush with its planes and box, one line per side ("side 12: (0 0 0) (0 64 0) (64 64 0) ...").
[[nodiscard]] std::string describeSolid(const Solid& solid);
/// An edit (its name and its brushes before and after, the first `maxBrushes` of them) for edits.log.
[[nodiscard]] std::string describeChange(const Change& change, std::size_t maxBrushes = 12);
/// A road plan and its settings: counts, the planner's notes and every face's texture.
[[nodiscard]] std::string describeRoadPlan(const RoadPlan& plan, const RoadOptions& options);
/// A texture flow plan and its settings.
[[nodiscard]] std::string describeFlowPlan(const FlowPlan& plan, const FlowOptions& options);

/// The objects within `margin` units of `area` (brushes, brush entities, point entities and
/// displacements), ids kept, world keys kept - or the whole map when it is written in fewer
/// than `wholeMapBytes`. `kept` gets the number of brushes and entities taken.
[[nodiscard]] MapDocument reportScene(const MapDocument& doc, const Bounds& area, double margin, std::size_t wholeMapBytes,
                                      std::size_t* keptSolids = nullptr, std::size_t* keptEntities = nullptr);

/// Home folder of the user ("" when unknown).
[[nodiscard]] std::string homeFolder();
/// `text` with the home folder replaced by "~" and user names in paths by "<user>".
[[nodiscard]] std::string maskPersonalText(std::string text, std::string_view home);

/// replay.txt: "key=value" lines (values on one line; newlines become spaces).
[[nodiscard]] std::string formatSettings(const std::vector<std::pair<std::string, std::string>>& settings);
[[nodiscard]] std::map<std::string, std::string> parseSettings(std::string_view text);
/// Selections in replay.txt: "0/12,0/13" (brushes), "0/12/45,0/13/50" (faces).
[[nodiscard]] std::string formatSolids(const std::vector<SolidRef>& solids);
[[nodiscard]] std::string formatFaces(const std::vector<FaceRef>& faces);
[[nodiscard]] std::vector<SolidRef> parseSolids(std::string_view text);
[[nodiscard]] std::vector<FaceRef> parseFaces(std::string_view text);
/// Road builder settings for replay.txt ("build.mode", "build.parking", ...; the clicked points
/// in "build.points") and back.
void addRoadBuildSettings(std::vector<std::pair<std::string, std::string>>& out, const RoadBuildOptions& options, bool fromSelection,
                          const std::vector<Vec3>& points, std::pair<int, int> textureSize);
[[nodiscard]] RoadBuildOptions roadBuildSettings(const std::map<std::string, std::string>& settings, bool* fromSelection, std::vector<Vec3>* points,
                                                 std::pair<int, int>* textureSize);

/// Road and texture flow settings for replay.txt ("road.material", "road.width", ...) and back.
void addRoadSettings(std::vector<std::pair<std::string, std::string>>& out, const RoadOptions& options, std::pair<int, int> textureSize);
void addFlowSettings(std::vector<std::pair<std::string, std::string>>& out, const FlowOptions& options, std::pair<int, int> textureSize);
[[nodiscard]] RoadOptions roadSettings(const std::map<std::string, std::string>& settings, std::pair<int, int>* textureSize);
[[nodiscard]] FlowOptions flowSettings(const std::map<std::string, std::string>& settings, std::pair<int, int>* textureSize);

/// The replay of a report (see the file comment) as text; nullopt with `error` when the ZIP
/// cannot be read or has no scene.vmf.
[[nodiscard]] std::optional<std::string> replayEditorReport(const std::filesystem::path& zip, std::string* error = nullptr);

} // namespace sme
