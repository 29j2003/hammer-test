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

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// One event of a sequence as the .qc writes it: { event <name> <frame> "<options>" }.
struct SequenceEvent {
	std::string sequence;   ///< Sequence name in the model ("reload")
	int frame = 0;          ///< Frame of the sequence (0 = first)
	std::string event;      ///< "AE_CL_CREATE_PARTICLE_EFFECT" (or a number as text for old events)
	std::string options;    ///< "muzzle_glow follow_attachment muzzle"
	bool operator==(const SequenceEvent&) const = default;
};

/// The .qc line of an event (inside $sequence's braces).
[[nodiscard]] std::string qcEventLine(const SequenceEvent& event);

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

/// The events a DuringAnimation effect needs: create at startFrame and, with an endFrame, stop
/// there (Mapbase's AE_CL_STOP_PARTICLE_EFFECT). An Always effect gives none (see effectKeyValues).
[[nodiscard]] std::vector<SequenceEvent> effectEvents(const ModelEffect& effect);
/// All events of the effects, sorted by sequence and frame.
[[nodiscard]] std::vector<SequenceEvent> effectEvents(const std::vector<ModelEffect>& effects);
/// The $keyvalues "particles" block for the Always effects ("" when there are none):
///   particles { effect { name "x" attachment_type "follow_attachment" attachment_point "muzzle" } }
[[nodiscard]] std::string effectKeyValues(const std::vector<ModelEffect>& effects);

/// The effects that events and a $keyvalues text describe again (a model compiled by Mapsmith,
/// or one of the game's): create/stop pairs of the same system in one sequence become one effect,
/// the particles block gives the Always ones. Other events are left out. `keyValues` is the text
/// of the model's $keyvalues (may be empty; broken text gives no Always effects).
[[nodiscard]] std::vector<ModelEffect> effectsFromModel(const std::vector<SequenceEvent>& events, std::string_view keyValues);

/// The name of an event number of SDK 2013's eventlist.h / the old client events ("5004" ->
/// "AE_CL_PLAYSOUND"); empty for unknown numbers.
[[nodiscard]] std::string_view eventNameForNumber(int number);

} // namespace sme
