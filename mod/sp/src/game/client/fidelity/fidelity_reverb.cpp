//========= mapbase-fidelity ==================================================//
//
// Purpose: Automatic room reverb where the soundscape sets none.
//
// The engine's reverb (DSP) has one "room type" for the listener, normally set
// by the soundscape's "dsp" key. Areas without a soundscape, or with one that
// sets no "dsp", keep whatever room was set last - a small corridor can sound
// like the hall the player came from. This system measures the space around
// the listener in the world (not on screen, so turning around changes
// nothing) and picks a fitting room type:
//
//   - 12 rays from the eye (8 around, 4 diagonally up) plus one straight up,
//     7 every 0.05 s in turn, so a full survey takes a tenth of a second
//     (about 140 short traces per second)
//   - sky (2D sky or skybox brushes) above (straight up, or two of the
//     diagonal rays: a ledge right above is no roof) -> outside, or a
//     courtyard / street between houses when the walls around are close
//     (the median of the 8 rays around: one wall or pillar next to the
//     player moves it hardly at all - by the share of open rays, stepping
//     up to a pillar turned the yard into a courtyard; Julius: "nur an die
//     Wand nahe gegangen"), with hysteresis (in below 400, out above 640)
//   - narrow and long with a low roof, closed at both ends, 768 or longer
//     -> tunnel (small / medium / large); a ray out through a door is no tunnel
//     (a small room by a door flipped between room and tunnel)
//   - otherwise a room by its median size, its sound by what most surfaces are
//     made of (surface properties of the brushes the rays hit):
//       metal, grate, vent               -> metal room (rings)
//       tile, glass                      -> bright room (hard, clear echo)
//       wood, dirt, sand, grass, flesh   -> soft room (swallows sound)
//       concrete, plastic, the rest      -> concrete room
//
// Every room change restarts the engine's reverb (its echoes start over), so
// changes must be rare: each ray's distance is smoothed over a few surveys
// (a single step does not move a room across a size limit), a new room must
// stay the same for snd_fidelity_reverb_delay seconds, and two changes are at
// least REVERB_MIN_SWITCH seconds apart. Seen in game before: walking made
// the room flip between sizes and every footstep echoed in fast, chopped
// repeats. The room numbers are the engine's room types
// (scripts/dsp_presets.txt) and can be changed per convar. As soon as a soundscape sets its own "dsp" (other than
// 0), the soundscape wins.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_reverb.h"
#include "igamesystem.h"
#include "engine/IEngineSound.h"
#include "c_baseplayer.h"
#include "view.h"
#include "decals.h"
#include "physics_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar snd_fidelity_reverb( "snd_fidelity_reverb", "1", FCVAR_ARCHIVE, "Automatic room reverb where the soundscape sets no \"dsp\". 0 = off." );
ConVar snd_fidelity_reverb_delay( "snd_fidelity_reverb_delay", "0.6", FCVAR_SERVER_CAN_EXECUTE, "Seconds a new room must stay the same before its reverb is applied." );

// Size limits (average distance to the surroundings, in units)
ConVar snd_fidelity_reverb_size_small( "snd_fidelity_reverb_size_small", "160", FCVAR_SERVER_CAN_EXECUTE, "Rooms smaller than this (average distance in units) are small." );
ConVar snd_fidelity_reverb_size_medium( "snd_fidelity_reverb_size_medium", "448", FCVAR_SERVER_CAN_EXECUTE, "Rooms smaller than this are medium." );
ConVar snd_fidelity_reverb_size_large( "snd_fidelity_reverb_size_large", "1024", FCVAR_SERVER_CAN_EXECUTE, "Rooms smaller than this are large, bigger ones huge." );

// Room types (engine DSP presets) per kind of space
ConVar snd_fidelity_reverb_outside( "snd_fidelity_reverb_outside", "1", FCVAR_SERVER_CAN_EXECUTE, "Room type outside (1 = generic)." );
ConVar snd_fidelity_reverb_small( "snd_fidelity_reverb_small", "18", FCVAR_SERVER_CAN_EXECUTE, "Room type of a small room (18 = concrete medium)." );
ConVar snd_fidelity_reverb_medium( "snd_fidelity_reverb_medium", "19", FCVAR_SERVER_CAN_EXECUTE, "Room type of a medium room (19 = concrete large)." );
ConVar snd_fidelity_reverb_large( "snd_fidelity_reverb_large", "20", FCVAR_SERVER_CAN_EXECUTE, "Room type of a large room (20 = big 1)." );
ConVar snd_fidelity_reverb_huge( "snd_fidelity_reverb_huge", "21", FCVAR_SERVER_CAN_EXECUTE, "Room type of a huge hall (21 = big 2)." );
ConVar snd_fidelity_reverb_courtyard( "snd_fidelity_reverb_courtyard", "18", FCVAR_SERVER_CAN_EXECUTE, "Room type under open sky with walls all around (18 = concrete medium)." );
ConVar snd_fidelity_reverb_bright_small( "snd_fidelity_reverb_bright_small", "12", FCVAR_SERVER_CAN_EXECUTE, "Room type of a small tiled / glass room (12 = bright medium)." );
ConVar snd_fidelity_reverb_bright_medium( "snd_fidelity_reverb_bright_medium", "13", FCVAR_SERVER_CAN_EXECUTE, "Room type of a medium tiled / glass room (13 = bright large)." );
ConVar snd_fidelity_reverb_bright_large( "snd_fidelity_reverb_bright_large", "13", FCVAR_SERVER_CAN_EXECUTE, "Room type of a large tiled / glass room (13 = bright large)." );
ConVar snd_fidelity_reverb_soft_small( "snd_fidelity_reverb_soft_small", "17", FCVAR_SERVER_CAN_EXECUTE, "Room type of a small wooden / earthy room (17 = concrete small)." );
ConVar snd_fidelity_reverb_soft_medium( "snd_fidelity_reverb_soft_medium", "18", FCVAR_SERVER_CAN_EXECUTE, "Room type of a medium wooden / earthy room (18 = concrete medium)." );
ConVar snd_fidelity_reverb_soft_large( "snd_fidelity_reverb_soft_large", "19", FCVAR_SERVER_CAN_EXECUTE, "Room type of a large wooden / earthy room (19 = concrete large)." );
ConVar snd_fidelity_reverb_metal_small( "snd_fidelity_reverb_metal_small", "3", FCVAR_SERVER_CAN_EXECUTE, "Room type of a small metal room (3 = metal medium)." );
ConVar snd_fidelity_reverb_metal_medium( "snd_fidelity_reverb_metal_medium", "4", FCVAR_SERVER_CAN_EXECUTE, "Room type of a medium metal room (4 = metal large)." );
ConVar snd_fidelity_reverb_metal_large( "snd_fidelity_reverb_metal_large", "4", FCVAR_SERVER_CAN_EXECUTE, "Room type of a large metal room (4 = metal large)." );
ConVar snd_fidelity_reverb_tunnel_small( "snd_fidelity_reverb_tunnel_small", "5", FCVAR_SERVER_CAN_EXECUTE, "Room type of a short tunnel (5 = tunnel small)." );
ConVar snd_fidelity_reverb_tunnel_medium( "snd_fidelity_reverb_tunnel_medium", "6", FCVAR_SERVER_CAN_EXECUTE, "Room type of a tunnel (6 = tunnel medium)." );
ConVar snd_fidelity_reverb_tunnel_large( "snd_fidelity_reverb_tunnel_large", "7", FCVAR_SERVER_CAN_EXECUTE, "Room type of a long tunnel (7 = tunnel large)." );

extern ConVar snd_fidelity_debug;

#define REVERB_RAYS				13		// 8 around, 4 diagonally up, 1 straight up
#define REVERB_RAY_UP			12
#define REVERB_RAYS_PER_UPDATE	7
#define REVERB_UPDATE_INTERVAL	0.05f
#define REVERB_MAX_DISTANCE		2048.0f
#define REVERB_COURTYARD_IN		400.0f	// under the sky, median distance around below this: courtyard
#define REVERB_COURTYARD_OUT	640.0f	// and back outside only above this
#define REVERB_TUNNEL_MIN		768.0f	// shorter "tunnels" are small rooms
#define REVERB_SIN45			0.70710678f	// sin / cos of 45 degrees (M_SQRT1_2 is not in MSVC's math.h by default)
// Seconds between two room changes at least (each one restarts the reverb)
#define REVERB_MIN_SWITCH		2.0f
// How much of a new measurement goes into a ray's distance (rest: the old one)
#define REVERB_SMOOTHING		0.3f

//-----------------------------------------------------------------------------
// Measures the space around the listener and sets the engine's room type.
//-----------------------------------------------------------------------------
class CFidelityReverb : public CAutoGameSystemPerFrame
{
public:
	CFidelityReverb() : CAutoGameSystemPerFrame( "CFidelityReverb" ) { Reset(); }

	virtual void LevelInitPreEntity() { Reset(); }
	virtual void LevelShutdownPostEntity() { Reset(); }
	virtual void Update( float frametime );

	void SoundscapeStarted() { m_bSoundscapeDSP = false; }
	// Room 0 ("off") is what empty and default soundscapes set (also when a map has
	// none at all): that does not count as the soundscape choosing a room.
	void SoundscapeSetDSP( int nRoomType ) { m_bSoundscapeDSP = nRoomType != 0; m_nApplied = nRoomType; }

private:
	// What a surface does to sound
	enum Surface_t { SURFACE_HARD, SURFACE_METAL, SURFACE_BRIGHT, SURFACE_SOFT, SURFACE_COUNT };

	struct Ray_t
	{
		float m_flDistance;
		bool m_bOpen;		// sky or nothing within reach
		int m_nSurface;		// Surface_t of the brush it hit
	};

	static int SurfaceOf( const trace_t &tr );

	void Reset();
	void TraceRay( int nRay, const Vector &vecEye );
	int Classify( char *pszWhy, int nWhySize );
	static float Median( float *pValues, int nCount );
	void Apply( int nRoomType );

	Ray_t m_Rays[ REVERB_RAYS ];
	Vector m_vecDirections[ REVERB_RAYS ];
	int m_nNextRay;
	int m_nTraced;			// rays traced since the level started (survey complete at REVERB_RAYS)
	float m_flNextUpdate;
	int m_nCandidate;		// room the survey suggests
	float m_flCandidateSince;
	int m_nApplied;			// room set last (by us or the soundscape), -1 = unknown
	float m_flAppliedAt;	// when we set it
	char m_szWhy[ 64 ];		// what the last survey found, for snd_fidelity_debug
	bool m_bSoundscapeDSP;	// the active soundscape set its own "dsp"
	bool m_bWeSetIt;		// the current room type came from us
	bool m_bCourtyard;		// last survey under the sky found a courtyard (hysteresis)
};

static CFidelityReverb s_FidelityReverb;

void Fidelity_SoundscapeStarted()
{
	s_FidelityReverb.SoundscapeStarted();
}

void Fidelity_SoundscapeSetDSP( int nRoomType )
{
	s_FidelityReverb.SoundscapeSetDSP( nRoomType );
}

void CFidelityReverb::Reset()
{
	for ( int i = 0; i < REVERB_RAYS; i++ )
	{
		m_Rays[ i ].m_flDistance = REVERB_MAX_DISTANCE;
		m_Rays[ i ].m_bOpen = true;
		m_Rays[ i ].m_nSurface = SURFACE_HARD;

		// 8 rays around at eye height, 4 at 45 degrees up, 1 straight up
		if ( i < 8 )
		{
			const float flYaw = DEG2RAD( 45.0f * i );
			m_vecDirections[ i ].Init( cosf( flYaw ), sinf( flYaw ), 0.0f );
		}
		else if ( i < REVERB_RAY_UP )
		{
			const float flYaw = DEG2RAD( 90.0f * ( i - 8 ) + 45.0f );
			m_vecDirections[ i ].Init( cosf( flYaw ) * REVERB_SIN45, sinf( flYaw ) * REVERB_SIN45, REVERB_SIN45 );
		}
		else
		{
			m_vecDirections[ i ].Init( 0.0f, 0.0f, 1.0f );
		}
	}
	m_nNextRay = 0;
	m_nTraced = 0;
	m_flNextUpdate = 0.0f;
	m_nCandidate = -1;
	m_flCandidateSince = 0.0f;
	m_nApplied = -1;
	m_bCourtyard = false;
	m_flAppliedAt = -REVERB_MIN_SWITCH;
	m_bSoundscapeDSP = false;
	m_bWeSetIt = false;
	m_szWhy[ 0 ] = 0;
}

int CFidelityReverb::SurfaceOf( const trace_t &tr )
{
	const surfacedata_t *pSurface = physprops ? physprops->GetSurfaceData( tr.surface.surfaceProps ) : NULL;
	if ( !pSurface )
		return SURFACE_HARD;

	switch ( pSurface->game.material )
	{
	case CHAR_TEX_METAL:
	case CHAR_TEX_VENT:
	case CHAR_TEX_GRATE:
	case CHAR_TEX_COMPUTER:
		return SURFACE_METAL;

	case CHAR_TEX_TILE:
	case CHAR_TEX_GLASS:
		return SURFACE_BRIGHT;

	case CHAR_TEX_WOOD:
	case CHAR_TEX_DIRT:
	case CHAR_TEX_SAND:
	case CHAR_TEX_FOLIAGE:
	case CHAR_TEX_FLESH:
	case CHAR_TEX_BLOODYFLESH:
	case CHAR_TEX_ALIENFLESH:
	case CHAR_TEX_ANTLION:
	case CHAR_TEX_SLOSH:
		return SURFACE_SOFT;

	default:
		return SURFACE_HARD;
	}
}

void CFidelityReverb::TraceRay( int nRay, const Vector &vecEye )
{
	trace_t tr;
	UTIL_TraceLine( vecEye, vecEye + m_vecDirections[ nRay ] * REVERB_MAX_DISTANCE, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );

	// Rays into the sky (2D sky or toolsskybox brushes) or into nothing are open.
	Ray_t &ray = m_Rays[ nRay ];
	ray.m_bOpen = !tr.DidHit() || ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) != 0;
	const float flDistance = ray.m_bOpen ? REVERB_MAX_DISTANCE : tr.fraction * REVERB_MAX_DISTANCE;
	// Smoothed once the first survey is complete (before that: no history yet)
	ray.m_flDistance = m_nTraced < REVERB_RAYS ? flDistance : Lerp( REVERB_SMOOTHING, ray.m_flDistance, flDistance );
	ray.m_nSurface = ray.m_bOpen ? SURFACE_HARD : SurfaceOf( tr );
}

// The middle value (sorts pValues)
float CFidelityReverb::Median( float *pValues, int nCount )
{
	for ( int i = 1; i < nCount; i++ )
	{
		const float flValue = pValues[ i ];
		int j = i - 1;
		for ( ; j >= 0 && pValues[ j ] > flValue; j-- )
			pValues[ j + 1 ] = pValues[ j ];
		pValues[ j + 1 ] = flValue;
	}
	return ( nCount & 1 ) ? pValues[ nCount / 2 ] : 0.5f * ( pValues[ nCount / 2 - 1 ] + pValues[ nCount / 2 ] );
}

int CFidelityReverb::Classify( char *pszWhy, int nWhySize )
{
	int nOpen = 0;
	int nSurfaces[ SURFACE_COUNT ] = { 0, 0, 0, 0 };
	for ( int i = 0; i < REVERB_RAYS; i++ )
	{
		if ( m_Rays[ i ].m_bOpen )
		{
			nOpen++;
			continue;
		}
		nSurfaces[ m_Rays[ i ].m_nSurface ]++;
	}

	const Ray_t &up = m_Rays[ REVERB_RAY_UP ];
	const float flOpenFraction = (float)nOpen / REVERB_RAYS;
	// Sky above: straight up, or at least two of the four diagonal rays (a
	// roof edge or ledge right above made the street a room - Julius: outside
	// the sound changed close to a wall)
	int nDiagonalSky = 0;
	for ( int i = 8; i < REVERB_RAY_UP; i++ )
	{
		if ( m_Rays[ i ].m_bOpen )
			nDiagonalSky++;
	}
	// How far the walls are around: the median of the 8 rays around (open
	// ones count as far), so a single wall or pillar close by changes little
	float flAround[ 8 ];
	for ( int i = 0; i < 8; i++ )
		flAround[ i ] = m_Rays[ i ].m_flDistance;
	const float flMedianAround = Median( flAround, 8 );

	if ( up.m_bOpen || nDiagonalSky >= 2 )
	{
		m_bCourtyard = flMedianAround < ( m_bCourtyard ? REVERB_COURTYARD_OUT : REVERB_COURTYARD_IN );
		if ( !m_bCourtyard )
		{
			Q_snprintf( pszWhy, nWhySize, "outside (around %.0f, open %.0f%%)", flMedianAround, flOpenFraction * 100.0f );
			return snd_fidelity_reverb_outside.GetInt();
		}
		Q_snprintf( pszWhy, nWhySize, "courtyard (around %.0f, open %.0f%%)", flMedianAround, flOpenFraction * 100.0f );
		return snd_fidelity_reverb_courtyard.GetInt();
	}
	m_bCourtyard = false;

	// The surface most rays hit decides the character (ties: hard).
	int nSurface = SURFACE_HARD;
	for ( int i = 0; i < SURFACE_COUNT; i++ )
	{
		if ( nSurfaces[ i ] > nSurfaces[ nSurface ] )
			nSurface = i;
	}

	// The median of all rays (open ones count as far): a door or window
	// moves it little, a big opening (many rays out) makes the room larger
	float flAll[ REVERB_RAYS ];
	for ( int i = 0; i < REVERB_RAYS; i++ )
		flAll[ i ] = m_Rays[ i ].m_flDistance;
	const float flSize = Median( flAll, REVERB_RAYS );
	const int nSize = flSize < snd_fidelity_reverb_size_small.GetFloat() ? 0 :
		flSize < snd_fidelity_reverb_size_medium.GetFloat() ? 1 :
		flSize < snd_fidelity_reverb_size_large.GetFloat() ? 2 : 3;
	static const char *s_pSizeNames[] = { "small", "medium", "large", "huge" };

	// Tunnel: narrow across one axis, long along it (closed at both ends: a
	// ray out through a door made a small room a tunnel), low roof
	float flWidth = REVERB_MAX_DISTANCE * 2.0f, flLength = 0.0f;
	for ( int i = 0; i < 4; i++ )
	{
		const float flAcross = m_Rays[ i ].m_flDistance + m_Rays[ i + 4 ].m_flDistance;
		flWidth = MIN( flWidth, flAcross );
		if ( !m_Rays[ i ].m_bOpen && !m_Rays[ i + 4 ].m_bOpen )
			flLength = MAX( flLength, flAcross );
	}
	const float flCeiling = up.m_flDistance;
	if ( flWidth < 288.0f && flCeiling < 288.0f && flLength >= REVERB_TUNNEL_MIN && flLength > 3.0f * flWidth )
	{
		Q_snprintf( pszWhy, nWhySize, "tunnel (width %.0f, length %.0f)", flWidth, flLength );
		if ( flLength < 1024.0f )
			return snd_fidelity_reverb_tunnel_small.GetInt();
		return flLength < 2048.0f ? snd_fidelity_reverb_tunnel_medium.GetInt() : snd_fidelity_reverb_tunnel_large.GetInt();
	}

	static const char *s_pSurfaceNames[ SURFACE_COUNT ] = { "concrete", "metal", "bright", "soft" };
	Q_snprintf( pszWhy, nWhySize, "%s %s room (size %.0f)", s_pSizeNames[ nSize ], s_pSurfaceNames[ nSurface ], flSize );

	// Metal, bright and soft rooms have three sizes; huge counts as large.
	const ConVar *pSizes[ SURFACE_COUNT ][ 3 ] =
	{
		{ NULL, NULL, NULL },	// hard: four sizes, below
		{ &snd_fidelity_reverb_metal_small, &snd_fidelity_reverb_metal_medium, &snd_fidelity_reverb_metal_large },
		{ &snd_fidelity_reverb_bright_small, &snd_fidelity_reverb_bright_medium, &snd_fidelity_reverb_bright_large },
		{ &snd_fidelity_reverb_soft_small, &snd_fidelity_reverb_soft_medium, &snd_fidelity_reverb_soft_large },
	};
	if ( nSurface != SURFACE_HARD )
		return pSizes[ nSurface ][ MIN( nSize, 2 ) ]->GetInt();

	switch ( nSize )
	{
	case 0: return snd_fidelity_reverb_small.GetInt();
	case 1: return snd_fidelity_reverb_medium.GetInt();
	case 2: return snd_fidelity_reverb_large.GetInt();
	default: return snd_fidelity_reverb_huge.GetInt();
	}
}

void CFidelityReverb::Apply( int nRoomType )
{
	if ( nRoomType == m_nApplied )
		return;
	CLocalPlayerFilter filter;
	enginesound->SetRoomType( filter, nRoomType );
	m_nApplied = nRoomType;
	m_bWeSetIt = true;
}

void CFidelityReverb::Update( float frametime )
{
	if ( snd_fidelity_debug.GetBool() )
	{
		if ( m_bSoundscapeDSP )
			engine->Con_NPrintf( 1, "[fidelity reverb] soundscape sets the room: %d", m_nApplied );
		else if ( snd_fidelity_reverb.GetBool() )
			engine->Con_NPrintf( 1, "[fidelity reverb] %s -> room %d, applied %d", m_szWhy, m_nCandidate, m_nApplied );
		else
			engine->Con_NPrintf( 1, "[fidelity reverb] off" );
	}

	if ( !snd_fidelity_reverb.GetBool() )
	{
		// Switched off: take our room back once, the soundscape's stays.
		if ( m_bWeSetIt && !m_bSoundscapeDSP )
			Apply( 0 );
		m_bWeSetIt = false;
		return;
	}

	if ( m_bSoundscapeDSP || gpGlobals->realtime < m_flNextUpdate )
		return;
	m_flNextUpdate = gpGlobals->realtime + REVERB_UPDATE_INTERVAL;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	const Vector &vecEye = MainViewOrigin();
	for ( int i = 0; i < REVERB_RAYS_PER_UPDATE; i++ )
	{
		TraceRay( m_nNextRay, vecEye );
		m_nNextRay = ( m_nNextRay + 1 ) % REVERB_RAYS;
		m_nTraced++;
	}
	if ( m_nTraced < REVERB_RAYS )
		return;

	const int nRoom = Classify( m_szWhy, sizeof( m_szWhy ) );
	if ( nRoom != m_nCandidate )
	{
		m_nCandidate = nRoom;
		m_flCandidateSince = gpGlobals->realtime;
	}
	if ( gpGlobals->realtime - m_flCandidateSince >= snd_fidelity_reverb_delay.GetFloat() &&
		gpGlobals->realtime - m_flAppliedAt >= REVERB_MIN_SWITCH && m_nCandidate != m_nApplied )
	{
		Apply( m_nCandidate );
		m_flAppliedAt = gpGlobals->realtime;
	}
}
