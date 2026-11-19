// model_effects.h - Effects on a model: a particle system at an attachment, always or only during
// part of an animation, written as the model's own data (its sequence events or its $keyvalues
// "particles" block). Mapsmith compiles them into the model with studiomdl (the model's .qc);
// this file only says what goes into the .qc.
//
// What the game does with them (Source SDK 2013 + Mapbase, game/client/c_baseanimating.cpp):
//   - Sequence events (in the .qc: { event AE_CL_CREATE_PARTICLE_EFFECT <frame> "<options>" }
//     inside $sequence) are fired by the client while the sequence plays. Options of
//     AE_CL_CREATE_PARTICLE_EFFECT: "<system> <attach type> <attachment>", the attachment by
//     name or number (C_BaseAnimating::FireEvent). AE_CL_STOP_PARTICLE_EFFECT "<system>
//     [instantly]" stops it again (Mapbase, from the Alien Swarm SDK; without "instantly" the
//     particles alive at that moment finish their life). A viewmodel passes the event to its
//     weapon first (C_BaseViewModel::FireEvent -> OnFireEvent), then to C_BaseAnimating.
//   - Attach types (particle_parse.cpp, GetAttachTypeFromString): start_at_origin,
//     follow_origin, start_at_customorigin, start_at_attachment, follow_attachment,
//     follow_rootbone. The attachment's place on a viewmodel is converted to the world's view
//     (C_BaseViewModel::FormatViewModelAttachment), so particles land on the weapon.
//   - "Always": the model's $keyvalues block "particles" (C_BaseAnimating::
//     DelayedInitModelEffects): every entry { "name" "<system>" "attachment_type" "<type>"
//     "attachment_point" "<attachment>" } starts when the model appears.
//   - The particle system must be loaded: listed in particles/particles_manifest.txt (the mod's
//     own copy replaces the game's, so it lists the game's files too) or maps/<map>_particles.txt.
//
// Event numbers: named events (AE_*) are stored by name in the model (studiomdl's new-style
// events) and looked up by the game's event list (game/shared/eventlist.cpp); the numbers below
// are those of SDK 2013's eventlist.h for showing old models whose events carry numbers.
//
// The events are model_events.h's ModelEvent (cycle, name, options), the same the Weapons page
// edits. Compiling (model_rebuild.h) takes effectChanges(): events per sequence and particles
// entries, stored apart from the sequence's own events and added on top of them on every
// rebuild, so sound events and effects never overwrite each other. A model built with them is
// read back without them (removeAddedEvents) by comparing with the stored set; the particles
// entries carry the block name "mapsmith_effect".

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/model_events.h>
#include <sme/model_export.h>

namespace sme {

/// Where a particle system sits on the model.
enum class EffectAttach { FollowAttachment, StartAtAttachment, FollowOrigin, StartAtOrigin, FollowRootBone };
/// The game's name of an attach type ("follow_attachment").
[[nodiscard]] std::string_view effectAttachName(EffectAttach attach);
/// Back from the game's name (any case), nullopt for unknown names.
[[nodiscard]] std::optional<EffectAttach> effectAttachFromName(std::string_view name);

/// An effect on a model.
struct ModelEffect {
	std::string particle;                          ///< Particle system name ("muzzle_glow")
	EffectAttach attach = EffectAttach::FollowAttachment;
	std::string attachment;                        ///< Attachment name ("muzzle"); empty for the origin types
	enum class When { Always, DuringAnimation };
	When when = When::DuringAnimation;
	std::string sequence;                          ///< DuringAnimation: the sequence ("reload")
	int startFrame = 0;                            ///< First frame it runs
	int endFrame = -1;                             ///< Frame it stops (-1: runs until it ends by itself)
	bool stopInstantly = false;                    ///< At endFrame: remove the particles at once (else they fade out)
	bool operator==(const ModelEffect&) const = default;
};

/// Problems that keep an effect from working (empty = fine): no particle name, an attachment the
/// model does not have, a sequence it does not have, frames outside the sequence (`frames` of the
/// sequence; 0 = unknown). `attachments` and `sequences` are the model's (empty = not checked).
[[nodiscard]] std::vector<std::string> effectProblems(const ModelEffect& effect, const std::vector<std::string>& attachments,
                                                      const std::vector<std::string>& sequences, int frames = 0);

/// The events a DuringAnimation effect needs in a sequence of `frames` frames: create at
/// startFrame and, with an endFrame, stop there (Mapbase's AE_CL_STOP_PARTICLE_EFFECT). An
/// Always effect gives none (see effectParticles).
[[nodiscard]] std::vector<ModelEvent> effectEvents(const ModelEffect& effect, int frames);
/// Frames of the model's sequences (key: name in lower case).
using SequenceFrames = std::map<std::string, int>;
/// All events of the effects per sequence (lower case), sorted by cycle. Effects on a sequence
/// `frames` does not name are left out (effectProblems reports them).
[[nodiscard]] std::map<std::string, std::vector<ModelEvent>> effectEvents(const std::vector<ModelEffect>& effects, const SequenceFrames& frames);
/// The Always effects as entries of the $keyvalues "particles" block.
[[nodiscard]] std::vector<ModelParticle> effectParticles(const std::vector<ModelEffect>& effects);
/// What compiling the effects into the model adds (for ModelRebuildChanges::effects).
[[nodiscard]] AddedModelEffects effectChanges(const std::vector<ModelEffect>& effects, const SequenceFrames& frames);
/// The $keyvalues "particles" block for the Always effects ("" when there are none), as the
/// .qc gets it (for copying into a .qc of one's own):
///   particles { mapsmith_effect { name "x" attachment_type "follow_attachment" attachment_point "muzzle" } }
[[nodiscard]] std::string effectKeyValues(const std::vector<ModelEffect>& effects);

/// The effects that events and a $keyvalues text describe again (a model compiled by Mapsmith,
/// or one of the game's): create/stop pairs of the same system in one sequence become one effect,
/// the particles block gives the Always ones. Other events are left out. `keyValues` is the text
/// of the model's $keyvalues (may be empty; broken text gives no Always effects).
[[nodiscard]] std::vector<ModelEffect> effectsFromModel(const std::vector<SequenceEvents>& events, std::string_view keyValues);

/// The effects a mapper set up for a model, kept in the mod until the model is compiled with
/// them: <mod>/modelsrc/<model path without .mdl>/mapsmith_effects.txt (KeyValues text; the
/// folder model export writes to). Empty path for a model path that is not safe. The format is
/// the same since the first version (effect { particle attach attachment when sequence start end
/// instantly }); unknown keys are ignored, missing ones take their defaults.
[[nodiscard]] std::filesystem::path modelEffectsFile(const std::filesystem::path& modDir, std::string_view modelPath);
[[nodiscard]] std::string writeEffectsText(const std::vector<ModelEffect>& effects);
/// Reads the text back (untrusted: broken text or values give fewer effects, never more than 256).
[[nodiscard]] std::vector<ModelEffect> readEffectsText(std::string_view text);

/// The name of an event number of SDK 2013's eventlist.h / the old client events ("5004" ->
/// "AE_CL_PLAYSOUND"); empty for unknown numbers.
[[nodiscard]] std::string_view eventNameForNumber(int number);

} // namespace sme
