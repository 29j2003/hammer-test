//========= mapbase-fidelity ==================================================//
//
// Purpose: The nearest dynamic lights per pixel (r_fidelity_dynlights, on by
//          default): Fidelity_Parallax lights the relief and a highlight with
//          them (as Black Mesa's lights, without its deferred renderer).
//
// The engine's dynamic lights (dlights: muzzle flashes, explosions, sparks,
// tracers, glowing things - most made by fidelity_glow.cpp) are added to the
// lightmaps, flat. Each frame before drawing, the FIDELITY_DYNAMIC_LIGHTS of
// them whose lit sphere is nearest to the view (within DYNLIGHT_REACH) go to
// the shader DLL as rendering parameters (fidelity_renderparm.h): position,
// radius and colour (linear, as on the lightmap). How bright a light is does
// not depend on where the view is: it faded in over 256 units as the view
// came near, so floors changed while walking and the light and its relief's
// shadows "popped in" (Julius 26.10.2026: "wie das Licht und der Schatten
// sich reinpoppt"). Only with more lights than places does the view choose
// which; the four places keep their light (by its key) while it stays among the nearest,
// give or take DYNLIGHT_KEEP: two lights about as near (a suit charger and a
// health charger) took turns every few steps, the relief flickered between
// them (Julius 26.10.2026); and a light coming or going fades in
// (DYNLIGHT_FADE_IN) or out (DYNLIGHT_FADE_OUT) instead of switching. Fewer
// lights: the rest has radius 0. Lights that light only models (elights)
// are not among them.
//
// Cost: one walk over the active lights per frame; in the shader up to four
// short light terms per wall pixel.
//
// The sun goes along the same way for Water_Fidelity (the water deploy.bat
// sets up): its direction and colour for the glint on the waves
// (r_fidelity_water_glint), and the foam at the shore (r_fidelity_water_foam).
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "iefx.h"
#include "dlight.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include "fidelity/fidelity_renderparm.h"
#include "fidelity_godrays.h"
#include "fidelity_sky.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_dynlights( "r_fidelity_dynlights", "1", FCVAR_ARCHIVE, "The nearest dynamic lights (muzzle flashes, explosions, glowing things) light the walls' relief and a highlight per pixel. 0 = off." );
ConVar r_fidelity_dynlights_strength( "r_fidelity_dynlights_strength", "1.0", FCVAR_SERVER_CAN_EXECUTE, "How strongly the dynamic lights show on the walls' relief and highlights." );
ConVar r_fidelity_water_glint( "r_fidelity_water_glint", "3.0", FCVAR_SERVER_CAN_EXECUTE, "How brightly the sun glints on the water's waves (Water_Fidelity). 0 = no glint." );
ConVar r_fidelity_water_foam( "r_fidelity_water_foam", "1.0", FCVAR_SERVER_CAN_EXECUTE, "How much foam the water shows where it is shallow at the shore (Water_Fidelity), up to 2. 0 = none." );

#define DYNLIGHT_REACH		1500.0f	// lights this far beyond their radius from the view still count (as r_fidelity_glow_distance)
#define DYNLIGHT_KEEP		0.75f	// a light in a place stays until another is this much nearer (share of its distance)
#define DYNLIGHT_FADE_IN	0.15f	// seconds
#define DYNLIGHT_FADE_OUT	0.3f

struct FidelityDynLightPlace_t
{
	int m_nKey;			// the light's key (dlight_t::key), -1 = free
	float m_flWeight;	// 0..1, fading in and out
	bool m_bWanted;		// among the nearest this frame
};

class CFidelityDynLights : public CAutoGameSystemPerFrame
{
public:
	CFidelityDynLights() : CAutoGameSystemPerFrame( "CFidelityDynLights" ), m_bSet( false ) { FreePlaces(); }

	virtual void PreRender();
	virtual void LevelShutdownPostEntity() { Clear(); }

private:
	void Clear();
	void FreePlaces();
	void SetSun();
	bool m_bSet;	// parameters were set (cleared when switched off)
	FidelityDynLightPlace_t m_Places[ FIDELITY_DYNAMIC_LIGHTS ];
};

static CFidelityDynLights s_FidelityDynLights;

void CFidelityDynLights::FreePlaces()
{
	for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
	{
		m_Places[ n ].m_nKey = -1;
		m_Places[ n ].m_flWeight = 0.0f;
		m_Places[ n ].m_bWanted = false;
	}
}

void CFidelityDynLights::Clear()
{
	FreePlaces();
	CMatRenderContextPtr pRenderContext( materials );
	for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
	{
		pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII + n, 0.0f );
		pRenderContext->SetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * n + 1, vec3_origin );
	}
	m_bSet = false;
}

//-----------------------------------------------------------------------------
// The sun for the water's glint: its direction (env_sun, else env_global_light)
// and the colour of its disc (env_sun's, brightest channel 1, linear; white
// without one) x r_fidelity_water_glint, less behind clouds. None: colour 0.
//-----------------------------------------------------------------------------
void CFidelityDynLights::SetSun()
{
	Vector vecDirection( 0.0f, 0.0f, 1.0f ), vecColor( 0.0f, 0.0f, 0.0f );
	const float flGlint = MAX( r_fidelity_water_glint.GetFloat(), 0.0f );
	if ( engine->IsInGame() && flGlint > 0.0f && Fidelity_SunDirection( vecDirection ) && vecDirection.z < 0.999f )
	{
		Vector vecSpriteDir, vecCore, vecGlow;
		float flCoreSize, flGlowSize;
		vecColor.Init( 1.0f, 1.0f, 1.0f );
		if ( Fidelity_SunSprite( vecSpriteDir, vecCore, flCoreSize, vecGlow, flGlowSize ) )
		{
			const float flMax = MAX( vecCore.x, MAX( vecCore.y, vecCore.z ) );
			if ( flMax > 0.001f )	// (gamma: to linear, as the shader adds it)
				vecColor.Init( powf( vecCore.x / flMax, 2.2f ), powf( vecCore.y / flMax, 2.2f ), powf( vecCore.z / flMax, 2.2f ) );
		}
		vecColor *= flGlint * Fidelity_SunLightThroughClouds();
	}
	else
	{
		vecDirection.Init( 0.0f, 0.0f, 1.0f );
	}
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_SUN_DIRECTION, vecDirection );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_R, vecColor.x );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_G, vecColor.y );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_B, vecColor.z );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_WATER_FOAM, clamp( r_fidelity_water_foam.GetFloat(), 0.0f, 2.0f ) );
}

void CFidelityDynLights::PreRender()
{
	SetSun();
	if ( !r_fidelity_dynlights.GetBool() || !effects || !engine->IsInGame() )
	{
		if ( m_bSet )
			Clear();
		return;
	}
	dlight_t *pActive[ MAX_DLIGHTS ];
	const int nActive = effects->CL_GetActiveDLights( pActive );
	const Vector &vecView = MainViewOrigin();

	// the lights that reach the view, nearest first; one already in a place counts
	// DYNLIGHT_KEEP nearer (it keeps its place against one about as near)
	int nOrder[ MAX_DLIGHTS ];
	float flScore[ MAX_DLIGHTS ];
	int nCount = 0;
	for ( int i = 0; i < nActive; i++ )
	{
		const dlight_t *pLight = pActive[ i ];
		if ( !pLight || pLight->radius <= 1.0f || ( pLight->flags & DLIGHT_NO_WORLD_ILLUMINATION ) )
			continue;
		const float flDist = vecView.DistTo( pLight->origin );
		if ( flDist > pLight->radius + DYNLIGHT_REACH )
			continue;
		float flRank = MAX( flDist - pLight->radius, 0.0f ) + 1.0f;	// how far its lit sphere is
		for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
		{
			if ( m_Places[ n ].m_nKey == pLight->key )
				flRank *= DYNLIGHT_KEEP;
		}
		int nAt = nCount++;
		while ( nAt > 0 && flScore[ nAt - 1 ] > flRank )
		{
			nOrder[ nAt ] = nOrder[ nAt - 1 ];
			flScore[ nAt ] = flScore[ nAt - 1 ];
			nAt--;
		}
		nOrder[ nAt ] = i;
		flScore[ nAt ] = flRank;
	}
	const int nWanted = MIN( nCount, FIDELITY_DYNAMIC_LIGHTS );

	// places: the wanted lights keep theirs (and fade in), the others fade out;
	// a light that went out leaves its place at once (the lightmap lost it too)
	for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
		m_Places[ n ].m_bWanted = false;
	bool bPlaced[ FIDELITY_DYNAMIC_LIGHTS ] = {};
	for ( int k = 0; k < nWanted; k++ )
	{
		const dlight_t *pLight = pActive[ nOrder[ k ] ];
		for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
		{
			if ( m_Places[ n ].m_nKey == pLight->key )
			{
				m_Places[ n ].m_bWanted = true;
				bPlaced[ k ] = true;
			}
		}
	}
	const dlight_t *pPlaceLight[ FIDELITY_DYNAMIC_LIGHTS ] = {};
	for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
	{
		for ( int i = 0; i < nActive && m_Places[ n ].m_nKey >= 0; i++ )
		{
			if ( pActive[ i ] && pActive[ i ]->key == m_Places[ n ].m_nKey )
				pPlaceLight[ n ] = pActive[ i ];
		}
		if ( m_Places[ n ].m_nKey >= 0 && !pPlaceLight[ n ] )
		{
			m_Places[ n ].m_nKey = -1;
			m_Places[ n ].m_flWeight = 0.0f;
		}
	}
	for ( int k = 0; k < nWanted; k++ )
	{
		if ( bPlaced[ k ] )
			continue;
		// a free place, else one whose light has faded out
		for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
		{
			FidelityDynLightPlace_t &place = m_Places[ n ];
			if ( place.m_nKey < 0 || ( !place.m_bWanted && place.m_flWeight <= 0.0f ) )
			{
				place.m_nKey = pActive[ nOrder[ k ] ]->key;
				place.m_flWeight = 0.0f;
				place.m_bWanted = true;
				pPlaceLight[ n ] = pActive[ nOrder[ k ] ];
				break;
			}
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	const float flStrength = MAX( r_fidelity_dynlights_strength.GetFloat(), 0.0f ) / 255.0f;	// (ColorRGBExp32ToVector: x 255)
	const float flTime = gpGlobals->frametime;
	for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS; n++ )
	{
		FidelityDynLightPlace_t &place = m_Places[ n ];
		Vector vecPos = vec3_origin, vecColor = vec3_origin;
		float flRadius = 0.0f;
		const dlight_t *pLight = pPlaceLight[ n ];
		if ( pLight )
		{
			place.m_flWeight = place.m_bWanted ? MIN( place.m_flWeight + flTime / DYNLIGHT_FADE_IN, 1.0f )
				: MAX( place.m_flWeight - flTime / DYNLIGHT_FADE_OUT, 0.0f );
			vecPos = pLight->origin;
			flRadius = pLight->radius;
			ColorRGBExp32ToVector( pLight->color, vecColor );
			vecColor *= flStrength * place.m_flWeight;
		}
		pRenderContext->SetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * n, vecPos );
		pRenderContext->SetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * n + 1, vecColor );
		pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII + n, ( vecColor.x + vecColor.y + vecColor.z ) > 0.0f ? flRadius : 0.0f );
	}
	m_bSet = true;
}
