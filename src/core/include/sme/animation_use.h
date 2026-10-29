// animation_use.h - Which entities of a map play which animation, and setting one: the keys a
// class keeps sequences in (prop_dynamic's DefaultAnim, the scripted_sequence fields), the model
// an entity animates (its own, the NPC a scripted_sequence controls, or the class's FGD model),
// and the one-step edits the Animations page offers ("Use on selected entity").

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <sme/assets.h>
#include <sme/entity_catalog.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// A key that holds a sequence name, with the label the UI shows.
struct SequenceKey {
	std::string key;     ///< "DefaultAnim", "m_iszIdle" ...
	std::string label;   ///< "Default animation", "Pre Action Idle" ...
};

/// The keys of a class that name a sequence (empty for classes without any): props play
/// DefaultAnim, scripted_sequence has its five, NPCs none (they get a pose helper instead).
[[nodiscard]] std::vector<SequenceKey> sequenceKeys(std::string_view classname);

/// Whether the class is an NPC (npc_* / monster_*): its animation is set through a pose helper.
[[nodiscard]] bool isNpcClass(std::string_view classname);

/// The models the FGD gives its NPC classes (studio()/studioprop() of npc_* / monster_*), lower case.
[[nodiscard]] std::vector<std::string> npcClassModels(const EntityCatalog& catalog);

/// Whether a model is a character - an NPC, a creature, a person - rather than a prop: one of
/// `npcModels` (npcClassModels, the map's NPCs), or in a character's folder or with a character's
/// name (humans/, characters/, zombie, combine_soldier, alyx ...); props_*, weapons, items and gibs
/// are not. Animation-only files (..._shared, _gestures, _postures, _ss, _anims) are not either:
/// they have nothing to show.
[[nodiscard]] bool isCharacterModel(std::string_view path, const std::vector<std::string>& npcModels = {});

/// Whether a model file holds only animations other models include, nothing to show
/// (..._shared, _gestures, _postures, _ss, _anims, _animations).
[[nodiscard]] bool isAnimationOnlyModel(std::string_view path);

/// Whether a model really animates: a skeleton of more than one bone and at least one sequence
/// beyond a still pose (idle, ref, ragdoll, bindpose ...), or $includemodel files with more.
/// A bottle or a crate with only "idle" does not.
[[nodiscard]] bool isAnimatedModel(const ModelInfo& info);

/// Reads the header of model `path` and asks isAnimatedModel (false if it cannot be read, and for
/// animation-only files).
[[nodiscard]] bool hasAnimations(const GameFileSystem& fs, std::string_view path);

/// The weapon a sequence is made for (bits; a sequence can fit several).
namespace sequence_weapon {
constexpr unsigned UNARMED = 1;    ///< No weapon word (ACT_IDLE, walk_all) or "unarmed"
constexpr unsigned PISTOL = 2;
constexpr unsigned SMG1 = 4;       ///< Also "rifle" (HL2's people hold SMG1 and AR2 alike)
constexpr unsigned AR2 = 8;        ///< Also "rifle"
constexpr unsigned SHOTGUN = 16;
constexpr unsigned RPG = 32;
constexpr unsigned CROSSBOW = 64;
constexpr unsigned MELEE = 128;    ///< melee, stunstick, crowbar
constexpr unsigned GRENADE = 256;  ///< grenade, throw
} // namespace sequence_weapon

/// What kind of motion a sequence is.
enum class SequenceKind {
	Idle,      ///< Standing or crouching still (ACT_IDLE*, idle names)
	Move,      ///< Walking, running (ACT_WALK*, ACT_RUN*, walk/run names)
	Action,    ///< Everything else with an activity: attacks, reloads, flinches, deaths ...
	Gesture,   ///< Layers played over others (ACT_GESTURE_*, delta/layer sequences, g_ names)
	Scene,     ///< Made for scripted scenes: map-named (d1_t01_..., ep1_c17_...) or without activity
};

/// The situation a sequence is made for (filters on the Animations page).
struct SequenceSituation {
	unsigned weapons = sequence_weapon::UNARMED;   ///< sequence_weapon bits
	bool crouch = false;                           ///< Crouching (crouch, ..._LOW activities)
	SequenceKind kind = SequenceKind::Action;
};

/// Sorts a sequence by its name and activity ("Idle_Relaxed_SMG1_1" / "ACT_IDLE_RELAXED": SMG1,
/// standing, idle). `layer`: a delta or layer sequence (a gesture).
[[nodiscard]] SequenceSituation classifySequence(std::string_view name, std::string_view activity, bool layer = false);

/// The model an entity animates: its "model" key; for scripted_sequence & co. the model of the
/// NPC they name (m_iszEntity / target); else the class's FGD studio() model. Empty if none.
[[nodiscard]] std::string animatedModel(const MapDocument& doc, const Entity& entity, const EntityCatalog* catalog);

/// One place a sequence is used in the map.
struct SequenceUse {
	int entityId = 0;       ///< The entity that holds the key (prop or scripted_sequence)
	int actorId = 0;        ///< The entity that moves (the prop itself, or the NPC of the script)
	std::string key;        ///< Key holding the name
	std::string label;      ///< Its label (SequenceKey::label)
	std::string sequence;   ///< The name it holds
};

/// Every sequence key in the map whose entity animates `model` (case and slashes do not matter).
[[nodiscard]] std::vector<SequenceUse> sequenceUses(const MapDocument& doc, const EntityCatalog* catalog, std::string_view model);

namespace edit {

/// Plays `sequence` on an entity (one undo step "Use animation"): a prop gets DefaultAnim, a
/// scripted_sequence the given key (default: the Pre Action Idle), an NPC a pose helper - a
/// scripted_sequence "<name>_pose" at its feet that keeps it in the animation (an existing one is
/// updated). With `model`, the entity that moves (the prop, the NPC - also the NPC of a script)
/// switches to that model in the same step (animations only a new model has). Empty Change if
/// the entity cannot take it.
[[nodiscard]] Change useSequence(MapDocument& doc, int entityId, std::string_view sequence, std::string_view key = {}, std::string_view model = {});

} // namespace edit

} // namespace sme
