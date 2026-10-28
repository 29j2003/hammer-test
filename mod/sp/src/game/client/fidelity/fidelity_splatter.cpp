//========= mapbase-fidelity ==================================================//
//
// Purpose: A bug bait (pheropod) bursting on impact makes a proper splat:
//          sticky drops flying off the surface and falling back, a short
//          burst of goo and more splatter marks around the impact.
//
// The game itself only plays a sound, a green spore cloud and one small
// decal. The bait is a server entity (npc_grenade_bugbait) without client
// code of its own, so the client watches it: a bait with the bug bait model
// that was flying a moment ago and is gone now burst (the server removes it
// on touch). Where it hit and the surface's normal come from a trace along
// its last flight direction. No change to Mapbase's server code.
//
// Cost: the entity list is looked through ten times a second for new baits;
// the few that fly are followed every frame.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "particles_simple.h"
#include "particle_util.h"
#include "fx.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_splatter( "r_fidelity_splatter", "1", FCVAR_ARCHIVE, "Bug bait makes a big splat with flying drops when it bursts. 0 = off." );

// Pheropod goo: yellow-green, like its spores and the antlions' blood
static const float s_vecGooColor[ 3 ] = { 0.62f, 0.7f, 0.18f };
#define SPLAT_DROPS				36
#define SPLAT_DECALS			6
#define SPLAT_DECAL_REACH		40.0f

//-----------------------------------------------------------------------------
// The splat at vecOrigin on a surface facing vecNormal
//-----------------------------------------------------------------------------
static void Fidelity_GooSplat( const Vector &vecOrigin, const Vector &vecNormal )
{
	// Lit like the place it hit (particles are not lit by themselves)
	const Vector vecLight = WorldGetLightForPoint( vecOrigin + vecNormal * 4.0f, true );
	Vector vecTint( s_vecGooColor[ 0 ] * vecLight.x, s_vecGooColor[ 1 ] * vecLight.y, s_vecGooColor[ 2 ] * vecLight.z );
	vecTint *= 1.6f;
	vecTint.x = MIN( vecTint.x, 1.0f );
	vecTint.y = MIN( vecTint.y, 1.0f );
	vecTint.z = MIN( vecTint.z, 1.0f );
	PMaterialHandle hMaterial = ParticleMgr()->GetPMaterial( "effects/splash2" );

	// Sticky drops thrown off the surface, falling back
	CSmartPtr< CTrailParticles > pDrops = CTrailParticles::Create( "fidelity_goo_drops" );
	if ( pDrops.IsValid() )
	{
		pDrops->SetSortOrigin( vecOrigin );
		pDrops->m_ParticleCollision.SetGravity( 600.0f );
		pDrops->SetFlag( bitsPARTICLE_TRAIL_VELOCITY_DAMPEN );
		pDrops->SetVelocityDampen( 2.0f );
		pDrops->GetBinding().SetBBox( vecOrigin - Vector( 96, 96, 96 ), vecOrigin + Vector( 96, 96, 96 ) );
		for ( int i = 0; i < SPLAT_DROPS; i++ )
		{
			TrailParticle *pParticle = (TrailParticle *)pDrops->AddParticle( sizeof( TrailParticle ), hMaterial, vecOrigin + vecNormal * 2.0f );
			if ( !pParticle )
				break;
			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = random->RandomFloat( 0.4f, 0.9f );
			// Off the surface, spreading wide (a hemisphere around its normal)
			Vector vecDir = RandomVector( -1.0f, 1.0f ) + vecNormal * 1.1f;
			VectorNormalize( vecDir );
			pParticle->m_vecVelocity = vecDir * random->RandomFloat( 80.0f, 260.0f );
			pParticle->m_flWidth = random->RandomFloat( 1.0f, 2.6f );
			pParticle->m_flLength = random->RandomFloat( 0.03f, 0.07f );
			const float flShade = random->RandomFloat( 0.75f, 1.1f );
			pParticle->m_color.r = (unsigned char)( MIN( vecTint.x * flShade, 1.0f ) * 255 );
			pParticle->m_color.g = (unsigned char)( MIN( vecTint.y * flShade, 1.0f ) * 255 );
			pParticle->m_color.b = (unsigned char)( MIN( vecTint.z * flShade, 1.0f ) * 255 );
			pParticle->m_color.a = 255;
		}
	}

	// A short burst of goo right at the impact
	CSmartPtr< CSimpleEmitter > pBurst = CSimpleEmitter::Create( "fidelity_goo_burst" );
	if ( pBurst.IsValid() )
	{
		pBurst->SetSortOrigin( vecOrigin );
		for ( int i = 0; i < 8; i++ )
		{
			SimpleParticle *pParticle = (SimpleParticle *)pBurst->AddParticle( sizeof( SimpleParticle ), hMaterial, vecOrigin + vecNormal * 3.0f );
			if ( !pParticle )
				break;
			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = random->RandomFloat( 0.25f, 0.5f );
			pParticle->m_vecVelocity = ( vecNormal * random->RandomFloat( 30.0f, 70.0f ) ) + RandomVector( -40.0f, 40.0f );
			pParticle->m_uchColor[ 0 ] = (unsigned char)( vecTint.x * 255 );
			pParticle->m_uchColor[ 1 ] = (unsigned char)( vecTint.y * 255 );
			pParticle->m_uchColor[ 2 ] = (unsigned char)( vecTint.z * 255 );
			pParticle->m_uchStartAlpha = 200;
			pParticle->m_uchEndAlpha = 0;
			pParticle->m_uchStartSize = (unsigned char)random->RandomInt( 6, 10 );
			pParticle->m_uchEndSize = (unsigned char)random->RandomInt( 18, 28 );
			pParticle->m_flRoll = random->RandomFloat( 0.0f, 360.0f );
			pParticle->m_flRollDelta = random->RandomFloat( -3.0f, 3.0f );
		}
	}

	// More splatter marks around: traces from just in front of the impact
	// outwards and back onto the surface (also onto walls next to it)
	for ( int i = 0; i < SPLAT_DECALS; i++ )
	{
		Vector vecDir = RandomVector( -1.0f, 1.0f ) - vecNormal * 0.8f;
		VectorNormalize( vecDir );
		trace_t tr;
		UTIL_TraceLine( vecOrigin + vecNormal * 8.0f, vecOrigin + vecNormal * 8.0f + vecDir * SPLAT_DECAL_REACH, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 1.0f && !tr.startsolid && tr.m_pEnt )
			UTIL_DecalTrace( &tr, "YellowBlood" );
	}
}

//-----------------------------------------------------------------------------
// Watches flying bug baits and makes the splat when one bursts
//-----------------------------------------------------------------------------
struct FidelityBait_t
{
	EHANDLE m_hEntity;
	Vector m_vecPos;
	Vector m_vecVelocity;	// from the last two positions (units/s)
	float m_flLastSeen;
};

class CFidelitySplatter : public CAutoGameSystemPerFrame
{
public:
	CFidelitySplatter() : CAutoGameSystemPerFrame( "CFidelitySplatter" ), m_flNextScan( 0.0f ) {}

	virtual void LevelShutdownPreEntity() { m_Baits.RemoveAll(); }	// baits going away with the map do not splat
	virtual void Update( float frametime );

private:
	void Scan();

	CUtlVector< FidelityBait_t > m_Baits;
	float m_flNextScan;
};

static CFidelitySplatter s_FidelitySplatter;

// A thrown bait (not the weapon lying around or in the hand)
static bool Fidelity_IsBait( C_BaseEntity *pEntity )
{
	const model_t *pModel = pEntity->GetModel();
	if ( !pModel || pEntity->IsBaseCombatWeapon() )
		return false;
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating && pAnimating->IsViewModel() )
		return false;
	return Q_stristr( modelinfo->GetModelName( pModel ), "bugbait" ) != NULL;
}

void CFidelitySplatter::Scan()
{
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		if ( !Fidelity_IsBait( pEntity ) )
			continue;
		bool bKnown = false;
		for ( int i = 0; i < m_Baits.Count() && !bKnown; i++ )
			bKnown = m_Baits[ i ].m_hEntity.Get() == pEntity;
		if ( bKnown )
			continue;
		FidelityBait_t &bait = m_Baits[ m_Baits.AddToTail() ];
		bait.m_hEntity = pEntity;
		bait.m_vecPos = pEntity->GetAbsOrigin();
		bait.m_vecVelocity.Init();
		bait.m_flLastSeen = gpGlobals->curtime;
	}
}

void CFidelitySplatter::Update( float frametime )
{
	if ( !r_fidelity_splatter.GetBool() || !engine->IsInGame() )
	{
		m_Baits.RemoveAll();
		return;
	}
	if ( gpGlobals->realtime >= m_flNextScan )
	{
		m_flNextScan = gpGlobals->realtime + 0.1f;
		Scan();
	}

	for ( int i = m_Baits.Count() - 1; i >= 0; i-- )
	{
		FidelityBait_t &bait = m_Baits[ i ];
		C_BaseEntity *pEntity = bait.m_hEntity.Get();
		if ( pEntity && !pEntity->IsDormant() )
		{
			const Vector vecPos = pEntity->GetAbsOrigin();
			const float flTime = gpGlobals->curtime - bait.m_flLastSeen;
			if ( flTime > 0.0f )
				bait.m_vecVelocity = ( vecPos - bait.m_vecPos ) / flTime;
			bait.m_vecPos = vecPos;
			bait.m_flLastSeen = gpGlobals->curtime;
			continue;
		}
		// Gone a moment after it flew: it burst. (Gone while lying still, or
		// long unseen: removed for another reason - no splat.)
		const float flSpeed = bait.m_vecVelocity.Length();
		if ( !pEntity && gpGlobals->curtime - bait.m_flLastSeen < 0.3f && flSpeed > 50.0f )
		{
			const Vector vecDir = bait.m_vecVelocity / flSpeed;
			trace_t tr;
			UTIL_TraceLine( bait.m_vecPos - vecDir * 8.0f, bait.m_vecPos + vecDir * 64.0f, MASK_SHOT, NULL, COLLISION_GROUP_NONE, &tr );
			const Vector vecNormal = ( tr.fraction < 1.0f && !tr.startsolid ) ? tr.plane.normal : -vecDir;
			const Vector vecOrigin = ( tr.fraction < 1.0f && !tr.startsolid ) ? tr.endpos : bait.m_vecPos;
			Fidelity_GooSplat( vecOrigin, vecNormal );
		}
		if ( !pEntity || gpGlobals->curtime - bait.m_flLastSeen > 5.0f )
			m_Baits.FastRemove( i );
	}
}
