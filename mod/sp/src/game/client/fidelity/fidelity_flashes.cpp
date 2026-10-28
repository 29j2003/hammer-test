//========= mapbase-fidelity ==================================================//
//
// Purpose: Muzzle flashes and explosions as short projected-texture lights
//          (r_fidelity_flashes, on by default) - Entropy : Zero 2's idea
//          (its branch "projtex-muzzleflashes", 07.10.2026), our own code.
//
// The engine's dynamic lights (fidelity_glow.cpp) light the world through
// its lightmaps, flat: no bump map, no specular, blotchy. A projected
// texture is drawn like the flashlight - per pixel, with the surfaces' bump
// maps and specular, on the world and on models (Mapbase's flashlight path,
// our Fidelity_Parallax has its own flashlight pass). So for its short life
// a flash also gets such a light:
//   - a muzzle flash: from just behind the muzzle along it (the player's: the
//     view; an NPC's: its weapon's "muzzle" attachment, else the way it
//     faces), PROJECTED_FLASH_FOV wide, PROJECTED_FLASH_SECONDS, fading; the engine light of the
//     flash stays, dimmed (it lights all ways, also behind),
//   - an explosion: from above it down onto the ground and the walls round
//     it, PROJECTED_EXPLOSION_FOV wide, PROJECTED_EXPLOSION_SECONDS.
// No shadows (each shadow map is a scene drawn again). At most PROJECTED_FLASH_SLOTS
// at once: the player's own flash takes the oldest slot, others only a free
// one; NPCs further than PROJECTED_FLASH_NPC_DISTANCE make none. The spot texture is a
// soft round spot made here (Half-Life 2 has only the flashlight's ring).
//
// Cost: each light draws what it shines on once more (as the flashlight),
// for a few frames.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_flashes.h"
#include "igamesystem.h"
#include "iclientshadowmgr.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_flashes( "r_fidelity_flashes", "1", FCVAR_ARCHIVE, "Muzzle flashes and explosions also light like the flashlight (bump maps, specular), for a moment. 0 = off." );
ConVar r_fidelity_flashes_brightness( "r_fidelity_flashes_brightness", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Brightness of these lights." );

#define PROJECTED_FLASH_SLOTS				2		// lights at once
#define PROJECTED_FLASH_SECONDS			0.07f	// a muzzle flash's light
#define PROJECTED_FLASH_FOV				150.0f	// degrees
#define PROJECTED_FLASH_BACK				12.0f	// units behind the muzzle (the light also reaches beside the gun)
#define PROJECTED_FLASH_FAR				320.0f
#define PROJECTED_FLASH_NEAR_FULL		48.0f	// full brightness up to this distance, then 1 / distance squared
												// (linear to 512 lit walls across the yard - Julius 26.10.2026)
#define PROJECTED_FLASH_BRIGHTNESS		0.35f	// (1.6 lit up everything - Julius 25.10.2026: "massiv hell"; 0.6 still "sehr harsch auf die Augen" - 26.10.2026)
#define PROJECTED_FLASH_NPC_DISTANCE		1500.0f
#define PROJECTED_EXPLOSION_SECONDS		0.4f
#define PROJECTED_EXPLOSION_FOV			160.0f
#define PROJECTED_EXPLOSION_HEIGHT		72.0f	// above the explosion
#define PROJECTED_EXPLOSION_FAR			900.0f
#define PROJECTED_EXPLOSION_LINEAR		180.0f
#define PROJECTED_EXPLOSION_BRIGHTNESS	4.0f
#define PROJECTED_SPOT_SIZE				64

//-----------------------------------------------------------------------------
// The spot texture: bright in the middle, softly dark towards the edge
//-----------------------------------------------------------------------------
class CFidelityFlashSpot : public ITextureRegenerator
{
public:
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		const int nWidth = pVTFTexture->Width();
		const int nHeight = pVTFTexture->Height();
		const int nBytes = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		unsigned char *pData = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pData || nBytes < 3 )
			return;
		for ( int y = 0; y < nHeight; y++ )
		{
			for ( int x = 0; x < nWidth; x++ )
			{
				const float u = ( ( x + 0.5f ) / nWidth ) * 2.0f - 1.0f;
				const float v = ( ( y + 0.5f ) / nHeight ) * 2.0f - 1.0f;
				const float t = clamp( 1.0f - ( u * u + v * v ), 0.0f, 1.0f );
				const float flLight = t * t;	// round, no edge to see
				const unsigned char nValue = (unsigned char)clamp( (int)( powf( flLight, 1.0f / 2.2f ) * 255.0f + 0.5f ), 0, 255 );
				unsigned char *pPixel = pData + ( y * nWidth + x ) * nBytes;
				pPixel[ 0 ] = pPixel[ 1 ] = pPixel[ 2 ] = nValue;
				if ( nBytes >= 4 )
					pPixel[ 3 ] = 255;
			}
		}
	}
	virtual void Release() {}
};

static CFidelityFlashSpot s_FlashSpot;

struct FidelityFlash_t
{
	ClientShadowHandle_t m_hLight;
	float m_flStart;
	float m_flSeconds;		// 0 = free
	FlashlightState_t m_State;
	Vector m_vecColor;		// at full strength
};

class CFidelityFlashes : public CAutoGameSystemPerFrame
{
public:
	CFidelityFlashes() : CAutoGameSystemPerFrame( "CFidelityFlashes" ), m_pSpot( NULL )
	{
		for ( int i = 0; i < PROJECTED_FLASH_SLOTS; i++ )
		{
			m_Flashes[ i ].m_hLight = CLIENTSHADOW_INVALID_HANDLE;
			m_Flashes[ i ].m_flSeconds = 0.0f;
		}
	}

	virtual void LevelShutdownPreEntity() { RemoveAll(); }
	virtual void Shutdown() { RemoveAll(); m_pSpot = NULL; }
	virtual void Update( float frametime );

	bool Start( const Vector &vecOrigin, const Vector &vecDir, float flFOV, float flFar, float flLinear, float flQuadratic, const Vector &vecColor, float flSeconds, bool bTakeOldest );

private:
	void RemoveAll();
	void Remove( FidelityFlash_t &flash );
	ITexture *Spot();

	FidelityFlash_t m_Flashes[ PROJECTED_FLASH_SLOTS ];
	ITexture *m_pSpot;
};

static CFidelityFlashes s_FidelityFlashes;

ITexture *CFidelityFlashes::Spot()
{
	if ( !m_pSpot )
	{
		m_pSpot = materials->CreateProceduralTexture( "fidelity/flash_spot", TEXTURE_GROUP_OTHER, PROJECTED_SPOT_SIZE, PROJECTED_SPOT_SIZE, IMAGE_FORMAT_RGBA8888,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL );
		if ( m_pSpot )
		{
			m_pSpot->SetTextureRegenerator( &s_FlashSpot );
			m_pSpot->Download();
		}
	}
	return m_pSpot;
}

void CFidelityFlashes::Remove( FidelityFlash_t &flash )
{
	if ( flash.m_hLight != CLIENTSHADOW_INVALID_HANDLE )
		g_pClientShadowMgr->DestroyFlashlight( flash.m_hLight );
	flash.m_hLight = CLIENTSHADOW_INVALID_HANDLE;
	flash.m_flSeconds = 0.0f;
}

void CFidelityFlashes::RemoveAll()
{
	for ( int i = 0; i < PROJECTED_FLASH_SLOTS; i++ )
		Remove( m_Flashes[ i ] );
}

bool CFidelityFlashes::Start( const Vector &vecOrigin, const Vector &vecDir, float flFOV, float flFar, float flLinear, float flQuadratic, const Vector &vecColor, float flSeconds, bool bTakeOldest )
{
	if ( !r_fidelity_flashes.GetBool() || !g_pClientShadowMgr || !Spot() )
		return false;
	// a free slot, else (the player's own flash) the oldest
	int nSlot = -1;
	for ( int i = 0; i < PROJECTED_FLASH_SLOTS && nSlot < 0; i++ )
	{
		if ( m_Flashes[ i ].m_flSeconds <= 0.0f )
			nSlot = i;
	}
	if ( nSlot < 0 && bTakeOldest )
	{
		nSlot = 0;
		for ( int i = 1; i < PROJECTED_FLASH_SLOTS; i++ )
		{
			if ( m_Flashes[ i ].m_flStart < m_Flashes[ nSlot ].m_flStart )
				nSlot = i;
		}
	}
	if ( nSlot < 0 )
		return false;

	FidelityFlash_t &flash = m_Flashes[ nSlot ];
	FlashlightState_t &state = flash.m_State;
	state = FlashlightState_t();
	Vector vecForward = vecDir, vecRight, vecUp;
	VectorNormalize( vecForward );
	VectorVectors( vecForward, vecRight, vecUp );	// (right as AngleVectors gives it, as the flashlight)
	BasisToQuaternion( vecForward, vecRight, vecUp, state.m_quatOrientation );
	state.m_vecLightOrigin = vecOrigin;
	state.m_fHorizontalFOVDegrees = flFOV;
	state.m_fVerticalFOVDegrees = flFOV;
	state.m_fConstantAtten = 0.0f;
	state.m_fLinearAtten = flLinear;
	state.m_fQuadraticAtten = flQuadratic;
	state.m_NearZ = 4.0f;
	state.m_FarZ = flFar;
	state.m_bEnableShadows = false;
	state.m_pSpotlightTexture = Spot();
	state.m_nSpotlightTextureFrame = 0;
	flash.m_vecColor = vecColor * MAX( r_fidelity_flashes_brightness.GetFloat(), 0.0f );
	flash.m_flStart = gpGlobals->curtime;
	flash.m_flSeconds = flSeconds;
	state.m_Color[ 0 ] = flash.m_vecColor.x;
	state.m_Color[ 1 ] = flash.m_vecColor.y;
	state.m_Color[ 2 ] = flash.m_vecColor.z;
	state.m_Color[ 3 ] = 0.0f;	// no ambient
	if ( flash.m_hLight == CLIENTSHADOW_INVALID_HANDLE )
		flash.m_hLight = g_pClientShadowMgr->CreateFlashlight( state );
	else
		g_pClientShadowMgr->UpdateFlashlightState( flash.m_hLight, state );
	if ( flash.m_hLight == CLIENTSHADOW_INVALID_HANDLE )
	{
		flash.m_flSeconds = 0.0f;
		return false;
	}
	g_pClientShadowMgr->UpdateProjectedTexture( flash.m_hLight, true );
	return true;
}

void CFidelityFlashes::Update( float frametime )
{
	for ( int i = 0; i < PROJECTED_FLASH_SLOTS; i++ )
	{
		FidelityFlash_t &flash = m_Flashes[ i ];
		if ( flash.m_flSeconds <= 0.0f )
			continue;
		const float t = ( gpGlobals->curtime - flash.m_flStart ) / flash.m_flSeconds;
		if ( t >= 1.0f || t < 0.0f || !r_fidelity_flashes.GetBool() )
		{
			Remove( flash );
			continue;
		}
		// bright at once, then fading
		const float flFade = ( 1.0f - t ) * ( 1.0f - t );
		flash.m_State.m_Color[ 0 ] = flash.m_vecColor.x * flFade;
		flash.m_State.m_Color[ 1 ] = flash.m_vecColor.y * flFade;
		flash.m_State.m_Color[ 2 ] = flash.m_vecColor.z * flFade;
		g_pClientShadowMgr->UpdateFlashlightState( flash.m_hLight, flash.m_State );
		g_pClientShadowMgr->UpdateProjectedTexture( flash.m_hLight, true );
	}
}

bool Fidelity_ProjectedMuzzleFlash( const Vector &vecOrigin, const Vector &vecColor, ClientEntityHandle_t hEntity, bool bFirstPerson )
{
	if ( !r_fidelity_flashes.GetBool() )
		return false;
	Vector vecDir;
	if ( bFirstPerson )
	{
		vecDir = MainViewForward();
	}
	else
	{
		if ( ( vecOrigin - MainViewOrigin() ).LengthSqr() > PROJECTED_FLASH_NPC_DISTANCE * PROJECTED_FLASH_NPC_DISTANCE )
			return false;
		C_BaseEntity *pEntity = ClientEntityList().GetBaseEntityFromHandle( hEntity );
		if ( !pEntity )
			return false;
		C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
		const int nMuzzle = pAnimating ? pAnimating->LookupAttachment( "muzzle" ) : 0;
		Vector vecMuzzle;
		QAngle angMuzzle;
		if ( nMuzzle > 0 && pAnimating->GetAttachment( nMuzzle, vecMuzzle, angMuzzle ) )
			AngleVectors( angMuzzle, &vecDir );
		else
			AngleVectors( pEntity->GetAbsAngles(), &vecDir );
	}
	if ( VectorNormalize( vecDir ) < 0.5f )
		return false;
	return s_FidelityFlashes.Start( vecOrigin - vecDir * PROJECTED_FLASH_BACK, vecDir, PROJECTED_FLASH_FOV, PROJECTED_FLASH_FAR, 0.0f,
		PROJECTED_FLASH_NEAR_FULL * PROJECTED_FLASH_NEAR_FULL, vecColor * PROJECTED_FLASH_BRIGHTNESS, PROJECTED_FLASH_SECONDS, bFirstPerson );
}

void Fidelity_ProjectedExplosion( const Vector &vecOrigin )
{
	if ( !r_fidelity_flashes.GetBool() )
		return;
	// from above it, but under a ceiling
	trace_t tr;
	UTIL_TraceLine( vecOrigin, vecOrigin + Vector( 0, 0, PROJECTED_EXPLOSION_HEIGHT ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	const Vector vecFrom = vecOrigin + Vector( 0, 0, PROJECTED_EXPLOSION_HEIGHT * tr.fraction * 0.9f );
	s_FidelityFlashes.Start( vecFrom, Vector( 0, 0, -1 ), PROJECTED_EXPLOSION_FOV, PROJECTED_EXPLOSION_FAR, PROJECTED_EXPLOSION_LINEAR, 0.0f,
		Vector( 1.0f, 0.68f, 0.32f ) * PROJECTED_EXPLOSION_BRIGHTNESS, PROJECTED_EXPLOSION_SECONDS, true );
}
