//========= mapbase-fidelity ==================================================//
//
// Purpose: Small world effects (ideas from MMod and Black Mesa, no new assets -
//          the game's own decals and smoke material):
//
//   - blood pools: a body that has come to rest bleeds a pool onto the floor
//     beneath it, growing over a few seconds (several blood decals around the
//     body's middle); red or yellow by what it was, none for machines;
//     follows the game's violence settings
//   - muzzle smoke, as Left 4 Dead does it: after quick shots (SMOKE_SHOTS
//     within SMOKE_WINDOW; the shotgun and the .357 after every shot) the
//     barrel smokes - Valve's own particle system weapon_muzzle_smoke from
//     Episode Two's particles/devtest.pcf (VDC "L4D Style Muzzle Smoke"),
//     following the view model's "muzzle", started anew with each such shot
//     and stopped when the weapon changes. Bullet weapons only. Our own puffs
//     and wisp are gone (Julius 25.10.2026: "der alte Rauch kann auch weg";
//     the pistol's Valve smoke was what he liked); without that file no smoke
//   - impact dust: on stone, plaster, tiles, dirt and wood a bullet leaves a
//     small cloud that hangs and spreads for a moment (the game's own flecks
//     and dust stay), lit like the place; capped when many hit at once
//   - splinters where the game throws none: clods of earth from dirt and
//     sand, chips from plastic - flecks that fly off and fall (the game's
//     fleck emitter, its cement fleck sprites in the colour of the surface);
//     stone, tiles and wood keep the game's own flecks
//   - distant shots (as Garry's Mod's Dynamic Weapon Reverb): a shot or an
//     explosion further than 900 units away also sounds its far-away echo
//     (sound/fidelity/distant/*.wav: deploy.bat makes them from the game's
//     own sounds - highs gone, echoes off walls, a tail), delayed by the time
//     sound needs (343 m/s, 13 500 units/s), at the place it came from; louder
//     from 1800 units on, gone towards 9000; automatic fire at most every
//     0.12 s per weapon sound. Missing files: nothing.
//   - rain splashes: the game's rain splashes on water only (a fifth of the
//     drops); here a drop that hits a floor, roof or wall within 1000 units
//     in front of the view makes a tiny splash (a third of them, at most 600
//     looked at a second), lit like the place
//
// Cost: the entity list is looked through four times a second for bodies;
// a few particles per shot.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_effects.h"
#include "igamesystem.h"
#include "particles_simple.h"
#include "fx.h"
#include "decals.h"
#include "c_baseviewmodel.h"
#include "fx_fleck.h"
#include "c_impact_effects.h"
#include "filesystem.h"
#include "engine/IEngineSound.h"
#include "view.h"
#include "particle_property.h"
#include "particles_new.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_bloodpools( "r_fidelity_bloodpools", "1", FCVAR_ARCHIVE, "The dead bleed a pool onto the floor beneath them. 0 = off." );
ConVar r_fidelity_muzzlesmoke( "r_fidelity_muzzlesmoke", "1", FCVAR_ARCHIVE, "The barrel smokes after quick shots (Valve's smoke of Left 4 Dead). 0 = off." );
ConVar r_fidelity_impactdust( "r_fidelity_impactdust", "1", FCVAR_ARCHIVE, "Bullet impacts on stone, dirt and wood leave dust hanging for a moment. 0 = off." );

#define BLOOD_SCAN_SECONDS		0.25f
#define BLOOD_REST_SECONDS		1.0f	// still this long: at rest
#define BLOOD_REST_MOVE			2.0f	// moved less than this (units) meanwhile
#define BLOOD_DECALS			6		// a pool is this many decals ...
#define BLOOD_SECONDS			5.0f	// ... laid over this time
#define BLOOD_SPREAD			20.0f	// out to this far from the middle
#define MAX_BLOOD_BODIES		24

#define SMOKE_SHOTS				5		// shots ...
#define SMOKE_WINDOW			2.0f	// ... within this time: the barrel smokes
#define SMOKE_VALVE				"weapon_muzzle_smoke"		// the smoke, Valve's (see top) ...
#define SMOKE_VALVE_FILE		"particles/devtest.pcf"	// ... from this file of Episode Two

#define DUST_PER_TENTH			6		// impact clouds at most per 0.1 s
#define GLASS_POWDER			5		// glass hit: puffs of powder ...
#define GLASS_CHIPS_MIN			6		// ... and chips (2-4 tiny ones and 3 faint puffs were too little - Julius 25.10.2026)
#define GLASS_CHIPS_MAX			10

ConVar r_fidelity_rainsplash( "r_fidelity_rainsplash", "1", FCVAR_ARCHIVE, "Rain splashes on floors, roofs and walls (not only on water). 0 = off." );

#define RAIN_SPLASH_SHARE		0.33f	// of the drops that hit
#define RAIN_SPLASH_SECOND		600		// drops looked at, at most a second (the same at any frame rate)
#define RAIN_SPLASH_REACH		1000.0f

ConVar snd_fidelity_distant( "snd_fidelity_distant", "1", FCVAR_ARCHIVE, "Shots and explosions far away echo, delayed as sound is. 0 = off." );
ConVar snd_fidelity_distant_volume( "snd_fidelity_distant_volume", "0.7", FCVAR_SERVER_CAN_EXECUTE, "Volume of the far-away echo of shots and explosions." );

#define DISTANT_NEAR			900.0f		// nearer: no echo (the shot itself is loud)
#define DISTANT_FULL			1800.0f		// full echo from here ...
#define DISTANT_FADE			4000.0f		// ... fading from here ...
#define DISTANT_FAR				9000.0f		// ... gone here
#define SPEED_OF_SOUND			13500.0f	// units per second (343 m/s, a unit is an inch)
#define DISTANT_REPEAT			0.12f		// automatic fire: an echo at most this often per sound
#define MAX_DISTANT				12			// waiting at most

// The far-away sounds deploy.bat makes (deploy.ps1, $DistantSources)
enum FidelityDistant_t
{
	FDISTANT_PISTOL = 0,
	FDISTANT_SMG1,
	FDISTANT_AR2,
	FDISTANT_SHOTGUN,
	FDISTANT_357,
	FDISTANT_EXPLOSION,
	FDISTANT_COUNT
};
static const char *s_pDistantSounds[ FDISTANT_COUNT ] = { "fidelity/distant/pistol.wav", "fidelity/distant/smg1.wav",
	"fidelity/distant/ar2.wav", "fidelity/distant/shotgun.wav", "fidelity/distant/357.wav", "fidelity/distant/explosion.wav" };

struct FidelityDistantEcho_t
{
	Vector m_vecOrigin;
	float m_flTime;		// when it arrives
	float m_flVolume;
	int m_nSound;
};

enum FidelityBlood_t
{
	FBLOOD_NONE = 0,
	FBLOOD_RED,
	FBLOOD_YELLOW,
};

// What bleeds what, by model name (the bodies do not tell); unknown ones not at all
static int Fidelity_BloodOf( const model_t *pModel )
{
	const char *pszModel = pModel ? modelinfo->GetModelName( pModel ) : "";
	static const char *s_pYellow[] = { "antlion", "headcrab", "vortigaunt", "bug" };
	static const char *s_pRed[] = { "combine_", "police", "humans/", "alyx", "barney", "eli", "kleiner", "monk", "breen",
		"mossman", "odessa", "gman", "zombie", "soldier", "classic" };
	for ( int i = 0; i < ARRAYSIZE( s_pYellow ); i++ )
	{
		if ( Q_stristr( pszModel, s_pYellow[ i ] ) )
			return FBLOOD_YELLOW;
	}
	for ( int i = 0; i < ARRAYSIZE( s_pRed ); i++ )
	{
		if ( Q_stristr( pszModel, s_pRed[ i ] ) )
			return FBLOOD_RED;
	}
	return FBLOOD_NONE;
}

struct FidelityBloodBody_t
{
	EHANDLE m_hBody;
	Vector m_vecLast;
	float m_flStill;		// seconds at rest
	int m_nLaid;			// decals of the pool laid so far (-1 = no pool)
	float m_flNextDecal;
	Vector m_vecPool;		// middle of the pool, on the floor
	int m_nBlood;
	bool m_bMoved;			// seen falling: a fresh body (a body lying there since the map or a save started gets none)
};


//-----------------------------------------------------------------------------
// The three effects (see top)
//-----------------------------------------------------------------------------
class CFidelityEffects : public CAutoGameSystemPerFrame
{
public:
	CFidelityEffects() : CAutoGameSystemPerFrame( "CFidelityEffects" ) {}

	virtual void LevelInitPreEntity();
	virtual void Update( float frametime );

	void Shot( int nMuzzleType );
	bool DustAllowed();
	void Distant( const Vector &vecOrigin, int nSound );
	virtual void LevelInitPostEntity();

private:
	void ScanBodies();
	void UpdatePools();
	void UpdateDistant();

	CUtlVector< FidelityBloodBody_t > m_Bodies;
	float m_flNextScan;
	float m_flShots[ SMOKE_SHOTS ];		// times of the last shots (ring)
	int m_nShot;
	bool m_bValveSmoke;					// SMOKE_VALVE is there (loaded on map load)
	EHANDLE m_hValveSmokeWeapon;		// the weapon whose view model smokes with it (stopped when it changes)
	bool LoadValveSmoke();
	void StopValveSmoke();
	float m_flDustTenth;
	int m_nDustInTenth;
	CUtlVector< FidelityDistantEcho_t > m_Echoes;
public:
	float m_flRainSecond;
	int m_nRainInSecond;
	// one emitter each for the whole level (an emitter per puff was a draw
	// call per puff: hundreds of them in heavy rain)
	CSimpleEmitter *Emitter( CSmartPtr< CSimpleEmitter > &pEmitter, const char *pszName, const Vector &vecAt );
	CSmartPtr< CSimpleEmitter > m_pDust;
	CSmartPtr< CSimpleEmitter > m_pRain;
	virtual void LevelShutdownPreEntity() { m_pDust = NULL; m_pRain = NULL; }
private:
	float m_flLastEcho[ FDISTANT_COUNT ];
	bool m_bHasSound[ FDISTANT_COUNT ];		// the file is there (looked at on map load)
};

static CFidelityEffects s_FidelityEffects;

void CFidelityEffects::LevelInitPreEntity()
{
	m_Bodies.RemoveAll();
	m_flNextScan = 0.0f;
	for ( int i = 0; i < SMOKE_SHOTS; i++ )
		m_flShots[ i ] = -100.0f;
	m_nShot = 0;
	m_bValveSmoke = false;
	m_hValveSmokeWeapon = NULL;
	m_flDustTenth = 0.0f;
	m_nDustInTenth = 0;
	m_Echoes.RemoveAll();
	m_flRainSecond = 0.0f;
	m_nRainInSecond = 0;
	m_pDust = NULL;
	m_pRain = NULL;
	for ( int i = 0; i < FDISTANT_COUNT; i++ )
	{
		m_flLastEcho[ i ] = -100.0f;
		m_bHasSound[ i ] = false;
	}
}

CSimpleEmitter *CFidelityEffects::Emitter( CSmartPtr< CSimpleEmitter > &pEmitter, const char *pszName, const Vector &vecAt )
{
	if ( !pEmitter.IsValid() )
		pEmitter = CSimpleEmitter::Create( pszName );
	if ( pEmitter.IsValid() )
		pEmitter->SetSortOrigin( vecAt );
	return pEmitter.IsValid() ? pEmitter.GetObject() : NULL;
}

void CFidelityEffects::LevelInitPostEntity()
{
	m_bValveSmoke = LoadValveSmoke();
	// the far-away sounds deploy.bat made: loaded now (no hitch at the first shot)
	for ( int i = 0; i < FDISTANT_COUNT; i++ )
	{
		char szPath[ MAX_PATH ];
		Q_snprintf( szPath, sizeof( szPath ), "sound/%s", s_pDistantSounds[ i ] );
		m_bHasSound[ i ] = g_pFullFileSystem->FileExists( szPath, "GAME" );
		if ( m_bHasSound[ i ] )
			enginesound->PrecacheSound( s_pDistantSounds[ i ], true );
	}
}

void CFidelityEffects::Update( float frametime )
{
	if ( !engine->IsInGame() || frametime <= 0.0f )
		return;
	if ( r_fidelity_bloodpools.GetBool() )
	{
		if ( gpGlobals->curtime >= m_flNextScan )
		{
			m_flNextScan = gpGlobals->curtime + BLOOD_SCAN_SECONDS;
			ScanBodies();
		}
		UpdatePools();
	}
	UpdateDistant();
	// Valve's smoke stays with the weapon that fired (VDC's "small fix": it went over to the next one)
	if ( m_hValveSmokeWeapon.Get() )
	{
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer || pPlayer->GetActiveWeapon() != m_hValveSmokeWeapon.Get() || !r_fidelity_muzzlesmoke.GetBool() )
			StopValveSmoke();
	}
}

//-----------------------------------------------------------------------------
// Valve's muzzle smoke (see top): its particle system is defined once the
// file is read; the game's manifests do not list devtest.pcf. Only read when
// the system is not there yet (another manifest may have it).
//-----------------------------------------------------------------------------
bool CFidelityEffects::LoadValveSmoke()
{
	if ( !g_pParticleSystemMgr )
		return false;
	if ( !g_pParticleSystemMgr->IsParticleSystemDefined( SMOKE_VALVE ) )
	{
		if ( !g_pFullFileSystem->FileExists( SMOKE_VALVE_FILE, "GAME" ) )
		{
			Msg( "[fidelity] Muzzle smoke: %s not found (Episode Two not mounted) - no smoke from the barrel.\n", SMOKE_VALVE_FILE );
			return false;
		}
		const int nBefore = g_pParticleSystemMgr->GetParticleSystemCount();
		g_pParticleSystemMgr->ReadParticleConfigFile( SMOKE_VALVE_FILE, true );
		if ( !g_pParticleSystemMgr->IsParticleSystemDefined( SMOKE_VALVE ) )
		{
			Msg( "[fidelity] Muzzle smoke: %s has no %s - no smoke from the barrel.\n", SMOKE_VALVE_FILE, SMOKE_VALVE );
			return false;
		}
		Msg( "[fidelity] Muzzle smoke: %s read (%d new particle systems).\n", SMOKE_VALVE_FILE, g_pParticleSystemMgr->GetParticleSystemCount() - nBefore );
	}
	g_pParticleSystemMgr->PrecacheParticleSystem( SMOKE_VALVE );
	return true;
}

void CFidelityEffects::StopValveSmoke()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
	if ( pViewModel )
		pViewModel->ParticleProp()->StopParticlesNamed( SMOKE_VALVE );	// emission stops, the smoke there fades
	m_hValveSmokeWeapon = NULL;
}

//-----------------------------------------------------------------------------
// Blood pools
//-----------------------------------------------------------------------------
void CFidelityEffects::ScanBodies()
{
	// the game's own gore settings (low violence: none)
	static ConVarRef violence_hblood( "violence_hblood" );
	static ConVarRef violence_ablood( "violence_ablood" );

	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
		ClientClass *pClass = pEntity->GetClientClass();
		const bool bRagdoll = ( pAnimating && pAnimating->IsRagdoll() ) || ( pClass && Q_stristr( pClass->m_pNetworkName, "Ragdoll" ) );
		if ( !bRagdoll || pEntity->IsDormant() )
			continue;
		bool bKnown = false;
		for ( int i = 0; i < m_Bodies.Count() && !bKnown; i++ )
			bKnown = m_Bodies[ i ].m_hBody.Get() == pEntity;
		if ( bKnown )
			continue;
		// bodies that are gone make room; a full list takes no more (dropping
		// the oldest made the list churn with many bodies: none ever came to rest)
		for ( int i = m_Bodies.Count() - 1; i >= 0; i-- )
		{
			if ( !m_Bodies[ i ].m_hBody.Get() )
				m_Bodies.Remove( i );
		}
		if ( m_Bodies.Count() >= MAX_BLOOD_BODIES )
			break;
		const int nBlood = Fidelity_BloodOf( pEntity->GetModel() );
		if ( nBlood == FBLOOD_NONE || ( nBlood == FBLOOD_RED && violence_hblood.IsValid() && !violence_hblood.GetBool() )
			|| ( nBlood == FBLOOD_YELLOW && violence_ablood.IsValid() && !violence_ablood.GetBool() ) )
			continue;
		FidelityBloodBody_t &body = m_Bodies[ m_Bodies.AddToTail() ];
		body.m_hBody = pEntity;
		body.m_vecLast = pEntity->WorldSpaceCenter();
		body.m_flStill = 0.0f;
		body.m_nLaid = -1;
		body.m_flNextDecal = 0.0f;
		body.m_vecPool = body.m_vecLast;
		body.m_nBlood = nBlood;
		body.m_bMoved = false;
	}
}

void CFidelityEffects::UpdatePools()
{
	for ( int i = 0; i < m_Bodies.Count(); i++ )
	{
		FidelityBloodBody_t &body = m_Bodies[ i ];
		C_BaseEntity *pBody = body.m_hBody.Get();
		if ( body.m_nLaid < 0 )
		{
			if ( !pBody || pBody->IsDormant() )
				continue;
			// at rest?
			const Vector vecNow = pBody->WorldSpaceCenter();
			if ( vecNow.DistTo( body.m_vecLast ) > BLOOD_REST_MOVE )
			{
				body.m_vecLast = vecNow;
				body.m_flStill = 0.0f;
				body.m_bMoved = true;
				continue;
			}
			if ( !body.m_bMoved )
				continue;
			body.m_flStill += gpGlobals->frametime;
			if ( body.m_flStill < BLOOD_REST_SECONDS )
				continue;
			// the floor beneath its middle
			trace_t tr;
			UTIL_TraceLine( vecNow + Vector( 0, 0, 8 ), vecNow - Vector( 0, 0, 64 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal.z < 0.7f )
			{
				body.m_nLaid = BLOOD_DECALS;	// no floor (water, a ledge): no pool
				continue;
			}
			body.m_vecPool = tr.endpos;
			body.m_nLaid = 0;
			body.m_flNextDecal = gpGlobals->curtime;
		}
		if ( body.m_nLaid >= BLOOD_DECALS || gpGlobals->curtime < body.m_flNextDecal )
			continue;
		// the pool grows: each decal a little further out, around the middle
		const float flOut = BLOOD_SPREAD * body.m_nLaid / ( BLOOD_DECALS - 1.0f );
		const float flAngle = random->RandomFloat( 0.0f, 2.0f * M_PI_F );
		const Vector vecAt = body.m_vecPool + Vector( cosf( flAngle ) * flOut, sinf( flAngle ) * flOut, 0.0f );
		trace_t tr;
		UTIL_TraceLine( vecAt + Vector( 0, 0, 8 ), vecAt - Vector( 0, 0, 24 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 1.0f && !tr.startsolid )
			UTIL_DecalTrace( &tr, body.m_nBlood == FBLOOD_YELLOW ? "YellowBlood" : "Blood" );
		body.m_nLaid++;
		body.m_flNextDecal = gpGlobals->curtime + BLOOD_SECONDS / BLOOD_DECALS * random->RandomFloat( 0.7f, 1.3f );
	}
}

//-----------------------------------------------------------------------------
// Muzzle smoke
//-----------------------------------------------------------------------------
void CFidelityEffects::Shot( int nMuzzleType )
{
	// bullet weapons only (the AR2's pulse and the RPG make no such smoke)
	if ( nMuzzleType == MUZZLEFLASH_AR2 || nMuzzleType == MUZZLEFLASH_COMBINE || nMuzzleType == MUZZLEFLASH_COMBINE_TURRET
		|| nMuzzleType == MUZZLEFLASH_RPG )
		return;
	m_flShots[ m_nShot ] = gpGlobals->curtime;
	m_nShot = ( m_nShot + 1 ) % SMOKE_SHOTS;
	// the oldest of the last SMOKE_SHOTS within the window: quick shots; the
	// heavy ones (shotgun, .357) smoke after every shot
	const bool bHeavy = ( nMuzzleType == MUZZLEFLASH_SHOTGUN || nMuzzleType == MUZZLEFLASH_357 );
	const bool bSmoke = bHeavy || gpGlobals->curtime - m_flShots[ m_nShot ] <= SMOKE_WINDOW;
	if ( !bSmoke || !m_bValveSmoke || !r_fidelity_muzzlesmoke.GetBool() )
		return;
	// Valve's smoke at the muzzle, anew with each such shot (as VDC's pistol)
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
	if ( !pViewModel || pViewModel->LookupAttachment( "muzzle" ) <= 0 )
		return;
	pViewModel->ParticleProp()->StopParticlesNamed( SMOKE_VALVE );
	pViewModel->ParticleProp()->Create( SMOKE_VALVE, PATTACH_POINT_FOLLOW, "muzzle" );
	m_hValveSmokeWeapon = pPlayer->GetActiveWeapon();
}

//-----------------------------------------------------------------------------
// Impact dust
//-----------------------------------------------------------------------------
bool CFidelityEffects::DustAllowed()
{
	if ( gpGlobals->curtime - m_flDustTenth > 0.1f )
	{
		m_flDustTenth = gpGlobals->curtime;
		m_nDustInTenth = 0;
	}
	return ++m_nDustInTenth <= DUST_PER_TENTH;
}

// Clods and chips (see top): flecks in the surface's colour, thrown off and falling
static void Fidelity_ImpactSplinters( const trace_t &tr, int iMaterial, int iScale )
{
	if ( iMaterial != CHAR_TEX_DIRT && iMaterial != CHAR_TEX_SAND && iMaterial != CHAR_TEX_PLASTIC )
		return;
	const Vector vecStart = tr.endpos + tr.plane.normal * 1.0f;
	CSmartPtr< CFleckParticles > pFlecks = CFleckParticles::Create( "fidelity_splinters", vecStart, Vector( 5, 5, 5 ) );
	if ( !pFlecks )
		return;
	const float flScale = clamp( (float)iScale, 1.0f, 3.0f );
	const float flSpray = 0.7f;
	pFlecks->m_ParticleCollision.Setup( vecStart, &tr.plane.normal, flSpray, 60.0f, 140.0f * flScale, 800.0f, 0.3f );
	Vector vecColor;
	GetColorForSurface( const_cast< trace_t * >( &tr ), &vecColor );
	if ( iMaterial != CHAR_TEX_PLASTIC )
		vecColor *= 0.7f;	// clods: darker than the dusty top
	PMaterialHandle hMaterials[ 2 ] = { pFlecks->GetPMaterial( "effects/fleck_cement1" ), pFlecks->GetPMaterial( "effects/fleck_cement2" ) };
	const int nFlecks = (int)( random->RandomInt( 4, 8 ) * flScale );
	for ( int i = 0; i < nFlecks; i++ )
	{
		FleckParticle *pFleck = (FleckParticle *)pFlecks->AddParticle( sizeof( FleckParticle ), hMaterials[ random->RandomInt( 0, 1 ) ], vecStart );
		if ( !pFleck )
			break;
		pFleck->m_flLifetime = 0.0f;
		pFleck->m_flDieTime = 2.5f;
		Vector vecDir = tr.plane.normal + RandomVector( -flSpray, flSpray );
		VectorNormalize( vecDir );
		pFleck->m_uchSize = (unsigned char)random->RandomInt( 1, iMaterial == CHAR_TEX_PLASTIC ? 2 : 3 );
		pFleck->m_vecVelocity = vecDir * random->RandomFloat( 60.0f, 140.0f * flScale ) * ( iMaterial == CHAR_TEX_PLASTIC ? 1.2f : 0.8f );
		pFleck->m_flRoll = random->RandomFloat( 0.0f, 360.0f );
		pFleck->m_flRollDelta = random->RandomFloat( 0.0f, 360.0f );
		const float flRamp = random->RandomFloat( 0.75f, 1.2f );
		for ( int c = 0; c < 3; c++ )
			pFleck->m_uchColor[ c ] = (unsigned char)( MIN( 1.0f, vecColor[ c ] * flRamp ) * 255.0f );
	}
}

// A bullet through a pane of glass that does not break: the game only draws a
// decal (its "impact_glass" particles are not in Half-Life 2; Julius: "bei Glas
// fehlt der Effekt"). A faint puff of fine glass powder and a few tiny
// glinting chips that drop - the game's own glass shards (FX_GlassImpact)
// were bright squares a hand wide (Julius: "passt nicht"). Breakable panes
// (func_breakable_surf) make their own. Then too faint ("nicht ausgeprägt
// genug"): now a clearer puff and 6-10 chips, some two units, thrown further.
static void Fidelity_GlassImpact( const trace_t &tr )
{
	if ( tr.m_pEnt && tr.m_pEnt->GetClientClass() && !Q_strcmp( tr.m_pEnt->GetClientClass()->GetName(), "CBreakableSurface" ) )
		return;
	const Vector vecStart = tr.endpos + tr.plane.normal * 1.0f;
	const Vector vecLight = WorldGetLightForPoint( vecStart, true );

	// powder: pale, thin, gone in under a second
	CSimpleEmitter *pEmitter = s_FidelityEffects.Emitter( s_FidelityEffects.m_pDust, "fidelity_impact_dust", vecStart );
	if ( pEmitter )
	{
		PMaterialHandle hMaterial = pEmitter->GetPMaterial( "particle/particle_smokegrenade" );
		for ( int i = 0; i < GLASS_POWDER; i++ )
		{
			SimpleParticle *pParticle = (SimpleParticle *)pEmitter->AddParticle( sizeof( SimpleParticle ), hMaterial, vecStart );
			if ( !pParticle )
				break;
			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = random->RandomFloat( 0.5f, 1.0f );
			pParticle->m_vecVelocity = tr.plane.normal * random->RandomFloat( 8.0f, 16.0f ) + RandomVector( -3.0f, 3.0f );
			pParticle->m_vecVelocity.z -= 4.0f;
			for ( int c = 0; c < 3; c++ )
				pParticle->m_uchColor[ c ] = (unsigned char)( clamp( 0.85f * MIN( vecLight[ c ] * 1.4f + 0.1f, 1.0f ), 0.0f, 1.0f ) * 255.0f );
			pParticle->m_uchStartAlpha = (unsigned char)random->RandomInt( 35, 55 );
			pParticle->m_uchEndAlpha = 0;
			pParticle->m_uchStartSize = 1;
			pParticle->m_uchEndSize = (unsigned char)random->RandomInt( 5, 8 );
			pParticle->m_flRoll = random->RandomInt( 0, 360 );
			pParticle->m_flRollDelta = random->RandomFloat( -0.5f, 0.5f );
		}
	}

	// chips: a few, one unit across, thrown off and falling
	CSmartPtr< CFleckParticles > pFlecks = CFleckParticles::Create( "fidelity_glass_chips", vecStart, Vector( 4, 4, 4 ) );
	if ( !pFlecks )
		return;
	const float flSpray = 0.6f;
	pFlecks->m_ParticleCollision.Setup( vecStart, &tr.plane.normal, flSpray, 60.0f, 160.0f, 800.0f, 0.3f );
	PMaterialHandle hMaterials[ 2 ] = { pFlecks->GetPMaterial( "effects/fleck_glass1" ), pFlecks->GetPMaterial( "effects/fleck_glass2" ) };
	const int nChips = random->RandomInt( GLASS_CHIPS_MIN, GLASS_CHIPS_MAX );
	for ( int i = 0; i < nChips; i++ )
	{
		FleckParticle *pFleck = (FleckParticle *)pFlecks->AddParticle( sizeof( FleckParticle ), hMaterials[ random->RandomInt( 0, 1 ) ], vecStart );
		if ( !pFleck )
			break;
		pFleck->m_flLifetime = 0.0f;
		pFleck->m_flDieTime = 1.5f;
		Vector vecDir = tr.plane.normal + RandomVector( -flSpray, flSpray );
		VectorNormalize( vecDir );
		pFleck->m_uchSize = (unsigned char)( random->RandomInt( 0, 2 ) == 0 ? 2 : 1 );	// a third a little bigger
		pFleck->m_vecVelocity = vecDir * random->RandomFloat( 60.0f, 160.0f );
		pFleck->m_flRoll = random->RandomFloat( 0.0f, 360.0f );
		pFleck->m_flRollDelta = random->RandomFloat( -720.0f, 720.0f );
		for ( int c = 0; c < 3; c++ )
			pFleck->m_uchColor[ c ] = (unsigned char)( clamp( 0.75f * MIN( vecLight[ c ] * 1.4f + 0.15f, 1.0f ), 0.0f, 1.0f ) * 255.0f );
	}
}

void Fidelity_ImpactDust( const Vector &vecOrigin, const trace_t &tr, int iMaterial, int iScale )
{
	if ( !r_fidelity_impactdust.GetBool() || !s_FidelityEffects.DustAllowed() )
		return;
	if ( iMaterial == CHAR_TEX_GLASS )
	{
		Fidelity_GlassImpact( tr );
		return;
	}
	Fidelity_ImpactSplinters( tr, iMaterial, iScale );
	// the colour of the dust by what was hit
	Vector vecColor;
	if ( iMaterial == CHAR_TEX_CONCRETE || iMaterial == CHAR_TEX_TILE )
		vecColor.Init( 0.62f, 0.6f, 0.57f );
	else if ( iMaterial == CHAR_TEX_DIRT || iMaterial == CHAR_TEX_SAND )
		vecColor.Init( 0.55f, 0.47f, 0.37f );
	else if ( iMaterial == CHAR_TEX_WOOD )
		vecColor.Init( 0.58f, 0.48f, 0.36f );
	else
		return;

	const Vector vecStart = vecOrigin + tr.plane.normal * 2.0f;
	CSimpleEmitter *pEmitter = s_FidelityEffects.Emitter( s_FidelityEffects.m_pDust, "fidelity_impact_dust", vecStart );
	if ( !pEmitter )
		return;
	const Vector vecLight = WorldGetLightForPoint( vecStart, true );
	const float flScale = clamp( iScale / 1.0f, 0.5f, 2.0f );
	PMaterialHandle hMaterial = pEmitter->GetPMaterial( "particle/particle_smokegrenade" );
	for ( int i = 0; i < 3; i++ )
	{
		SimpleParticle *pParticle = (SimpleParticle *)pEmitter->AddParticle( sizeof( SimpleParticle ), hMaterial, vecStart );
		if ( !pParticle )
			return;
		pParticle->m_flLifetime = 0.0f;
		pParticle->m_flDieTime = random->RandomFloat( 1.4f, 2.2f );
		Vector vecDrift = tr.plane.normal * random->RandomFloat( 6.0f, 14.0f );
		vecDrift += RandomVector( -4.0f, 4.0f );
		vecDrift.z -= 2.0f;		// settles a little
		pParticle->m_vecVelocity = vecDrift;
		for ( int c = 0; c < 3; c++ )
			pParticle->m_uchColor[ c ] = (unsigned char)( clamp( vecColor[ c ] * MIN( vecLight[ c ] * 1.4f + 0.05f, 1.0f ), 0.0f, 1.0f ) * 255.0f );
		pParticle->m_uchStartAlpha = (unsigned char)random->RandomInt( 50, 80 );
		pParticle->m_uchEndAlpha = 0;
		pParticle->m_uchStartSize = (unsigned char)( random->RandomFloat( 3.0f, 5.0f ) * flScale );
		pParticle->m_uchEndSize = (unsigned char)( random->RandomFloat( 12.0f, 18.0f ) * flScale );
		pParticle->m_flRoll = random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta = random->RandomFloat( -0.6f, 0.6f );
	}
}

//-----------------------------------------------------------------------------
// Distant shots
//-----------------------------------------------------------------------------
void CFidelityEffects::Distant( const Vector &vecOrigin, int nSound )
{
	if ( !snd_fidelity_distant.GetBool() || nSound < 0 || nSound >= FDISTANT_COUNT || !m_bHasSound[ nSound ] )
		return;
	const float flDistance = MainViewOrigin().DistTo( vecOrigin );
	if ( flDistance < DISTANT_NEAR || flDistance > DISTANT_FAR || gpGlobals->curtime - m_flLastEcho[ nSound ] < DISTANT_REPEAT )
		return;
	const float flVolume = snd_fidelity_distant_volume.GetFloat()
		* SimpleSpline( RemapValClamped( flDistance, DISTANT_NEAR, DISTANT_FULL, 0.0f, 1.0f ) )
		* ( 1.0f - SimpleSpline( RemapValClamped( flDistance, DISTANT_FADE, DISTANT_FAR, 0.0f, 1.0f ) ) );
	if ( flVolume < 0.02f || m_Echoes.Count() >= MAX_DISTANT )
		return;
	m_flLastEcho[ nSound ] = gpGlobals->curtime;
	FidelityDistantEcho_t &echo = m_Echoes[ m_Echoes.AddToTail() ];
	echo.m_vecOrigin = vecOrigin;
	echo.m_flTime = gpGlobals->curtime + flDistance / SPEED_OF_SOUND;
	echo.m_flVolume = MIN( flVolume, 1.0f );
	echo.m_nSound = nSound;
}

void CFidelityEffects::UpdateDistant()
{
	for ( int i = m_Echoes.Count() - 1; i >= 0; i-- )
	{
		if ( gpGlobals->curtime < m_Echoes[ i ].m_flTime )
			continue;
		const FidelityDistantEcho_t &echo = m_Echoes[ i ];
		CLocalPlayerFilter filter;
		EmitSound_t ep;
		ep.m_nChannel = CHAN_STATIC;
		ep.m_pSoundName = s_pDistantSounds[ echo.m_nSound ];
		ep.m_flVolume = echo.m_flVolume;
		ep.m_SoundLevel = SNDLVL_140dB;		// heard far; the volume above sets how loud
		ep.m_pOrigin = &echo.m_vecOrigin;
		C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
		m_Echoes.Remove( i );
	}
}

void Fidelity_DistantSound( const Vector &vecOrigin, int nMuzzleType )
{
	int nSound = -1;
	switch ( nMuzzleType )
	{
	case -1:						nSound = FDISTANT_EXPLOSION; break;
	case MUZZLEFLASH_PISTOL:		nSound = FDISTANT_PISTOL; break;
	case MUZZLEFLASH_SMG1:
	case MUZZLEFLASH_SMG2:			nSound = FDISTANT_SMG1; break;
	case MUZZLEFLASH_SHOTGUN:		nSound = FDISTANT_SHOTGUN; break;
	case MUZZLEFLASH_357:			nSound = FDISTANT_357; break;
	case MUZZLEFLASH_AR2:
	case MUZZLEFLASH_COMBINE:
	case MUZZLEFLASH_COMBINE_TURRET:	nSound = FDISTANT_AR2; break;
	default:						return;
	}
	s_FidelityEffects.Distant( vecOrigin, nSound );
}

//-----------------------------------------------------------------------------
// Rain splashes
//-----------------------------------------------------------------------------
void Fidelity_RainHit( const Vector &vecFrom, const Vector &vecTo )
{
	if ( !r_fidelity_rainsplash.GetBool() || random->RandomFloat() > RAIN_SPLASH_SHARE )
		return;
	if ( gpGlobals->curtime - s_FidelityEffects.m_flRainSecond >= 1.0f || gpGlobals->curtime < s_FidelityEffects.m_flRainSecond )
	{
		s_FidelityEffects.m_flRainSecond = gpGlobals->curtime;
		s_FidelityEffects.m_nRainInSecond = 0;
	}
	if ( ++s_FidelityEffects.m_nRainInSecond > RAIN_SPLASH_SECOND )
		return;
	// near and in front of the view only
	const Vector vecToDrop = vecTo - MainViewOrigin();
	if ( vecToDrop.LengthSqr() > RAIN_SPLASH_REACH * RAIN_SPLASH_REACH || DotProduct( vecToDrop, MainViewForward() ) < 0.0f )
		return;
	trace_t tr;
	UTIL_TraceLine( vecFrom, vecTo, MASK_SOLID_BRUSHONLY | CONTENTS_WATER | CONTENTS_SLIME, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction >= 1.0f || tr.startsolid || ( tr.contents & ( CONTENTS_WATER | CONTENTS_SLIME ) ) || ( tr.surface.flags & SURF_SKY ) )
		return;	// (water: the game's own ripple)

	const Vector vecAt = tr.endpos + tr.plane.normal * 0.5f;
	CSimpleEmitter *pEmitter = s_FidelityEffects.Emitter( s_FidelityEffects.m_pRain, "fidelity_rain_splash", vecAt );
	if ( !pEmitter )
		return;
	const Vector vecLight = WorldGetLightForPoint( vecAt, true );
	const unsigned char uchGrey = (unsigned char)( clamp( ( vecLight.x + vecLight.y + vecLight.z ) / 3.0f * 0.9f + 0.15f, 0.1f, 1.0f ) * 255.0f );
	SimpleParticle *pParticle = (SimpleParticle *)pEmitter->AddParticle( sizeof( SimpleParticle ), pEmitter->GetPMaterial( "effects/splash2" ), vecAt );
	if ( !pParticle )
		return;
	pParticle->m_flLifetime = 0.0f;
	pParticle->m_flDieTime = random->RandomFloat( 0.15f, 0.25f );
	pParticle->m_vecVelocity = tr.plane.normal * random->RandomFloat( 10.0f, 25.0f );
	pParticle->m_uchColor[ 0 ] = pParticle->m_uchColor[ 1 ] = pParticle->m_uchColor[ 2 ] = uchGrey;
	pParticle->m_uchStartAlpha = (unsigned char)random->RandomInt( 50, 90 );
	pParticle->m_uchEndAlpha = 0;
	pParticle->m_uchStartSize = 1;
	pParticle->m_uchEndSize = (unsigned char)random->RandomInt( 3, 5 );
	pParticle->m_flRoll = random->RandomInt( 0, 360 );
	pParticle->m_flRollDelta = 0.0f;
}

void Fidelity_MuzzleSmokeShot( int nMuzzleType )
{
	s_FidelityEffects.Shot( nMuzzleType );
}
