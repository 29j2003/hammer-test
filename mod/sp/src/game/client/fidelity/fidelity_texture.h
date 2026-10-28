//========= mapbase-fidelity ==================================================//
//
// Purpose: Reads a small version of a texture (.vtf) on the CPU, to see where
//          a model glows (selfillum mask) or holds liquid (its colours).
//
//=============================================================================//

#ifndef FIDELITY_TEXTURE_H
#define FIDELITY_TEXTURE_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"

//-----------------------------------------------------------------------------
// A texture's pixels as RGBA, 0..255 as stored (gamma), at most a few dozen
// texels wide (one of its mip levels).
//-----------------------------------------------------------------------------
class CFidelityImage
{
public:
	CFidelityImage() : m_nWidth( 0 ), m_nHeight( 0 ) {}

	bool IsValid() const { return m_nWidth > 0 && m_nHeight > 0; }
	int Width() const { return m_nWidth; }
	int Height() const { return m_nHeight; }

	// Texel at texture coordinates u, v (wrapping, nearest), rgba 0..1
	void Sample( float u, float v, float *pRGBA ) const;

	// Part of the texels whose alpha (bAlpha) or brightest colour channel is above flThreshold
	float Coverage( bool bAlpha, float flThreshold ) const;

	int m_nWidth;
	int m_nHeight;
	CUtlVector< unsigned char > m_Pixels;	// RGBA8888, row by row
};

//-----------------------------------------------------------------------------
// The texture materials/<pszTexture>.vtf (from the mod or the game's archives),
// decoded at the largest mip level not wider or higher than nMaxSize. Read once
// per texture and kept until the level ends; NULL if it can not be read (cube
// maps, odd formats, no mip level that small).
//-----------------------------------------------------------------------------
const CFidelityImage *Fidelity_TextureImage( const char *pszTexture, int nMaxSize = 64 );

// Frees all read textures (level end): pointers from Fidelity_TextureImage
// are invalid afterwards.
void Fidelity_ForgetTextureImages();

#endif // FIDELITY_TEXTURE_H
