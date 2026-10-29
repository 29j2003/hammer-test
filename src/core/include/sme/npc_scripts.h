// npc_scripts.h - NPCs that play a scene: how a scripted_sequence starts and loops, and the
// one-step edits the Properties panel offers for it.
//
// What the engine does (Source SDK 2013, ai_scripted / scripted.cpp):
// - A scripted_sequence without a name starts by itself: the NPC moves into place and plays the
//   Action as soon as it arrives.
// - A named one with "Start on Spawn" moves the NPC into place at map start and keeps it in the
//   Pre Action Idle - the Action waits for the BeginSequence input (a trigger, a logic_auto).
//   That is why "Start on Spawn" alone never plays the Action of a named script.
// - A named one without "Start on Spawn" does nothing until BeginSequence (or MoveToPosition).
// - "Loop Action Animation" (m_bLoopActionSequence) repeats the Action until StopActionLoop;
//   "Loop in Post Idle" (flag 256) repeats the Post Action Idle after the Action.
// - Move to position (m_fMoveTo): No - plays where the NPC stands; Walk/Run - the NPC needs a way
//   there (info_node network or a clear straight line); Instantaneous - teleports it to the
//   script's spot and direction.
// - "Search Radius" (m_flRadius) only matters when Target NPC names a class, not a targetname.
// - npc_citizen picks a random head unless its type is Unique (citizentype 4): only then does
//   its "model" key decide the look.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// scripted_sequence spawnflags (the same in every Source 1 game).
namespace script_flags {
constexpr long long REPEATABLE = 4;
constexpr long long START_ON_SPAWN = 16;
constexpr long long NO_INTERRUPTIONS = 32;
constexpr long long OVERRIDE_AI = 64;
constexpr long long NO_TELEPORT_AT_END = 128;
constexpr long long LOOP_POST_IDLE = 256;
} // namespace script_flags

/// How a scripted_sequence starts and loops, read from the map.
struct ScriptState {
	enum class Start {
		Never,          ///< Named, no Start on Spawn and nothing fires BeginSequence: it never runs
		Immediately,    ///< No name: starts at map start and plays the Action right away
		WaitsInIdle,    ///< Start on Spawn, nothing fires BeginSequence: Pre Action Idle forever
		AtMapStart,     ///< A logic_auto fires BeginSequence
		ByOthers,       ///< A trigger (or another entity) fires BeginSequence
	};
	enum class Loop {
		Once,           ///< Plays the Action once, then the Post Action Idle once (if any)
		Action,         ///< Loop Action Animation: the Action until StopActionLoop
		PostIdle,       ///< Loop in Post Idle: the Post Action Idle forever after the Action
	};
	Start start = Start::Never;
	Loop loop = Loop::Once;
	bool startOnSpawn = false;    ///< Spawnflag 16
	std::vector<int> starters;    ///< Entities whose outputs fire BeginSequence on the script
	int npcId = 0;                ///< The NPC Target NPC (m_iszEntity) names; 0 if none found
};

/// The state of scripted_sequence `scriptId` (default state for anything else).
[[nodiscard]] ScriptState scriptState(const MapDocument& doc, int scriptId);

/// Where "Start with a trigger" puts the trigger box: 128 x 128 x 112 units on the floor of the
/// script, 192 units in front of it (along its yaw).
[[nodiscard]] Bounds sequenceTriggerBox(const MapDocument& doc, int scriptId);

/// The heads of Half-Life 2's citizens (models/humans/groupNN/...), Group01 first.
[[nodiscard]] std::vector<std::string> citizenModels();

namespace edit {

/// Puts NPC `npcId` into `sequence` on the spot (one undo step "Use animation"): a
/// scripted_sequence "<name>_pose" at its feet with the sequence as Pre Action Idle and Start on
/// Spawn, No Interruptions, Override AI - so it loops the sequence from map start (an existing
/// helper is updated). Names the NPC if needed. `setNpcFlags` / `clearNpcFlags` change the NPC's
/// spawnflags in the same step (Gag, Don't drop weapons ...). Empty Change for a non-entity.
[[nodiscard]] Change poseNpc(MapDocument& doc, int npcId, std::string_view sequence, long long setNpcFlags = 0, long long clearNpcFlags = 0);

/// Starts scripted_sequence `scriptId` at map start (one undo step): Start on Spawn, and a
/// logic_auto (an existing one or a new one at the script) fires BeginSequence OnMapSpawn.
/// Names the script if needed. Empty Change if it is already started that way or not a script.
[[nodiscard]] Change startSequenceAtMapStart(MapDocument& doc, int scriptId);

/// Starts scripted_sequence `scriptId` when the player enters `box` (one undo step): a
/// trigger_once (tools/toolstrigger, clients only) fires BeginSequence OnStartTouch, and Start on
/// Spawn makes the NPC wait at its spot in the Pre Action Idle until then. Names the script if
/// needed. The trigger is the last entity of the document. Empty Change if not a script.
[[nodiscard]] Change addSequenceTrigger(MapDocument& doc, int scriptId, const Bounds& box);

/// Sets how scripted_sequence `scriptId` loops (one undo step "Script loop"): Action sets
/// m_bLoopActionSequence; PostIdle sets Loop in Post Idle (and the Post Action Idle to the Action
/// when it is empty); Once clears both. Empty Change if nothing changes.
[[nodiscard]] Change setSequenceLoop(MapDocument& doc, int scriptId, ScriptState::Loop loop);

/// Gives npc_citizen `npcId` a fixed look (one undo step "Fixed look"): citizentype Unique (4) and
/// `model` (empty: its current humans model, else male_07 of its group). Empty Change otherwise.
[[nodiscard]] Change keepCitizenLook(MapDocument& doc, int npcId, std::string_view model = {});

} // namespace edit

} // namespace sme
