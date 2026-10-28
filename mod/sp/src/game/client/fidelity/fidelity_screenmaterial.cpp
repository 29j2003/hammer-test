//========= mapbase-fidelity ==================================================//
//
// Purpose: Procedural full-screen materials for the fidelity pixel shaders.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_screenmaterial.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/materialsystem_config.h"
#include "materialsystem/itexture.h"
#include "rendertexture.h"
#include <KeyValues.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_depth_fullres( "r_fidelity_depth_fullres", "1", FCVAR_ARCHIVE, "SSAO and light shafts read depth at full screen resolution (one extra copy per frame). 0 = use the engine's smaller _rt_FullFrameDepth." );

#define FIDELITY_FULLRES_DEPTH_RT "_rt_FidelityDepth"

static const char *s_pTextureNames[] =
{
	"$basetexture", "$texture1", "$texture2",
};

static const char *s_pConstantNames[] =
{
	"$c0_x", "$c0_y", "$c0_z", "$c0_w",
	"$c1_x", "$c1_y", "$c1_z", "$c1_w",
	"$c2_x", "$c2_y", "$c2_z", "$c2_w",
	"$c3_x", "$c3_y", "$c3_z", "$c3_w",
};

CFidelityScreenMaterial::CFidelityScreenMaterial() : m_nVarsFrame( -1 )
{
	for ( int i = 0; i < NUM_CONSTANTS; i++ )
		m_pConstants[ i ] = NULL;
	for ( int i = 0; i < NUM_TEXTURES; i++ )
		m_pTextures[ i ] = NULL;
}

void CFidelityScreenMaterial::Init( const char *pMaterialName, const char *pPixelShader, const char *pBaseTexture, const char *pTexture1, const char *pTexture2 )
{
	Shutdown();

	// All inputs and outputs are data (depth, occlusion) or already gamma-encoded
	// colour, so every sRGB conversion is switched off.
	KeyValues *pKV = new KeyValues( "SDK_screenspace_general" );
	pKV->SetString( "$pixshader", pPixelShader );
	pKV->SetString( "$basetexture", pBaseTexture );
	pKV->SetInt( "$linearread_basetexture", 1 );
	if ( pTexture1 )
	{
		pKV->SetString( "$texture1", pTexture1 );
		pKV->SetInt( "$linearread_texture1", 1 );
	}
	if ( pTexture2 )
	{
		pKV->SetString( "$texture2", pTexture2 );
		pKV->SetInt( "$linearread_texture2", 1 );
	}
	pKV->SetInt( "$linearwrite", 1 );
	pKV->SetInt( "$ignorez", 1 );

	for ( int i = 0; i < NUM_CONSTANTS; i++ )
		pKV->SetFloat( s_pConstantNames[ i ], 0.0f );

	// FindProceduralMaterial takes ownership of the KeyValues
	IMaterial *pMaterial = materials->FindProceduralMaterial( pMaterialName, TEXTURE_GROUP_OTHER, pKV );
	m_Material.Init( pMaterial );

	if ( !IsValid() )
	{
		Warning( "[fidelity] Could not create material %s (pixel shader %s)\n", pMaterialName, pPixelShader );
		return;
	}

	m_nVarsFrame = -1;
	UpdateVars();
}

void CFidelityScreenMaterial::UpdateVars()
{
	// Reloading materials (buildcubemaps, mat_reloadallmaterials) replaces the
	// vars of every material, so pointers kept from an earlier frame may point
	// to freed memory and the shader would get the initial values back (e.g.
	// the small _rt_FullFrameDepth). Look them up again once per frame.
	if ( m_nVarsFrame == gpGlobals->framecount )
		return;
	m_nVarsFrame = gpGlobals->framecount;

	const bool bValid = IsValid();
	for ( int i = 0; i < NUM_CONSTANTS; i++ )
	{
		bool bFound = false;
		m_pConstants[ i ] = bValid ? m_Material->FindVar( s_pConstantNames[ i ], &bFound, false ) : NULL;
		if ( !bFound )
			m_pConstants[ i ] = NULL;
	}

	for ( int i = 0; i < NUM_TEXTURES; i++ )
	{
		bool bFound = false;
		m_pTextures[ i ] = bValid ? m_Material->FindVar( s_pTextureNames[ i ], &bFound, false ) : NULL;
		if ( !bFound )
			m_pTextures[ i ] = NULL;
	}
}

void CFidelityScreenMaterial::Shutdown()
{
	m_Material.Shutdown();
	for ( int i = 0; i < NUM_CONSTANTS; i++ )
		m_pConstants[ i ] = NULL;
	for ( int i = 0; i < NUM_TEXTURES; i++ )
		m_pTextures[ i ] = NULL;
}

bool CFidelityScreenMaterial::IsValid() const
{
	IMaterial *pMaterial = m_Material;
	return pMaterial && !pMaterial->IsErrorMaterial();
}

void CFidelityScreenMaterial::SetConstant( int nRegister, float x, float y, float z, float w )
{
	Assert( nRegister >= 0 && nRegister < 4 );
	UpdateVars();
	const float values[ 4 ] = { x, y, z, w };
	for ( int i = 0; i < 4; i++ )
	{
		IMaterialVar *pVar = m_pConstants[ nRegister * 4 + i ];
		if ( pVar )
			pVar->SetFloatValue( values[ i ] );
	}
}

void CFidelityScreenMaterial::SetTexture( int nSampler, ITexture *pTexture )
{
	Assert( nSampler >= 0 && nSampler < NUM_TEXTURES );
	UpdateVars();
	IMaterialVar *pVar = m_pTextures[ nSampler ];
	if ( pVar && pTexture && pVar->GetTextureValue() != pTexture )
		pVar->SetTextureValue( pTexture );
}

//-----------------------------------------------------------------------------
// Owns _rt_FidelityDepth, the full-size depth copy shared by SSAO and the
// light shafts.
//-----------------------------------------------------------------------------
class CFidelityDepth : public CAutoGameSystem
{
public:
	CFidelityDepth() : CAutoGameSystem( "CFidelityDepth" ) {}

	virtual bool Init()
	{
		if ( !g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_b() )
			return true;

		// RGBA8888 keeps the alpha channel (the frame buffer copy may not).
		materials->BeginRenderTargetAllocation();
		m_Texture.InitRenderTarget( 1, 1, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, false, FIDELITY_FULLRES_DEPTH_RT );
		materials->EndRenderTargetAllocation();
		return true;
	}

	virtual void Shutdown()
	{
		m_Texture.Shutdown();
	}

	CTextureReference m_Texture;
};

static CFidelityDepth s_FidelityDepth;

// Which depth the passes used last, for r_fidelity_info
static const char *s_pDepthSource = "none yet (SSAO and light shafts off?)";

ITexture *Fidelity_UpdateDepthTexture( int x, int y, int w, int h )
{
	ITexture *pEngineDepth = GetFullFrameDepthTexture();
	ITexture *pDepth = s_FidelityDepth.m_Texture;
	if ( !r_fidelity_depth_fullres.GetBool() )
	{
		s_pDepthSource = "_rt_FullFrameDepth (r_fidelity_depth_fullres 0)";
		return pEngineDepth;
	}
	if ( !pDepth || pDepth->IsError() )
	{
		s_pDepthSource = "_rt_FullFrameDepth (" FIDELITY_FULLRES_DEPTH_RT " missing)";
		return pEngineDepth;
	}

	CMatRenderContextPtr pRenderContext( materials );
	int nTargetW = 0, nTargetH = 0;
	pRenderContext->GetRenderTargetDimensions( nTargetW, nTargetH );

	// Same size as the frame buffer copy, and the copy must not be scaled.
	ITexture *pFrame = GetFullFrameFrameBufferTexture( 0 );
	const int nTexW = pDepth->GetActualWidth();
	const int nTexH = pDepth->GetActualHeight();
	if ( nTexW != pFrame->GetActualWidth() || nTexH != pFrame->GetActualHeight() || nTargetW > nTexW || nTargetH > nTexH
		|| w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > nTexW || y + h > nTexH )
	{
		s_pDepthSource = "_rt_FullFrameDepth (sizes do not match, see above)";
		return pEngineDepth;
	}

	Rect_t rect;
	rect.x = x;
	rect.y = y;
	rect.width = w;
	rect.height = h;
	pRenderContext->CopyRenderTargetToTextureEx( pDepth, 0, &rect, &rect );
	s_pDepthSource = FIDELITY_FULLRES_DEPTH_RT;
	return pDepth;
}

static bool s_bInfoPending[ 2 ] = { false, false };

CON_COMMAND( r_fidelity_info, "Prints the sizes of the render targets the fidelity passes use (for bug reports)." )
{
	s_bInfoPending[ FIDELITY_INFO_SCENE ] = true;
	s_bInfoPending[ FIDELITY_INFO_POST ] = true;
	Msg( "[fidelity] Render target info follows with the next frame (needs a loaded map).\n" );
}

static void PrintTextureSize( const char *pName, ITexture *pTexture )
{
	if ( !pTexture || pTexture->IsError() )
		Msg( "  %-22s missing\n", pName );
	else
		Msg( "  %-22s %d x %d (format %d)\n", pName, pTexture->GetActualWidth(), pTexture->GetActualHeight(), (int)pTexture->GetImageFormat() );
}

void Fidelity_PrintTargetInfo( int nStage, int x, int y, int w, int h )
{
	if ( nStage < 0 || nStage > 1 || !s_bInfoPending[ nStage ] )
		return;
	s_bInfoPending[ nStage ] = false;

	CMatRenderContextPtr pRenderContext( materials );
	int nTargetW = 0, nTargetH = 0, nBackW = 0, nBackH = 0;
	pRenderContext->GetRenderTargetDimensions( nTargetW, nTargetH );
	materials->GetBackBufferDimensions( nBackW, nBackH );

	Msg( "[fidelity] %s: view %d,%d %d x %d  render target %d x %d  back buffer %d x %d  MSAA %d  HDR type %d\n",
		nStage == FIDELITY_INFO_SCENE ? "3D scene (SSAO, light shafts)" : "after post processing (FXAA, grade)",
		x, y, w, h, nTargetW, nTargetH, nBackW, nBackH,
		materials->GetCurrentConfigForVideoCard().m_nAASamples, (int)g_pMaterialSystemHardwareConfig->GetHDRType() );
	PrintTextureSize( "_rt_FullFrameFB", GetFullFrameFrameBufferTexture( 0 ) );
	if ( nStage == FIDELITY_INFO_SCENE )
	{
		PrintTextureSize( "_rt_FullFrameDepth", GetFullFrameDepthTexture() );
		PrintTextureSize( FIDELITY_FULLRES_DEPTH_RT, s_FidelityDepth.m_Texture );
		Msg( "  depth used by the passes: %s\n", s_pDepthSource );
		PrintTextureSize( "_rt_FidelityAO0", materials->FindTexture( "_rt_FidelityAO0", TEXTURE_GROUP_RENDER_TARGET, false ) );
		PrintTextureSize( "_rt_FidelityRays0", materials->FindTexture( "_rt_FidelityRays0", TEXTURE_GROUP_RENDER_TARGET, false ) );
	}
}

void Fidelity_DrawScreenPass( IMaterial *pMaterial, int x, int y, int w, int h,
	int srcX, int srcY, int srcW, int srcH, int nTexWidth, int nTexHeight )
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->DrawScreenSpaceRectangle( pMaterial, x, y, w, h,
		srcX, srcY, srcX + srcW - 1, srcY + srcH - 1,
		nTexWidth, nTexHeight );
}
