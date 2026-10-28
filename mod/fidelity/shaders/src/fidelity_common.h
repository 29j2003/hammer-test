//========= mapbase-fidelity ==================================================//
//
// Shared helpers for the fidelity shaders.
//
// The screen-space shaders are combo-less ps_2_b pixel shaders driven through
// Mapbase's SDK_screenspace_general shader: the C++ side fills the sixteen
// float constants c0..c3 ($c0_x ... $c3_w) and binds up to four textures
// ($basetexture, $texture1..3 -> s0..s3). The model and sky shaders
// (fidelity_liquid, fidelity_sky; ps_3_0) belong to our own shader DLL.
//
//=============================================================================//

#ifndef FIDELITY_COMMON_H
#define FIDELITY_COMMON_H

// The engine writes linear depth into the frame buffer's destination alpha
// (see DepthToDestAlpha in common_ps_fxc.h). Projected Z is divided by a fixed
// range (512 units by default), so alpha covers roughly the first 512 units in
// front of the camera with 8 bit precision.
//
//   viewDepth = alpha * depthScale + zNear,  depthScale = range * (zFar - zNear) / zFar
//
float FidelityViewDepth( float flDestAlpha, float flDepthScale, float flNear )
{
	return flDestAlpha * flDepthScale + flNear;
}

// Per-pixel jitter that repeats every 4 x 4 pixels: two different orderings
// of the 16 values (k + 0.5) / 16. A blur that weighs every pixel of a 4 pixel
// period equally (fidelity_ssao_blur_ps2x.fxc) removes it completely on smooth
// surfaces, so no grain is left. vPixel is in pixels, at pixel centres (n + 0.5).
float2 FidelityTileNoise( float2 vPixel )
{
	float2 vCell = floor( frac( vPixel * 0.25 ) * 4.0 );
	float flIndex = vCell.x + 4.0 * vCell.y;
	return frac( flIndex * float2( 5.0 / 16.0, 11.0 / 16.0 ) + 1.0 / 32.0 );
}

float FidelityLuminance( float3 vColor )
{
	return dot( vColor, float3( 0.2126, 0.7152, 0.0722 ) );
}

// Pseudo random 0..1 per point (noise lattices, bubbles). No sin(): D3D9
// reduces its argument with frc, so for the large inputs of a drifting sky or
// rising bubbles the classic sin hash falls into a few repeating values.
float FidelityHash( float2 p )
{
	float3 p3 = frac( p.xyx * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return frac( ( p3.x + p3.y ) * p3.z );
}

#endif // FIDELITY_COMMON_H
