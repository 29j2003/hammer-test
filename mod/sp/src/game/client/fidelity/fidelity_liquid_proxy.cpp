//========= mapbase-fidelity ==================================================//
//
// Purpose: "FidelityLiquid" material proxy for the Fidelity_Liquid shader
//          (bottles, jars, health vials and kits with liquid inside).
//
// Everything a mapper would otherwise have to set per prop is worked out
// here, so a material only needs   "Proxies" { "FidelityLiquid" { } }:
//
//   - amount: every prop holds its own amount of liquid, a share of the
//     container's volume between $fillmin and $fillmax (or $fill), stable for
//     that prop. The surface sits where exactly that volume is below it - in
//     every position: lying on its side a bottle holds as much as standing
//     (the first version took a fixed height along the model, so a lying
//     bottle was always half full). The volume comes from the model's shape
//     (Fidelity_AnalyseShape: slices along its long axis, the radius of each
//     from the vertices, a few hundred sample points with their volume).
//   - which parts hold liquid: all of the material, or with $liquidkey only
//     the parts of that colour (the green tube of a health vial or medkit);
//     the shape comes from those parts only
//   - slosh: moving, carrying, turning or knocking over a bottle tilts the
//     surface against the push (as much as the real one would: tilt =
//     acceleration / gravity) and lets small waves run over it; a damped
//     spring per prop brings it back to rest in about two seconds ($wobble
//     scales it, $viscosity makes it slower and heavier)
//   - colour: a prop's Render Color (Hammer) other than white replaces
//     $liquidcolor, so one material serves wine, water and oil
//   - splash ($splash 1): when a prop that was just drawn disappears (it
//     broke), its liquid splashes out in its colour, as much as it held
//
// Works for prop_static too (the proxy gets the renderable; static props
// have no entity, so they keep the material's colour). The result is written
// into the material's variables right before the prop is drawn.
//
//=============================================================================//

#include "cbase.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imesh.h"
#include "materialsystem/itexture.h"
#include "materialsystem/IShader.h"
#include "datacache/imdlcache.h"
#include "studio.h"
#include "model_types.h"
#include "utlmap.h"
#include "utlbuffer.h"
#include "fmtstr.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "fx_sparks.h"
#include "particle_util.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "fidelity_texture.h"
#include "fidelity_model.h"
#include "fidelity_liquid.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Per-prop sway state (a damped spring for the tilt of the surface).
//-----------------------------------------------------------------------------
struct FidelityLiquidState_t
{
	Vector m_vecLastPos;
	Vector m_vecLastVelocity;
	Vector2D m_vecTilt;			// surface tilt (x, y) in world space
	Vector2D m_vecTiltSpeed;
	float m_flWave;				// height of the waves, 0..1 of the most
	float m_flFizz;				// more and faster bubbles, foam: 0..1, only when really shaken
	float m_flBubblePhase;		// how far the bubbles have risen (seconds at their own speed)
	float m_flRandom;			// 0..1, fixed when the prop is first drawn (its own amount)
	int m_nLastFrame;
	int m_nLevel;				// s_nLiquidLevel when it was made (a renderable of an earlier map starts over)
	int m_nSpillFrame;			// frame the spilling was last worked out
	float m_flDropDebt;			// drops of the pouring stream still to come (fractions add up)
	// Last fill height and what it was found for: a resting prop (and every
	// static prop) needs no new search per bind and view
	Vector m_vecHeightNormal;
	float m_flHeightFill;
	float m_flHeight;
};

// Counts map loads, so states of renderables from an earlier map are not reused
static int s_nLiquidLevel = 0;

// Materials with this proxy, for r_fidelity_liquid_info.
static CUtlVector< IMaterial * > s_LiquidMaterials;

//-----------------------------------------------------------------------------
// Key colour: does a colour have the hue of the key and enough colour? Both
// linear, like on the GPU (it reads the texture as sRGB and gets the key
// linearised): same rule as KeyMatch in fidelity_liquid_ps30.fxc.
//-----------------------------------------------------------------------------
static float Fidelity_KeyMatch( const Vector &vecColor, const Vector &vecKey )
{
	const float flMin = MIN( vecColor.x, MIN( vecColor.y, vecColor.z ) );
	const float flMax = MAX( vecColor.x, MAX( vecColor.y, vecColor.z ) );
	const Vector c = vecColor - Vector( flMin, flMin, flMin );
	const float flKeyMin = MIN( vecKey.x, MIN( vecKey.y, vecKey.z ) );
	const Vector k = vecKey - Vector( flKeyMin, flKeyMin, flKeyMin );
	const float flHue = DotProduct( c, k ) / MAX( c.Length() * k.Length(), 0.0001f );
	const float flSaturation = ( flMax - flMin ) / MAX( flMax, 0.001f );
	return SimpleSplineRemapValClamped( flHue, 0.7f, 0.85f, 0.0f, 1.0f ) * SimpleSplineRemapValClamped( flSaturation, 0.15f, 0.3f, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// The space inside a container: sample points with the volume each stands
// for, in model space. Found once per model and material.
//-----------------------------------------------------------------------------
#define SHAPE_SLICES		16
#define SHAPE_GRID			5		// sample grid per slice (points inside the circle count)
#define SHAPE_PROFILE		8		// slices of the profile the shader gets (two of SHAPE_SLICES each)
#define SLOSH_DEADZONE		150.0f	// accelerations below this (units/s^2) do not slosh
#define SPILL_RATE			0.35f	// share of an open bottle's volume pouring out per second (neck deep under)
#define SPILL_DROPS			120.0f	// drops of the stream per whole volume poured

struct FidelityLiquidShape_t
{
	const model_t *m_pModel;
	IMaterial *m_pMaterial;
	CUtlVector< Vector > m_Points;
	CUtlVector< float > m_Weights;
	CUtlVector< float > m_Cells;	// grid cell size across the axis, per point
	int m_nAxis, m_nA, m_nB;	// long axis and the two across it (model space)
	float m_flSliceLength;
	Vector m_vecAxisStart;		// where the long axis starts, on its centre line (model space)
	Vector m_vecOpening;		// middle of the neck's end (model space), for pouring out
	float m_flOpening;			// radius of the neck there; 0: no narrow end (no neck to pour from)
	float m_flProfile[ SHAPE_PROFILE ];	// radius along the axis, for the surface in the shader (0: unknown)
	bool m_bPiece;				// a piece of a broken container (same material, no liquid)
	float m_flVolume;			// cubic units
	Vector m_vecCenter;			// middle of the space (model space)
	Vector m_vecColor;			// average colour of the liquid parts (gamma), for the splash
	float m_flWidth;			// width across the long axis
	int m_nVertices;			// vertices of the material
	int m_nUsed;				// of these, the ones that hold liquid
	bool m_bTextureRead;
};
static CUtlVector< FidelityLiquidShape_t * > s_LiquidShapes;

static Vector Fidelity_GammaToLinear( const Vector &vecColor )
{
	return Vector( SrgbGammaToLinear( vecColor.x ), SrgbGammaToLinear( vecColor.y ), SrgbGammaToLinear( vecColor.z ) );
}

//-----------------------------------------------------------------------------
// Where a material with a key colour holds liquid, as a texture the shader
// reads ($liquidmask), made once per material:
//   - from the model's triangles (its .dx90.vtx): a triangle holds liquid if
//     a good part of it lies on the key colour, it is part of the wall (not a
//     cap) and of a large piece (Fidelity_BuildTriangleMask); its whole
//     area in the texture is marked - the details painted into the tube count
//     (the health vial's dark swirls), while bars and frames that are
//     triangles of their own stay solid (the medkit's frame around its tube
//     became glass when enclosed texture regions were filled in);
//   - without a readable .vtx, from the texture alone: the key colour's texels
//     and small regions of other colours inside it.
// The shader used to blur the texture instead: on a texture atlas the parts
// lying next to the green one (the medkit's box) then became glass too.
//-----------------------------------------------------------------------------
#define LIQUID_MASK_SIZE		128		// texture read for the key colour (texels a side, at most)
#define LIQUID_MASK_RASTER		256		// the mask made from triangles
#define LIQUID_MASK_SHARE		0.35f	// a triangle holds liquid with this share of it on the key colour
#define LIQUID_MASK_CAP			0.7f	// ... but not if its face looks along the long axis more than this (caps)
#define LIQUID_MASK_PIECE		0.2f	// ... nor if its piece is smaller than this share of the largest
#define LIQUID_MASK_WALL		1.12f	// ... nor if a corner lies further out than this times the wall's radius (rings)
#define LIQUID_MASK_SMALL		0.015f	// regions of other colours up to this share of the texture are filled in
#define LIQUID_MASK_ENCLOSED	0.06f	// ... up to this share if they do not reach the texture's edge

class CFidelityLiquidMask : public ITextureRegenerator
{
public:
	CFidelityLiquidMask() : m_nWidth( 0 ), m_nHeight( 0 ), m_pTexture( NULL ) {}

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		const int nBytes = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		unsigned char *pData = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pData || nBytes < 1 || pVTFTexture->Width() != m_nWidth || pVTFTexture->Height() != m_nHeight )
			return;
		for ( int i = 0; i < m_nWidth * m_nHeight; i++ )
		{
			unsigned char *pPixel = pData + i * nBytes;
			for ( int c = 0; c < nBytes; c++ )
				pPixel[ c ] = ( c == 3 ) ? 255 : m_Mask[ i ];
		}
	}
	virtual void Release() {}

	// Holds liquid at texture coordinates u, v (wrapping, nearest)?
	bool At( float u, float v ) const
	{
		const int x = clamp( (int)floorf( ( u - floorf( u ) ) * m_nWidth ), 0, m_nWidth - 1 );
		const int y = clamp( (int)floorf( ( v - floorf( v ) ) * m_nHeight ), 0, m_nHeight - 1 );
		return m_Mask[ y * m_nWidth + x ] != 0;
	}
	bool IsValid() const { return m_nWidth > 0 && m_pTexture; }

	CUtlString m_Material;
	int m_nWidth;
	int m_nHeight;
	CUtlVector< unsigned char > m_Mask;	// 0 or 255 per texel, row by row
	ITexture *m_pTexture;
};
static CUtlVector< CFidelityLiquidMask * > s_LiquidMasks;

// Fills the key colour's part of the mask: key texels, then regions of other
// colours that are small (painted details) or enclosed by it
static void Fidelity_BuildMask( CFidelityLiquidMask &mask, const CFidelityImage &image, const Vector &vecKey )
{
	const int w = image.Width(), h = image.Height(), n = w * h;
	mask.m_nWidth = w;
	mask.m_nHeight = h;
	mask.m_Mask.SetCount( n );
	const Vector vecKeyLinear = Fidelity_GammaToLinear( vecKey );
	for ( int i = 0; i < n; i++ )
	{
		const unsigned char *pTexel = image.m_Pixels.Base() + i * 4;
		const Vector vecColor( pTexel[ 0 ] / 255.0f, pTexel[ 1 ] / 255.0f, pTexel[ 2 ] / 255.0f );
		mask.m_Mask[ i ] = Fidelity_KeyMatch( Fidelity_GammaToLinear( vecColor ), vecKeyLinear ) > 0.5f ? 255 : 0;
	}

	// Regions of other colours (4-connected), each filled in if it is small or enclosed
	CUtlVector< int > region, stack;
	CUtlVector< unsigned char > seen;
	seen.SetCount( n );
	V_memset( seen.Base(), 0, n );
	for ( int nStart = 0; nStart < n; nStart++ )
	{
		if ( mask.m_Mask[ nStart ] || seen[ nStart ] )
			continue;
		region.RemoveAll();
		stack.RemoveAll();
		stack.AddToTail( nStart );
		seen[ nStart ] = 1;
		bool bEdge = false;
		while ( stack.Count() )
		{
			const int i = stack.Tail();
			stack.RemoveMultipleFromTail( 1 );
			region.AddToTail( i );
			const int x = i % w, y = i / w;
			bEdge |= ( x == 0 || y == 0 || x == w - 1 || y == h - 1 );
			const int nNeighbours[ 4 ] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
			for ( int k = 0; k < 4; k++ )
			{
				const int j = nNeighbours[ k ];
				if ( j >= 0 && !seen[ j ] && !mask.m_Mask[ j ] )
				{
					seen[ j ] = 1;
					stack.AddToTail( j );
				}
			}
		}
		const float flShare = (float)region.Count() / n;
		if ( flShare <= LIQUID_MASK_SMALL || ( !bEdge && flShare <= LIQUID_MASK_ENCLOSED ) )
		{
			for ( int k = 0; k < region.Count(); k++ )
				mask.m_Mask[ region[ k ] ] = 255;
		}
	}
}


// The texels (index into a w x h grid, wrapping) whose centres lie in a
// triangle given in texture coordinates, grown by flGrow texels along each
// edge's normal
static void Fidelity_TriangleTexels( const Vector2D *pUV, int w, int h, float flGrow, CUtlVector< int > &texels )
{
	texels.RemoveAll();
	// Into the texture's 0..1 by the triangle's middle (wrapping UVs)
	const Vector2D vecShift( floorf( ( pUV[ 0 ].x + pUV[ 1 ].x + pUV[ 2 ].x ) / 3.0f ), floorf( ( pUV[ 0 ].y + pUV[ 1 ].y + pUV[ 2 ].y ) / 3.0f ) );
	Vector2D p[ 3 ];
	for ( int i = 0; i < 3; i++ )
	{
		p[ i ].x = ( pUV[ i ].x - vecShift.x ) * w;
		p[ i ].y = ( pUV[ i ].y - vecShift.y ) * h;
	}
	const float flArea = ( p[ 1 ].x - p[ 0 ].x ) * ( p[ 2 ].y - p[ 0 ].y ) - ( p[ 2 ].x - p[ 0 ].x ) * ( p[ 1 ].y - p[ 0 ].y );
	if ( fabsf( flArea ) < 1e-6f )
		return;
	const int x0 = clamp( (int)floorf( MIN( p[ 0 ].x, MIN( p[ 1 ].x, p[ 2 ].x ) ) - 1.0f ), 0, w - 1 );
	const int x1 = clamp( (int)ceilf( MAX( p[ 0 ].x, MAX( p[ 1 ].x, p[ 2 ].x ) ) + 1.0f ), 0, w - 1 );
	const int y0 = clamp( (int)floorf( MIN( p[ 0 ].y, MIN( p[ 1 ].y, p[ 2 ].y ) ) - 1.0f ), 0, h - 1 );
	const int y1 = clamp( (int)ceilf( MAX( p[ 0 ].y, MAX( p[ 1 ].y, p[ 2 ].y ) ) + 1.0f ), 0, h - 1 );
	const float flSign = flArea > 0.0f ? 1.0f : -1.0f;
	for ( int y = y0; y <= y1; y++ )
	{
		for ( int x = x0; x <= x1; x++ )
		{
			const Vector2D c( x + 0.5f, y + 0.5f );
			bool bInside = true;
			for ( int e = 0; e < 3 && bInside; e++ )
			{
				const Vector2D &a = p[ e ], &b = p[ ( e + 1 ) % 3 ];
				const Vector2D vecEdge = b - a;
				const float flLength = MAX( vecEdge.Length(), 1e-6f );
				// Signed distance of the texel centre to the edge (inside > 0)
				const float flDist = flSign * ( vecEdge.x * ( c.y - a.y ) - vecEdge.y * ( c.x - a.x ) ) / flLength;
				bInside = flDist >= -flGrow;
			}
			if ( bInside )
				texels.AddToTail( y * w + x );
		}
	}
}

// Share of a triangle on the key colour (keyed: 0/1 per texel of the image):
// the texels it covers and 7 points on it (corners, middles of the edges,
// middle), so thin triangles that cover no texel centre count too
static float Fidelity_TriangleKeyShare( const Vector2D *pUV, const CUtlVector< unsigned char > &keyed, int w, int h, CUtlVector< int > &texels )
{
	Fidelity_TriangleTexels( pUV, w, h, 0.0f, texels );
	int nKeyed = 0;
	for ( int i = 0; i < texels.Count(); i++ )
		nKeyed += keyed[ texels[ i ] ];
	const Vector2D points[ 7 ] = { pUV[ 0 ], pUV[ 1 ], pUV[ 2 ], ( pUV[ 0 ] + pUV[ 1 ] ) * 0.5f, ( pUV[ 1 ] + pUV[ 2 ] ) * 0.5f,
		( pUV[ 2 ] + pUV[ 0 ] ) * 0.5f, ( pUV[ 0 ] + pUV[ 1 ] + pUV[ 2 ] ) / 3.0f };
	for ( int k = 0; k < 7; k++ )
	{
		const int x = clamp( (int)floorf( ( points[ k ].x - floorf( points[ k ].x ) ) * w ), 0, w - 1 );
		const int y = clamp( (int)floorf( ( points[ k ].y - floorf( points[ k ].y ) ) * h ), 0, h - 1 );
		nKeyed += keyed[ y * w + x ];
	}
	return (float)nKeyed / ( texels.Count() + 7 );
}

// Ascending order of floats, for CUtlVector::Sort
static int __cdecl Fidelity_SortFloat( const float *a, const float *b )
{
	return ( *a < *b ) ? -1 : ( ( *a > *b ) ? 1 : 0 );
}

// Union-find over triangles: the root of t (paths halved on the way)
static int Fidelity_Root( CUtlVector< int > &parent, int t )
{
	while ( parent[ t ] != t )
	{
		parent[ t ] = parent[ parent[ t ] ];
		t = parent[ t ];
	}
	return t;
}

//-----------------------------------------------------------------------------
// The mask from the triangles. A triangle holds liquid if
//   - at least a third of it lies on the key colour (the health vial's tube
//     is painted with dark swirls: with a test of only its corners and middle
//     it came out solid),
//   - it is part of the wall around the long axis, not a cap or bottom (the
//     vial's caps have green segments: one looked through them),
//   - it belongs to a large connected piece of such triangles (small green
//     bits at the sides of the medkit showed the effect),
//   - it lies on the wall, not further out from the axis (the rings at the
//     ends of the medkit's tube, painted a little green, became glass at
//     the top).
// Liquid ones first, grown 1.5 texels (no seams between its UV islands),
// then the solid ones over them, grown half a texel (no liquid slivers on
// their edges). False without the model's triangles.
//-----------------------------------------------------------------------------
static bool Fidelity_BuildTriangleMask( CFidelityLiquidMask &mask, const CFidelityImage &image, const Vector &vecKey,
	const model_t *pModel, IMaterial *pMaterial )
{
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
		return false;
	const MDLHandle_t hModel = modelinfo->GetCacheHandle( pModel );
	studiohwdata_t *pHardware = mdlcache->GetHardwareData( hModel );
	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( pModel );
	if ( !pHardware || pHardware->m_NumLODs <= 0 || !pStudioHdr )
		return false;
	CUtlVector< const mstudiovertex_t * > vertices;
	CUtlVector< int > triangles;
	if ( !Fidelity_ModelVertices( mdlcache->GetVertexData( hModel ), pHardware->m_RootLOD, vertices ) ||
		!Fidelity_ModelTriangles( pModel, pStudioHdr, pHardware, pMaterial, triangles ) )
		return false;
	const int nTriangles = triangles.Count() / 3;
	for ( int i = 0; i < nTriangles * 3; i++ )
	{
		if ( triangles[ i ] < 0 || triangles[ i ] >= vertices.Count() )
			return false;
	}

	// Which texels of the image have the key colour
	const int w = image.Width(), h = image.Height();
	const Vector vecKeyLinear = Fidelity_GammaToLinear( vecKey );
	CUtlVector< unsigned char > keyed;
	keyed.SetCount( w * h );
	for ( int i = 0; i < w * h; i++ )
	{
		const unsigned char *pTexel = image.m_Pixels.Base() + i * 4;
		const Vector vecColor( pTexel[ 0 ] / 255.0f, pTexel[ 1 ] / 255.0f, pTexel[ 2 ] / 255.0f );
		keyed[ i ] = Fidelity_KeyMatch( Fidelity_GammaToLinear( vecColor ), vecKeyLinear ) > 0.5f ? 1 : 0;
	}

	// 1. On the key colour
	CUtlVector< int > texels;
	CUtlVector< bool > liquid;
	liquid.SetCount( nTriangles );
	Vector vecMins( FLT_MAX, FLT_MAX, FLT_MAX ), vecMaxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for ( int t = 0; t < nTriangles; t++ )
	{
		Vector2D uv[ 3 ];
		for ( int c = 0; c < 3; c++ )
			uv[ c ] = vertices[ triangles[ t * 3 + c ] ]->m_vecTexCoord;
		liquid[ t ] = Fidelity_TriangleKeyShare( uv, keyed, w, h, texels ) >= LIQUID_MASK_SHARE;
		for ( int c = 0; liquid[ t ] && c < 3; c++ )
		{
			VectorMin( vecMins, vertices[ triangles[ t * 3 + c ] ]->m_vecPosition, vecMins );
			VectorMax( vecMaxs, vertices[ triangles[ t * 3 + c ] ]->m_vecPosition, vecMaxs );
		}
	}

	// 2. Not a cap: the face must not look along the long axis (only for a
	// container clearly longer than wide - a round jar has no long axis)
	if ( vecMins.x <= vecMaxs.x )
	{
		const Vector vecSize = vecMaxs - vecMins;
		const int nAxis = ( vecSize.x >= vecSize.y && vecSize.x >= vecSize.z ) ? 0 : ( vecSize.y >= vecSize.z ? 1 : 2 );
		const float flAcross = MAX( vecSize[ ( nAxis + 1 ) % 3 ], vecSize[ ( nAxis + 2 ) % 3 ] );
		if ( vecSize[ nAxis ] > flAcross * 1.2f )
		{
			for ( int t = 0; t < nTriangles; t++ )
			{
				if ( !liquid[ t ] )
					continue;
				const Vector &a = vertices[ triangles[ t * 3 ] ]->m_vecPosition;
				Vector vecNormal = CrossProduct( vertices[ triangles[ t * 3 + 1 ] ]->m_vecPosition - a, vertices[ triangles[ t * 3 + 2 ] ]->m_vecPosition - a );
				if ( VectorNormalize( vecNormal ) > 1e-6f && fabsf( vecNormal[ nAxis ] ) > LIQUID_MASK_CAP )
					liquid[ t ] = false;
			}
		}
	}

	// 3. Large connected pieces only: triangles sharing a corner position
	// (UV seams split vertices, positions stay) are one piece
	CUtlVector< int > parent;
	CUtlVector< float > area;
	parent.SetCount( nTriangles );
	area.SetCount( nTriangles );
	CUtlMap< uint64, int, int > corners( DefLessFunc( uint64 ) );
	for ( int t = 0; t < nTriangles; t++ )
	{
		parent[ t ] = t;
		area[ t ] = 0.0f;
		if ( !liquid[ t ] )
			continue;
		for ( int c = 0; c < 3; c++ )
		{
			const Vector &p = vertices[ triangles[ t * 3 + c ] ]->m_vecPosition;
			// position to 1/32 unit, 21 bits per axis
			const uint64 nKey = ( (uint64)( (int)( p.x * 32.0f ) & 0x1FFFFF ) << 42 ) | ( (uint64)( (int)( p.y * 32.0f ) & 0x1FFFFF ) << 21 ) |
				(uint64)( (int)( p.z * 32.0f ) & 0x1FFFFF );
			const int nFound = corners.Find( nKey );
			if ( nFound == corners.InvalidIndex() )
				corners.Insert( nKey, t );
			else
				parent[ Fidelity_Root( parent, t ) ] = Fidelity_Root( parent, corners[ nFound ] );
		}
	}
	float flLargest = 0.0f;
	for ( int t = 0; t < nTriangles; t++ )
	{
		if ( !liquid[ t ] )
			continue;
		const Vector &a = vertices[ triangles[ t * 3 ] ]->m_vecPosition;
		const float flArea = 0.5f * CrossProduct( vertices[ triangles[ t * 3 + 1 ] ]->m_vecPosition - a, vertices[ triangles[ t * 3 + 2 ] ]->m_vecPosition - a ).Length();
		const int nRoot = Fidelity_Root( parent, t );
		area[ nRoot ] += flArea;
		flLargest = MAX( flLargest, area[ nRoot ] );
	}
	for ( int t = 0; t < nTriangles; t++ )
	{
		if ( liquid[ t ] && area[ Fidelity_Root( parent, t ) ] < flLargest * LIQUID_MASK_PIECE )
			liquid[ t ] = false;
	}

	// 4. On the wall: no corner further from the axis than the wall's typical
	// radius (the median of all corners) allows - only for long containers
	vecMins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	vecMaxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for ( int t = 0; t < nTriangles; t++ )
	{
		for ( int c = 0; liquid[ t ] && c < 3; c++ )
		{
			VectorMin( vecMins, vertices[ triangles[ t * 3 + c ] ]->m_vecPosition, vecMins );
			VectorMax( vecMaxs, vertices[ triangles[ t * 3 + c ] ]->m_vecPosition, vecMaxs );
		}
	}
	if ( vecMins.x <= vecMaxs.x )
	{
		const Vector vecSize = vecMaxs - vecMins;
		const int nAxis = ( vecSize.x >= vecSize.y && vecSize.x >= vecSize.z ) ? 0 : ( vecSize.y >= vecSize.z ? 1 : 2 );
		const int nA = ( nAxis + 1 ) % 3, nB = ( nAxis + 2 ) % 3;
		if ( vecSize[ nAxis ] > MAX( vecSize[ nA ], vecSize[ nB ] ) * 1.2f )
		{
			const float flCentreA = ( vecMins[ nA ] + vecMaxs[ nA ] ) * 0.5f, flCentreB = ( vecMins[ nB ] + vecMaxs[ nB ] ) * 0.5f;
			CUtlVector< float > radii;
			for ( int t = 0; t < nTriangles; t++ )
			{
				for ( int c = 0; liquid[ t ] && c < 3; c++ )
				{
					const Vector &p = vertices[ triangles[ t * 3 + c ] ]->m_vecPosition;
					radii.AddToTail( sqrtf( Square( p[ nA ] - flCentreA ) + Square( p[ nB ] - flCentreB ) ) );
				}
			}
			radii.Sort( Fidelity_SortFloat );
			const float flWall = radii.Count() ? radii[ radii.Count() / 2 ] : 0.0f;
			for ( int t = 0; flWall > 0.0f && t < nTriangles; t++ )
			{
				for ( int c = 0; liquid[ t ] && c < 3; c++ )
				{
					const Vector &p = vertices[ triangles[ t * 3 + c ] ]->m_vecPosition;
					if ( sqrtf( Square( p[ nA ] - flCentreA ) + Square( p[ nB ] - flCentreB ) ) > flWall * LIQUID_MASK_WALL )
						liquid[ t ] = false;
				}
			}
		}
	}

	// Into the mask
	mask.m_nWidth = mask.m_nHeight = LIQUID_MASK_RASTER;
	mask.m_Mask.SetCount( LIQUID_MASK_RASTER * LIQUID_MASK_RASTER );
	V_memset( mask.m_Mask.Base(), 0, mask.m_Mask.Count() );
	for ( int pass = 0; pass < 2; pass++ )
	{
		for ( int t = 0; t < nTriangles; t++ )
		{
			if ( liquid[ t ] != ( pass == 0 ) )
				continue;
			Vector2D uv[ 3 ];
			for ( int c = 0; c < 3; c++ )
				uv[ c ] = vertices[ triangles[ t * 3 + c ] ]->m_vecTexCoord;
			Fidelity_TriangleTexels( uv, mask.m_nWidth, mask.m_nHeight, liquid[ t ] ? 1.5f : 0.5f, texels );
			for ( int i = 0; i < texels.Count(); i++ )
				mask.m_Mask[ texels[ i ] ] = liquid[ t ] ? 255 : 0;
		}
	}
	return true;
}

// The mask of a material with a key colour (made on first use, from pModel's
// triangles if it can), or NULL if its texture can not be read (the shader
// then keys the texture itself)
static const CFidelityLiquidMask *Fidelity_LiquidMask( IMaterial *pMaterial, const Vector &vecKey, const model_t *pModel )
{
	for ( int i = 0; i < s_LiquidMasks.Count(); i++ )
	{
		if ( !Q_stricmp( s_LiquidMasks[ i ]->m_Material.Get(), pMaterial->GetName() ) )
			return s_LiquidMasks[ i ]->IsValid() ? s_LiquidMasks[ i ] : NULL;
	}
	CFidelityLiquidMask *pMask = new CFidelityLiquidMask;
	pMask->m_Material = pMaterial->GetName();
	s_LiquidMasks.AddToTail( pMask );

	bool bFound = false;
	IMaterialVar *pBase = pMaterial->FindVar( "$basetexture", &bFound, false );
	const CFidelityImage *pImage = ( bFound && pBase->IsTexture() && pBase->GetTextureValue() ) ? Fidelity_TextureImage( pBase->GetTextureValue()->GetName(), LIQUID_MASK_SIZE ) : NULL;
	if ( !pImage )
		return NULL;
	if ( !Fidelity_BuildTriangleMask( *pMask, *pImage, vecKey, pModel, pMaterial ) )
		Fidelity_BuildMask( *pMask, *pImage, vecKey );
	pMask->m_pTexture = materials->CreateProceduralTexture( CFmtStr( "fidelity/liquidmask%d", s_LiquidMasks.Count() ), TEXTURE_GROUP_OTHER,
		pMask->m_nWidth, pMask->m_nHeight, IMAGE_FORMAT_RGBA8888, TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_POINTSAMPLE );	// no blending at the edges: no seams
	if ( pMask->m_pTexture && pMask->m_pTexture->IsError() )
		pMask->m_pTexture = NULL;
	if ( !pMask->m_pTexture )
		return NULL;
	pMask->m_pTexture->SetTextureRegenerator( pMask );
	pMask->m_pTexture->Download();
	return pMask;
}

// The masks' textures point at regenerators in this DLL: let go before it is unloaded
static void Fidelity_ForgetMasks()
{
	for ( int i = 0; i < s_LiquidMasks.Count(); i++ )
	{
		ITexture *pTexture = s_LiquidMasks[ i ]->m_pTexture;
		if ( pTexture )
		{
			pTexture->SetTextureRegenerator( NULL );
			pTexture->DecrementReferenceCount();
		}
	}
	s_LiquidMasks.PurgeAndDeleteElements();
}

static void Fidelity_AnalyseShape( FidelityLiquidShape_t &shape, const Vector &vecKey, bool bKey )
{
	shape.m_flVolume = 0.0f;
	shape.m_vecCenter.Init();
	shape.m_vecColor.Init( 0.5f, 0.5f, 0.5f );
	shape.m_flWidth = 0.0f;
	shape.m_nVertices = shape.m_nUsed = 0;
	shape.m_bTextureRead = false;
	shape.m_nAxis = 2;
	shape.m_nA = 0;
	shape.m_nB = 1;
	shape.m_flSliceLength = 0.0f;
	shape.m_vecAxisStart.Init();
	shape.m_vecOpening.Init();
	shape.m_flOpening = 0.0f;
	for ( int s = 0; s < SHAPE_PROFILE; s++ )
		shape.m_flProfile[ s ] = 0.0f;
	if ( modelinfo->GetModelType( shape.m_pModel ) != mod_studio )
		return;	// a brush or sprite with the material: the render bounds stand in

	const MDLHandle_t hModel = modelinfo->GetCacheHandle( shape.m_pModel );
	studiohwdata_t *pHardware = mdlcache->GetHardwareData( hModel );
	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( shape.m_pModel );
	const vertexFileHeader_t *pVertexHdr = mdlcache->GetVertexData( hModel );
	if ( !pHardware || pHardware->m_NumLODs <= 0 || !pStudioHdr || !pVertexHdr )
		return;

	// The base texture, to see which vertices hold liquid and their colour
	bool bFound = false;
	IMaterialVar *pBaseVar = shape.m_pMaterial->FindVar( "$basetexture", &bFound, false );
	const CFidelityImage *pImage = ( bFound && pBaseVar->IsTexture() && pBaseVar->GetTextureValue() ) ? Fidelity_TextureImage( pBaseVar->GetTextureValue()->GetName() ) : NULL;
	shape.m_bTextureRead = pImage != NULL;

	// The vertices drawn with this material (any skin, every submodel of each
	// body part: the slices take the widest): all, and the ones of the key colour
	const studioloddata_t &lod = pHardware->m_pLODs[ pHardware->m_RootLOD ];
	const Vector vecKeyLinear = Fidelity_GammaToLinear( vecKey );
	const CFidelityLiquidMask *pMask = bKey ? Fidelity_LiquidMask( shape.m_pMaterial, vecKey, shape.m_pModel ) : NULL;	// as the shader decides
	CUtlVector< const mstudiovertex_t * > vertices;
	if ( !Fidelity_ModelVertices( pVertexHdr, pHardware->m_RootLOD, vertices ) )
		return;
	const int nVertices = vertices.Count();
	CUtlVector< Vector > all, keyed;
	Vector vecAllColor( 0, 0, 0 ), vecKeyColor( 0, 0, 0 );
	for ( int nPart = 0; nPart < pStudioHdr->numbodyparts; nPart++ )
	{
		mstudiobodyparts_t *pPart = pStudioHdr->pBodypart( nPart );
		for ( int nSub = 0; nSub < pPart->nummodels; nSub++ )
		{
			mstudiomodel_t *pSubModel = pPart->pModel( nSub );
			const int nBase = pSubModel->vertexindex / sizeof( mstudiovertex_t );
			for ( int nMesh = 0; nMesh < pSubModel->nummeshes; nMesh++ )
			{
				mstudiomesh_t *pMesh = pSubModel->pMesh( nMesh );
				if ( pMesh->material < 0 || pMesh->material >= pStudioHdr->numskinref )
					continue;
				if ( !Fidelity_SkinRefUses( pStudioHdr, lod, pMesh->material, shape.m_pMaterial ) )
					continue;
				for ( int i = 0; i < pMesh->numvertices; i++ )
				{
					const int nIndex = nBase + pMesh->vertexoffset + i;
					if ( nIndex < 0 || nIndex >= nVertices )
						break;
					const mstudiovertex_t &vertex = *vertices[ nIndex ];
					float rgba[ 4 ] = { 0.5f, 0.5f, 0.5f, 1.0f };
					if ( pImage )
						pImage->Sample( vertex.m_vecTexCoord.x, vertex.m_vecTexCoord.y, rgba );
					else
						shape.m_pMaterial->GetLowResColorSample( vertex.m_vecTexCoord.x, vertex.m_vecTexCoord.y, rgba );
					const Vector vecColor( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ] );
					all.AddToTail( vertex.m_vecPosition );
					vecAllColor += vecColor;
					const bool bKeyed = pMask ? pMask->At( vertex.m_vecTexCoord.x, vertex.m_vecTexCoord.y ) :
						( bKey && Fidelity_KeyMatch( Fidelity_GammaToLinear( vecColor ), vecKeyLinear ) > 0.5f );
					if ( bKeyed )
					{
						keyed.AddToTail( vertex.m_vecPosition );
						vecKeyColor += vecColor;
					}
				}
			}
		}
	}
	shape.m_nVertices = all.Count();
	const bool bUseKeyed = bKey && keyed.Count() >= 16;
	const CUtlVector< Vector > &points = bUseKeyed ? keyed : all;
	shape.m_nUsed = points.Count();
	if ( points.Count() < 16 )
		return;
	shape.m_vecColor = bUseKeyed ? vecKeyColor / keyed.Count() : vecAllColor / all.Count();

	// Long axis, centre line across it
	Vector vecMins = points[ 0 ], vecMaxs = points[ 0 ];
	for ( int i = 1; i < points.Count(); i++ )
	{
		VectorMin( vecMins, points[ i ], vecMins );
		VectorMax( vecMaxs, points[ i ], vecMaxs );
	}
	const Vector vecSize = vecMaxs - vecMins;
	const int nAxis = ( vecSize.x >= vecSize.y && vecSize.x >= vecSize.z ) ? 0 : ( vecSize.y >= vecSize.z ? 1 : 2 );
	const int nA = ( nAxis == 0 ) ? 1 : 0;
	const int nB = ( nAxis == 2 ) ? 1 : 2;
	const float flCentreA = ( vecMins[ nA ] + vecMaxs[ nA ] ) * 0.5f;
	const float flCentreB = ( vecMins[ nB ] + vecMaxs[ nB ] ) * 0.5f;
	const float flLength = MAX( vecSize[ nAxis ], 0.01f );
	shape.m_flWidth = MIN( vecSize[ nA ], vecSize[ nB ] );
	shape.m_nAxis = nAxis;
	shape.m_nA = nA;
	shape.m_nB = nB;

	// Radius of each slice: the farthest vertex in it (empty slices take the neighbour's)
	float flRadius[ SHAPE_SLICES ];
	for ( int s = 0; s < SHAPE_SLICES; s++ )
		flRadius[ s ] = -1.0f;
	for ( int i = 0; i < points.Count(); i++ )
	{
		const int s = clamp( (int)( ( points[ i ][ nAxis ] - vecMins[ nAxis ] ) / flLength * SHAPE_SLICES ), 0, SHAPE_SLICES - 1 );
		const float r = sqrtf( Square( points[ i ][ nA ] - flCentreA ) + Square( points[ i ][ nB ] - flCentreB ) );
		flRadius[ s ] = MAX( flRadius[ s ], r );
	}
	for ( int s = 1; s < SHAPE_SLICES; s++ )
	{
		if ( flRadius[ s ] < 0.0f )
			flRadius[ s ] = flRadius[ s - 1 ];
	}
	for ( int s = SHAPE_SLICES - 2; s >= 0; s-- )
	{
		if ( flRadius[ s ] < 0.0f )
			flRadius[ s ] = flRadius[ s + 1 ];
	}

	// The profile for the shader: the wider of each two slices
	for ( int s = 0; s < SHAPE_PROFILE; s++ )
	{
		const int nFirst = s * SHAPE_SLICES / SHAPE_PROFILE;
		shape.m_flProfile[ s ] = MAX( MAX( flRadius[ nFirst ], flRadius[ nFirst + 1 ] ), 0.0f );
	}
	shape.m_vecAxisStart[ nAxis ] = vecMins[ nAxis ];
	shape.m_vecAxisStart[ nA ] = flCentreA;
	shape.m_vecAxisStart[ nB ] = flCentreB;
	// The neck: the end clearly narrower than the other (bottles)
	const float flFirst = shape.m_flProfile[ 0 ], flLast = shape.m_flProfile[ SHAPE_PROFILE - 1 ];
	if ( flFirst < flLast * 0.7f || flLast < flFirst * 0.7f )
	{
		shape.m_vecOpening = shape.m_vecAxisStart;
		if ( flLast < flFirst )
			shape.m_vecOpening[ nAxis ] = vecMaxs[ nAxis ];
		shape.m_flOpening = MAX( MIN( flFirst, flLast ), 0.25f );
	}

	// Sample points: a grid in each slice, the ones inside its circle
	const float flSliceLength = flLength / SHAPE_SLICES;
	shape.m_flSliceLength = flSliceLength;
	Vector vecCenterSum( 0, 0, 0 );
	for ( int s = 0; s < SHAPE_SLICES; s++ )
	{
		const float r = MAX( flRadius[ s ], 0.0f );
		if ( r <= 0.001f )
			continue;
		const float flCell = 2.0f * r / SHAPE_GRID;
		const float flWeight = flCell * flCell * flSliceLength;
		for ( int u = 0; u < SHAPE_GRID; u++ )
		{
			for ( int v = 0; v < SHAPE_GRID; v++ )
			{
				const float a = -r + ( u + 0.5f ) * flCell;
				const float b = -r + ( v + 0.5f ) * flCell;
				if ( a * a + b * b > r * r )
					continue;
				Vector p;
				p[ nAxis ] = vecMins[ nAxis ] + ( s + 0.5f ) * flSliceLength;
				p[ nA ] = flCentreA + a;
				p[ nB ] = flCentreB + b;
				shape.m_Points.AddToTail( p );
				shape.m_Weights.AddToTail( flWeight );
				shape.m_Cells.AddToTail( flCell );
				shape.m_flVolume += flWeight;
				vecCenterSum += p * flWeight;
			}
		}
	}
	if ( shape.m_flVolume > 0.0f )
		shape.m_vecCenter = vecCenterSum / shape.m_flVolume;
}

static const FidelityLiquidShape_t &Fidelity_LiquidShape( const model_t *pModel, IMaterial *pMaterial, const Vector &vecKey, bool bKey )
{
	for ( int i = 0; i < s_LiquidShapes.Count(); i++ )
	{
		if ( s_LiquidShapes[ i ]->m_pModel == pModel && s_LiquidShapes[ i ]->m_pMaterial == pMaterial )
			return *s_LiquidShapes[ i ];
	}
	FidelityLiquidShape_t *pShape = new FidelityLiquidShape_t;
	pShape->m_pModel = pModel;
	pShape->m_pMaterial = pMaterial;
	// Pieces of a broken bottle share its material, but hold no liquid (seen in
	// game: a chunk came out as a solid black block).
	const char *pszModel = modelinfo->GetModelName( pModel );
	pShape->m_bPiece = Q_stristr( pszModel, "chunk" ) || Q_stristr( pszModel, "gib" ) || Q_stristr( pszModel, "break" );
	Fidelity_AnalyseShape( *pShape, vecKey, bKey );
	s_LiquidShapes.AddToTail( pShape );
	return *pShape;
}

static void Fidelity_ForgetShapes()
{
	s_LiquidShapes.PurgeAndDeleteElements();
}

//-----------------------------------------------------------------------------
// Height (along vecNormal, model space) below which the share flFill of the
// shape's volume lies. Each sample point stands for a small box (a grid cell
// across, a slice along the axis) that fills up gradually as the surface
// passes it: the level moves smoothly with fill and tilt instead of jumping
// from one row of points to the next.
//-----------------------------------------------------------------------------
static float Fidelity_FillHeight( const FidelityLiquidShape_t &shape, const Vector &vecNormal, float flFill )
{
	const int nPoints = MIN( shape.m_Points.Count(), SHAPE_SLICES * SHAPE_GRID * SHAPE_GRID );
	if ( nPoints <= 0 )
		return 0.0f;
	// Half the extent of a point's box along the normal
	const float flAlong = 0.5f * fabsf( vecNormal[ shape.m_nAxis ] ) * shape.m_flSliceLength;
	const float flAcross = 0.5f * ( fabsf( vecNormal[ shape.m_nA ] ) + fabsf( vecNormal[ shape.m_nB ] ) );
	float flHeights[ SHAPE_SLICES * SHAPE_GRID * SHAPE_GRID ];
	float flHalves[ SHAPE_SLICES * SHAPE_GRID * SHAPE_GRID ];
	float flLow = FLT_MAX, flHigh = -FLT_MAX;
	for ( int i = 0; i < nPoints; i++ )
	{
		flHeights[ i ] = DotProduct( shape.m_Points[ i ], vecNormal );
		flHalves[ i ] = MAX( flAlong + flAcross * shape.m_Cells[ i ], 0.001f );
		flLow = MIN( flLow, flHeights[ i ] - flHalves[ i ] );
		flHigh = MAX( flHigh, flHeights[ i ] + flHalves[ i ] );
	}
	const float flTarget = clamp( flFill, 0.0f, 1.0f ) * shape.m_flVolume;
	for ( int nStep = 0; nStep < 14; nStep++ )
	{
		const float flMid = ( flLow + flHigh ) * 0.5f;
		float flBelow = 0.0f;
		for ( int i = 0; i < nPoints; i++ )
			flBelow += shape.m_Weights[ i ] * clamp( ( flMid - flHeights[ i ] + flHalves[ i ] ) / ( 2.0f * flHalves[ i ] ), 0.0f, 1.0f );
		if ( flBelow < flTarget )
			flLow = flMid;
		else
			flHigh = flMid;
	}
	return ( flLow + flHigh ) * 0.5f;
}

//-----------------------------------------------------------------------------
// Models with a liquid material keep their normal lighting at rest (see
// fidelity_liquid.h). Looked up once per model.
//-----------------------------------------------------------------------------
static CUtlMap< const model_t *, bool > s_LiquidModelLight( DefLessFunc( const model_t * ) );

bool Fidelity_KeepsDynamicLight( const model_t *pModel )
{
	if ( !pModel )
		return false;
	unsigned short index = s_LiquidModelLight.Find( pModel );
	if ( index != s_LiquidModelLight.InvalidIndex() )
		return s_LiquidModelLight[ index ];

	bool bLiquid = false;
	studiohwdata_t *pHardware = ( modelinfo->GetModelType( pModel ) == mod_studio ) ? mdlcache->GetHardwareData( modelinfo->GetCacheHandle( pModel ) ) : NULL;
	if ( pHardware && pHardware->m_NumLODs > 0 )
	{
		const studioloddata_t &lod = pHardware->m_pLODs[ pHardware->m_RootLOD ];
		for ( int i = 0; i < lod.numMaterials && !bLiquid; i++ )
			bLiquid = lod.ppMaterials[ i ] && !Q_stricmp( lod.ppMaterials[ i ]->GetShaderName(), "Fidelity_Liquid" );
	}
	s_LiquidModelLight.Insert( pModel, bLiquid );
	return bLiquid;
}

//-----------------------------------------------------------------------------
// Props whose liquid splashes when they break: the last time they were drawn,
// where and how much. A prop that was on screen a moment ago and is gone now
// broke (entities that leave the view stay, they only go dormant).
//-----------------------------------------------------------------------------
struct FidelitySplashProp_t
{
	EHANDLE m_hEntity;
	Vector m_vecPos;
	Vector m_vecColor;		// gamma 0..1
	float m_flAmount;		// cubic units of liquid
	float m_flLastSeen;
};
static CUtlVector< FidelitySplashProp_t > s_SplashProps;

static void Fidelity_NoteSplashProp( C_BaseEntity *pEntity, const Vector &vecPos, const Vector &vecColor, float flAmount )
{
	for ( int i = 0; i < s_SplashProps.Count(); i++ )
	{
		if ( s_SplashProps[ i ].m_hEntity.Get() == pEntity )
		{
			s_SplashProps[ i ].m_vecPos = vecPos;
			s_SplashProps[ i ].m_vecColor = vecColor;
			s_SplashProps[ i ].m_flAmount = flAmount;
			s_SplashProps[ i ].m_flLastSeen = gpGlobals->curtime;
			return;
		}
	}
	FidelitySplashProp_t &prop = s_SplashProps[ s_SplashProps.AddToTail() ];
	prop.m_hEntity = pEntity;
	prop.m_vecPos = vecPos;
	prop.m_vecColor = vecColor;
	prop.m_flAmount = flAmount;
	prop.m_flLastSeen = gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// Open bottles ($spill 1) poured out: how much of their liquid is gone, per
// entity (handle with serial: never mixed up with a later entity), for the
// whole map - looking away does not fill them up again.
//-----------------------------------------------------------------------------
static CUtlMap< unsigned int, float > s_Spilled( DefLessFunc( unsigned int ) );

static float Fidelity_Spilled( C_BaseEntity *pEntity )
{
	const unsigned short index = s_Spilled.Find( (unsigned int)pEntity->GetRefEHandle().ToInt() );
	return index != s_Spilled.InvalidIndex() ? s_Spilled[ index ] : 0.0f;
}

static void Fidelity_AddSpilled( C_BaseEntity *pEntity, float flAmount )
{
	const unsigned int nKey = (unsigned int)pEntity->GetRefEHandle().ToInt();
	unsigned short index = s_Spilled.Find( nKey );
	if ( index == s_Spilled.InvalidIndex() )
		index = s_Spilled.Insert( nKey, 0.0f );
	s_Spilled[ index ] = MIN( s_Spilled[ index ] + flAmount, 1.0f );
}

// Drops of a pouring stream leaving the neck at vecOrigin along vecOut
static void Fidelity_SpillDrops( const Vector &vecOrigin, const Vector &vecOut, const Vector &vecColor, int nDrops, float flRadius )
{
	const Vector vecLight = WorldGetLightForPoint( vecOrigin, true );
	const Vector vecTint( MIN( vecColor.x * vecLight.x * 1.5f, 1.0f ), MIN( vecColor.y * vecLight.y * 1.5f, 1.0f ), MIN( vecColor.z * vecLight.z * 1.5f, 1.0f ) );
	CSmartPtr< CTrailParticles > pDrops = CTrailParticles::Create( "fidelity_liquid_pour" );
	if ( !pDrops.IsValid() )
		return;
	pDrops->SetSortOrigin( vecOrigin );
	pDrops->m_ParticleCollision.SetGravity( 800.0f );
	pDrops->GetBinding().SetBBox( vecOrigin - Vector( 32, 32, 96 ), vecOrigin + Vector( 32, 32, 16 ) );
	PMaterialHandle hMaterial = ParticleMgr()->GetPMaterial( "effects/splash2" );
	for ( int i = 0; i < nDrops; i++ )
	{
		TrailParticle *pParticle = (TrailParticle *)pDrops->AddParticle( sizeof( TrailParticle ), hMaterial, vecOrigin + RandomVector( -1.0f, 1.0f ) * flRadius * 0.5f );
		if ( !pParticle )
			break;
		pParticle->m_flLifetime = 0.0f;
		pParticle->m_flDieTime = random->RandomFloat( 0.35f, 0.6f );
		pParticle->m_vecVelocity = vecOut * random->RandomFloat( 10.0f, 30.0f ) + RandomVector( -4.0f, 4.0f );
		pParticle->m_flWidth = random->RandomFloat( 0.4f, 0.9f ) * clamp( flRadius, 0.5f, 2.0f );
		pParticle->m_flLength = random->RandomFloat( 0.04f, 0.08f );
		pParticle->m_color.r = (unsigned char)( vecTint.x * 255 );
		pParticle->m_color.g = (unsigned char)( vecTint.y * 255 );
		pParticle->m_color.b = (unsigned char)( vecTint.z * 255 );
		pParticle->m_color.a = 220;
	}
}

//-----------------------------------------------------------------------------
// Liquid splashing out of a broken container: drops flying up and out that
// fall back down, and a short spray, in the liquid's colour (lit like the
// place), more for more liquid.
//-----------------------------------------------------------------------------
static void Fidelity_LiquidSplash( const Vector &vecOrigin, const Vector &vecColor, float flAmount )
{
	const float flScale = clamp( powf( MAX( flAmount, 0.0f ), 1.0f / 3.0f ) / 4.0f, 0.4f, 2.0f );
	const Vector vecLight = WorldGetLightForPoint( vecOrigin, true );
	Vector vecTint( vecColor.x * vecLight.x, vecColor.y * vecLight.y, vecColor.z * vecLight.z );
	vecTint.x = MIN( vecTint.x * 1.5f, 1.0f );
	vecTint.y = MIN( vecTint.y * 1.5f, 1.0f );
	vecTint.z = MIN( vecTint.z * 1.5f, 1.0f );
	PMaterialHandle hMaterial = ParticleMgr()->GetPMaterial( "effects/splash2" );

	CSmartPtr< CTrailParticles > pDrops = CTrailParticles::Create( "fidelity_liquid_drops" );
	if ( pDrops.IsValid() )
	{
		pDrops->SetSortOrigin( vecOrigin );
		pDrops->m_ParticleCollision.SetGravity( 800.0f );
		pDrops->SetFlag( bitsPARTICLE_TRAIL_VELOCITY_DAMPEN );
		pDrops->SetVelocityDampen( 1.0f );
		pDrops->GetBinding().SetBBox( vecOrigin - Vector( 64, 64, 64 ), vecOrigin + Vector( 64, 64, 64 ) );
		const int nDrops = (int)( 12 + 24 * flScale );
		for ( int i = 0; i < nDrops; i++ )
		{
			TrailParticle *pParticle = (TrailParticle *)pDrops->AddParticle( sizeof( TrailParticle ), hMaterial, vecOrigin + RandomVector( -3.0f, 3.0f ) * flScale );
			if ( !pParticle )
				break;
			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = random->RandomFloat( 0.4f, 0.9f );
			Vector vecDir = RandomVector( -1.0f, 1.0f );
			vecDir.z = fabsf( vecDir.z ) + 0.6f;
			VectorNormalize( vecDir );
			pParticle->m_vecVelocity = vecDir * random->RandomFloat( 60.0f, 180.0f ) * flScale;
			pParticle->m_flWidth = random->RandomFloat( 0.6f, 1.6f ) * flScale;
			pParticle->m_flLength = random->RandomFloat( 0.03f, 0.06f );
			const float flRamp = random->RandomFloat( 0.8f, 1.2f );
			pParticle->m_color.r = (unsigned char)( MIN( vecTint.x * flRamp, 1.0f ) * 255 );
			pParticle->m_color.g = (unsigned char)( MIN( vecTint.y * flRamp, 1.0f ) * 255 );
			pParticle->m_color.b = (unsigned char)( MIN( vecTint.z * flRamp, 1.0f ) * 255 );
			pParticle->m_color.a = 255;
		}
	}

	CSmartPtr< CSimpleEmitter > pSpray = CSimpleEmitter::Create( "fidelity_liquid_spray" );
	if ( pSpray.IsValid() )
	{
		pSpray->SetSortOrigin( vecOrigin );
		for ( int i = 0; i < 6; i++ )
		{
			SimpleParticle *pParticle = (SimpleParticle *)pSpray->AddParticle( sizeof( SimpleParticle ), hMaterial, vecOrigin );
			if ( !pParticle )
				break;
			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = random->RandomFloat( 0.3f, 0.6f );
			pParticle->m_vecVelocity = RandomVector( -40.0f, 40.0f ) * flScale + Vector( 0, 0, 40.0f * flScale );
			pParticle->m_uchColor[ 0 ] = (unsigned char)( vecTint.x * 255 );
			pParticle->m_uchColor[ 1 ] = (unsigned char)( vecTint.y * 255 );
			pParticle->m_uchColor[ 2 ] = (unsigned char)( vecTint.z * 255 );
			pParticle->m_uchStartAlpha = 140;
			pParticle->m_uchEndAlpha = 0;
			pParticle->m_uchStartSize = (unsigned char)clamp( 6.0f * flScale, 1.0f, 255.0f );
			pParticle->m_uchEndSize = (unsigned char)clamp( 18.0f * flScale, 1.0f, 255.0f );
			pParticle->m_flRoll = random->RandomFloat( 0.0f, 360.0f );
			pParticle->m_flRollDelta = random->RandomFloat( -2.0f, 2.0f );
		}
	}
}

class CFidelityLiquidProxy : public IMaterialProxy
{
public:
	CFidelityLiquidProxy();

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void OnBind( void *pRenderable );
	virtual void Release() { delete this; }
	virtual IMaterial *GetMaterial() { return m_pMaterial; }

private:
	bool LookUpVars();
	float FillLevel( float flRandom ) const;
	FidelityLiquidState_t &State( IClientRenderable *pRenderable, const Vector &vecPos, C_BaseEntity *pEntity );
	void Simulate( FidelityLiquidState_t &state, const Vector &vecPos );
	void SetProfile( const FidelityLiquidShape_t *pShape, const matrix3x4_t &toWorld );

	IMaterial *m_pMaterial;
	IMaterialVar *m_pPlane;
	IMaterialVar *m_pColor;
	IMaterialVar *m_pFill;
	IMaterialVar *m_pFillMin;
	IMaterialVar *m_pFillMax;
	IMaterialVar *m_pWobble;
	IMaterialVar *m_pWave;
	IMaterialVar *m_pViscosity;
	IMaterialVar *m_pCenter;
	IMaterialVar *m_pKey;
	IMaterialVar *m_pSplash;
	IMaterialVar *m_pSpill;		// $spill: an open bottle pours its liquid out when tipped over
	IMaterialVar *m_pMask;		// $liquidmask (newer shader DLLs only)
	IMaterialVar *m_pProfile[ 4 ];	// $liquidaxis, $liquiddir, $liquidradii, $liquidradii2 (newer shader DLLs only)
	bool m_bMaskSet;
	Vector m_vecMaterialColor;	// $liquidcolor as written in the material (negative: the texture's colour)
	int m_nLookUp;				// 0 = not yet, 1 = vars found, -1 = not a Fidelity_Liquid material

	CUtlMap< IClientRenderable *, FidelityLiquidState_t > m_States;
	int m_nLastCleanup;
};

CFidelityLiquidProxy::CFidelityLiquidProxy() : m_States( DefLessFunc( IClientRenderable * ) )
{
	m_pMaterial = NULL;
	m_pPlane = m_pColor = m_pFill = m_pFillMin = m_pFillMax = m_pWobble = m_pWave = NULL;
	m_pViscosity = m_pCenter = m_pKey = m_pSplash = m_pMask = m_pSpill = NULL;
	m_bMaskSet = false;
	for ( int i = 0; i < 4; i++ )
		m_pProfile[ i ] = NULL;
	m_vecMaterialColor.Init( -1.0f, -1.0f, -1.0f );
	m_nLastCleanup = 0;
	m_nLookUp = 0;
}

bool CFidelityLiquidProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	// The shader's own parameters may not exist yet while proxies are set up
	// (only the ones written in the .vmt do), so they are looked up on first use.
	m_pMaterial = pMaterial;
	if ( s_LiquidMaterials.Find( pMaterial ) == s_LiquidMaterials.InvalidIndex() )
		s_LiquidMaterials.AddToTail( pMaterial );
	return true;
}

// A material variable, or NULL if the material does not have it
static IMaterialVar *Fidelity_Var( IMaterial *pMaterial, const char *pszName )
{
	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( pszName, &bFound, false );
	return bFound ? pVar : NULL;
}

bool CFidelityLiquidProxy::LookUpVars()
{
	if ( m_nLookUp != 0 )
		return m_nLookUp > 0;

	// The engine replaces an unknown shader (shader DLL missing) or one the
	// hardware can't run with another one; the props then render wrong or not at all.
	const char *pShader = m_pMaterial->GetShaderName();
	if ( !pShader || Q_stricmp( pShader, "Fidelity_Liquid" ) )
	{
		Warning( "[fidelity] Material %s uses shader %s instead of Fidelity_Liquid: bin\\game_shader_dx6.dll is not in the mod "
			"(build games.sln, run deploy.bat) or the graphics card has no shader model 3.0.\n", m_pMaterial->GetName(), pShader ? pShader : "(none)" );
		m_nLookUp = -1;
		return false;
	}

	m_pPlane = Fidelity_Var( m_pMaterial, "$liquidplane" );
	if ( !m_pPlane )
	{
		Warning( "[fidelity] Material %s uses the FidelityLiquid proxy but not the Fidelity_Liquid shader.\n", m_pMaterial->GetName() );
		m_nLookUp = -1;
		return false;
	}
	m_pColor = Fidelity_Var( m_pMaterial, "$liquidcolor" );
	m_pFill = Fidelity_Var( m_pMaterial, "$fill" );
	m_pFillMin = Fidelity_Var( m_pMaterial, "$fillmin" );
	m_pFillMax = Fidelity_Var( m_pMaterial, "$fillmax" );
	m_pWobble = Fidelity_Var( m_pMaterial, "$wobble" );
	m_pWave = Fidelity_Var( m_pMaterial, "$liquidwave" );
	m_pViscosity = Fidelity_Var( m_pMaterial, "$viscosity" );
	m_pCenter = Fidelity_Var( m_pMaterial, "$liquidcenter" );
	m_pKey = Fidelity_Var( m_pMaterial, "$liquidkey" );
	m_pSplash = Fidelity_Var( m_pMaterial, "$splash" );
	m_pSpill = Fidelity_Var( m_pMaterial, "$spill" );
	m_pMask = Fidelity_Var( m_pMaterial, "$liquidmask" );
	m_pProfile[ 0 ] = Fidelity_Var( m_pMaterial, "$liquidaxis" );
	m_pProfile[ 1 ] = Fidelity_Var( m_pMaterial, "$liquiddir" );
	m_pProfile[ 2 ] = Fidelity_Var( m_pMaterial, "$liquidradii" );
	m_pProfile[ 3 ] = Fidelity_Var( m_pMaterial, "$liquidradii2" );
	if ( m_pColor )
		m_pColor->GetVecValue( m_vecMaterialColor.Base(), 3 );
	m_nLookUp = 1;
	DevMsg( "[fidelity] Liquid in %s\n", m_pMaterial->GetName() );
	return true;
}

float CFidelityLiquidProxy::FillLevel( float flRandom ) const
{
	const float flFill = m_pFill ? m_pFill->GetFloatValue() : -1.0f;
	if ( flFill >= 0.0f )
		return MIN( flFill, 1.0f );

	// Same defaults as the shader's parameters
	const float flMin = m_pFillMin ? m_pFillMin->GetFloatValue() : 0.25f;
	const float flMax = m_pFillMax ? m_pFillMax->GetFloatValue() : 0.8f;
	return clamp( flMin + ( flMax - flMin ) * flRandom, 0.0f, 1.0f );
}

FidelityLiquidState_t &CFidelityLiquidProxy::State( IClientRenderable *pRenderable, const Vector &vecPos, C_BaseEntity *pEntity )
{
	unsigned short index = m_States.Find( pRenderable );
	if ( index != m_States.InvalidIndex() && m_States[ index ].m_nLevel != s_nLiquidLevel )
	{
		m_States.RemoveAt( index );	// same address, a renderable of an earlier map
		index = m_States.InvalidIndex();
	}
	if ( index == m_States.InvalidIndex() )
	{
		FidelityLiquidState_t state;
		state.m_nLevel = s_nLiquidLevel;
		state.m_vecHeightNormal.Init();
		state.m_flHeightFill = -1.0f;
		state.m_flHeight = 0.0f;
		state.m_vecLastPos = vecPos;
		state.m_vecLastVelocity.Init();
		state.m_vecTilt.Init();
		state.m_vecTiltSpeed.Init();
		state.m_flWave = 0.0f;
		state.m_flFizz = 0.0f;
		state.m_flBubblePhase = 0.0f;
		state.m_nSpillFrame = 0;
		state.m_flDropDebt = 0.0f;
		state.m_nLastFrame = gpGlobals->framecount - 10;	// the first step only takes the position

		// A level of its own for every prop: entities by their index (kept by
		// save and load, also when the player moved the bottle), static props
		// by where they stand. Unsigned maths: no overflow.
		const unsigned int nHash = pEntity ? (unsigned int)pEntity->entindex() * 2654435761u :
			( (unsigned int)(int)vecPos.x * 73856093u ) ^ ( (unsigned int)(int)vecPos.y * 19349663u ) ^ ( (unsigned int)(int)vecPos.z * 83492791u );
		state.m_flRandom = ( nHash % 1000u ) / 999.0f;
		index = m_States.Insert( pRenderable, state );
	}
	return m_States[ index ];
}

void CFidelityLiquidProxy::Simulate( FidelityLiquidState_t &state, const Vector &vecPos )
{
	if ( state.m_nLastFrame == gpGlobals->framecount )
		return;
	const bool bLongPause = gpGlobals->framecount - state.m_nLastFrame > 2;
	state.m_nLastFrame = gpGlobals->framecount;

	// The spring steps at most 0.05 s (stable at low frame rates); speeds come
	// from the real frame time (moved distance / time it took).
	const float flFrameTime = gpGlobals->frametime;
	const float dt = clamp( flFrameTime, 0.0f, 0.05f );
	if ( dt <= 0.0f || bLongPause )
	{
		// Paused or not drawn for a while: no jump when it comes back.
		state.m_vecLastPos = vecPos;
		state.m_vecLastVelocity.Init();
		return;
	}

	// Velocity smoothed (positions per frame are jittery: a prop carried
	// along while walking shook all the time), then the acceleration, of
	// which the small jitter is ignored
	const Vector vecRawVelocity = ( vecPos - state.m_vecLastPos ) / flFrameTime;
	const Vector vecVelocity = Lerp( MIN( flFrameTime / 0.12f, 1.0f ), state.m_vecLastVelocity, vecRawVelocity );
	Vector vecAccel = ( vecVelocity - state.m_vecLastVelocity ) / flFrameTime;
	const float flAccel = vecAccel.Length();
	vecAccel *= flAccel > SLOSH_DEADZONE ? ( flAccel - SLOSH_DEADZONE ) / flAccel : 0.0f;
	state.m_vecLastPos = vecPos;
	state.m_vecLastVelocity = vecVelocity;

	// Pushed sideways, liquid stands at a slant of acceleration / gravity (the
	// surface leans towards the push, the liquid piles up behind). A damped
	// spring gets it there and back to rest: about 1.5 swings per second,
	// settling in about two seconds.
	static ConVarRef s_Gravity( "sv_gravity" );
	const float flGravity = MAX( s_Gravity.IsValid() ? s_Gravity.GetFloat() : 600.0f, 100.0f );
	const float flWobble = m_pWobble ? m_pWobble->GetFloatValue() : 1.0f;
	const float flViscosity = m_pViscosity ? clamp( m_pViscosity->GetFloatValue(), 0.0f, 1.0f ) : 0.0f;
	const float flMaxAccel = 4000.0f;
	vecAccel.x = clamp( vecAccel.x, -flMaxAccel, flMaxAccel );
	vecAccel.y = clamp( vecAccel.y, -flMaxAccel, flMaxAccel );
	// Thick liquid: slower swings, settles sooner
	const float flStiffness = Lerp( flViscosity, 90.0f, 40.0f ), flDamping = Lerp( flViscosity, 2.2f, 5.0f );
	const float flPush = flStiffness / flGravity * flWobble;
	float flMotion = 0.0f;
	for ( int i = 0; i < 2; i++ )
	{
		float &flTilt = state.m_vecTilt[ i ];
		float &flSpeed = state.m_vecTiltSpeed[ i ];
		flSpeed += ( -flStiffness * flTilt - flDamping * flSpeed + vecAccel[ i ] * flPush ) * dt;
		flTilt = clamp( flTilt + flSpeed * dt, -0.7f, 0.7f );
		flMotion += fabsf( flSpeed ) * 0.15f + fabsf( flTilt ) * 0.6f;
	}

	// Waves: rise quickly with the sloshing, calm down a bit slower than the tilt.
	const float flWaveTarget = clamp( flMotion * flWobble * ( 1.0f - 0.6f * flViscosity ), 0.0f, 1.0f );
	state.m_flWave = Lerp( MIN( dt / ( flWaveTarget > state.m_flWave ? 0.1f : 0.6f ), 1.0f ), state.m_flWave, flWaveTarget );

	// Fizz: only when it is really shaken or thrown (a nudge only makes waves),
	// at once, then dying down slowly over a few seconds (Julius: bubbling on
	// a light touch was too much; after a throw it did not fizz at all with a
	// slow build-up - a throw is a short jolt).
	const float flShaken = clamp( ( flMotion - 0.3f ) / 0.4f, 0.0f, 1.0f ) * flWobble * ( 1.0f - 0.3f * flViscosity );	// thick liquid bubbles too, slowly (Julius)
	state.m_flFizz = Lerp( MIN( dt / ( flShaken > state.m_flFizz ? 0.08f : 3.0f ), 1.0f ), state.m_flFizz, flShaken );

	// The bubbles rise steadily, faster while it fizzes: added up here, so a
	// change of speed never moves them back (in the shader time x speed did).
	state.m_flBubblePhase = fmodf( state.m_flBubblePhase + dt * ( 1.0f + 2.0f * state.m_flFizz ), 4096.0f );
}

// Models this proxy was bound for (also static props, which are no entities),
// for r_fidelity_liquid_info. Few entries, so a plain list.
struct FidelityLiquidModel_t
{
	const model_t *m_pModel;
	bool m_bStatic;
};
static CUtlVector< FidelityLiquidModel_t > s_LiquidModels;

static bool Fidelity_LiquidModelKnown( const model_t *pModel )
{
	for ( int i = 0; i < s_LiquidModels.Count(); i++ )
	{
		if ( s_LiquidModels[ i ].m_pModel == pModel )
			return true;
	}
	return false;
}

// The key colour of a liquid material ($liquidkey, gamma); false if it has none
static bool Fidelity_MaterialKey( IMaterialVar *pKey, Vector &vecKey )
{
	vecKey.Init();
	if ( pKey )
		pKey->GetVecValue( vecKey.Base(), 3 );
	return vecKey.x + vecKey.y + vecKey.z > 0.0f;
}

//-----------------------------------------------------------------------------
// The container's profile in world space for the shader (it draws the surface
// where the view through the glass meets it); off without a known shape
//-----------------------------------------------------------------------------
void CFidelityLiquidProxy::SetProfile( const FidelityLiquidShape_t *pShape, const matrix3x4_t &toWorld )
{
	if ( !m_pProfile[ 0 ] || !m_pProfile[ 1 ] || !m_pProfile[ 2 ] || !m_pProfile[ 3 ] )
		return;
	if ( !pShape || pShape->m_flVolume <= 0.0f || pShape->m_flSliceLength <= 0.0f )
	{
		m_pProfile[ 1 ]->SetVecValue( 0.0f, 0.0f, 1.0f, 0.0f );
		return;
	}
	Vector vecAxis( 0, 0, 0 ), vecStart, vecWorldAxis;
	vecAxis[ pShape->m_nAxis ] = 1.0f;
	VectorTransform( pShape->m_vecAxisStart, toWorld, vecStart );
	VectorRotate( vecAxis, toWorld, vecWorldAxis );
	const float flScale = VectorNormalize( vecWorldAxis );	// the prop's model scale
	const float *r = pShape->m_flProfile;
	m_pProfile[ 0 ]->SetVecValue( vecStart.x, vecStart.y, vecStart.z, pShape->m_flSliceLength * ( SHAPE_SLICES / SHAPE_PROFILE ) * flScale );
	m_pProfile[ 1 ]->SetVecValue( vecWorldAxis.x, vecWorldAxis.y, vecWorldAxis.z, 1.0f );
	m_pProfile[ 2 ]->SetVecValue( r[ 0 ] * flScale, r[ 1 ] * flScale, r[ 2 ] * flScale, r[ 3 ] * flScale );
	m_pProfile[ 3 ]->SetVecValue( r[ 4 ] * flScale, r[ 5 ] * flScale, r[ 6 ] * flScale, r[ 7 ] * flScale );
}

void CFidelityLiquidProxy::OnBind( void *pArg )
{
	IClientRenderable *pRenderable = (IClientRenderable *)pArg;
	const model_t *pModel = pRenderable ? pRenderable->GetModel() : NULL;
	IClientUnknown *pUnknown = pRenderable ? pRenderable->GetIClientUnknown() : NULL;
	C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
	if ( pModel && !Fidelity_LiquidModelKnown( pModel ) )
	{
		FidelityLiquidModel_t &entry = s_LiquidModels[ s_LiquidModels.AddToTail() ];
		entry.m_pModel = pModel;
		entry.m_bStatic = !pEntity;
	}
	if ( !pRenderable || !LookUpVars() )
		return;
	if ( !pModel )
	{
		m_pPlane->SetVecValue( 0.0f, 0.0f, 1.0f, -100000.0f );
		SetProfile( NULL, matrix3x4_t() );
		return;
	}

	// The space that holds liquid (only the key colour's parts, if there is one)
	Vector vecKey;
	const bool bKey = Fidelity_MaterialKey( m_pKey, vecKey );
	const FidelityLiquidShape_t &shape = Fidelity_LiquidShape( pModel, m_pMaterial, vecKey, bKey );
	if ( bKey && m_pMask && !m_bMaskSet )
	{
		m_bMaskSet = true;
		const CFidelityLiquidMask *pMask = Fidelity_LiquidMask( m_pMaterial, vecKey, pModel );
		if ( pMask )
			m_pMask->SetTextureValue( pMask->m_pTexture );
	}
	if ( shape.m_bPiece )
	{
		m_pPlane->SetVecValue( 0.0f, 0.0f, 1.0f, -100000.0f );	// no liquid at all
		SetProfile( NULL, matrix3x4_t() );
		return;
	}

	const matrix3x4_t &toWorld = pRenderable->RenderableToWorldTransform();
	SetProfile( &shape, toWorld );
	// Render bounds only stand in where the shape is unknown
	Vector vecMins( 0, 0, 0 ), vecMaxs( 0, 0, 0 );
	if ( shape.m_flVolume <= 0.0f || shape.m_flWidth <= 0.0f )
		pRenderable->GetRenderBounds( vecMins, vecMaxs );
	FidelityLiquidState_t &state = State( pRenderable, pRenderable->GetRenderOrigin(), pEntity );
	// An open bottle that was tipped over has lost some of its liquid
	const bool bSpill = pEntity && m_pSpill && m_pSpill->GetIntValue() && shape.m_flOpening > 0.0f;
	const float flFill = MAX( FillLevel( state.m_flRandom ) - ( bSpill ? Fidelity_Spilled( pEntity ) : 0.0f ), 0.0f );

	// Moving or turning the container moves the middle of its liquid space.
	Vector vecMiddle;
	VectorTransform( shape.m_flVolume > 0.0f ? shape.m_vecCenter : ( vecMins + vecMaxs ) * 0.5f, toWorld, vecMiddle );
	Simulate( state, vecMiddle );

	// Level with the world, tilted by the sloshing
	Vector vecNormal( state.m_vecTilt.x, state.m_vecTilt.y, 1.0f );
	VectorNormalize( vecNormal );
	Vector vecSurface;	// a point of the surface, world space
	if ( shape.m_flVolume > 0.0f )
	{
		// Where the prop's amount of liquid comes up to, in its current position
		Vector vecLocalNormal;
		VectorIRotate( vecNormal, toWorld, vecLocalNormal );
		VectorNormalize( vecLocalNormal );
		if ( state.m_flHeightFill != flFill || DotProduct( vecLocalNormal, state.m_vecHeightNormal ) < 0.99999f )
		{
			state.m_flHeight = Fidelity_FillHeight( shape, vecLocalNormal, flFill );
			state.m_vecHeightNormal = vecLocalNormal;
			state.m_flHeightFill = flFill;
		}
		VectorTransform( vecLocalNormal * state.m_flHeight, toWorld, vecSurface );
	}
	else
	{
		// Shape unknown: a share of the model's height
		const Vector vecLocal( ( vecMins.x + vecMaxs.x ) * 0.5f, ( vecMins.y + vecMaxs.y ) * 0.5f, vecMins.z + ( vecMaxs.z - vecMins.z ) * flFill );
		VectorTransform( vecLocal, toWorld, vecSurface );
	}
	m_pPlane->SetVecValue( vecNormal.x, vecNormal.y, vecNormal.z, DotProduct( vecNormal, vecSurface ) );

	// Pouring out (once a frame): the neck under the surface lets liquid out,
	// faster the deeper it is; a stream of drops falls from it
	if ( bSpill && flFill > 0.0f && state.m_nSpillFrame != gpGlobals->framecount && shape.m_flVolume > 0.0f )
	{
		state.m_nSpillFrame = gpGlobals->framecount;
		Vector vecOpening;
		VectorTransform( shape.m_vecOpening, toWorld, vecOpening );
		Vector vecColumn( toWorld[ 0 ][ 0 ], toWorld[ 1 ][ 0 ], toWorld[ 2 ][ 0 ] );
		const float flScale = vecColumn.Length();	// the prop's model scale
		// how far the surface reaches over the lowest edge of the opening (its
		// middle alone missed bottles lying on their side or sliding down)
		const float flDepth = DotProduct( vecNormal, vecSurface - vecOpening ) + shape.m_flOpening * flScale;
		if ( flDepth > 0.0f )
		{
			const float dt = clamp( gpGlobals->frametime, 0.0f, 0.1f );
			const float flRate = SPILL_RATE * clamp( flDepth / ( 2.0f * shape.m_flOpening * flScale ), 0.2f, 1.0f );	// share of the volume per second
			Fidelity_AddSpilled( pEntity, MIN( flRate * dt, flFill ) );
			state.m_flDropDebt += flRate * dt * SPILL_DROPS;
			const int nDrops = (int)state.m_flDropDebt;
			if ( nDrops > 0 )
			{
				state.m_flDropDebt -= nDrops;
				Vector vecMiddle;
				VectorTransform( shape.m_vecCenter, toWorld, vecMiddle );
				Vector vecOut = vecOpening - vecMiddle;
				VectorNormalize( vecOut );
				const Vector vecColor = m_vecMaterialColor.x >= 0.0f ? m_vecMaterialColor : shape.m_vecColor;
				Fidelity_SpillDrops( vecOpening, vecOut, vecColor, MIN( nDrops, 8 ), shape.m_flOpening );
			}
		}
	}

	// Waves on the surface: a ripple even at rest, higher while it sloshes,
	// at most a few percent of the container's width.
	if ( m_pWave )
	{
		const float flWidth = shape.m_flWidth > 0.0f ? shape.m_flWidth : MIN( vecMaxs.x - vecMins.x, vecMaxs.y - vecMins.y );
		const float flHeight = clamp( flWidth * 0.07f, 0.05f, 0.8f ) * ( 0.04f + 0.96f * state.m_flWave );
		// .y: time (wrapped: the sin-free noise in the shader stays precise
		// that far); .z: how far the bubbles have risen; .w: fizz (more
		// bubbles, foam)
		m_pWave->SetVecValue( flHeight, fmodf( gpGlobals->curtime, 4096.0f ), state.m_flBubblePhase, state.m_flFizz );
	}
	if ( m_pCenter )
		m_pCenter->SetVecValue( vecMiddle.x, vecMiddle.y, vecMiddle.z );

	// Render Color of the prop (white = not set) replaces the material's colour.
	// Without either: one colour for all the liquid, the average of the parts
	// that hold it (a bit more saturated). Taking the texture pixel by pixel
	// showed its printed details as orange spots in the liquid (health vial).
	// With a key colour (health vial, medkit) the liquid has exactly that colour:
	// the average of its parts also took in highlights and came out grey.
	Vector vecLiquidColor = m_vecMaterialColor;
	if ( vecLiquidColor.x < 0.0f && bKey )
	{
		vecLiquidColor = vecKey;
	}
	else if ( vecLiquidColor.x < 0.0f && shape.m_flVolume > 0.0f )
	{
		const float flGray = ( shape.m_vecColor.x + shape.m_vecColor.y + shape.m_vecColor.z ) / 3.0f;
		vecLiquidColor = Vector( flGray, flGray, flGray ) + ( shape.m_vecColor - Vector( flGray, flGray, flGray ) ) * 1.3f;
		vecLiquidColor.x = clamp( vecLiquidColor.x, 0.0f, 1.0f );
		vecLiquidColor.y = clamp( vecLiquidColor.y, 0.0f, 1.0f );
		vecLiquidColor.z = clamp( vecLiquidColor.z, 0.0f, 1.0f );
	}
	if ( pEntity )
	{
		const color32 color = pEntity->GetRenderColor();
		if ( color.r != 255 || color.g != 255 || color.b != 255 )
			vecLiquidColor.Init( color.r / 255.0f, color.g / 255.0f, color.b / 255.0f );
	}
	if ( m_pColor )
		m_pColor->SetVecValue( vecLiquidColor.x, vecLiquidColor.y, vecLiquidColor.z );

	// Remember it for the splash when it breaks
	if ( pEntity && m_pSplash && m_pSplash->GetIntValue() && shape.m_flVolume > 0.0f )
	{
		VectorTransform( shape.m_vecCenter, toWorld, vecMiddle );
		Fidelity_NoteSplashProp( pEntity, vecMiddle, vecLiquidColor.x >= 0.0f ? vecLiquidColor : shape.m_vecColor, flFill * shape.m_flVolume );
	}

	// Forget props that were not drawn for ten seconds or so.
	if ( gpGlobals->framecount - m_nLastCleanup > 600 )
	{
		m_nLastCleanup = gpGlobals->framecount;
		for ( unsigned short i = m_States.FirstInorder(); i != m_States.InvalidIndex(); )
		{
			const unsigned short next = m_States.NextInorder( i );
			if ( gpGlobals->framecount - m_States[ i ].m_nLastFrame > 600 )
				m_States.RemoveAt( i );
			i = next;
		}
	}
}

EXPOSE_INTERFACE( CFidelityLiquidProxy, IMaterialProxy, "FidelityLiquid" IMATERIAL_PROXY_INTERFACE_VERSION );

//-----------------------------------------------------------------------------
// r_fidelity_liquid_info: for every model in the map that uses a
// Fidelity_Liquid material, prints the vertex format the material asks for
// and the one of the model's meshes. The engine refuses to draw a mesh whose
// format does not fit ("does not support vertex format used by the mesh").
//-----------------------------------------------------------------------------
static void Fidelity_FormatText( VertexFormat_t fmt, char *pOut, int nOutSize )
{
	Q_snprintf( pOut, nOutSize, "%s%s%s%s%s%s%s%s%s%s bones %d, userdata %d, tc0 %d, tc1 %d",
		( fmt & VERTEX_POSITION ) ? "pos " : "", ( fmt & VERTEX_NORMAL ) ? "normal " : "",
		( fmt & VERTEX_COLOR ) ? "color " : "", ( fmt & VERTEX_SPECULAR ) ? "specular " : "",
		( fmt & VERTEX_TANGENT_S ) ? "tangentS " : "", ( fmt & VERTEX_TANGENT_T ) ? "tangentT " : "",
		( fmt & VERTEX_WRINKLE ) ? "wrinkle " : "", ( fmt & VERTEX_BONE_INDEX ) ? "boneindex " : "",
		( fmt & VERTEX_FORMAT_VERTEX_SHADER ) ? "vs " : "", ( fmt & VERTEX_FORMAT_COMPRESSED ) ? "COMPRESSED" : "uncompressed",
		NumBoneWeights( fmt ), UserDataSize( fmt ), TexCoordSize( 0, fmt ), TexCoordSize( 1, fmt ) );
}

static bool Fidelity_IsLiquidMaterial( IMaterial *pMaterial )
{
	if ( !pMaterial || pMaterial->IsErrorMaterial() )
		return false;
	bool bFound = false;
	pMaterial->FindVar( "$liquidplane", &bFound, false );
	return bFound || !Q_stricmp( pMaterial->GetShaderName(), "Fidelity_Liquid" );
}

//-----------------------------------------------------------------------------
// Whether the material system knows a shader of this name (from a shader DLL
// it loaded). A material whose shader it does not know gets the stand-in
// Wireframe_DX8 (cmaterial.cpp: "uses unknown shader"). On a model that stand-in
// fails the engine's check before every draw (meshdx8.cpp, IsValidVertexFormat:
// the model's compressed vertices need a material that supports compression),
// so the prop stays invisible and the console fills with "does not support
// vertex format used by the mesh ... Grab a programmer!".
//-----------------------------------------------------------------------------
static bool Fidelity_EngineHasShader( const char *pName )
{
	IShader *pShaders[ 64 ];
	const int nTotal = materials->ShaderCount();
	for ( int nFirst = 0; nFirst < nTotal; nFirst += ARRAYSIZE( pShaders ) )
	{
		const int nCount = materials->GetShaders( nFirst, ARRAYSIZE( pShaders ), pShaders );
		for ( int i = 0; i < nCount; i++ )
		{
			if ( pShaders[ i ] && !Q_stricmp( pShaders[ i ]->GetName(), pName ) )
				return true;
		}
		if ( nCount <= 0 )
			break;
	}
	return false;
}

//-----------------------------------------------------------------------------
// The plain look for a liquid material when the engine has no Fidelity_Liquid:
// the game's glass (VertexLitGeneric with the same texture, see-through where
// the original was). Better a bottle without liquid than an invisible one.
//-----------------------------------------------------------------------------
static KeyValues *Fidelity_PlainGlassKeys( KeyValues *pVmt )
{
	KeyValues *pPlain = new KeyValues( "VertexLitGeneric" );
	const char *pBase = pVmt->GetString( "$basetexture", "" );
	if ( pBase[ 0 ] )
		pPlain->SetString( "$basetexture", pBase );
	// $labelthreshold 2: deploy.ps1 / Mapsmith found an opaque original texture;
	// $liquidkey: an opaque container with a vial (medkit)
	if ( !pVmt->FindKey( "$liquidkey" ) && pVmt->GetFloat( "$labelthreshold", 0.8f ) < 1.5f )
		pPlain->SetInt( "$translucent", 1 );
	pPlain->SetString( "$envmap", "env_cubemap" );
	pPlain->SetString( "$envmaptint", "[0.35 0.35 0.35]" );
	return pPlain;
}

//-----------------------------------------------------------------------------
// Why the engine may not know our shaders: where it looks for shader DLLs
// (search path GAMEBIN), whether game_shader_dx6.dll is there and whether it
// can be loaded at all (missing file vs. broken DLL vs. a name the engine
// does not load). Written into the report.
// (A test material made with CreateMaterial is no proof: it reports the
// shader name it was given until it is first used.)
//-----------------------------------------------------------------------------
static void Fidelity_ShaderDllReport( CUtlBuffer &buf )
{
	char szPaths[ 4096 ];
	filesystem->GetSearchPath( "GAMEBIN", false, szPaths, sizeof( szPaths ) );
	// PutString: Printf formats into 2048 bytes and would cut a long path off
	buf.PutString( "Shader DLL search path (GAMEBIN): " );
	buf.PutString( szPaths );
	buf.PutChar( '\n' );

	static const char *s_pDlls[] = { "game_shader_dx6.dll", "game_shader_dx9.dll", "game_shader_dx9_fidelity.dll" };
	for ( int i = 0; i < ARRAYSIZE( s_pDlls ); i++ )
	{
		char szFull[ MAX_PATH ] = "";
		filesystem->RelativePathToFullPath( s_pDlls[ i ], "GAMEBIN", szFull, sizeof( szFull ) );
		buf.Printf( "  %s: %s\n", s_pDlls[ i ], szFull[ 0 ] ? szFull : "not found" );
	}

	// Can ours be loaded? (Loaded and unloaded again; the engine keeps its own copy if it has one.)
	CSysModule *pModule = filesystem->LoadModule( "game_shader_dx6", "GAMEBIN", false );
	buf.Printf( "  game_shader_dx6 can be loaded: %s\n", pModule ? "yes" : "NO (missing, or a DLL it needs is missing)" );
	if ( pModule )
		filesystem->UnloadModule( pModule );

	// What the material system really registered (loadable is no proof: it loads
	// mod shader DLLs once at start, only from the mod's own bin).
	buf.Printf( "  shader Fidelity_Liquid known to the material system: %s\n",
		Fidelity_EngineHasShader( "Fidelity_Liquid" ) ? "yes" : "NO - materials that ask for it get the stand-in Wireframe_DX8" );
}

//-----------------------------------------------------------------------------
// Materials can be loaded before the engine knows the Fidelity_Liquid shader
// (seen in game: the bottle materials ended up with the stand-in shader
// Wireframe_DX8 although the shader was loaded). Every material that has the
// stand-in but asks for Fidelity_Liquid in its .vmt is loaded again.
// Returns how many were reloaded.
//-----------------------------------------------------------------------------
static int s_nRepairedLiquidMaterials = 0;
static int s_nPlainGlass = 0;			// liquid materials drawn as plain glass (no Fidelity_Liquid)
static CUtlVector< CUtlString > s_RepairLog;	// what the repair saw, for the report
static CUtlVector< IMaterial * > s_RepairTried;	// each material is looked at once

static int Fidelity_RepairLiquidMaterials()
{
	// Collect first: reloading while walking the material list could change it.
	CUtlVector< IMaterial * > stale;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( pMaterial && !Q_strnicmp( pMaterial->GetShaderName(), "Wireframe", 9 ) && s_RepairTried.Find( pMaterial ) == s_RepairTried.InvalidIndex() )
		{
			stale.AddToTail( pMaterial );
			s_RepairTried.AddToTail( pMaterial );
		}
	}

	int nRepaired = 0;
	for ( int i = 0; i < stale.Count(); i++ )
	{
		IMaterial *pMaterial = stale[ i ];
		char szPath[ MAX_PATH ];
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vmt", pMaterial->GetName() );
		KeyValues *pKeys = new KeyValues( "vmt" );
		const bool bLoaded = pKeys->LoadFromFile( filesystem, szPath, "GAME" );
		if ( bLoaded && !Q_stricmp( pKeys->GetName(), "Fidelity_Liquid" ) )
		{
			pMaterial->Refresh();
			s_RepairLog.AddToTail( CUtlString( CFmtStr( "  %s: reloaded, shader now %s", pMaterial->GetName(), pMaterial->GetShaderName() ).Access() ) );

			// Still the stand-in? Hand the material its .vmt directly (as for the sky,
			// fidelity_sky.cpp; the KeyValues stay alive on purpose).
			if ( !Q_strnicmp( pMaterial->GetShaderName(), "Wireframe", 9 ) )
			{
				pMaterial->SetShaderAndParams( pKeys->MakeCopy() );
				s_RepairLog.AddToTail( CUtlString( CFmtStr( "    set from its .vmt: shader now %s", pMaterial->GetShaderName() ).Access() ) );
			}

			// Still the stand-in? Then the .vmt itself does not work: try it as a new material.
			if ( !Q_strnicmp( pMaterial->GetShaderName(), "Wireframe", 9 ) )
			{
				IMaterial *pTest = materials->CreateMaterial( CFmtStr( "__fidelity_test_%d", s_RepairLog.Count() ), pKeys->MakeCopy() );
				s_RepairLog.AddToTail( CUtlString( CFmtStr( "    same .vmt as a new material: shader %s", pTest ? pTest->GetShaderName() : "(none)" ).Access() ) );
			}

			// Still the stand-in: the engine cannot draw it on a model (invisible prop, a
			// warning every frame). Draw the container as the game's plain glass instead.
			if ( !Q_strnicmp( pMaterial->GetShaderName(), "Wireframe", 9 ) )
			{
				pMaterial->SetShaderAndParams( Fidelity_PlainGlassKeys( pKeys ) );
				s_nPlainGlass++;
				s_RepairLog.AddToTail( CUtlString( CFmtStr( "    drawn as plain glass: shader now %s", pMaterial->GetShaderName() ).Access() ) );
			}
			else
			{
				nRepaired++;
			}
		}
		else if ( s_RepairLog.Count() < 64 && !( bLoaded && !Q_strnicmp( pKeys->GetName(), "Wireframe", 9 ) ) )
		{
			s_RepairLog.AddToTail( CUtlString( CFmtStr( "  %s: stand-in shader, .vmt %s (root \"%s\")", pMaterial->GetName(),
				bLoaded ? "read" : "NOT readable", bLoaded ? pKeys->GetName() : "" ).Access() ) );
		}
		pKeys->deleteThis();
	}
	s_nRepairedLiquidMaterials += nRepaired;
	return nRepaired;
}

//-----------------------------------------------------------------------------
// Writes the report into fidelity_liquid_info.txt in the mod folder (the
// console may be flooded by the engine's own message) and, if asked, prints it.
//-----------------------------------------------------------------------------
static void Fidelity_LiquidInfo( bool bPrint )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );

	Fidelity_RepairLiquidMaterials();
	Fidelity_ShaderDllReport( buf );

	// The skybox materials and their shaders (Fidelity_Sky = drifting clouds, fidelity_sky.cpp)
	buf.Printf( "Skybox materials:\n" );
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pSky = materials->GetMaterial( h );
		if ( !pSky || Q_strnicmp( pSky->GetName(), "skybox/", 7 ) )
			continue;
		bool bFound = false;
		IMaterialVar *pClouds = pSky->FindVar( "$cloudparams", &bFound, false );
		float vClouds[ 4 ] = { 0, 0, 0, 0 };
		if ( bFound )
			pClouds->GetVecValue( vClouds, 4 );
		buf.Printf( "  %s: shader %s, clouds %s [%.2f %.2f %.2f %.2f]\n", pSky->GetName(), pSky->GetShaderName(),
			bFound ? "set" : "NOT set", vClouds[ 0 ], vClouds[ 1 ], vClouds[ 2 ], vClouds[ 3 ] );
	}

	char szFormat[ 256 ];
	buf.Printf( "Materials reloaded because they had the stand-in shader: %d\n", s_nRepairedLiquidMaterials );
	buf.Printf( "Liquid materials drawn as plain glass (shader not usable): %d\n", s_nPlainGlass );
	for ( int i = 0; i < s_RepairLog.Count(); i++ )
		buf.Printf( "%s\n", s_RepairLog[ i ].Get() );
	// Only materials the material system still has (one may have been freed)
	CUtlVector< IMaterial * > live;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
		live.AddToTail( materials->GetMaterial( h ) );
	buf.Printf( "Materials with the FidelityLiquid proxy: %d\n", s_LiquidMaterials.Count() );
	for ( int i = 0; i < s_LiquidMaterials.Count(); i++ )
	{
		IMaterial *pMaterial = s_LiquidMaterials[ i ];
		if ( live.Find( pMaterial ) == live.InvalidIndex() )
			continue;
		Fidelity_FormatText( pMaterial->GetVertexFormat(), szFormat, sizeof( szFormat ) );
		buf.Printf( "  %s: shader %s, precached %d, error %d\n    wants %s\n", pMaterial->GetName(), pMaterial->GetShaderName(),
			pMaterial->IsPrecached() ? 1 : 0, pMaterial->IsErrorMaterial() ? 1 : 0, szFormat );
	}

	// Models of all entities, then the ones the proxy saw (static props are no entities).
	CUtlVector< const model_t * > models;
	CUtlVector< CUtlString > kinds;
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		const model_t *pModel = pEntity->GetModel();
		if ( pModel && models.Find( pModel ) == models.InvalidIndex() )
		{
			models.AddToTail( pModel );
			kinds.AddToTail( CUtlString( pEntity->GetClassname() ) );
		}
	}
	for ( int i = 0; i < s_LiquidModels.Count(); i++ )
	{
		if ( models.Find( s_LiquidModels[ i ].m_pModel ) == models.InvalidIndex() )
		{
			models.AddToTail( s_LiquidModels[ i ].m_pModel );
			kinds.AddToTail( CUtlString( s_LiquidModels[ i ].m_bStatic ? "prop_static" : "drawn, no entity found" ) );
		}
	}

	int nLiquidModels = 0;
	for ( int nModel = 0; nModel < models.Count(); nModel++ )
	{
		const model_t *pModel = models[ nModel ];
		if ( modelinfo->GetModelType( pModel ) != mod_studio )
			continue;

		studiohwdata_t *pHardware = mdlcache->GetHardwareData( modelinfo->GetCacheHandle( pModel ) );
		if ( !pHardware || pHardware->m_NumLODs <= 0 )
			continue;
		const studioloddata_t &lod = pHardware->m_pLODs[ pHardware->m_RootLOD ];
		bool bLiquid = false;
		for ( int i = 0; i < lod.numMaterials && !bLiquid; i++ )
		{
			bLiquid = Fidelity_IsLiquidMaterial( lod.ppMaterials[ i ] ) ||
				s_LiquidMaterials.Find( lod.ppMaterials[ i ] ) != s_LiquidMaterials.InvalidIndex();
		}
		// Containers by name too: shows what they really use when it is not the liquid shader.
		const char *pModelName = modelinfo->GetModelName( pModel );
		if ( !bLiquid && !Q_stristr( pModelName, "bottle" ) && !Q_stristr( pModelName, "vial" ) && !Q_stristr( pModelName, "jar" ) )
			continue;
		nLiquidModels++;

		buf.Printf( "%s (%s, %d bones)\n", modelinfo->GetModelName( pModel ), kinds[ nModel ].Get(), modelinfo->GetStudiomodel( pModel )->numbones );
		for ( int i = 0; i < lod.numMaterials; i++ )
		{
			IMaterial *pMaterial = lod.ppMaterials[ i ];
			if ( !pMaterial )
				continue;
			Fidelity_FormatText( pMaterial->GetVertexFormat(), szFormat, sizeof( szFormat ) );
			buf.Printf( "  material %s: shader %s, precached %d, sw skinning %d, tangents %d\n    wants %s\n",
				pMaterial->GetName(), pMaterial->GetShaderName(), pMaterial->IsPrecached() ? 1 : 0,
				pMaterial->NeedsSoftwareSkinning() ? 1 : 0, pMaterial->NeedsTangentSpace() ? 1 : 0, szFormat );
			if ( Fidelity_IsLiquidMaterial( pMaterial ) )
			{
				// Which parts hold liquid and how much space they have
				Vector vecKey;
				const bool bKey = Fidelity_MaterialKey( Fidelity_Var( pMaterial, "$liquidkey" ), vecKey );
				const FidelityLiquidShape_t &shape = Fidelity_LiquidShape( pModel, pMaterial, vecKey, bKey );
				buf.Printf( "    liquid: %d of %d vertices hold liquid (%s), volume %.0f cubic units, width %.1f, colour %.2f %.2f %.2f, texture %s\n",
					shape.m_nUsed, shape.m_nVertices, bKey ? ( shape.m_nUsed < shape.m_nVertices ? "key colour" : "key colour NOT found: all" ) : "all",
					shape.m_flVolume, shape.m_flWidth, shape.m_vecColor.x, shape.m_vecColor.y, shape.m_vecColor.z,
					shape.m_bTextureRead ? "read" : "NOT readable (thumbnail used)" );
			}
		}
		for ( int nMesh = 0; nMesh < pHardware->m_NumStudioMeshes; nMesh++ )
		{
			const studiomeshdata_t &mesh = lod.m_pMeshData[ nMesh ];
			for ( int nGroup = 0; nGroup < mesh.m_NumGroup; nGroup++ )
			{
				const studiomeshgroup_t &group = mesh.m_pMeshGroup[ nGroup ];
				if ( !group.m_pMesh )
					continue;
				Fidelity_FormatText( group.m_pMesh->GetVertexFormat(), szFormat, sizeof( szFormat ) );
				buf.Printf( "  mesh %d.%d (flags %d, %d verts): %s\n", nMesh, nGroup, group.m_Flags, group.m_NumVertices, szFormat );
			}
		}
	}
	if ( nLiquidModels == 0 )
		buf.Printf( "No models with a liquid material in the map (static props count once they were on screen).\n" );

	const bool bWritten = filesystem->WriteFile( "fidelity_liquid_info.txt", "DEFAULT_WRITE_PATH", buf );
	if ( bPrint )
	{
		buf.PutChar( '\0' );
		Msg( "%s", (const char *)buf.Base() );
	}
	Msg( "[fidelity] Liquid report %s fidelity_liquid_info.txt in the mod folder.\n", bWritten ? "written to" : "could NOT be written to" );
}

CON_COMMAND( r_fidelity_liquid_info, "Writes shader and vertex formats of the models with liquid (Fidelity_Liquid) materials to fidelity_liquid_info.txt and the console." )
{
	Fidelity_LiquidInfo( true );
}

//-----------------------------------------------------------------------------
// Writes the report by itself (into the file only) once, a few seconds after
// a map is loaded; r_fidelity_liquid_info writes a fresh one any time. Not
// again during play: it walks all materials and entities (a hitch).
//-----------------------------------------------------------------------------
class CFidelityLiquidInfo : public CAutoGameSystemPerFrame
{
public:
	CFidelityLiquidInfo() : CAutoGameSystemPerFrame( "CFidelityLiquidInfo" ), m_flReportTime( -1.0f ) {}

	virtual void LevelInitPostEntity()
	{
		m_flReportTime = gpGlobals->realtime + 3.0f;
		const int nPlainBefore = s_nPlainGlass;
		const int nRepaired = Fidelity_RepairLiquidMaterials();
		if ( nRepaired > 0 )
			Msg( "[fidelity] Reloaded %d liquid materials that were loaded before their shader.\n", nRepaired );
		if ( s_nPlainGlass > nPlainBefore )
		{
			Warning( "[fidelity] %d bottle materials drawn as plain glass: %s. Fix: \"Update mod\" in Mapsmith (or update.bat), "
				"then start the game again; r_fidelity_liquid_info tells more.\n", s_nPlainGlass - nPlainBefore,
				Fidelity_EngineHasShader( "Fidelity_Liquid" )
					? "their shader Fidelity_Liquid does not work"
					: "the game has no Fidelity_Liquid shader (bin\\game_shader_dx6.dll missing, not loadable, or the game was started for another mod folder)" );
		}
	}
	virtual void Shutdown() { Fidelity_ForgetMasks(); }
	virtual void LevelShutdownPreEntity()
	{
		m_flReportTime = -1.0f;
		s_nLiquidLevel++;
		s_LiquidModels.Purge();
		s_RepairTried.Purge();		// a new material may come at an old address
		Fidelity_ForgetShapes();	// model pointers are only valid for one map
		s_Spilled.RemoveAll();
		s_LiquidModelLight.RemoveAll();
		s_SplashProps.Purge();		// props going away with the map do not splash
	}
	virtual void Update( float frametime )
	{
		// Splash: a prop drawn a moment ago that is gone now broke.
		for ( int i = s_SplashProps.Count() - 1; i >= 0; i-- )
		{
			const FidelitySplashProp_t &prop = s_SplashProps[ i ];
			const float flAge = gpGlobals->curtime - prop.m_flLastSeen;
			if ( !prop.m_hEntity.Get() )
			{
				if ( flAge < 0.5f && prop.m_flAmount > 0.5f )
					Fidelity_LiquidSplash( prop.m_vecPos, prop.m_vecColor, prop.m_flAmount );
				s_SplashProps.FastRemove( i );
			}
			else if ( flAge > 5.0f || flAge < 0.0f )
			{
				s_SplashProps.FastRemove( i );
			}
		}

		if ( m_flReportTime < 0.0f || gpGlobals->realtime < m_flReportTime )
			return;
		m_flReportTime = -1.0f;
		Fidelity_LiquidInfo( false );
	}

private:
	float m_flReportTime;
};

static CFidelityLiquidInfo s_FidelityLiquidInfo;
