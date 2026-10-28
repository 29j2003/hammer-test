//========= mapbase-fidelity ==================================================//
//
// Purpose: Reflections that fit the room (parallax-corrected cubemaps) without
//          any work in Hammer.
//
// A cubemap is a picture of the room taken at one point (env_cubemap). Seen
// from anywhere else, a plain reflection shows the room as if it were
// infinitely far away: windows and lamps slide over the floor as you walk.
// Mapbase corrects that (Lagarde 2012, as Strata Source and Portal 2: CE):
// the reflected ray is followed to the wall of the room's box and the
// cubemap looked up towards that point - but only where the mapper drew a
// parallax_obb brush around the room.
//
// Here every env_cubemap in a closed room gets that box by itself: when the
// map is loaded, six traces from the cubemap to the walls, floor and ceiling
// (brushes only) give the room's box. The map's patched materials (VBSP
// names them maps/<map>/<material>_<x>_<y>_<z>, x y z = the cubemap's place)
// get Mapbase's parameters ($envmapparallax, $envmaporigin,
// $envmapparallaxobb1..3) and their shader state is rebuilt. Left as they
// are: cubemaps under the open sky or in very large spaces (a trace finds no
// wall within 4096 units, or sky), materials that have a box from Hammer
// already, and shaders that do not know the parameters (Mapbase's
// LightmappedGeneric / WorldVertexTransition and Fidelity_Parallax do).
// r_fidelity_cubemaps 0 puts the materials back at once.
//
// World space: the reflection does not depend on what is on screen. Cost: a
// few traces per cubemap when the map loads, nothing per frame.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/utlmap.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static void FidelityCubemapsSwitched( IConVar *pVar, const char *pszOldValue, float flOldValue );
ConVar r_fidelity_cubemaps( "r_fidelity_cubemaps", "1", FCVAR_ARCHIVE, "Reflections fit the room: each env_cubemap's box is measured when the map loads (parallax-corrected cubemaps). 0 = as the map was built.", FidelityCubemapsSwitched );

#define CUBEMAP_REACH		4096.0f		// a room's wall must be nearer than this
#define CUBEMAP_MIN_SIZE	16.0f		// each side at least this far from the cubemap

struct FidelityCubemapBox_t
{
	bool m_bValid;
	Vector m_vecMins;
	Vector m_vecMaxs;
};

//-----------------------------------------------------------------------------
// The boxes and the materials given one (to put back)
//-----------------------------------------------------------------------------
class CFidelityCubemaps : public CAutoGameSystem
{
public:
	CFidelityCubemaps() : CAutoGameSystem( "CFidelityCubemaps" ), m_Boxes( DefLessFunc( int64 ) ) {}

	virtual void LevelInitPostEntity() { if ( r_fidelity_cubemaps.GetBool() ) Apply(); }
	// put back before the map goes: loading the same map again (a save) keeps
	// its materials, and they would look like boxes from Hammer
	virtual void LevelShutdownPreEntity() { Restore(); m_Boxes.RemoveAll(); }

	void Apply();
	void Restore();

private:
	const FidelityCubemapBox_t &BoxAt( int x, int y, int z );

	CUtlMap< int64, FidelityCubemapBox_t > m_Boxes;	// by place
	CUtlVector< IMaterial * > m_Changed;
};

static CFidelityCubemaps s_FidelityCubemaps;

static void FidelityCubemapsSwitched( IConVar *pVar, const char *pszOldValue, float flOldValue )
{
	if ( !engine->IsInGame() )
		return;
	if ( r_fidelity_cubemaps.GetBool() )
		s_FidelityCubemaps.Apply();
	else
		s_FidelityCubemaps.Restore();
}

// The room around a cubemap: six traces to its walls (brushes only)
const FidelityCubemapBox_t &CFidelityCubemaps::BoxAt( int x, int y, int z )
{
	const int64 nKey = ( (int64)( x & 0x1FFFFF ) << 42 ) | ( (int64)( y & 0x1FFFFF ) << 21 ) | (int64)( z & 0x1FFFFF );
	unsigned short nIndex = m_Boxes.Find( nKey );
	if ( nIndex != m_Boxes.InvalidIndex() )
		return m_Boxes[ nIndex ];

	FidelityCubemapBox_t box;
	box.m_bValid = true;
	const Vector vecOrigin( x, y, z );
	box.m_vecMins = box.m_vecMaxs = vecOrigin;
	for ( int nAxis = 0; nAxis < 3 && box.m_bValid; nAxis++ )
	{
		for ( int nSign = -1; nSign <= 1 && box.m_bValid; nSign += 2 )
		{
			Vector vecDir( 0, 0, 0 );
			vecDir[ nAxis ] = (float)nSign;
			trace_t tr;
			UTIL_TraceLine( vecOrigin, vecOrigin + vecDir * CUBEMAP_REACH, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.startsolid || tr.fraction >= 1.0f || ( tr.surface.flags & SURF_SKY ) || tr.fraction * CUBEMAP_REACH < CUBEMAP_MIN_SIZE )
			{
				box.m_bValid = false;	// open sky, a huge space, or a cubemap inside a wall
				break;
			}
			if ( nSign < 0 )
				box.m_vecMins[ nAxis ] = tr.endpos[ nAxis ];
			else
				box.m_vecMaxs[ nAxis ] = tr.endpos[ nAxis ];
		}
	}
	nIndex = m_Boxes.Insert( nKey, box );
	return m_Boxes[ nIndex ];
}

// The cubemap's place from the patched material's name (..._<x>_<y>_<z>)
static bool Fidelity_CubemapPlace( const char *pszName, int &x, int &y, int &z )
{
	int nValues[ 3 ];
	const char *pszEnd = pszName + Q_strlen( pszName );
	for ( int i = 2; i >= 0; i-- )
	{
		const char *pszStart = pszEnd;
		while ( pszStart > pszName && ( isdigit( (unsigned char)pszStart[ -1 ] ) || pszStart[ -1 ] == '-' ) )
			pszStart--;
		if ( pszStart == pszEnd || pszStart == pszName || pszStart[ -1 ] != '_' )
			return false;
		char szNumber[ 16 ];
		Q_strncpy( szNumber, pszStart, MIN( (int)sizeof( szNumber ), (int)( pszEnd - pszStart ) + 1 ) );
		nValues[ i ] = atoi( szNumber );
		pszEnd = pszStart - 1;
	}
	x = nValues[ 0 ];
	y = nValues[ 1 ];
	z = nValues[ 2 ];
	return true;
}

static IMaterialVar *Fidelity_CubemapVar( IMaterial *pMaterial, const char *pszName )
{
	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( pszName, &bFound, false );
	return bFound ? pVar : NULL;
}

void CFidelityCubemaps::Apply()
{
	Restore();
	// this map's patched materials: maps/<map>/...
	char szPrefix[ MAX_PATH ];
	Q_FileBase( engine->GetLevelName(), szPrefix, sizeof( szPrefix ) );
	char szMaps[ MAX_PATH ];
	Q_snprintf( szMaps, sizeof( szMaps ), "maps/%s/", szPrefix );
	const int nPrefix = Q_strlen( szMaps );

	int nRooms = 0, nOpen = 0;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || pMaterial->IsErrorMaterial() || Q_strnicmp( pMaterial->GetName(), szMaps, nPrefix ) )
			continue;
		IMaterialVar *pEnvmap = Fidelity_CubemapVar( pMaterial, "$envmap" );
		IMaterialVar *pParallax = Fidelity_CubemapVar( pMaterial, "$envmapparallax" );
		IMaterialVar *pOrigin = Fidelity_CubemapVar( pMaterial, "$envmaporigin" );
		IMaterialVar *pBox[ 3 ] = { Fidelity_CubemapVar( pMaterial, "$envmapparallaxobb1" ), Fidelity_CubemapVar( pMaterial, "$envmapparallaxobb2" ), Fidelity_CubemapVar( pMaterial, "$envmapparallaxobb3" ) };
		// shaders without the parameters, no reflection, or a box from Hammer
		if ( !pEnvmap || !pEnvmap->IsTexture() || !pParallax || !pOrigin || !pBox[ 0 ] || !pBox[ 1 ] || !pBox[ 2 ] || pParallax->GetIntValue() > 0 )
			continue;
		int x, y, z;
		if ( !Fidelity_CubemapPlace( pMaterial->GetName(), x, y, z ) )
			continue;
		const FidelityCubemapBox_t &box = BoxAt( x, y, z );
		if ( !box.m_bValid )
		{
			nOpen++;
			continue;
		}
		// world -> box: 0..1 inside, axis by axis
		const Vector vecSize = box.m_vecMaxs - box.m_vecMins;
		pBox[ 0 ]->SetVecValue( 1.0f / vecSize.x, 0.0f, 0.0f, -box.m_vecMins.x / vecSize.x );
		pBox[ 1 ]->SetVecValue( 0.0f, 1.0f / vecSize.y, 0.0f, -box.m_vecMins.y / vecSize.y );
		pBox[ 2 ]->SetVecValue( 0.0f, 0.0f, 1.0f / vecSize.z, -box.m_vecMins.z / vecSize.z );
		pOrigin->SetVecValue( (float)x, (float)y, (float)z );
		pParallax->SetIntValue( 1 );
		pMaterial->RecomputeStateSnapshots();
		m_Changed.AddToTail( pMaterial );
		nRooms++;
	}
	DevMsg( "[fidelity] reflections fit the room: %d materials corrected, %d left (cubemaps under the open sky or in huge spaces)\n", nRooms, nOpen );
}

void CFidelityCubemaps::Restore()
{
	for ( int i = 0; i < m_Changed.Count(); i++ )
	{
		IMaterialVar *pParallax = Fidelity_CubemapVar( m_Changed[ i ], "$envmapparallax" );
		if ( pParallax )
			pParallax->SetIntValue( 0 );
		m_Changed[ i ]->RecomputeStateSnapshots();
	}
	m_Changed.RemoveAll();
}
