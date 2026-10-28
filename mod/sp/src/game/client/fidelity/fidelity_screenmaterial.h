//========= mapbase-fidelity ==================================================//
//
// Purpose: Small helper around a procedural SDK_screenspace_general material
//          that runs one of the fidelity pixel shaders. The sixteen shader
//          constants c0..c3 map to the material vars $c0_x ... $c3_w.
//
//=============================================================================//

#ifndef FIDELITY_SCREENMATERIAL_H
#define FIDELITY_SCREENMATERIAL_H
#ifdef _WIN32
#pragma once
#endif

#include "materialsystem/MaterialSystemUtil.h"

class IMaterialVar;
class ITexture;

//-----------------------------------------------------------------------------
// A full-screen material for one fidelity pixel shader.
//-----------------------------------------------------------------------------
class CFidelityScreenMaterial
{
public:
	CFidelityScreenMaterial();

	// pTexture1 and pTexture2 may be NULL. Textures are render target names such as "_rt_FullFrameFB".
	void Init( const char *pMaterialName, const char *pPixelShader, const char *pBaseTexture, const char *pTexture1 = NULL, const char *pTexture2 = NULL );
	void Shutdown();

	bool IsValid() const;
	IMaterial *Get() { return m_Material; }

	// Sets shader constant register cRegister (0..3) to x, y, z, w.
	void SetConstant( int nRegister, float x, float y, float z, float w );

	// Replaces the texture of sampler nSampler (0 = $basetexture, 1 = $texture1,
	// 2 = $texture2) for the next draws.
	void SetTexture( int nSampler, ITexture *pTexture );

private:
	enum { NUM_CONSTANTS = 16, NUM_TEXTURES = 3 };

	// Looks up the material vars again, at most once per frame (see the .cpp).
	void UpdateVars();

	CMaterialReference m_Material;
	int m_nVarsFrame;
	IMaterialVar *m_pConstants[ NUM_CONSTANTS ];
	IMaterialVar *m_pTextures[ NUM_TEXTURES ];
};

//-----------------------------------------------------------------------------
// Draws pMaterial over the whole of the current render target region
// (x, y, w, h). Texture coordinates address the source texture rectangle
// (srcX, srcY, srcW, srcH) of a texture sized nTexWidth x nTexHeight.
//-----------------------------------------------------------------------------
void Fidelity_DrawScreenPass( IMaterial *pMaterial, int x, int y, int w, int h,
	int srcX, int srcY, int srcW, int srcH, int nTexWidth, int nTexHeight );

//-----------------------------------------------------------------------------
// Copies the linear depth in destination alpha of the current render target
// (region x, y, w, h) to _rt_FidelityDepth, a texture as large as the screen,
// and returns it. The engine's _rt_FullFrameDepth can be much smaller than the
// screen (1024 x 1024 at 2560 x 1440), which made SSAO blocky. Falls back to
// _rt_FullFrameDepth when r_fidelity_depth_fullres is 0 or the sizes do not
// match the frame buffer copy (the passes read both with the same uv).
// Call it in the main view after the opaque scene, before any pass draws.
//-----------------------------------------------------------------------------
ITexture *Fidelity_UpdateDepthTexture( int x, int y, int w, int h );

// Depth texture the SSAO and light shaft materials are made with; their
// Render() binds the one Fidelity_UpdateDepthTexture returned.
#define FIDELITY_DEPTH_RT "_rt_FullFrameDepth"

//-----------------------------------------------------------------------------
// r_fidelity_info: the next frame prints the sizes of the render targets the
// passes work with, once in the 3D scene (FIDELITY_INFO_SCENE, SSAO hook) and
// once after post processing (FIDELITY_INFO_POST, grade pass). For bug reports.
//-----------------------------------------------------------------------------
enum
{
	FIDELITY_INFO_SCENE = 0,
	FIDELITY_INFO_POST,
};
void Fidelity_PrintTargetInfo( int nStage, int x, int y, int w, int h );

#endif // FIDELITY_SCREENMATERIAL_H
