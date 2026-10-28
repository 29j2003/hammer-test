//========= mapbase-fidelity ==================================================//
//
// Purpose: Reads a small version of a texture (.vtf) on the CPU.
//
// The glow lights need to know which parts of a model really glow (the
// selfillum mask: the base texture's alpha or $selfillummask), the liquid
// which parts of a container hold liquid (their colour). Materials only give
// a 16x16 colour thumbnail without alpha, so the .vtf is read here: the
// header, then just one small mip level (at most nMaxSize texels), decoded to
// RGBA with the SDK's ImageLoader (DXT1/3/5 and the plain formats).
//
// File layout (VTF 7.0 - 7.5): header (headerSize bytes; 7.3+ with a list of
// resources, the image data is resource 0x30), then the low-res thumbnail,
// then the mip levels from the smallest to the largest, each with all frames.
// Cube maps and volume textures are not read.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_texture.h"
#include "filesystem.h"
#include "bitmap/imageformat.h"
#include "utlstring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define VTF_FLAG_ENVMAP		0x4000

void CFidelityImage::Sample( float u, float v, float *pRGBA ) const
{
	if ( !IsValid() )
	{
		pRGBA[ 0 ] = pRGBA[ 1 ] = pRGBA[ 2 ] = pRGBA[ 3 ] = 0.0f;
		return;
	}
	int x = (int)floorf( ( u - floorf( u ) ) * m_nWidth );
	int y = (int)floorf( ( v - floorf( v ) ) * m_nHeight );
	x = clamp( x, 0, m_nWidth - 1 );
	y = clamp( y, 0, m_nHeight - 1 );
	const unsigned char *pTexel = m_Pixels.Base() + ( y * m_nWidth + x ) * 4;
	for ( int i = 0; i < 4; i++ )
		pRGBA[ i ] = pTexel[ i ] / 255.0f;
}

float CFidelityImage::Coverage( bool bAlpha, float flThreshold ) const
{
	if ( !IsValid() )
		return 0.0f;
	const int nThreshold = (int)( flThreshold * 255.0f );
	int nCount = 0;
	for ( int i = 0; i < m_nWidth * m_nHeight; i++ )
	{
		const unsigned char *pTexel = m_Pixels.Base() + i * 4;
		const int nValue = bAlpha ? pTexel[ 3 ] : MAX( pTexel[ 0 ], MAX( pTexel[ 1 ], pTexel[ 2 ] ) );
		if ( nValue > nThreshold )
			nCount++;
	}
	return (float)nCount / ( m_nWidth * m_nHeight );
}

static int Fidelity_ReadInt( const unsigned char *pData, int nOffset )
{
	return pData[ nOffset ] | ( pData[ nOffset + 1 ] << 8 ) | ( pData[ nOffset + 2 ] << 16 ) | ( pData[ nOffset + 3 ] << 24 );
}

static int Fidelity_ReadShort( const unsigned char *pData, int nOffset )
{
	return pData[ nOffset ] | ( pData[ nOffset + 1 ] << 8 );
}

static bool Fidelity_LoadImage( const char *pszTexture, int nMaxSize, CFidelityImage &image )
{
	char szPath[ MAX_PATH ];
	Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vtf", pszTexture );
	Q_FixSlashes( szPath, '/' );
	FileHandle_t hFile = filesystem->Open( szPath, "rb", "GAME" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return false;

	// The file may come from a downloaded map: every size and offset in it is
	// checked before use, and all sizes stay small (at most nMaxSize texels).
	bool bResult = false;
	unsigned char header[ 80 + 32 * 8 ] = {};
	const int nFileSize = (int)filesystem->Size( hFile );
	const int nHeaderRead = filesystem->Read( header, MIN( (int)sizeof( header ), nFileSize ), hFile );
	const int nMinor = nHeaderRead >= 12 ? Fidelity_ReadInt( header, 8 ) : -1;
	if ( nHeaderRead >= ( nMinor >= 3 ? 80 : 65 ) && !Q_memcmp( header, "VTF", 3 ) && Fidelity_ReadInt( header, 4 ) == 7 )
	{
		const int nHeaderSize = Fidelity_ReadInt( header, 12 );
		const int nWidth = Fidelity_ReadShort( header, 16 );
		const int nHeight = Fidelity_ReadShort( header, 18 );
		const int nFlags = Fidelity_ReadInt( header, 20 );
		const int nFrames = MAX( Fidelity_ReadShort( header, 24 ), 1 );
		const ImageFormat format = (ImageFormat)Fidelity_ReadInt( header, 52 );
		const int nMips = header[ 56 ];
		const ImageFormat lowFormat = (ImageFormat)Fidelity_ReadInt( header, 57 );
		const int nLowWidth = header[ 61 ];
		const int nLowHeight = header[ 62 ];
		const int nDepth = nMinor >= 2 ? MAX( Fidelity_ReadShort( header, 63 ), 1 ) : 1;

		// Where the mip levels start
		int nDataStart = -1;
		if ( nMinor >= 3 )
		{
			const int nResources = Fidelity_ReadInt( header, 68 );
			for ( int i = 0; i < nResources && 80 + i * 8 + 8 <= nHeaderRead; i++ )
			{
				const unsigned char *pEntry = header + 80 + i * 8;
				if ( pEntry[ 0 ] == 0x30 && pEntry[ 1 ] == 0 && pEntry[ 2 ] == 0 )
					nDataStart = Fidelity_ReadInt( pEntry, 4 );
			}
		}
		else
		{
			const int nLowSize = ( lowFormat >= 0 && lowFormat < NUM_IMAGE_FORMATS && nLowWidth > 0 && nLowHeight > 0 ) ? ImageLoader::GetMemRequired( nLowWidth, nLowHeight, 1, lowFormat, false ) : 0;
			nDataStart = nHeaderSize + nLowSize;
		}

		// 65535 texels have at most 17 mip levels (more: a broken file, and the shifts below would overflow)
		if ( nDataStart > 0 && !( nFlags & VTF_FLAG_ENVMAP ) && nDepth == 1 && nMips > 0 && nMips <= 17 && nWidth > 0 && nHeight > 0 && format >= 0 && format < NUM_IMAGE_FORMATS )
		{
			// The largest mip level that fits in nMaxSize (DXT needs at least 4 texels)
			int nMip = 0;
			while ( nMip + 1 < nMips && ( ( nWidth >> nMip ) > nMaxSize || ( nHeight >> nMip ) > nMaxSize ) )
				nMip++;
			const int nMipWidth = MAX( nWidth >> nMip, 1 );
			const int nMipHeight = MAX( nHeight >> nMip, 1 );

			// Smaller levels come first, every level with all frames (64 bit: a
			// broken header must not wrap the offset)
			int64 nOffset = nDataStart;
			for ( int nLevel = nMips - 1; nLevel > nMip; nLevel-- )
				nOffset += (int64)nFrames * ImageLoader::GetMemRequired( MAX( nWidth >> nLevel, 1 ), MAX( nHeight >> nLevel, 1 ), 1, format, false );
			// No level small enough (a texture without mips): not read
			const int nSize = ( nMipWidth <= nMaxSize && nMipHeight <= nMaxSize ) ? ImageLoader::GetMemRequired( nMipWidth, nMipHeight, 1, format, false ) : 0;

			if ( nSize > 0 && nOffset + nSize <= nFileSize )
			{
				CUtlVector< unsigned char > data;
				data.SetCount( nSize );
				filesystem->Seek( hFile, (int)nOffset, FILESYSTEM_SEEK_HEAD );
				if ( filesystem->Read( data.Base(), nSize, hFile ) == nSize )
				{
					image.m_Pixels.SetCount( nMipWidth * nMipHeight * 4 );
					if ( ImageLoader::ConvertImageFormat( data.Base(), format, image.m_Pixels.Base(), IMAGE_FORMAT_RGBA8888, nMipWidth, nMipHeight ) )
					{
						image.m_nWidth = nMipWidth;
						image.m_nHeight = nMipHeight;
						bResult = true;
					}
				}
			}
		}
	}
	filesystem->Close( hFile );
	return bResult;
}

// Read textures, kept until the level ends (a few KB each; a map may pack
// its own texture under a stock name)
struct FidelityImageEntry_t
{
	CUtlString m_Name;
	int m_nMaxSize;
	bool m_bValid;
	CFidelityImage m_Image;
};
static CUtlVector< FidelityImageEntry_t * > s_Images;

const CFidelityImage *Fidelity_TextureImage( const char *pszTexture, int nMaxSize )
{
	if ( !pszTexture || !pszTexture[ 0 ] )
		return NULL;
	for ( int i = 0; i < s_Images.Count(); i++ )
	{
		if ( s_Images[ i ]->m_nMaxSize == nMaxSize && !Q_stricmp( s_Images[ i ]->m_Name.Get(), pszTexture ) )
			return s_Images[ i ]->m_bValid ? &s_Images[ i ]->m_Image : NULL;
	}
	FidelityImageEntry_t *pEntry = new FidelityImageEntry_t;
	pEntry->m_Name = pszTexture;
	pEntry->m_nMaxSize = nMaxSize;
	pEntry->m_bValid = Fidelity_LoadImage( pszTexture, nMaxSize, pEntry->m_Image );
	s_Images.AddToTail( pEntry );
	return pEntry->m_bValid ? &pEntry->m_Image : NULL;
}

void Fidelity_ForgetTextureImages()
{
	s_Images.PurgeAndDeleteElements();
}
