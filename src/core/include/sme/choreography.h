// choreography.h - NPC scenes laid out step by step: who walks where, plays which animation,
// says which line - and what Source needs to play them.
//
// A scene lives in the map as a logic_choreographed_scene with Mapsmith's plan in keyvalues
// ("mapsmith_scene" 1, "mapsmith_trigger", "mapsmith_step01" ...; the engine ignores unknown keys
// and Hammer keeps them), so it is saved with the .vmf, undone like any edit and survives a trip
// through Hammer. setChoreoScene() keeps the helpers in step with the plan:
//   - an info_target "<scene>_p<N>" for every point a step walks to or faces (the VCD names them),
//   - the trigger: a logic_auto (map start), a trigger_once (player walks in) or an output of
//     another scene (OnCompletion); helpers carry "mapsmith_scene" "<scene>".
// writeVcd() turns the plan into Valve's text choreography (.vcd: actors, channels, events with
// start/end times - the format CChoreoScene::ParseFromBuffer in the SDK reads); Mapbase plays a
// loose scenes/<map>/<scene>.vcd from the mod folder (sceneentity.cpp: "check if it's a loose
// file"), so no scenes.image is needed. Steps run one after another in list order; a step marked
// "with previous" starts together with the one before it (two NPCs at once).
//   - Move: moveto (param target, param2 Walk/Run, resumecondition) and a "section" pause at its
//     planned end: if the NPC is late the scene waits for it (CSceneEntity::PauseThink).
//   - Animation: sequence or gesture event with the sequence's length (or the time set).
//   - Say: speak event playing a sound script entry (caption token = the entry's name).
//   - Wait: nothing happens for the time set. Face: face event towards a point.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/history.h>
#include <sme/map.h>
#include <sme/math.h>

namespace sme {

/// One step of a scene.
struct ChoreoStep {
	enum class Kind { Move, Animation, Say, Wait, Face };
	Kind kind = Kind::Wait;
	std::string actor;          ///< The NPC's targetname
	bool withPrevious = false;  ///< Starts together with the step before (else after it ends)
	Vec3 point;                 ///< Move: where to; Face: what to face
	bool run = false;           ///< Move: run instead of walk
	std::string sequence;       ///< Animation: sequence name
	bool gesture = false;       ///< Animation: played as a gesture (layered, the NPC keeps moving)
	double seconds = 0;         ///< Wait: length; Animation/Say: 0 = the default length (see choreoStepLength)
	std::string sound;          ///< Say: sound script entry ("mymod.scene1.line1")
	std::string caption;        ///< Say: subtitle text (the caption token is `sound`)
	bool operator==(const ChoreoStep&) const = default;
};

/// What starts a scene.
enum class ChoreoTrigger {
	MapStart,     ///< A logic_auto fires Start when the map has loaded
	Trigger,      ///< A trigger_once fires Start when the player walks into it
	Input,        ///< Nothing of Mapsmith's: fire "Start" from your own I/O
	AfterScene,   ///< When another scene ends (its OnCompletion)
};

/// A whole scene (see the file comment).
struct ChoreoScene {
	std::string name;                 ///< The logic_choreographed_scene's targetname
	ChoreoTrigger trigger = ChoreoTrigger::MapStart;
	std::string afterScene;           ///< AfterScene: the other scene's name
	std::vector<ChoreoStep> steps;
	bool operator==(const ChoreoScene&) const = default;
};

/// Keyvalues of a scene entity (see the file comment).
inline constexpr std::string_view CHOREO_SCENE_KEY = "mapsmith_scene";
inline constexpr std::string_view CHOREO_TRIGGER_KEY = "mapsmith_trigger";
inline constexpr std::string_view CHOREO_STEP_KEY = "mapsmith_step";

/// One step as the text of a "mapsmith_stepNN" key (fields split by '|', '|' and '%' escaped,
/// '"' turned into '\'' - a VMF value cannot hold it) and back. nullopt for text it cannot read.
[[nodiscard]] std::string encodeChoreoStep(const ChoreoStep& step);
[[nodiscard]] std::optional<ChoreoStep> decodeChoreoStep(std::string_view text);

/// Whether an entity is a scene made here (a logic_choreographed_scene with "mapsmith_scene").
[[nodiscard]] bool isChoreoScene(const Entity& entity);
/// The plan kept in a scene entity (steps that cannot be read are left out).
[[nodiscard]] ChoreoScene readChoreoScene(const Entity& entity);
/// The same, with every Move/Face point taken from its info_target when the mapper moved that
/// (the helpers are ordinary entities: select one and move it with the gizmo, undo included).
[[nodiscard]] ChoreoScene readChoreoScene(const MapDocument& doc, const Entity& entity);
/// Ids of all scenes made here, in map order.
[[nodiscard]] std::vector<int> choreoScenes(const MapDocument& doc);
/// Ids of the scenes in which the NPC named `actor` has a step.
[[nodiscard]] std::vector<int> choreoScenesOfActor(const MapDocument& doc, std::string_view actor);
/// The info_target a step walks to or faces: "<scene>_p<step number>" (1-based).
[[nodiscard]] std::string choreoPointName(std::string_view scene, std::size_t stepIndex);
/// Where a scene's .vcd goes, below the mod folder: "scenes/<map>/<scene>.vcd" (lower case).
[[nodiscard]] std::string choreoSceneFile(std::string_view mapName, std::string_view scene);
/// The entity named `targetname` (case-insensitive), or nullptr.
[[nodiscard]] const Entity* findNamedEntity(const MapDocument& doc, std::string_view targetname);

/// Speeds the timeline assumes for walking and running NPCs (units per second, roughly HL2's
/// citizens); the scene waits at the end of a move when the NPC is slower.
inline constexpr double CHOREO_WALK_SPEED = 80;
inline constexpr double CHOREO_RUN_SPEED = 180;

/// Length of one step in seconds: Move = 2D distance / speed (at least 0.5 s), Animation =
/// seconds or 2 s, Say = seconds or 3 s, Wait = seconds, Face = 1 s. `from` is where the actor
/// stands before the step.
[[nodiscard]] double choreoStepLength(const ChoreoStep& step, const Vec3& from);

/// When each step runs and where its actor is afterwards.
struct ChoreoTiming {
	double start = 0, end = 0;
	Vec3 from, to;          ///< The actor's position before and after the step
	double yaw = 0;         ///< The actor's facing after the step (degrees)
};
/// The timeline of a scene: steps in order, a step "with previous" starting with the one before
/// it; a group starts when every step of the group before has ended. Actors start where their
/// entity stands (facing its "angles" yaw).
[[nodiscard]] std::vector<ChoreoTiming> choreoTimeline(const MapDocument& doc, const ChoreoScene& scene);
/// The whole scene's length in seconds.
[[nodiscard]] double choreoLength(const std::vector<ChoreoTiming>& timeline);

/// Where an actor is at `time` in the scene: position, facing, and the step it is in (-1 when
/// none) with the time since that step started.
struct ChoreoPose {
	Vec3 position;
	double yaw = 0;
	int step = -1;
	double stepTime = 0;
};
[[nodiscard]] ChoreoPose choreoActorPose(const MapDocument& doc, const ChoreoScene& scene, const std::vector<ChoreoTiming>& timeline,
                                         std::string_view actor, double time);

/// The scene as Valve's text .vcd (see the file comment). Actors appear in the order of their
/// first step; "faceposermodel" is the NPC's "model" key when it has one.
[[nodiscard]] std::string writeVcd(const MapDocument& doc, const ChoreoScene& scene);

/// An event read back from a .vcd (for tests and imports).
struct VcdEvent {
	std::string type, name;
	std::string actor, channel;     ///< Empty for scene-wide events (section, stoppoint)
	double start = 0, end = -1;
	std::string param, param2, param3;
	std::vector<std::string> flags;  ///< resumecondition, fixedlength, lockbodyfacing ...
	std::string cctype, cctoken;
};
/// Reads the events of a text .vcd (tolerant: unknown keys are skipped).
[[nodiscard]] std::vector<VcdEvent> parseVcd(std::string_view text);

/// Something about a scene the mapper should fix.
struct ChoreoIssue {
	int step = -1;            ///< The step (-1: the scene as a whole)
	std::string message;
};
/// Checks a scene against the map: steps without an actor or with an actor no entity is named,
/// Say without a sound, moves far from any info_node (NPCs find their way along nodes; with no
/// node at all every move warns once). `soundKnown` (optional) says whether a sound script entry
/// exists; `sequenceKnown` (optional) whether the actor's model has a sequence (nullopt: unknown).
[[nodiscard]] std::vector<ChoreoIssue> checkChoreoScene(const MapDocument& doc, const ChoreoScene& scene,
                                                        const std::function<bool(std::string_view)>& soundKnown = {},
                                                        const std::function<std::optional<bool>(std::string_view actor, std::string_view sequence)>& sequenceKnown = {});

// --- Files ------------------------------------------------------------------------------------

/// Writes the .vcd of every scene of the map into the mod folder (at its SceneFile path). Returns
/// the paths written (relative, "scenes/..."); `error` names a file that could not be written.
std::vector<std::string> writeChoreoFiles(const MapDocument& doc, const std::filesystem::path& modDir, std::string* error = nullptr);

/// The sound script name of a scene's voice line: "<scene>.<line>" cleaned for the game.
[[nodiscard]] std::string voiceLineName(std::string_view scene, std::string_view line);
/// Puts a caption (subtitle) for `token` into the mod's resource/closecaption_english.txt (made
/// if missing, kept as it was otherwise) and compiles closecaption_english.dat next to it.
bool setVoiceCaption(const std::filesystem::path& modDir, std::string_view token, std::string_view text, std::string* error = nullptr);

namespace edit {

/// A new scene for NPC `npcId` (one undo step "New scene"): names the NPC if needed, adds a
/// logic_choreographed_scene "<npc>_scene" (or _scene2 ...) next to it with SceneFile
/// choreoSceneFile(mapName, name), started at map start. Empty Change for a non-entity.
[[nodiscard]] Change createChoreoScene(MapDocument& doc, int npcId, std::string_view mapName);

/// Writes `scene` into scene entity `sceneId` (one undo step named `undoName`): the plan keys, the
/// targetname, the info_target points and the trigger helpers (see the file comment). Helpers no
/// longer needed are removed; a trigger_once the mapper moved keeps its box. Empty Change if
/// nothing changes or `sceneId` is no scene.
[[nodiscard]] Change setChoreoScene(MapDocument& doc, int sceneId, const ChoreoScene& scene, std::string_view undoName = "Scene");

/// Removes a scene with its helpers (one undo step "Delete scene").
[[nodiscard]] Change deleteChoreoScene(MapDocument& doc, int sceneId);

} // namespace edit

} // namespace sme
