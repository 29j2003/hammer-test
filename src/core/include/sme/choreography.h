// choreography.h - NPC scenes laid out step by step: who walks where, plays which animation,
// says which line - what starts them, under which conditions - and what Source needs to play them.
//
// A scene lives in the map as a logic_choreographed_scene with Mapsmith's plan in keyvalues
// ("mapsmith_scene" 1, "mapsmith_start01" ..., "mapsmith_if01" ..., "mapsmith_step01" ...; the
// engine ignores unknown keys and Hammer keeps them), so it is saved with the .vmf, undone like any
// edit and survives a trip through Hammer. Older maps' single "mapsmith_trigger" key is still read.
// Every edit regenerates the logic of ALL scenes from their plans (syncChoreoLogic, one pass over
// the map, deterministic): Mapsmith's entities carry "mapsmith_scene" "<scene>" and
// "mapsmith_role" (what they are for); hand-made entities are never touched, only the outputs
// Mapsmith added to them (those aimed at its own entities). Per scene:
//   - an info_target "<scene>_p<N>" for every point a step walks to or faces (the VCD names them),
//   - a logic_relay "<scene>_start": every way of starting the scene fires its Trigger input,
//   - one entity per start (see ChoreoStart), firing "<scene>_start":
//       map start  - logic_auto "<scene>_auto<N>" OnMapSpawn,
//       area       - trigger_once / trigger_multiple "<scene>_area<N>" (a box in front of the NPC),
//       look at    - trigger_look "<scene>_look<N>" (player looks at an entity while inside),
//       near       - trigger_multiple "<scene>_near<N>" around the NPC, parented to it,
//       timer      - logic_timer "<scene>_timer<N>" (fixed or random time),
//       use / output / after scene - an output on that entity (OnPlayerUse, any output, the other
//       scene's OnCompletion), input - nothing (fire "<scene>_start" Trigger or the scene's Start),
//   - the conditions in a chain after "<scene>_start", all must hold, the last one Starts the scene:
//       scene played / not played - logic_branch_listener "<scene>_if<N>" on the other scene's
//         logic_branch "<other>_played" (set by its OnCompletion), Test -> OnAllTrue / OnAllFalse,
//       variable (number) - logic_compare "<scene>_if<N>" kept up to date by the variable's math_counter
//         (OutValue -> SetValue), Compare -> OnEqualTo / OnNotEqualTo / OnGreaterThan / OnLessThan,
//       variable (flag) - logic_branch_listener on the variable's logic_branch,
//       global state - env_global "<scene>_if<N>g" GetCounter -> OutCounter -> logic_compare
//         "<scene>_if<N>" SetValueCompare (the env_global's counter of that global state),
//       random - logic_case "<scene>_if<N>" with ten cases, PickRandom, N/10 of them go on.
// Variables are map entities made here: math_counter (number) or logic_branch (flag) with
// "mapsmith_var" set; "Set variable" steps fire them from the scene (a VCD "firetrigger" event,
// the scene's OnTrigger<N> output -> SetValue / Add / Toggle).
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
//   - Set variable: a firetrigger event (scene-wide) at the step's time.

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
	enum class Kind { Move, Animation, Say, Wait, Face, SetVariable };
	enum class VariableOp { Set, Add, Toggle };
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
	std::string variable;       ///< Set variable: the variable's name
	VariableOp op = VariableOp::Set;   ///< Set variable: what to do (Toggle: flags only)
	double value = 0;           ///< Set variable: the value to set or add (flags: 0 / 1)
	bool operator==(const ChoreoStep&) const = default;
};

/// One way to start a scene (a scene may have several: any of them starts it).
struct ChoreoStart {
	enum class Kind { MapStart, Area, Use, LookAt, Near, AfterScene, Output, Timer, Input };
	Kind kind = Kind::MapStart;
	bool once = true;       ///< Only the first time (Area, Near, Use, Output, Timer); else every time
	double delay = 0;       ///< Seconds from the trigger to the scene starting
	std::string target;     ///< Use / LookAt / Output: the entity's targetname (empty: the scene's first NPC); AfterScene: the other scene
	std::string output;     ///< Output: the output's name; Use: empty = the class's own (OnPlayerUse, OnPressed)
	double a = 0, b = 0;    ///< Timer: seconds (a), random between a and b when b > a; Near: radius (a); Area/Near every time: wait between (a)
	bool operator==(const ChoreoStart&) const = default;
};

/// A condition that must hold when a start fires (all conditions of a scene must hold).
struct ChoreoCondition {
	enum class Kind { ScenePlayed, SceneNotPlayed, Variable, Global, Random };
	enum class Compare { Equal, NotEqual, Greater, Less };
	Kind kind = Kind::ScenePlayed;
	std::string name;       ///< Scene / variable / global state name
	Compare compare = Compare::Equal;
	double value = 0;       ///< Variable / Global: the value compared with (flags: 1 = set); Random: percent
	bool operator==(const ChoreoCondition&) const = default;
};

/// A whole scene (see the file comment).
struct ChoreoScene {
	std::string name;                       ///< The logic_choreographed_scene's targetname
	std::vector<ChoreoStart> starts;        ///< Ways to start it (none: only by its Start input)
	std::vector<ChoreoCondition> conditions;
	std::vector<ChoreoStep> steps;
	bool operator==(const ChoreoScene&) const = default;
};

/// A variable scenes test and set (a map entity, see the file comment).
struct ChoreoVariable {
	std::string name;
	bool flag = false;      ///< logic_branch (true/false) instead of a math_counter number
	double initial = 0;
	int entityId = 0;
	bool operator==(const ChoreoVariable&) const = default;
};

/// Keyvalues of a scene entity (see the file comment).
inline constexpr std::string_view CHOREO_SCENE_KEY = "mapsmith_scene";
inline constexpr std::string_view CHOREO_TRIGGER_KEY = "mapsmith_trigger";   ///< Older maps: one start ("mapstart", "trigger", "input", "after:<scene>")
inline constexpr std::string_view CHOREO_START_KEY = "mapsmith_start";
inline constexpr std::string_view CHOREO_CONDITION_KEY = "mapsmith_if";
inline constexpr std::string_view CHOREO_ROLE_KEY = "mapsmith_role";
inline constexpr std::string_view CHOREO_VARIABLE_KEY = "mapsmith_var";
inline constexpr std::string_view CHOREO_STEP_KEY = "mapsmith_step";

/// One step as the text of a "mapsmith_stepNN" key (fields split by '|', '|' and '%' escaped,
/// '"' turned into '\'' - a VMF value cannot hold it) and back. nullopt for text it cannot read.
[[nodiscard]] std::string encodeChoreoStep(const ChoreoStep& step);
[[nodiscard]] std::optional<ChoreoStep> decodeChoreoStep(std::string_view text);
/// The same for a start ("mapsmith_startNN") and a condition ("mapsmith_ifNN").
[[nodiscard]] std::string encodeChoreoStart(const ChoreoStart& start);
[[nodiscard]] std::optional<ChoreoStart> decodeChoreoStart(std::string_view text);
[[nodiscard]] std::string encodeChoreoCondition(const ChoreoCondition& condition);
[[nodiscard]] std::optional<ChoreoCondition> decodeChoreoCondition(std::string_view text);
/// Short texts for lists ("Player enters an area", "scene intro has played" ...).
[[nodiscard]] std::string describeChoreoStart(const ChoreoStart& start);
[[nodiscard]] std::string describeChoreoCondition(const ChoreoCondition& condition);
/// The output a "player uses it" start listens to on an entity of `classname` (OnPressed for
/// buttons, OnPlayerUse for NPCs and props).
[[nodiscard]] std::string defaultUseOutput(std::string_view classname);

/// Writes a plan into a scene entity's keys ("mapsmith_stepNN", "mapsmith_startNN", "mapsmith_ifNN";
/// old ones and an older map's "mapsmith_trigger" go). No transaction, no logic: for the edits below.
void writeChoreoPlanKeys(Entity& entity, const ChoreoScene& scene);
/// Whether an entity is a scene made here (a logic_choreographed_scene with "mapsmith_scene").
[[nodiscard]] bool isChoreoScene(const Entity& entity);
/// The plan kept in a scene entity (steps that cannot be read are left out).
[[nodiscard]] ChoreoScene readChoreoScene(const Entity& entity);
/// The same, with every Move/Face point taken from its info_target when the mapper moved that
/// (the helpers are ordinary entities: select one and move it with the gizmo, undo included).
[[nodiscard]] ChoreoScene readChoreoScene(const MapDocument& doc, const Entity& entity);
/// Ids of all scenes made here, in map order.
[[nodiscard]] std::vector<int> choreoScenes(const MapDocument& doc);
/// The NPCs of a scene in the order of their first step (targetnames).
[[nodiscard]] std::vector<std::string> choreoActors(const ChoreoScene& scene);
/// A free scene name: `base`, else base_2, base_3 ... (no entity has it).
[[nodiscard]] std::string uniqueChoreoName(const MapDocument& doc, std::string_view base);
/// The variables of the map (entities with "mapsmith_var"), in map order.
[[nodiscard]] std::vector<ChoreoVariable> choreoVariables(const MapDocument& doc);
/// Whether an entity is one Mapsmith made for a scene (a helper: "mapsmith_scene" with a scene name).
[[nodiscard]] bool isChoreoHelper(const Entity& entity);
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

/// Checks the starts and conditions of every scene of the map: a start aimed at an entity no one is
/// named, an area or look trigger outside the map, a condition on a scene or variable that is gone,
/// two scenes of one NPC that start the same way without any condition, more than 16 "Set
/// variable" steps in a scene. Each issue names its scene (sceneId).
struct ChoreoLogicIssue {
	int sceneId = 0;
	std::string scene;
	std::string message;
	bool warning = true;     ///< else an error (the scene cannot work like this)
};
[[nodiscard]] std::vector<ChoreoLogicIssue> checkChoreoLogic(const MapDocument& doc);

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

/// A copy of scene `sceneId` under a new name (`<name>_2` ...), with its points, starts and
/// conditions (one undo step "Duplicate scene"). Returns the change; the new scene is the last one.
[[nodiscard]] Change duplicateChoreoScene(MapDocument& doc, int sceneId, std::string_view mapName);

/// Regenerates the logic of every scene from the plans (see the file comment), inside an open
/// transaction. Returns whether anything changed. Called by the edits here; exposed for callers that
/// changed plans or variables by hand.
bool syncChoreoLogic(MapDocument& doc, Transaction& t);

/// A new variable (math_counter or logic_branch) at `origin` (one undo step "New variable"). The
/// name is made unique. Empty Change when `name` is empty.
[[nodiscard]] Change createChoreoVariable(MapDocument& doc, std::string_view name, bool flag, double initial, const Vec3& origin);
/// Renames a variable; every scene condition and "Set variable" step follows (one undo step).
[[nodiscard]] Change renameChoreoVariable(MapDocument& doc, std::string_view oldName, std::string_view newName);
/// Sets a variable's starting value (one undo step).
[[nodiscard]] Change setChoreoVariableInitial(MapDocument& doc, std::string_view name, double initial);
/// Removes a variable (one undo step); conditions and steps that used it stay and are reported by
/// checkChoreoLogic.
[[nodiscard]] Change deleteChoreoVariable(MapDocument& doc, std::string_view name);

} // namespace edit

} // namespace sme
