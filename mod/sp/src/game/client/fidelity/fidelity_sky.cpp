//========= mapbase-fidelity ==================================================//
//
// Purpose: Drifting clouds in the skybox, without any .vmt or Hammer work.
//
// When a map is loaded, the skybox materials (materials/skybox/..., drawn
// with Valve's Sky shaders) are switched over to Fidelity_Sky from our shader
// DLL, with the parameters of their own .vmt - the sky looks exactly as before
// and gets a layer of clouds on top, grey in the brightness of that sky
// (darker bellies, lighter edges: a night sky gets dark clouds, a bright day
// light grey ones). Every frame they drift with the wind - the map's env_wind
// or the breeze of fidelity_wind.cpp - as fast as it blows, so sky and
// swaying trees agree.
//
// Safe by design: if the shader DLL is not loaded, or a material does not end
// up with Fidelity_Sky, it gets its original parameters back. Skies using
// Valve's 3-texture HDR method ($hdrcompressedtexture0) stay as they are.
// The clouds also pass in front of the sun: the same cloud maths as the
// shader, on the CPU for the direction of the sun once a frame, tells how much
// of it they cover (Fidelity_SunCloudCover). The sun's light and shadows
// (env_global_light, fidelity_shadows.cpp), its glow and its light shafts
// (fidelity_godrays.cpp) dim with it - a little: clouds never block it fully.
//
// Cost: a few material values per frame, the clouds in front of the sun once
// a frame on the CPU; the clouds themselves are noise maths on sky pixels only
// (fidelity_sky_ps30.fxc).
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "fidelity_wind.h"
#include "fidelity_godrays.h"
#include "fidelity_sky.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_sky( "r_fidelity_sky", "1", FCVAR_ARCHIVE, "Skybox gets drifting clouds (within a few seconds). 0 = original sky." );
ConVar r_fidelity_sky_clouds( "r_fidelity_sky_clouds", "0.85", FCVAR_SERVER_CAN_EXECUTE, "How visible the drifting clouds are, 0 = none .. 1 = strong." );
ConVar r_fidelity_sky_clouds_coverage( "r_fidelity_sky_clouds_coverage", "0.38", FCVAR_SERVER_CAN_EXECUTE, "How much of the sky the clouds cover, 0.3 = a lot .. 0.7 = a few wisps." );
ConVar r_fidelity_sky_clouds_darkness( "r_fidelity_sky_clouds_darkness", "0.5", FCVAR_SERVER_CAN_EXECUTE, "How dark the thick parts of the clouds are, 0 = as bright as the sky .. 1 = very dark." );
ConVar r_fidelity_sky_clouds_scale( "r_fidelity_sky_clouds_scale", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Size of the cloud pattern; bigger = smaller clouds." );
ConVar r_fidelity_sky_clouds_speed( "r_fidelity_sky_clouds_speed", "1.0", FCVAR_SERVER_CAN_EXECUTE, "How fast the clouds drift, times the wind speed (direction: the wind)." );

#define SKY_SHADER			"Fidelity_Sky"
// Drift per second at speed 1 in a breeze (wind 1, see Fidelity_WindVector),
// in cloud pattern units (one unit is roughly a cloud at scale 1): a cloud
// moves its own width in about 12 seconds; stronger wind, faster clouds.
#define SKY_DRIFT_PER_SECOND	0.08f
// Clouds never stand quite still, and a storm does not make them race.
#define SKY_WIND_MIN			0.3f
#define SKY_WIND_MAX			4.0f
// The drift is kept below this (the noise has no period: wrapping makes the
// layer jump once, at the strongest wind every few hours)
#define SKY_DRIFT_WRAP			4096.0f
// Must match fidelity_sky_ps30.fxc (the clouds in front of the sun)
#define SKY_CLOUD_STEPS			6
#define SKY_CLOUD_THICKNESS		0.35f
#define SKY_CLOUD_SOFTNESS		0.22f
#define SKY_SUN_COVER_TIME		4.0f	// seconds for the sun's light to follow a passing cloud (0.6: the scene pumped brighter and darker)
#define SKY_SUN_GLOW			0.5f	// strength of the glow round the sun the sky draws (of the env_sun's glow)

class CFidelitySky : public CAutoGameSystemPerFrame
{
public:
	CFidelitySky() : CAutoGameSystemPerFrame( "CFidelitySky" ), m_flNextScan( 0.0f ), m_flSunCover( 0.0f ), m_bDrawsSun( false ) { m_vecDrift.Init(); m_vecDriftSpeed.Init(); }

	virtual void LevelInitPostEntity() { m_flNextScan = 0.0f; }
	virtual void LevelShutdownPreEntity() { m_Skies.RemoveAll(); m_Tried.RemoveAll(); }
	virtual void Update( float frametime );
	void Report();

private:
	void Scan();
	void RestoreAll();
	static bool SwitchOver( IMaterial *pMaterial );

	CUtlVector< IMaterial * > m_Skies;	// materials that use Fidelity_Sky
	CUtlVector< IMaterial * > m_Tried;	// switched or refused this map: not tried again (a refusal reloads the .vmt)
	Vector2D m_vecDrift;
	Vector2D m_vecDriftSpeed;	// follows the wind smoothly (gusts do not jerk the sky)
	float m_flNextScan;
public:
	float m_flSunCover;			// how much the clouds cover the sun, 0..1, smoothed
	bool m_bDrawsSun;			// the sky draws the env_sun (its sprites are hidden)
};

static CFidelitySky s_FidelitySky;

//-----------------------------------------------------------------------------
// The shader's cloud maths on the CPU (fidelity_sky_ps30.fxc, fidelity_common.h):
// same hash, value noise, octaves and layer
//-----------------------------------------------------------------------------
static inline float Fidelity_Frac( float x ) { return x - floorf( x ); }

static float Fidelity_Hash( float x, float y )
{
	float a = Fidelity_Frac( x * 0.1031f ), b = Fidelity_Frac( y * 0.1031f ), c = Fidelity_Frac( x * 0.1031f );
	const float d = a * ( b + 33.33f ) + b * ( c + 33.33f ) + c * ( a + 33.33f );
	a += d;
	b += d;
	c += d;
	return Fidelity_Frac( ( a + b ) * c );
}

static float Fidelity_ValueNoise( float x, float y )
{
	const float ix = floorf( x ), iy = floorf( y );
	const float fx = x - ix, fy = y - iy;
	const float ux = fx * fx * ( 3.0f - 2.0f * fx ), uy = fy * fy * ( 3.0f - 2.0f * fy );
	const float a = Lerp( ux, Fidelity_Hash( ix, iy ), Fidelity_Hash( ix + 1.0f, iy ) );
	const float b = Lerp( ux, Fidelity_Hash( ix, iy + 1.0f ), Fidelity_Hash( ix + 1.0f, iy + 1.0f ) );
	return Lerp( uy, a, b );
}

static float Fidelity_CloudNoise( float x, float y )
{
	float v = 0.0f, a = 0.5f;
	for ( int k = 0; k < 4; k++ )
	{
		v += a * Fidelity_ValueNoise( x, y );
		x = x * 2.03f + 17.1f;
		y = y * 2.03f + 9.3f;
		a *= 0.5f;
	}
	return v;
}

// How much the clouds cover direction vecDir (0 clear .. 1 thick), from the
// layer's coverage and drift - as the shader draws it
static float Fidelity_CloudCover( const Vector &vecDir, const Vector2D &vecDrift, float flScale, float flCoverage )
{
	if ( vecDir.z <= 0.0f )
		return 0.0f;
	const float flFade = SimpleSplineRemapValClamped( vecDir.z, -0.01f, 0.04f, 0.0f, 1.0f );	// as the shader's fade
	const float flAlongX = vecDir.x / MAX( vecDir.z, 0.012f ) * flScale * 1.6f;
	const float flAlongY = vecDir.y / MAX( vecDir.z, 0.012f ) * flScale * 1.6f;
	const float flStepDepth = MIN( SKY_CLOUD_THICKNESS / MAX( vecDir.z, 0.012f ), 3.0f ) / SKY_CLOUD_STEPS * 12.0f;
	float flTransmit = 1.0f;
	for ( int k = 0; k < SKY_CLOUD_STEPS; k++ )
	{
		const float h = ( k + 0.5f ) / SKY_CLOUD_STEPS;
		const float px = flAlongX * ( 1.0f + h * SKY_CLOUD_THICKNESS ), py = flAlongY * ( 1.0f + h * SKY_CLOUD_THICKNESS );
		float n = Fidelity_CloudNoise( px + vecDrift.x, py + vecDrift.y ) * 0.65f +
			Fidelity_CloudNoise( px * 0.47f + vecDrift.x * 0.6f + 31.7f, py * 0.47f + vecDrift.y * 0.6f + 31.7f ) * 0.35f;
		n += ( Fidelity_ValueNoise( px * 6.3f + vecDrift.x * 1.3f, py * 6.3f + vecDrift.y * 1.3f ) - 0.5f ) * 0.1f;
		const float flNeed = flCoverage + h * h * 0.22f;
		const float flDensity = clamp( ( n - flNeed ) / SKY_CLOUD_SOFTNESS, 0.0f, 1.0f ) * SimpleSplineRemapValClamped( h, 0.0f, 0.12f, 0.0f, 1.0f );
		flTransmit *= expf( -flDensity * flStepDepth );
	}
	return ( 1.0f - flTransmit ) * flFade;
}

float Fidelity_SunCloudCover()
{
	return s_FidelitySky.m_flSunCover;
}

bool Fidelity_SkyDrawsSun()
{
	return s_FidelitySky.m_bDrawsSun;
}

// What the sky does and why the sun might not be drawn behind the clouds
void CFidelitySky::Report()
{
	Msg( "[fidelity] sky: %d skybox material(s) on Fidelity_Sky, clouds %.2f, sun %s by the sky, clouds cover it %.0f%%\n",
		m_Skies.Count(), r_fidelity_sky_clouds.GetFloat(), m_bDrawsSun ? "drawn" : "NOT drawn", m_flSunCover * 100.0f );
	for ( int i = 0; i < m_Skies.Count(); i++ )
	{
		bool bFound = false;
		m_Skies[ i ]->FindVar( "$sundisc", &bFound, false );
		Msg( "[fidelity]   %s%s\n", m_Skies[ i ]->GetName(), bFound ? "" : " - no $sundisc: bin\\game_shader_dx6.dll is old (build games.sln, run deploy.bat)" );
	}
	Vector vecDir, vecCore, vecGlow;
	float flCore, flGlow;
	if ( Fidelity_SunSprite( vecDir, vecCore, flCore, vecGlow, flGlow ) )
		Msg( "[fidelity]   env_sun: direction %.2f %.2f %.2f, size %.3f rad, glow %.3f rad\n", vecDir.x, vecDir.y, vecDir.z, flCore, flGlow );
	else
		Msg( "[fidelity]   no env_sun that is on and has a direction (\"UseAngles\" or a target): the engine draws its sun\n" );
}

CON_COMMAND( r_fidelity_sky_info, "Prints what the animated sky does: its materials, and whether it draws the sun behind its clouds." )
{
	s_FidelitySky.Report();
}

//-----------------------------------------------------------------------------
// Puts one skybox material on Fidelity_Sky with the parameters of its .vmt.
// The KeyValues handed to the material stay alive on purpose: it is not
// documented whether the material keeps a pointer to them (a few small
// blocks per sky and map).
//-----------------------------------------------------------------------------
bool CFidelitySky::SwitchOver( IMaterial *pMaterial )
{
	char szPath[ MAX_PATH ];
	Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vmt", pMaterial->GetName() );
	KeyValues *pOriginal = new KeyValues( "vmt" );
	if ( !pOriginal->LoadFromFile( filesystem, szPath, "GAME" ) || pOriginal->FindKey( "$hdrcompressedtexture0" ) || pOriginal->FindKey( "include" ) )
	{
		pOriginal->deleteThis();
		return false;
	}

	KeyValues *pKeys = pOriginal->MakeCopy();
	pKeys->SetName( SKY_SHADER );
	pMaterial->SetShaderAndParams( pKeys );
	if ( !Q_stricmp( pMaterial->GetShaderName(), SKY_SHADER ) )
	{
		pOriginal->deleteThis();
		return true;
	}

	// Did not work (no shader model 3.0, or something unexpected): as before.
	DevMsg( "[fidelity] %s stays with its own sky shader (got %s)\n", pMaterial->GetName(), pMaterial->GetShaderName() );
	pMaterial->SetShaderAndParams( pOriginal );
	return false;
}

//-----------------------------------------------------------------------------
// Finds the skybox materials of the map (every few seconds: a map can change
// its sky) and switches the new ones over.
//-----------------------------------------------------------------------------
void CFidelitySky::Scan()
{
	const int nBefore = m_Skies.Count();
	m_Skies.RemoveAll();
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || Q_strnicmp( pMaterial->GetName(), "skybox/", 7 ) )
			continue;
		const char *pShader = pMaterial->GetShaderName();
		if ( !Q_stricmp( pShader, SKY_SHADER ) )
		{
			m_Skies.AddToTail( pMaterial );
		}
		else if ( Q_stristr( pShader, "Sky" ) && !pMaterial->IsErrorMaterial() && m_Tried.Find( pMaterial ) == m_Tried.InvalidIndex() )
		{
			m_Tried.AddToTail( pMaterial );
			if ( SwitchOver( pMaterial ) )
			{
				DevMsg( "[fidelity] Sky %s has drifting clouds\n", pMaterial->GetName() );
				m_Skies.AddToTail( pMaterial );
			}
		}
	}
	if ( m_Skies.Count() != nBefore )
		Msg( "[fidelity] Sky: %d skybox materials have drifting clouds.\n", m_Skies.Count() );
}

//-----------------------------------------------------------------------------
// r_fidelity_sky 0: every skybox material on Fidelity_Sky gets its own .vmt
// back (also one that kept our shader from an earlier map with the same sky).
//-----------------------------------------------------------------------------
void CFidelitySky::RestoreAll()
{
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || Q_strnicmp( pMaterial->GetName(), "skybox/", 7 ) || Q_stricmp( pMaterial->GetShaderName(), SKY_SHADER ) )
			continue;
		char szPath[ MAX_PATH ];
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vmt", pMaterial->GetName() );
		KeyValues *pOriginal = new KeyValues( "vmt" );
		if ( pOriginal->LoadFromFile( filesystem, szPath, "GAME" ) )
			pMaterial->SetShaderAndParams( pOriginal );	// kept alive, as in SwitchOver
		else
			pOriginal->deleteThis();
	}
	m_Skies.RemoveAll();
	m_Tried.RemoveAll();
}

void CFidelitySky::Update( float frametime )
{
	m_bDrawsSun = false;	// set again below while the sky can draw it
	if ( !engine->IsInGame() )
		return;
	const bool bScan = gpGlobals->realtime >= m_flNextScan;
	if ( bScan )
		m_flNextScan = gpGlobals->realtime + 5.0f;
	if ( !r_fidelity_sky.GetBool() )
	{
		if ( bScan )
			RestoreAll();
		m_flSunCover = 0.0f;
		return;
	}
	if ( bScan )
		Scan();
	if ( m_Skies.Count() == 0 )
	{
		m_flSunCover = 0.0f;
		return;
	}

	// Drift with the wind, as fast as it blows (direction r_fidelity_wind_yaw
	// when there is no wind at all).
	Vector2D vecWind( Fidelity_WindVector().x, Fidelity_WindVector().y );
	float flWind = vecWind.Length();
	if ( flWind < 0.01f )
	{
		static ConVarRef s_WindYaw( "r_fidelity_wind_yaw" );
		const float flYaw = DEG2RAD( s_WindYaw.IsValid() ? s_WindYaw.GetFloat() : 30.0f );
		vecWind.Init( cosf( flYaw ), sinf( flYaw ) );
		flWind = 0.0f;
	}
	else
	{
		vecWind /= flWind;
	}
	const Vector2D vecTarget = vecWind * ( SKY_DRIFT_PER_SECOND * clamp( flWind, SKY_WIND_MIN, SKY_WIND_MAX ) * r_fidelity_sky_clouds_speed.GetFloat() );
	const float flFollow = MIN( frametime / 2.0f, 1.0f );
	m_vecDriftSpeed += ( vecTarget - m_vecDriftSpeed ) * flFollow;
	// The shader reads the noise at p + drift: the pattern moves against the
	// drift, so the drift runs against the wind.
	m_vecDrift.x = fmodf( m_vecDrift.x - m_vecDriftSpeed.x * frametime, SKY_DRIFT_WRAP );
	m_vecDrift.y = fmodf( m_vecDrift.y - m_vecDriftSpeed.y * frametime, SKY_DRIFT_WRAP );

	const float flAmount = clamp( r_fidelity_sky_clouds.GetFloat(), 0.0f, 1.0f );
	const float flScale = MAX( r_fidelity_sky_clouds_scale.GetFloat(), 0.05f );
	const float flCoverage = clamp( r_fidelity_sky_clouds_coverage.GetFloat(), 0.0f, 1.0f );
	const float flDarkness = clamp( r_fidelity_sky_clouds_darkness.GetFloat(), 0.0f, 1.0f );
	// FindVarFast: a cached symbol instead of a string lookup each frame (and,
	// unlike a kept IMaterialVar pointer, safe after a material reload)
	static unsigned int s_nParamsToken = 0, s_nShapeToken = 0, s_nSunToken = 0, s_nDiscToken = 0, s_nHaloToken = 0;
	// The sun lights the clouds (its side bright, the other in shadow)
	Vector vecSun;
	bool bSun = Fidelity_SunDirection( vecSun );
	// The env_sun is drawn by the sky, behind the clouds (not with clouds off:
	// then the engine's sprites do as before)
	Vector vecSpriteDir( 0, 0, 1 ), vecCore( 0, 0, 0 ), vecGlow( 0, 0, 0 );
	float flCoreSize = 0.0f, flGlowSize = 0.0f;
	const bool bDrawSun = flAmount > 0.0f && Fidelity_SunSprite( vecSpriteDir, vecCore, flCoreSize, vecGlow, flGlowSize );
	if ( bDrawSun )
	{
		vecSun = vecSpriteDir;	// the disc where the sprites were
		bSun = true;
		for ( int c = 0; c < 3; c++ )
		{
			vecCore[ c ] = powf( MAX( vecCore[ c ], 0.0f ), 2.2f );	// the shader works linear
			vecGlow[ c ] = powf( MAX( vecGlow[ c ], 0.0f ), 2.2f );
		}
		// The glow is far wider here than the engine's sprite (it fades into the
		// sky) and took the env_sun's colour - Half-Life 2's suns are pinkish
		// ("100 80 80") for a small sprite: a wide pink halo (Julius 25.10.2026:
		// "viel zu stark", "gedoppelt"). Half as strong, half way to white.
		const float flGlowGrey = ( vecGlow.x + vecGlow.y + vecGlow.z ) / 3.0f;
		vecGlow = ( vecGlow + Vector( flGlowGrey, flGlowGrey, flGlowGrey ) ) * ( 0.5f * SKY_SUN_GLOW );
	}
	bool bAllHaveDisc = bDrawSun;
	for ( int i = 0; i < m_Skies.Count(); i++ )
	{
		IMaterialVar *pParams = m_Skies[ i ]->FindVarFast( "$cloudparams", &s_nParamsToken );
		if ( pParams )
			pParams->SetVecValue( flAmount, flScale, m_vecDrift.x, m_vecDrift.y );
		IMaterialVar *pShape = m_Skies[ i ]->FindVarFast( "$cloudshape", &s_nShapeToken );
		if ( pShape )
			pShape->SetVecValue( flCoverage, 0.22f, flDarkness, 0.0f );
		IMaterialVar *pSun = m_Skies[ i ]->FindVarFast( "$cloudsun", &s_nSunToken );
		if ( pSun )
			pSun->SetVecValue( vecSun.x, vecSun.y, vecSun.z, bSun ? 1.0f : 0.0f );
		IMaterialVar *pDisc = m_Skies[ i ]->FindVarFast( "$sundisc", &s_nDiscToken );
		IMaterialVar *pHalo = m_Skies[ i ]->FindVarFast( "$sunhalo", &s_nHaloToken );
		if ( pDisc && pHalo )
		{
			pDisc->SetVecValue( vecCore.x, vecCore.y, vecCore.z, bDrawSun ? flCoreSize : 0.0f );
			pHalo->SetVecValue( vecGlow.x, vecGlow.y, vecGlow.z, flGlowSize );
		}
		else
		{
			bAllHaveDisc = false;	// an older shader DLL: the engine keeps drawing the sun
		}
	}
	m_bDrawsSun = bAllHaveDisc;

	// The clouds in front of the sun, followed smoothly
	const float flCover = bSun ? Fidelity_CloudCover( vecSun, m_vecDrift, flScale, flCoverage ) * flAmount : 0.0f;
	m_flSunCover += ( flCover - m_flSunCover ) * MIN( frametime / SKY_SUN_COVER_TIME, 1.0f );
}
