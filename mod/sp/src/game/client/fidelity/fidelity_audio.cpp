//========= mapbase-fidelity ==================================================//
//
// Purpose: Client-side audio mixer for better blending of many sound sources.
//
// The Source 2013 sound mixer lives in the closed engine, but the client can
// list every playing sound (IEngineSound::GetActiveSounds) and change its
// volume (SetVolumeByGuid). This system sorts each playing sound into a
// category and drives its volume every frame:
//
//   - category volumes  (dialogue, music, ambience, weapons, player, effects)
//   - dialogue ducking  music, ambience and effects dip while someone talks
//   - combat ducking    ambience steps back during firefights / when hurt
//   - headroom          when many loud sounds overlap, everything except
//                       dialogue and music is gently compressed so the mix
//                       stays clear instead of turning into mush
//   - occlusion         sounds behind walls are quieter: a few traces per
//                       frame (round robin) check the line from the listener
//                       to each sound; the gain fades in and out smoothly
//   - shock             right after an explosion nearby everything but the
//                       music drops (the bang itself plays out first) and
//                       comes back over two seconds - the ears are numb (as
//                       MMod's explosions); on death the world fades away
//
// Volumes set by other code (soundscape fades, ambient_generic ramps, sound
// patches) are respected: the mixer remembers each sound's own volume and only
// applies its gain on top. Nothing in the engine or in other game code needs
// to change.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_audio.h"
#include "igamesystem.h"
#include "engine/IEngineSound.h"
#include "engine/SndInfo.h"
#include "filesystem.h"
#include "c_baseplayer.h"
#include "view.h"
#include "utlmap.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar snd_fidelity( "snd_fidelity", "1", FCVAR_ARCHIVE, "Fidelity audio mixer (category volumes, ducking, headroom). 0 = off." );
ConVar snd_fidelity_debug( "snd_fidelity_debug", "0", FCVAR_NONE, "Shows the mixer state on screen. 2 = also lists the playing sounds." );

// Category volumes
ConVar snd_fidelity_vol_dialogue( "snd_fidelity_vol_dialogue", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Volume of dialogue (vo/, sentences)." );
ConVar snd_fidelity_vol_music( "snd_fidelity_vol_music", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Volume of music (music/, #-prefixed sounds)." );
ConVar snd_fidelity_vol_ambience( "snd_fidelity_vol_ambience", "0.9", FCVAR_SERVER_CAN_EXECUTE, "Volume of ambience (ambient/, soundscapes, world sounds)." );
ConVar snd_fidelity_vol_weapons( "snd_fidelity_vol_weapons", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Volume of weapons (weapons/, weapon channel)." );
ConVar snd_fidelity_vol_player( "snd_fidelity_vol_player", "0.85", FCVAR_SERVER_CAN_EXECUTE, "Volume of the player's own body sounds (player/, footsteps)." );
ConVar snd_fidelity_vol_effects( "snd_fidelity_vol_effects", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Volume of everything else (NPCs, physics, doors ...)." );

// Dialogue ducking
ConVar snd_fidelity_duck_dialogue_music( "snd_fidelity_duck_dialogue_music", "0.45", FCVAR_SERVER_CAN_EXECUTE, "Music volume while someone talks." );
ConVar snd_fidelity_duck_dialogue_ambience( "snd_fidelity_duck_dialogue_ambience", "0.55", FCVAR_SERVER_CAN_EXECUTE, "Ambience volume while someone talks." );
ConVar snd_fidelity_duck_dialogue_effects( "snd_fidelity_duck_dialogue_effects", "0.8", FCVAR_SERVER_CAN_EXECUTE, "Effects volume while someone talks." );
ConVar snd_fidelity_duck_dialogue_weapons( "snd_fidelity_duck_dialogue_weapons", "0.9", FCVAR_SERVER_CAN_EXECUTE, "Weapon volume while someone talks." );
ConVar snd_fidelity_dialogue_threshold( "snd_fidelity_dialogue_threshold", "0.05", FCVAR_SERVER_CAN_EXECUTE, "How loud (at the listener) a voice line must be to duck the mix." );
ConVar snd_fidelity_duck_attack( "snd_fidelity_duck_attack", "0.15", FCVAR_SERVER_CAN_EXECUTE, "Seconds to duck down." );
ConVar snd_fidelity_duck_release( "snd_fidelity_duck_release", "0.9", FCVAR_SERVER_CAN_EXECUTE, "Seconds to come back after dialogue." );

// Combat ducking
ConVar snd_fidelity_duck_combat_ambience( "snd_fidelity_duck_combat_ambience", "0.6", FCVAR_SERVER_CAN_EXECUTE, "Ambience volume during combat." );
ConVar snd_fidelity_duck_combat_music( "snd_fidelity_duck_combat_music", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Music volume during combat." );
ConVar snd_fidelity_combat_threshold( "snd_fidelity_combat_threshold", "0.2", FCVAR_SERVER_CAN_EXECUTE, "How loud (at the listener) a weapon sound must be to count as combat." );
ConVar snd_fidelity_combat_hold( "snd_fidelity_combat_hold", "4.0", FCVAR_SERVER_CAN_EXECUTE, "Seconds combat stays active after the last shot or hit." );
ConVar snd_fidelity_combat_release( "snd_fidelity_combat_release", "2.5", FCVAR_SERVER_CAN_EXECUTE, "Seconds to fade back after combat." );

// Headroom
ConVar snd_fidelity_headroom( "snd_fidelity_headroom", "1", FCVAR_ARCHIVE, "Compress the mix when many loud sounds overlap. 0 = off." );
ConVar snd_fidelity_headroom_threshold( "snd_fidelity_headroom_threshold", "2.0", FCVAR_SERVER_CAN_EXECUTE, "Summed loudness (at the listener) above which compression starts." );
ConVar snd_fidelity_headroom_ratio( "snd_fidelity_headroom_ratio", "0.5", FCVAR_SERVER_CAN_EXECUTE, "Compression amount, 0 = none, 1 = hard limit." );
ConVar snd_fidelity_headroom_min( "snd_fidelity_headroom_min", "0.55", FCVAR_SERVER_CAN_EXECUTE, "Lowest gain the compression may apply." );

// Occlusion
ConVar snd_fidelity_occlusion( "snd_fidelity_occlusion", "1", FCVAR_ARCHIVE, "Sounds behind walls are quieter. 0 = off." );
ConVar snd_fidelity_occlusion_traces( "snd_fidelity_occlusion_traces", "8", FCVAR_ARCHIVE, "Line-of-sight checks per frame (each sound is checked again every few frames)." );
ConVar snd_fidelity_occlusion_gain( "snd_fidelity_occlusion_gain", "0.5", FCVAR_SERVER_CAN_EXECUTE, "Volume of a sound that is fully behind a wall, 0..1." );
ConVar snd_fidelity_occlusion_attack( "snd_fidelity_occlusion_attack", "0.12", FCVAR_SERVER_CAN_EXECUTE, "Seconds to fade a sound down when it goes behind a wall." );
ConVar snd_fidelity_occlusion_release( "snd_fidelity_occlusion_release", "0.25", FCVAR_SERVER_CAN_EXECUTE, "Seconds to fade it back when it comes into view." );

// Shock: explosions nearby and death
ConVar snd_fidelity_shock( "snd_fidelity_shock", "1", FCVAR_ARCHIVE, "Explosions nearby numb the ears for a moment, death fades the world out. 0 = off." );
ConVar snd_fidelity_shock_gain( "snd_fidelity_shock_gain", "0.4", FCVAR_SERVER_CAN_EXECUTE, "Volume (all but music) right after an explosion next to the player." );
ConVar snd_fidelity_shock_reach( "snd_fidelity_shock_reach", "600", FCVAR_SERVER_CAN_EXECUTE, "Units from an explosion within which it numbs the ears (more the closer)." );
ConVar snd_fidelity_shock_release( "snd_fidelity_shock_release", "2.0", FCVAR_SERVER_CAN_EXECUTE, "Seconds until the ears are back after an explosion." );
ConVar snd_fidelity_death_gain( "snd_fidelity_death_gain", "0.45", FCVAR_SERVER_CAN_EXECUTE, "Volume (all but music) once the player is dead." );

#define SHOCK_ONSET_START		0.08f	// seconds after the bang: the bang plays out ...
#define SHOCK_ONSET_END			0.2f	// ... then the ears are numb
#define DEATH_FADE				1.2f	// seconds to fade the world out after death

enum FidelitySoundCategory_t
{
	FSND_DIALOGUE = 0,
	FSND_MUSIC,
	FSND_AMBIENCE,
	FSND_WEAPONS,
	FSND_PLAYER,
	FSND_EFFECTS,

	FSND_COUNT
};

static const char *s_pCategoryNames[ FSND_COUNT ] = { "dialogue", "music", "ambience", "weapons", "player", "effects" };

// Moves flValue towards flTarget with separate time constants for falling
// (attack, ducking down) and rising (release).
static float SmoothTowards( float flValue, float flTarget, float flAttack, float flRelease, float flDeltaTime )
{
	float flTime = ( flTarget < flValue ) ? flAttack : flRelease;
	if ( flTime <= 0.0f )
		return flTarget;
	float flBlend = 1.0f - expf( -flDeltaTime / flTime );
	return flValue + ( flTarget - flValue ) * flBlend;
}

// Engine channel volumes are stored as 0..255
static float QuantizeVolume( float flVolume )
{
	return floorf( clamp( flVolume, 0.0f, 1.0f ) * 255.0f + 0.5f ) / 255.0f;
}

//-----------------------------------------------------------------------------
// The mixer
//-----------------------------------------------------------------------------
class CFidelityAudioMixer : public CAutoGameSystemPerFrame
{
public:
	CFidelityAudioMixer();

	virtual void LevelInitPreEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Update( float frametime );

private:
	struct TrackedSound_t
	{
		float m_flBaseVolume;		// the volume other code wants this sound to have
		float m_flAppliedVolume;	// what we last set (quantized), < 0 = nothing set yet
		int m_nCategory;
		int m_nLastSeen;
		float m_flOcclusion;		// 0 = clear .. 1 = fully behind a wall (smoothed)
		float m_flOcclusionTarget;	// result of the last trace
		int m_nLastTrace;			// frame of the last trace
	};

	int Classify( const SndInfo_t &info, const char *pName, int nLocalPlayer ) const;
	bool CanBeOccluded( const SndInfo_t &info, const TrackedSound_t &sound, int nLocalPlayer ) const;
	float TraceOcclusion( const SndInfo_t &info ) const;
	void RestoreAll();
	void DrawDebug( const CUtlVector< SndInfo_t > &sounds ) const;

	CUtlMap< int, TrackedSound_t > m_Sounds;
	CUtlVector< SndInfo_t > m_ActiveSounds;			// this frame's playing sounds
	CUtlVector< unsigned short > m_ActiveIndices;	// their entries in m_Sounds
	CUtlVector< bool > m_Occludable;				// this frame: may each of them be occluded
	float m_flSpatialScale;							// 1, or 1/255 on engines that report 0..255
	int m_nFrame;

	float m_flDialogueDuck;		// 0 = no dialogue .. 1 = fully ducked
	float m_flCombat;			// 0 = calm .. 1 = combat
	float m_flCombatTimer;
	float m_flHeadroomGain;
	float m_flLoudness;
	float m_flShockPeak;		// strength of the last explosion nearby, 0..1
	float m_flShockAge;			// seconds since
	float m_flDeath;			// 0 = alive .. 1 = faded out
public:
	void Explosion( const Vector &vecOrigin );
private:
	int m_nLastHealth;
	int m_nOccluded;
	bool m_bActive;
	bool m_bDialogueActive;
};

static CFidelityAudioMixer s_FidelityAudioMixer;

CFidelityAudioMixer::CFidelityAudioMixer() : CAutoGameSystemPerFrame( "CFidelityAudioMixer" ), m_Sounds( DefLessFunc( int ) )
{
	m_nFrame = 0;
	m_flDialogueDuck = 0.0f;
	m_flCombat = 0.0f;
	m_flCombatTimer = 0.0f;
	m_flHeadroomGain = 1.0f;
	m_flLoudness = 0.0f;
	m_flShockPeak = 0.0f;
	m_flShockAge = 0.0f;
	m_flDeath = 0.0f;
	m_nLastHealth = -1;
	m_nOccluded = 0;
	m_bActive = false;
	m_bDialogueActive = false;
	m_flSpatialScale = 1.0f;
}

void CFidelityAudioMixer::LevelInitPreEntity()
{
	m_Sounds.RemoveAll();
	m_flDialogueDuck = 0.0f;
	m_flCombat = 0.0f;
	m_flCombatTimer = 0.0f;
	m_flHeadroomGain = 1.0f;
	m_flShockPeak = 0.0f;
	m_flDeath = 0.0f;
	m_nLastHealth = -1;
}

//-----------------------------------------------------------------------------
// An explosion (fx_explosion.cpp, through Fidelity_ExplosionLight): the closer,
// the more the ears are numbed (squared falloff); a weaker one never cuts a
// stronger one short
//-----------------------------------------------------------------------------
void CFidelityAudioMixer::Explosion( const Vector &vecOrigin )
{
	if ( !snd_fidelity_shock.GetBool() )
		return;
	const float flReach = MAX( snd_fidelity_shock_reach.GetFloat(), 1.0f );
	const float flNear = 1.0f - clamp( MainViewOrigin().DistTo( vecOrigin ) / flReach, 0.0f, 1.0f );
	const float flStrength = flNear * flNear;
	const float flRelease = MAX( snd_fidelity_shock_release.GetFloat(), 0.1f );
	const float flNow = m_flShockPeak * MAX( 0.0f, 1.0f - MAX( m_flShockAge - SHOCK_ONSET_END, 0.0f ) / flRelease );
	if ( flStrength > flNow )
	{
		m_flShockPeak = flStrength;
		m_flShockAge = 0.0f;
	}
}

void Fidelity_AudioExplosion( const Vector &vecOrigin )
{
	s_FidelityAudioMixer.Explosion( vecOrigin );
}

void CFidelityAudioMixer::LevelShutdownPreEntity()
{
	// Sound guids do not survive a level change
	m_Sounds.RemoveAll();
}

int CFidelityAudioMixer::Classify( const SndInfo_t &info, const char *pName, int nLocalPlayer ) const
{
	// Dialogue: sentences and anything from a vo folder (vo/..., npc/combine_soldier/vo/...)
	if ( info.m_bIsSentence || !Q_strncmp( pName, "vo/", 3 ) || Q_strstr( pName, "/vo/" ) )
		return FSND_DIALOGUE;

	// Music: '#' (dry mix) sounds and the music folder
	if ( info.m_bDryMix || !Q_strncmp( pName, "music/", 6 ) )
		return FSND_MUSIC;

	if ( !Q_strncmp( pName, "weapons/", 8 ) || info.m_nChannel == CHAN_WEAPON )
		return FSND_WEAPONS;

	if ( !Q_strncmp( pName, "player/", 7 ) || ( nLocalPlayer > 0 && info.m_nSoundSource == nLocalPlayer && info.m_nChannel == CHAN_BODY ) )
		return FSND_PLAYER;

	// Ambience: ambient folder, soundscapes and other sounds from the world itself
	if ( !Q_strncmp( pName, "ambient/", 8 ) || info.m_nSoundSource == 0 || info.m_nChannel == CHAN_STATIC )
		return FSND_AMBIENCE;

	return FSND_EFFECTS;
}

//-----------------------------------------------------------------------------
// Sounds that get the wall check: positional sounds from the world, NPCs and
// objects. Music, the player's own sounds and sounds right next to the
// listener are left alone.
//-----------------------------------------------------------------------------
bool CFidelityAudioMixer::CanBeOccluded( const SndInfo_t &info, const TrackedSound_t &sound, int nLocalPlayer ) const
{
	if ( !info.m_pOrigin || info.m_bDryMix )
		return false;
	if ( sound.m_nCategory == FSND_MUSIC || sound.m_nCategory == FSND_PLAYER )
		return false;
	if ( nLocalPlayer > 0 && info.m_nSoundSource == nLocalPlayer )
		return false;
	return info.m_pOrigin->DistToSqr( MainViewOrigin() ) > 48.0f * 48.0f;
}

//-----------------------------------------------------------------------------
// Walls only: the world and brush entities (doors, func_brush). NPCs, props
// and the player (the trace starts inside his hull) do not block sound.
//-----------------------------------------------------------------------------
class CFidelityWallFilter : public CTraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		C_BaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		return pEntity && ( pEntity->IsWorld() || pEntity->GetSolid() == SOLID_BSP );
	}
	virtual TraceType_t GetTraceType() const { return TRACE_EVERYTHING; }
};

//-----------------------------------------------------------------------------
// 1 when a wall is between the listener and the sound, else 0. A hit right
// at the sound (a speaker mounted on a wall, a door that makes the sound)
// does not count.
//-----------------------------------------------------------------------------
float CFidelityAudioMixer::TraceOcclusion( const SndInfo_t &info ) const
{
	CFidelityWallFilter filter;
	trace_t tr;
	UTIL_TraceLine( MainViewOrigin(), *info.m_pOrigin, CONTENTS_SOLID | CONTENTS_MOVEABLE, &filter, &tr );
	if ( tr.fraction >= 1.0f || tr.startsolid )
		return 0.0f;
	return tr.endpos.DistToSqr( *info.m_pOrigin ) > 24.0f * 24.0f ? 1.0f : 0.0f;
}

void CFidelityAudioMixer::RestoreAll()
{
	for ( unsigned short i = m_Sounds.FirstInorder(); i != m_Sounds.InvalidIndex(); i = m_Sounds.NextInorder( i ) )
	{
		const TrackedSound_t &sound = m_Sounds[ i ];
		if ( sound.m_flAppliedVolume >= 0.0f && enginesound->IsSoundStillPlaying( m_Sounds.Key( i ) ) )
			enginesound->SetVolumeByGuid( m_Sounds.Key( i ), sound.m_flBaseVolume );
	}
	m_Sounds.RemoveAll();
}

void CFidelityAudioMixer::Update( float frametime )
{
	if ( !snd_fidelity.GetBool() || !engine->IsInGame() )
	{
		if ( m_bActive )
		{
			RestoreAll();
			m_bActive = false;
		}
		return;
	}
	m_bActive = true;
	m_nFrame++;

	// Real time between frames; frametime is 0 while paused, and that is fine:
	// envelopes simply hold.
	const float dt = clamp( frametime, 0.0f, 0.25f );

	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	const int nLocalPlayer = pLocal ? pLocal->entindex() : -1;

	// Member lists keep their memory between frames: no allocation per frame.
	CUtlVector< SndInfo_t > &sounds = m_ActiveSounds;
	sounds.RemoveAll();
	enginesound->GetActiveSounds( sounds );

	// Some engine builds report spatialized volume as 0..255 instead of 0..1.
	// Once seen, it stays (frames with only quiet sounds would look like 0..1).
	for ( int i = 0; i < sounds.Count() && m_flSpatialScale == 1.0f; i++ )
	{
		if ( sounds[ i ].m_flLastSpatializedVolume > 1.5f )
			m_flSpatialScale = 1.0f / 255.0f;
	}
	const float flSpatialScale = m_flSpatialScale;

	// --- Pass 1: classify, track base volumes and measure the scene
	bool bDialogue = false;
	bool bCombatEvent = false;
	float flLoudness = 0.0f;
	char szName[ MAX_PATH ];

	CUtlVector< unsigned short > &active = m_ActiveIndices;
	active.RemoveAll();
	active.EnsureCapacity( sounds.Count() );

	for ( int i = 0; i < sounds.Count(); i++ )
	{
		const SndInfo_t &info = sounds[ i ];
		const float flVolume = QuantizeVolume( info.m_flVolume );
		const float flHeard = info.m_flLastSpatializedVolume * flSpatialScale;

		unsigned short index = m_Sounds.Find( info.m_nGuid );
		bool bNew = false;
		if ( index == m_Sounds.InvalidIndex() )
		{
			szName[ 0 ] = 0;
			if ( !filesystem->String( info.m_filenameHandle, szName, sizeof( szName ) ) )
				szName[ 0 ] = 0;
			Q_FixSlashes( szName, '/' );
			Q_strlower( szName );
			const char *pName = szName;
			if ( !Q_strncmp( pName, "sound/", 6 ) )
				pName += 6;

			TrackedSound_t sound;
			sound.m_flBaseVolume = flVolume;
			sound.m_flAppliedVolume = -1.0f;
			sound.m_nCategory = Classify( info, pName, nLocalPlayer );
			sound.m_nLastSeen = m_nFrame;
			sound.m_flOcclusion = 0.0f;
			sound.m_flOcclusionTarget = 0.0f;
			sound.m_nLastTrace = 0;
			index = m_Sounds.Insert( info.m_nGuid, sound );
			bNew = true;
		}

		TrackedSound_t &sound = m_Sounds[ index ];
		sound.m_nLastSeen = m_nFrame;

		// Someone else changed the volume since our last write -> that is the new base.
		if ( sound.m_flAppliedVolume < 0.0f || fabsf( flVolume - sound.m_flAppliedVolume ) > 0.6f / 255.0f )
			sound.m_flBaseVolume = flVolume;

		active.AddToTail( index );

		// what the sound would be without our gain (the compressor must not
		// measure its own output: that softened it to two thirds of its ratio)
		const float flUngained = flHeard * ( sound.m_flAppliedVolume > 0.0f ? MIN( sound.m_flBaseVolume / sound.m_flAppliedVolume, 4.0f ) : 1.0f );

		switch ( sound.m_nCategory )
		{
		case FSND_DIALOGUE:
			if ( flHeard >= snd_fidelity_dialogue_threshold.GetFloat() )
				bDialogue = true;
			break;

		case FSND_WEAPONS:
			if ( bNew && flHeard >= snd_fidelity_combat_threshold.GetFloat() )
				bCombatEvent = true;
			flLoudness += flUngained;
			break;

		case FSND_MUSIC:
			break;

		default:
			flLoudness += flUngained;
			break;
		}
	}

	// Forget sounds that stopped
	for ( unsigned short i = m_Sounds.FirstInorder(); i != m_Sounds.InvalidIndex(); )
	{
		unsigned short next = m_Sounds.NextInorder( i );
		if ( m_Sounds[ i ].m_nLastSeen != m_nFrame )
			m_Sounds.RemoveAt( i );
		i = next;
	}

	// Taking damage also counts as combat
	if ( pLocal )
	{
		const int nHealth = pLocal->GetHealth();
		if ( m_nLastHealth >= 0 && nHealth < m_nLastHealth )
			bCombatEvent = true;
		m_nLastHealth = nHealth;
	}

	// --- Envelopes
	m_bDialogueActive = bDialogue;
	// (ducking amounts rise when ducking: the attack time is their rising one)
	m_flDialogueDuck = SmoothTowards( m_flDialogueDuck, bDialogue ? 1.0f : 0.0f,
		snd_fidelity_duck_release.GetFloat(), snd_fidelity_duck_attack.GetFloat(), dt );

	if ( bCombatEvent )
		m_flCombatTimer = snd_fidelity_combat_hold.GetFloat();
	else
		m_flCombatTimer = MAX( 0.0f, m_flCombatTimer - dt );
	m_flCombat = SmoothTowards( m_flCombat, m_flCombatTimer > 0.0f ? 1.0f : 0.0f,
		snd_fidelity_combat_release.GetFloat(), 0.5f, dt );

	m_flLoudness = flLoudness;
	float flHeadroomTarget = 1.0f;
	const float flThreshold = MAX( 0.1f, snd_fidelity_headroom_threshold.GetFloat() );
	if ( snd_fidelity_headroom.GetBool() && flLoudness > flThreshold )
	{
		flHeadroomTarget = powf( flThreshold / flLoudness, clamp( snd_fidelity_headroom_ratio.GetFloat(), 0.0f, 1.0f ) );
		flHeadroomTarget = MAX( flHeadroomTarget, clamp( snd_fidelity_headroom_min.GetFloat(), 0.0f, 1.0f ) );
	}
	m_flHeadroomGain = SmoothTowards( m_flHeadroomGain, flHeadroomTarget, 0.05f, 0.6f, dt );

	// Shock: numb after a blast (from the onset on, then back), faded out in death
	float flShock = 1.0f;
	if ( snd_fidelity_shock.GetBool() )
	{
		m_flShockAge += dt;
		const float flRelease = MAX( snd_fidelity_shock_release.GetFloat(), 0.1f );
		const float flEnvelope = m_flShockPeak * SimpleSpline( RemapValClamped( m_flShockAge, SHOCK_ONSET_START, SHOCK_ONSET_END, 0.0f, 1.0f ) )
			* MAX( 0.0f, 1.0f - MAX( m_flShockAge - SHOCK_ONSET_END, 0.0f ) / flRelease );
		m_flDeath = ( pLocal && !pLocal->IsAlive() ) ? MIN( m_flDeath + dt / DEATH_FADE, 1.0f ) : MAX( m_flDeath - dt / 0.3f, 0.0f );
		flShock = Lerp( flEnvelope, 1.0f, clamp( snd_fidelity_shock_gain.GetFloat(), 0.0f, 1.0f ) )
			* Lerp( m_flDeath, 1.0f, clamp( snd_fidelity_death_gain.GetFloat(), 0.0f, 1.0f ) );
	}
	else
	{
		m_flShockPeak = 0.0f;
		m_flDeath = 0.0f;
	}

	// --- Occlusion: trace the sounds that waited longest, a few per frame
	m_nOccluded = 0;
	const bool bOcclusion = snd_fidelity_occlusion.GetBool();
	m_Occludable.SetCount( active.Count() );
	for ( int i = 0; i < active.Count(); i++ )
		m_Occludable[ i ] = bOcclusion && CanBeOccluded( sounds[ i ], m_Sounds[ active[ i ] ], nLocalPlayer );
	if ( bOcclusion )
	{
		int nBudget = clamp( snd_fidelity_occlusion_traces.GetInt(), 0, 64 );
		while ( nBudget-- > 0 )
		{
			int nOldest = -1;
			int nOldestFrame = m_nFrame;
			for ( int i = 0; i < active.Count(); i++ )
			{
				const TrackedSound_t &sound = m_Sounds[ active[ i ] ];
				if ( sound.m_nLastTrace < nOldestFrame && m_Occludable[ i ] )
				{
					nOldestFrame = sound.m_nLastTrace;
					nOldest = i;
				}
			}
			if ( nOldest < 0 )
				break;
			TrackedSound_t &sound = m_Sounds[ active[ nOldest ] ];
			// a new sound starts where its trace says (a shot behind a wall
			// played its loudest part unoccluded while the gain faded in)
			const bool bFirstTrace = sound.m_nLastTrace == 0;
			sound.m_flOcclusionTarget = TraceOcclusion( sounds[ nOldest ] );
			if ( bFirstTrace )
				sound.m_flOcclusion = sound.m_flOcclusionTarget;
			sound.m_nLastTrace = m_nFrame;
		}
	}

	for ( int i = 0; i < active.Count(); i++ )
	{
		TrackedSound_t &sound = m_Sounds[ active[ i ] ];
		const float flTarget = ( bOcclusion && m_Occludable[ i ] ) ? sound.m_flOcclusionTarget : 0.0f;
		// Rising occlusion = going behind a wall = fading down (attack)
		sound.m_flOcclusion = SmoothTowards( sound.m_flOcclusion, flTarget,
			snd_fidelity_occlusion_release.GetFloat(), snd_fidelity_occlusion_attack.GetFloat(), dt );
		if ( sound.m_flOcclusion > 0.5f )
			m_nOccluded++;
	}
	const float flOcclusionGain = clamp( snd_fidelity_occlusion_gain.GetFloat(), 0.0f, 1.0f );

	// --- Per-category gains
	float flGain[ FSND_COUNT ];
	flGain[ FSND_DIALOGUE ] = snd_fidelity_vol_dialogue.GetFloat();
	flGain[ FSND_MUSIC ] = snd_fidelity_vol_music.GetFloat()
		* Lerp( m_flDialogueDuck, 1.0f, snd_fidelity_duck_dialogue_music.GetFloat() )
		* Lerp( m_flCombat, 1.0f, snd_fidelity_duck_combat_music.GetFloat() );
	flGain[ FSND_AMBIENCE ] = snd_fidelity_vol_ambience.GetFloat()
		* Lerp( m_flDialogueDuck, 1.0f, snd_fidelity_duck_dialogue_ambience.GetFloat() )
		* Lerp( m_flCombat, 1.0f, snd_fidelity_duck_combat_ambience.GetFloat() )
		* m_flHeadroomGain;
	flGain[ FSND_WEAPONS ] = snd_fidelity_vol_weapons.GetFloat()
		* Lerp( m_flDialogueDuck, 1.0f, snd_fidelity_duck_dialogue_weapons.GetFloat() )
		* m_flHeadroomGain;
	flGain[ FSND_PLAYER ] = snd_fidelity_vol_player.GetFloat() * m_flHeadroomGain;
	flGain[ FSND_EFFECTS ] = snd_fidelity_vol_effects.GetFloat()
		* Lerp( m_flDialogueDuck, 1.0f, snd_fidelity_duck_dialogue_effects.GetFloat() )
		* m_flHeadroomGain;
	for ( int c = 0; c < FSND_COUNT; c++ )
	{
		if ( c != FSND_MUSIC )
			flGain[ c ] *= flShock;
	}

	// --- Pass 2: apply
	for ( int i = 0; i < active.Count(); i++ )
	{
		TrackedSound_t &sound = m_Sounds[ active[ i ] ];
		const float flOcclusion = Lerp( sound.m_flOcclusion, 1.0f, flOcclusionGain );
		const float flTarget = QuantizeVolume( sound.m_flBaseVolume * MAX( 0.0f, flGain[ sound.m_nCategory ] ) * flOcclusion );
		if ( sound.m_flAppliedVolume < 0.0f || flTarget != sound.m_flAppliedVolume )
		{
			enginesound->SetVolumeByGuid( m_Sounds.Key( active[ i ] ), flTarget );
			sound.m_flAppliedVolume = flTarget;
		}
	}

	if ( snd_fidelity_debug.GetInt() > 0 )
		DrawDebug( sounds );
}

void CFidelityAudioMixer::DrawDebug( const CUtlVector< SndInfo_t > &sounds ) const
{
	int nLine = 0;
	engine->Con_NPrintf( nLine++, "[fidelity mixer] sounds %d  dialogue %s (duck %.2f)  combat %.2f (%.1fs)  loudness %.2f  headroom %.2f  behind walls %d",
		sounds.Count(), m_bDialogueActive ? "ON" : "off", m_flDialogueDuck, m_flCombat, m_flCombatTimer, m_flLoudness, m_flHeadroomGain, m_nOccluded );

	if ( snd_fidelity_debug.GetInt() < 2 )
		return;

	nLine = 2;	// line 1 belongs to the automatic reverb (fidelity_reverb.cpp)

	char szName[ MAX_PATH ];
	for ( int i = 0; i < sounds.Count() && nLine < 40; i++ )
	{
		const SndInfo_t &info = sounds[ i ];
		unsigned short index = m_Sounds.Find( info.m_nGuid );
		if ( index == m_Sounds.InvalidIndex() )
			continue;
		const TrackedSound_t &sound = m_Sounds[ index ];
		if ( !filesystem->String( info.m_filenameHandle, szName, sizeof( szName ) ) )
			Q_strncpy( szName, "?", sizeof( szName ) );
		engine->Con_NPrintf( nLine++, "%-9s base %.2f -> %.2f  heard %.2f  wall %.2f  ch %d  ent %d  %s",
			s_pCategoryNames[ sound.m_nCategory ], sound.m_flBaseVolume, sound.m_flAppliedVolume,
			info.m_flLastSpatializedVolume, sound.m_flOcclusion, info.m_nChannel, info.m_nSoundSource, szName );
	}
}
