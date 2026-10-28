//========= mapbase-fidelity ==================================================//
//
// Purpose: A light breeze for Mapbase's tree sway when the map has no env_wind.
//
// Mapbase's VertexLitGeneric moves foliage with $treesway (deploy.bat turns it
// on for Half-Life 2's foliage), driven by the wind vector env_wind writes into
// VECTOR_RENDERPARM_WIND_DIRECTION. Without an env_wind that vector is zero
// and nothing moves. This system writes a gentle, gusting breeze instead -
// only while the map has no env_wind, which then takes over unchanged.
//
// The breeze is the same everywhere in the world (no dependence on the view),
// changes slowly, and costs one vector per frame plus an entity scan every
// two seconds.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "renderparm.h"
#include "env_wind_shared.h"
#include "fidelity_wind.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_wind( "r_fidelity_wind", "1", FCVAR_ARCHIVE, "Light breeze for swaying foliage in maps without env_wind. 0 = off." );
ConVar r_fidelity_wind_strength( "r_fidelity_wind_strength", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Strength of the breeze (about env_wind speed / 25)." );
ConVar r_fidelity_wind_yaw( "r_fidelity_wind_yaw", "30", FCVAR_SERVER_CAN_EXECUTE, "Direction the breeze blows to, in degrees (0 = +x, 90 = +y)." );
ConVar r_fidelity_wind_gusts( "r_fidelity_wind_gusts", "0.5", FCVAR_SERVER_CAN_EXECUTE, "How much the breeze varies, 0 = steady .. 1 = strong gusts." );

class CFidelityWind : public CAutoGameSystemPerFrame
{
public:
	CFidelityWind() : CAutoGameSystemPerFrame( "CFidelityWind" ), m_bMapHasWind( false ), m_bWeSetIt( false ), m_flNextScan( 0.0f ) {}

	virtual void LevelInitPostEntity() { m_flNextScan = 0.0f; m_bMapHasWind = false; }
	virtual void LevelShutdownPreEntity() { Set( vec3_origin ); m_bWeSetIt = false; }
	virtual void Update( float frametime );

private:
	static bool MapHasWind();
	void Set( const Vector &vecWind );

	bool m_bMapHasWind;
	bool m_bWeSetIt;
	float m_flNextScan;
};

static CFidelityWind s_FidelityWind;

// The wind of this frame, from env_wind or the breeze (for the clouds)
static Vector s_vecFidelityWind( 0.0f, 0.0f, 0.0f );

const Vector &Fidelity_WindVector()
{
	return s_vecFidelityWind;
}

bool CFidelityWind::MapHasWind()
{
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		ClientClass *pClass = pEntity->GetClientClass();
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "CEnvWind" ) )
			return true;
	}
	return false;
}

void CFidelityWind::Set( const Vector &vecWind )
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_WIND_DIRECTION, vecWind );
}

void CFidelityWind::Update( float frametime )
{
	if ( gpGlobals->realtime >= m_flNextScan )
	{
		m_flNextScan = gpGlobals->realtime + 2.0f;
		m_bMapHasWind = MapHasWind();
	}

	if ( m_bMapHasWind )
	{
		// Same scale as env_wind gives the tree sway (speed / 25)
		Vector vecWind;
		GetWindspeedAtTime( gpGlobals->curtime, vecWind );
		s_vecFidelityWind = vecWind / 25.0f;
	}
	else if ( !r_fidelity_wind.GetBool() )
	{
		s_vecFidelityWind.Init();
	}

	if ( !r_fidelity_wind.GetBool() || m_bMapHasWind )
	{
		// Hand back a still wind once; env_wind writes its own every frame.
		if ( m_bWeSetIt && !m_bMapHasWind )
			Set( vec3_origin );
		m_bWeSetIt = false;
		return;
	}

	// Slow gusts: a few unrelated sines, so the pattern never visibly repeats.
	const float t = gpGlobals->curtime;
	const float flGust = 0.5f * sinf( t * 0.23f ) + 0.3f * sinf( t * 0.61f + 1.7f ) + 0.2f * sinf( t * 1.37f + 4.1f );
	const float flStrength = MAX( r_fidelity_wind_strength.GetFloat(), 0.0f ) * ( 1.0f + clamp( r_fidelity_wind_gusts.GetFloat(), 0.0f, 1.0f ) * flGust );
	const float flYaw = DEG2RAD( r_fidelity_wind_yaw.GetFloat() + 12.0f * sinf( t * 0.11f ) );
	s_vecFidelityWind = Vector( cosf( flYaw ), sinf( flYaw ), 0.0f ) * MAX( flStrength, 0.0f );
	Set( s_vecFidelityWind );
	m_bWeSetIt = true;
}
