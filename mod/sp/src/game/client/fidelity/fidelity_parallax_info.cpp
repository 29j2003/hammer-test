//========= mapbase-fidelity ==================================================//
//
// Purpose: r_fidelity_parallax_info - which walls have parallax;
//          r_fidelity_parallax - switches it off and on at once, to compare.
//
// deploy.bat gives the game's bump-mapped walls and floors the shader
// Fidelity_Parallax (real depth, see materialsystem/fidelity/
// fidelity_parallax.cpp; blended displacements Fidelity_WorldVertexTransition,
// the same shader). Where a wall shows none, this tells why: it names
// the material in the middle of the screen (look at the wall, then type the
// command), its shader and what it lacks, and counts the materials of the
// map with and without parallax.
//
// r_fidelity_parallax 0 sets every Fidelity_Parallax material's
// $parallaxdepth to 0 (the shader then draws the wall flat, with its lighting
// unchanged) and 1 puts the depths back - what is the relief's doing and what
// not shows at a glance (Julius: odd spots where floors meet walls). Off over
// a map change: back before the map goes, off again after the next is loaded.
//
// Nothing runs per frame.
//
//=============================================================================//

#include "cbase.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "igamesystem.h"
#include "tier1/utlmap.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Fidelity_Parallax, and Fidelity_WorldVertexTransition for blended surfaces
static bool FidelityIsParallaxShader( const char *pszShader )
{
	return !Q_stricmp( pszShader, "Fidelity_Parallax" ) || !Q_stricmp( pszShader, "Fidelity_WorldVertexTransition" );
}

static void FidelityApplyParallaxSwitch();

static void FidelityParallaxSwitched( IConVar *pVar, const char *pszOldValue, float flOldValue )
{
	FidelityApplyParallaxSwitch();
}

ConVar r_fidelity_parallax( "r_fidelity_parallax", "1", FCVAR_ARCHIVE, "Depth (parallax) on walls, floors and bullet holes. 0 = flat, to compare.", FidelityParallaxSwitched );

// The depths of the materials switched off, to put back
static CUtlMap< IMaterial *, float > s_FidelityParallaxDepths( DefLessFunc( IMaterial * ) );

//-----------------------------------------------------------------------------
// Every Fidelity_Parallax material flat (r_fidelity_parallax 0), or the
// depths back.
//-----------------------------------------------------------------------------
static void FidelitySetParallax( bool bOn )
{
	if ( bOn )
	{
		FOR_EACH_MAP_FAST( s_FidelityParallaxDepths, i )
		{
			bool bFound = false;
			IMaterialVar *pDepth = s_FidelityParallaxDepths.Key( i )->FindVar( "$parallaxdepth", &bFound, false );
			if ( bFound && pDepth )
				pDepth->SetFloatValue( s_FidelityParallaxDepths.Element( i ) );
		}
		s_FidelityParallaxDepths.RemoveAll();
		return;
	}
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || pMaterial->IsErrorMaterial() || !FidelityIsParallaxShader( pMaterial->GetShaderName() ) )
			continue;
		if ( s_FidelityParallaxDepths.Find( pMaterial ) != s_FidelityParallaxDepths.InvalidIndex() )
			continue;
		bool bFound = false;
		IMaterialVar *pDepth = pMaterial->FindVar( "$parallaxdepth", &bFound, false );
		if ( !bFound || !pDepth )
			continue;
		s_FidelityParallaxDepths.Insert( pMaterial, pDepth->GetFloatValue() );
		pDepth->SetFloatValue( 0.0f );
	}
}

static void FidelityApplyParallaxSwitch()
{
	FidelitySetParallax( r_fidelity_parallax.GetBool() );
}

//-----------------------------------------------------------------------------
// Off over a map change: the depths back before the map goes (its materials
// may stay loaded for the next), off again once the next is loaded
//-----------------------------------------------------------------------------
class CFidelityParallaxSwitch : public CAutoGameSystem
{
public:
	CFidelityParallaxSwitch() : CAutoGameSystem( "CFidelityParallaxSwitch" ) {}
	virtual void LevelInitPostEntity() { FidelityApplyParallaxSwitch(); }
	virtual void LevelShutdownPreEntity() { FidelitySetParallax( true ); }
};

static CFidelityParallaxSwitch s_FidelityParallaxSwitch;

//-----------------------------------------------------------------------------
// Does the material have this parameter, set?
//-----------------------------------------------------------------------------
static bool FidelityHasVar( IMaterial *pMaterial, const char *pszName )
{
	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( pszName, &bFound, false );
	return bFound && pVar && pVar->IsDefined();
}

//-----------------------------------------------------------------------------
// A world material that deploy.bat could have given parallax (it uses
// LightmappedGeneric or WorldVertexTransition and has a bump map)
//-----------------------------------------------------------------------------
static bool FidelityIsBumpedWall( IMaterial *pMaterial )
{
	const char *pszShader = pMaterial->GetShaderName();
	if ( !Q_stristr( pszShader, "LightmappedGeneric" ) && !Q_stristr( pszShader, "WorldVertexTransition" ) )
		return false;
	return FidelityHasVar( pMaterial, "$bumpmap" );
}

//-----------------------------------------------------------------------------
// What the material in the middle of the screen is, and why it has no parallax
//-----------------------------------------------------------------------------
static void FidelityReportLookedAt()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	trace_t tr;
	UTIL_TraceLine( MainViewOrigin(), MainViewOrigin() + MainViewForward() * MAX_TRACE_LENGTH, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction >= 1.0f || !tr.surface.name || !tr.surface.name[ 0 ] )
	{
		Msg( "[fidelity]   looking at: nothing (look at a wall, then type the command)\n" );
		return;
	}
	if ( tr.surface.name[ 0 ] == '*' )
	{
		Msg( "[fidelity]   looking at: %s (a model, not a wall)\n", tr.surface.name );
		return;
	}
	IMaterial *pMaterial = materials->FindMaterial( tr.surface.name, TEXTURE_GROUP_WORLD, false );
	if ( !pMaterial || pMaterial->IsErrorMaterial() )
	{
		Msg( "[fidelity]   looking at: %s (material not found)\n", tr.surface.name );
		return;
	}
	const char *pszShader = pMaterial->GetShaderName();
	Msg( "[fidelity]   looking at: %s, shader %s\n", pMaterial->GetName(), pszShader );
	if ( FidelityIsParallaxShader( pszShader ) )
	{
		bool bFound = false;
		IMaterialVar *pDepth = pMaterial->FindVar( "$parallaxdepth", &bFound, false );
		IMaterialVar *pHeight = pMaterial->FindVar( "$parallaxmap", &bFound, false );
		const bool bHeight = pHeight && pHeight->IsTexture() && !pHeight->GetTextureValue()->IsError();
		Msg( "[fidelity]   parallax: yes, depth %.3f%s%s%s%s\n", pDepth ? pDepth->GetFloatValue() : 0.0f,
			bHeight ? "" : " - but its height map is missing (run deploy.bat again)",
			FidelityHasVar( pMaterial, "$envmap" ) ? ", with reflection" : "", FidelityHasVar( pMaterial, "$detail" ) ? ", with detail texture" : "",
			!FidelityHasVar( pMaterial, "$basetexture2" ) ? "" : ( FidelityHasVar( pMaterial, "$parallaxmap2" ) ? ", blended: both layers with depth" : ", blended: the second layer flat" ) );
	}
	else if ( FidelityIsBumpedWall( pMaterial ) )
		Msg( "[fidelity]   parallax: no - it has a bump map; deploy.bat leaves out see-through walls, three-way mapped ones ($seamless_scale) and detail modes other than 0 and 1 (its line 'left out'), or it was not run since\n" );
	else if ( Q_stristr( pszShader, "LightmappedGeneric" ) )
		Msg( "[fidelity]   parallax: no - without a bump map deploy.bat takes only walls whose name tells of relief (brick, stone, cobble, rock, tile, block, slate, plank, roof, shingle ...); run it again after updating\n" );
	else if ( Q_stristr( pszShader, "WorldVertexTransition" ) )
		Msg( "[fidelity]   parallax: no - a blended surface without a bump map whose names tell of no relief; run deploy.bat again after updating\n" );
	else
		Msg( "[fidelity]   parallax: no - shader %s is not a plain wall shader\n", pszShader );
}

CON_COMMAND( r_fidelity_parallax_info, "Prints which wall materials have parallax (depth), and for the one in the middle of the screen why it has none." )
{
	if ( !engine->IsInGame() )
	{
		Msg( "[fidelity] r_fidelity_parallax_info: load a map first\n" );
		return;
	}
	// The map's own patched copies (maps/<map>/..., with its cubemaps) are
	// not counted twice
	int nParallax = 0, nWithout = 0;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || pMaterial->IsErrorMaterial() || !pMaterial->IsPrecached() || !Q_strnicmp( pMaterial->GetName(), "maps/", 5 ) )
			continue;
		if ( FidelityIsParallaxShader( pMaterial->GetShaderName() ) )
			nParallax++;
		else if ( FidelityIsBumpedWall( pMaterial ) )
			nWithout++;
	}
	Msg( "[fidelity] parallax: %d materials in use with depth, %d bump-mapped walls without\n", nParallax, nWithout );
	if ( nParallax == 0 )
		Msg( "[fidelity]   none at all: run deploy.bat (it needs the shader DLL, bin\\game_shader_dx6.dll) and restart the mod\n" );
	FidelityReportLookedAt();
}
