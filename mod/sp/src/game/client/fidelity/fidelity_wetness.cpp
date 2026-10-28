//========= mapbase-fidelity ==================================================//
//
// Purpose: Wet surfaces and puddles in the rain (r_fidelity_wetness, on by
//          default): how wet the world is and where the rain reaches, for
//          Fidelity_Parallax.
//
// Raining: the map has a rain func_precipitation (classic or particle rain,
// not dormant). The world then gets wet over r_fidelity_wetness_wet seconds
// and dries over r_fidelity_wetness_dry once it stops (0..1, the
// FIDELITY_FLOAT_RENDERPARM_WETNESS rendering parameter).
//
// Where the rain reaches: a map of the area around the player seen from the
// sky, WETNESS_CELLS x WETNESS_CELLS cells of WETNESS_CELL units: for each
// cell the height of the highest solid surface below the sky (a trace down
// from just below the sky over the player). A wall pixel is wet when it is
// that surface - not under a roof, a bridge or inside. A cell whose trace
// starts in something solid (a higher roof, the sky lower there) counts as
// dry. The map is built cell by cell, WETNESS_TRACES a frame, nearest first,
// moves along with the player in steps of WETNESS_STEP (keeping what is
// still inside) and lives in a small texture (fidelity/rain_map: height in
// red and green, blue 255 = known; point sampled), uploaded at most every
// WETNESS_UPLOAD seconds. Its place: floats WETNESS_FLOAT_* (fidelity_renderparm.h).
// It is all in the world: wet is where the rain falls, wherever one looks.
//
// Cost: up to WETNESS_TRACES traces a frame while the map is incomplete (on
// arriving, after moving on), nothing while it is complete and dry.
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "view.h"
#include "dt_recv.h"
#include "precipitation_shared.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "fidelity/fidelity_renderparm.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_wetness( "r_fidelity_wetness", "1", FCVAR_ARCHIVE, "In the rain, walls and floors open to the sky get wet, puddles gather in hollows. 0 = off." );
ConVar r_fidelity_wetness_wet( "r_fidelity_wetness_wet", "25", FCVAR_SERVER_CAN_EXECUTE, "Seconds of rain until the world is wet through." );
ConVar r_fidelity_wetness_dry( "r_fidelity_wetness_dry", "120", FCVAR_SERVER_CAN_EXECUTE, "Seconds without rain until it is dry again." );

#define WETNESS_CELLS		256			// cells along each side of the map
#define WETNESS_CELL		16.0f		// units per cell (the map covers 4096 x 4096)
#define WETNESS_STEP		512.0f		// the map moves in steps of this (whole cells)
#define WETNESS_TRACES		384			// cells traced per frame at most
#define WETNESS_UPLOAD		0.25f		// seconds between uploads while it changes
#define WETNESS_RANGE		8192.0f		// heights the texture holds, from the base down
#define WETNESS_SCAN		2.0f		// seconds between looks for rain

class CFidelityWetness;
static void Fidelity_RainMapBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect );

class CFidelityRainMapRegen : public ITextureRegenerator
{
public:
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect ) { Fidelity_RainMapBits( pTexture, pVTFTexture, pRect ); }
	virtual void Release() {}
};
static CFidelityRainMapRegen s_RainMapRegen;

class CFidelityWetness : public CAutoGameSystemPerFrame
{
public:
	CFidelityWetness() : CAutoGameSystemPerFrame( "CFidelityWetness" ), m_pTexture( NULL ) { Reset(); }

	virtual bool Init();
	virtual void Shutdown() { m_pTexture = NULL; }
	virtual void LevelInitPreEntity() { Reset(); }
	virtual void LevelShutdownPostEntity() { Reset(); SetParameters(); }
	virtual void Update( float frametime );

	// the map's cells (top height, known) - read by the texture regenerator
	unsigned short m_nHeight[ WETNESS_CELLS * WETNESS_CELLS ];
	unsigned char m_nKnown[ WETNESS_CELLS * WETNESS_CELLS ];	// 0 not yet traced, 1 dry (solid start), 2 known

private:
	void Reset();
	bool Raining();
	void Recentre( const Vector &vecPlayer );
	void TraceCells( const Vector &vecPlayer );
	void SetParameters();

	ITexture *m_pTexture;
	float m_flWetness;
	bool m_bRaining;
	float m_flNextScan;
	bool m_bHaveMap;
	float m_flOriginX, m_flOriginY;		// world position of the map's corner (cell 0, 0)
	float m_flSkyZ;						// just below the sky over the player (the traces start there)
	float m_flBaseZ;					// texture height 0 (WETNESS_RANGE below the sky)
	int m_nRing;						// how far out the cells are traced (from the player's cell)
	int m_nRingX, m_nRingY;				// the player's cell the rings are round
	bool m_bDirty;
	float m_flNextUpload;
};

static CFidelityWetness s_FidelityWetness;

static void Fidelity_RainMapBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
{
	const int nWidth = pVTFTexture->Width();
	const int nHeight = pVTFTexture->Height();
	const int nBytes = ImageLoader::SizeInBytes( pVTFTexture->Format() );
	unsigned char *pData = pVTFTexture->ImageData( 0, 0, 0 );
	if ( !pData || nBytes < 4 || nWidth != WETNESS_CELLS || nHeight != WETNESS_CELLS )
		return;
	// (RGBA8888 in memory: r g b a)
	for ( int i = 0; i < WETNESS_CELLS * WETNESS_CELLS; i++ )
	{
		unsigned char *pPixel = pData + i * nBytes;
		const unsigned short nValue = s_FidelityWetness.m_nHeight[ i ];
		pPixel[ 0 ] = (unsigned char)( nValue >> 8 );
		pPixel[ 1 ] = (unsigned char)( nValue & 0xFF );
		pPixel[ 2 ] = s_FidelityWetness.m_nKnown[ i ] == 2 ? 255 : 0;
		pPixel[ 3 ] = 255;
	}
}

bool CFidelityWetness::Init()
{
	m_pTexture = materials->CreateProceduralTexture( "fidelity/rain_map", TEXTURE_GROUP_OTHER, WETNESS_CELLS, WETNESS_CELLS, IMAGE_FORMAT_RGBA8888,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_POINTSAMPLE | TEXTUREFLAGS_PROCEDURAL );
	if ( m_pTexture )
	{
		m_pTexture->SetTextureRegenerator( &s_RainMapRegen );
		m_pTexture->Download();
	}
	return true;
}

void CFidelityWetness::Reset()
{
	m_flWetness = 0.0f;
	m_bRaining = false;
	m_flNextScan = 0.0f;
	m_bHaveMap = false;
	m_flOriginX = m_flOriginY = 0.0f;
	m_flSkyZ = m_flBaseZ = 0.0f;
	m_nRing = 0;
	m_nRingX = m_nRingY = 0;
	m_bDirty = false;
	m_flNextUpload = 0.0f;
	memset( m_nHeight, 0, sizeof( m_nHeight ) );
	memset( m_nKnown, 0, sizeof( m_nKnown ) );
}

// Is there rain in the map (a func_precipitation with rain, not dormant)?
bool CFidelityWetness::Raining()
{
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		ClientClass *pClass = pEntity->GetClientClass();
		if ( !pClass || Q_strcmp( pClass->m_pNetworkName, "CPrecipitation" ) || pEntity->IsDormant() )
			continue;
		RecvTable *pTable = pClass->m_pRecvTable;
		for ( int i = 0; pTable && i < pTable->GetNumProps(); i++ )
		{
			RecvProp *pProp = pTable->GetProp( i );
			if ( !pProp || Q_strcmp( pProp->GetName(), "m_nPrecipType" ) )
				continue;
			const int nType = *reinterpret_cast<const int *>( reinterpret_cast<const char *>( pEntity ) + pProp->GetOffset() );
			if ( nType == PRECIPITATION_TYPE_RAIN || nType == PRECIPITATION_TYPE_PARTICLERAIN || nType == PRECIPITATION_TYPE_PARTICLERAINSTORM )
				return true;
		}
	}
	return false;
}

// A new place for the map (round the player, whole steps): what is still inside stays
void CFidelityWetness::Recentre( const Vector &vecPlayer )
{
	const float flHalf = WETNESS_CELLS * WETNESS_CELL * 0.5f;
	const float flX = floorf( ( vecPlayer.x - flHalf ) / WETNESS_STEP ) * WETNESS_STEP;
	const float flY = floorf( ( vecPlayer.y - flHalf ) / WETNESS_STEP ) * WETNESS_STEP;
	if ( !m_bHaveMap )
	{
		// the sky over the player: the traces start just below it
		trace_t tr;
		UTIL_TraceLine( vecPlayer + Vector( 0, 0, 64 ), vecPlayer + Vector( 0, 0, MAX_TRACE_LENGTH ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 1.0f || !( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) )
			return;		// no sky above the player (inside): try again later
		m_flSkyZ = tr.endpos.z - 2.0f;
		m_flBaseZ = m_flSkyZ - WETNESS_RANGE;
	}
	else if ( flX == m_flOriginX && flY == m_flOriginY )
	{
		return;
	}
	if ( m_bHaveMap )
	{
		// keep the cells both places share
		const int nDX = (int)( ( flX - m_flOriginX ) / WETNESS_CELL );
		const int nDY = (int)( ( flY - m_flOriginY ) / WETNESS_CELL );
		static unsigned short s_nHeight[ WETNESS_CELLS * WETNESS_CELLS ];
		static unsigned char s_nKnown[ WETNESS_CELLS * WETNESS_CELLS ];
		memset( s_nKnown, 0, sizeof( s_nKnown ) );
		for ( int y = 0; y < WETNESS_CELLS; y++ )
		{
			const int nOldY = y + nDY;
			if ( nOldY < 0 || nOldY >= WETNESS_CELLS )
				continue;
			for ( int x = 0; x < WETNESS_CELLS; x++ )
			{
				const int nOldX = x + nDX;
				if ( nOldX < 0 || nOldX >= WETNESS_CELLS )
					continue;
				s_nHeight[ y * WETNESS_CELLS + x ] = m_nHeight[ nOldY * WETNESS_CELLS + nOldX ];
				s_nKnown[ y * WETNESS_CELLS + x ] = m_nKnown[ nOldY * WETNESS_CELLS + nOldX ];
			}
		}
		memcpy( m_nHeight, s_nHeight, sizeof( m_nHeight ) );
		memcpy( m_nKnown, s_nKnown, sizeof( m_nKnown ) );
	}
	m_flOriginX = flX;
	m_flOriginY = flY;
	m_bHaveMap = true;
	m_nRing = 0;
	m_bDirty = true;
}

// The cells not yet traced, ring by ring from the player's cell (only the
// ring's border is walked), WETNESS_TRACES a frame
void CFidelityWetness::TraceCells( const Vector &vecPlayer )
{
	const int nPX = clamp( (int)( ( vecPlayer.x - m_flOriginX ) / WETNESS_CELL ), 0, WETNESS_CELLS - 1 );
	const int nPY = clamp( (int)( ( vecPlayer.y - m_flOriginY ) / WETNESS_CELL ), 0, WETNESS_CELLS - 1 );
	// the player went on: the rings round his new cell go back by as much (else cells near the edge of the done ones were missed)
	m_nRing = MAX( 0, m_nRing - MAX( abs( nPX - m_nRingX ), abs( nPY - m_nRingY ) ) );
	m_nRingX = nPX;
	m_nRingY = nPY;
	int nTraces = 0;
	while ( m_nRing < WETNESS_CELLS && nTraces < WETNESS_TRACES )
	{
		const int r = m_nRing;
		const int nSide = r == 0 ? 1 : 8 * r;	// cells on the ring's border
		for ( int k = 0; k < nSide && nTraces < WETNESS_TRACES; k++ )
		{
			// walk the border: top row, right column, bottom row, left column
			int x, y;
			if ( r == 0 )					{ x = nPX; y = nPY; }
			else if ( k < 2 * r )			{ x = nPX - r + k; y = nPY - r; }
			else if ( k < 4 * r )			{ x = nPX + r; y = nPY - r + ( k - 2 * r ); }
			else if ( k < 6 * r )			{ x = nPX + r - ( k - 4 * r ); y = nPY + r; }
			else							{ x = nPX - r; y = nPY + r - ( k - 6 * r ); }
			if ( x < 0 || y < 0 || x >= WETNESS_CELLS || y >= WETNESS_CELLS )
				continue;
			const int i = y * WETNESS_CELLS + x;
			if ( m_nKnown[ i ] )
				continue;
			// down from just below the sky; walls, roofs, props - not NPCs and players
			const Vector vecStart( m_flOriginX + ( x + 0.5f ) * WETNESS_CELL, m_flOriginY + ( y + 0.5f ) * WETNESS_CELL, m_flSkyZ );
			trace_t tr;
			UTIL_TraceLine( vecStart, vecStart - Vector( 0, 0, WETNESS_RANGE ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			nTraces++;
			if ( tr.startsolid || tr.fraction >= 1.0f )
			{
				m_nKnown[ i ] = 1;	// starts in something solid, or nothing below: dry
			}
			else
			{
				m_nHeight[ i ] = (unsigned short)clamp( (int)( ( tr.endpos.z - m_flBaseZ ) * ( 65535.0f / WETNESS_RANGE ) ), 0, 65535 );
				m_nKnown[ i ] = 2;
			}
			m_bDirty = true;
		}
		if ( nTraces < WETNESS_TRACES )
			m_nRing++;	// the whole ring done (a ring stopped halfway goes on next frame)
	}
}

void CFidelityWetness::SetParameters()
{
	CMatRenderContextPtr pRenderContext( materials );
	const float flWet = r_fidelity_wetness.GetBool() && m_bHaveMap ? m_flWetness : 0.0f;
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_WETNESS, flWet );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_X, m_flOriginX );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_Y, m_flOriginY );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_SIZE, WETNESS_CELLS * WETNESS_CELL );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_BASE, m_flBaseZ );
	pRenderContext->SetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE, WETNESS_RANGE );
}

void CFidelityWetness::Update( float frametime )
{
	if ( !engine->IsInGame() || frametime <= 0.0f )
		return;
	if ( gpGlobals->curtime >= m_flNextScan )
	{
		m_flNextScan = gpGlobals->curtime + WETNESS_SCAN;
		m_bRaining = r_fidelity_wetness.GetBool() && Raining();
	}
	if ( m_bRaining )
		m_flWetness = MIN( 1.0f, m_flWetness + frametime / MAX( r_fidelity_wetness_wet.GetFloat(), 0.1f ) );
	else
		m_flWetness = MAX( 0.0f, m_flWetness - frametime / MAX( r_fidelity_wetness_dry.GetFloat(), 0.1f ) );

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( m_flWetness > 0.0f && pPlayer && m_pTexture )
	{
		const Vector vecPlayer = pPlayer->GetAbsOrigin();
		Recentre( vecPlayer );
		if ( m_bHaveMap )
			TraceCells( vecPlayer );
		if ( m_bDirty && gpGlobals->realtime >= m_flNextUpload )
		{
			m_bDirty = false;
			m_flNextUpload = gpGlobals->realtime + WETNESS_UPLOAD;
			m_pTexture->Download();
		}
	}
	SetParameters();
}
