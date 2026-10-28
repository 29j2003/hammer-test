//========= mapbase-fidelity ==================================================//
//
// Parallax occlusion mapping shared by Fidelity_Parallax's two passes (the
// lightmapped one, fidelity_parallax_ps30.fxc, and the one for the
// flashlight, projected textures and the sun's shadows,
// fidelity_parallax_flashlight_ps30.fxc): both must move the texture by
// exactly the same amount, or the light lies on a flat copy of the wall
// (Julius: "bei Licht bricht die Illusion").
//
// FidelityParallaxAxes: the texture's axes in the world from the
//   screen-space derivatives of position and texture coordinate (no vertex
//   tangents needed; they always fit the texture). Call it outside any
//   branch (derivatives need all pixels of a quad).
// FidelityParallaxShift: walks into the height map (white = top) along the
//   view, 8 to 32 steps (more at flat angles), then interpolates the last
//   step; moves both texture coordinates, returns the height there.
//   Reference plane (FidelityParallaxPlane): which height lies on the
//   polygon. With the top on it (1, the classic way) every ray moves the
//   texture away from the viewer - where a floor meets a wall (and in every
//   inside corner) that is towards and past the shared edge: the floor's
//   joints ran on under the wall (Julius). With the relief standing partly
//   out of the polygon the walk starts above it, the texture moves towards
//   the viewer as well, and past a far edge only by r x the shift. (As
//   Unreal's "Reference Plane"; Strata's $parallaxcenter.) The shift is also
//   held to a few world units (narrow faces pulled texture from beyond their
//   edges).
// FidelityParallaxHeightNormal: the relief's normal from the height map
//   (walls without a bump map).
// FidelityParallaxShadow: from that point towards a light, how much of the
//   relief is in the way (soft, 1 = lit).
//
// Blended surfaces (WorldVertexTransition: displacements painted from one
// texture to another, FIDELITY_BLEND = 1 before this file is included): the
// second layer has a height map of its own, often in other texture
// coordinates. FidelityParallaxBlend holds where it is and how a step in the
// first layer's coordinates moves in the second's (from the screen
// derivatives of both: exact for any scale, offset or rotation between them).
// The relief walked is the two height maps blended by height
// (FidelityHeightBlend): near the painted transition the higher layer shows
// through - grass between cobbles, sand in the joints - instead of both
// fading into each other; where only one layer is painted it is that one.
// Without FIDELITY_BLEND the second sampler is never read.
//
//=============================================================================//

#ifndef FIDELITY_PARALLAX_COMMON_H
#define FIDELITY_PARALLAX_COMMON_H

#ifndef FIDELITY_BLEND
#define FIDELITY_BLEND 0
#endif

#define PARALLAX_MIN_STEPS		8
#define PARALLAX_MAX_STEPS		32
#define PARALLAX_SHADOW_STEPS	8

struct FidelityParallaxFrame
{
	float3 T;			// along the bump map's u (world, not unit: scaled as B)
	float3 B;			// along its v
	float3 N;			// the surface's normal
	float2 duv1;		// screen derivatives of the bump map coordinate
	float2 duv2;
	float2 dBase1;		// and of the base texture coordinate
	float2 dBase2;
	float flEven;		// 1 where the texture lies evenly on the surface, 0 where it is smeared (see FidelityParallaxAxes)
	float flUVPerUnit;	// bump map texture units per world unit (mean of both axes)
};

FidelityParallaxFrame FidelityParallaxAxes( float3 vWorldPos, float3 vWorldNormal, float2 vBaseUV, float2 vBumpUV )
{
	FidelityParallaxFrame f;
	const float3 dp1 = ddx( vWorldPos ), dp2 = ddy( vWorldPos );
	f.duv1 = ddx( vBumpUV );
	f.duv2 = ddy( vBumpUV );
	f.dBase1 = ddx( vBaseUV );
	f.dBase2 = ddy( vBaseUV );
	// Which normal: displacements are curved on purpose - there the smooth
	// vertex normal (one per triangle made the walk jump along every
	// triangle's edge: Julius' displacement seams, also on a cobbled road with
	// one texture). On a brush the face's normal: VRAD leans the vertex
	// normals of a flat face towards every face up to 45 degrees off that
	// shares a corner (smoothing for the light), by up to 22 degrees there and
	// less further in - the walk tilted with it, and where two floor brushes
	// met it broke (Julius: "eine Kerbe" in the floor). A curved surface leans
	// only as far as its normal turns within one of its triangles (a
	// displacement's are 16 to 128 units); a lean that would take more than
	// 96 to 192 units of turning is a flat face's: there the face's normal.
	// Past 23 to 37 degrees always the face's (faces smoothed round a corner).
	// Fading from one into the other leaves no seam where they nearly agree.
	const float3 vFace = cross( dp1, dp2 );
	const float flFace = dot( vFace, vFace );
	const float3 vVertexN = normalize( vWorldNormal );
	const float3 vFaceN = flFace > 1e-16 ? vFace * rsqrt( max( flFace, 1e-30 ) ) : vVertexN;
	const float3 vFaceSide = vFaceN * ( dot( vFaceN, vVertexN ) < 0.0 ? -1.0 : 1.0 );
	// how fast the vertex normal turns per world unit (its gradient along the
	// surface from the screen derivatives, as T and B below)
	const float3 dn1 = ddx( vVertexN ), dn2 = ddy( vVertexN );
	const float3 q1 = cross( dp2, vFaceSide ), q2 = cross( vFaceSide, dp1 );
	const float flTurn = sqrt( max( dot( q1, q1 ) * dot( dn1, dn1 ) + dot( q2, q2 ) * dot( dn2, dn2 ) + 2.0 * dot( q1, q2 ) * dot( dn1, dn2 ), 0.0 ) )
		/ max( abs( dot( dp1, q1 ) ), 1e-20 );
	const float flReach = length( vVertexN - vFaceSide ) / max( flTurn, 1e-6 );
	const float flSmooth = smoothstep( 0.8, 0.92, dot( vFaceSide, vVertexN ) ) * ( 1.0 - smoothstep( 96.0, 192.0, flReach ) );
	f.N = normalize( lerp( vFaceSide, vVertexN, flSmooth ) );
	const float3 dp2perp = cross( dp2, f.N ), dp1perp = cross( f.N, dp1 );
	float3 T = dp2perp * f.duv1.x + dp1perp * f.duv2.x;
	float3 B = dp2perp * f.duv1.y + dp1perp * f.duv2.y;
	// T and B are the gradients of u and v times the screen area on the surface
	f.flUVPerUnit = 0.5 * ( length( T ) + length( B ) ) / max( abs( dot( dp1, dp2perp ) ), 1e-20 );
	// The formula (from OpenGL) has the screen's handedness in it: in D3D
	// ddy runs down, both axes came out reversed - the relief stood inside out
	// (Julius: "reversed"). The sign of the screen area on the surface puts it right.
	const float flHanded = dot( cross( dp1, dp2 ), f.N ) < 0.0 ? -1.0 : 1.0;
	const float flInvMax = rsqrt( max( max( dot( T, T ), dot( B, B ) ), 1e-20 ) ) * flHanded;
	// (the axes lie in the triangle: square them to the normal, which may be the smooth one)
	T -= f.N * dot( f.N, T );
	B -= f.N * dot( f.N, B );
	f.T = T * flInvMax;
	f.B = B * flInvMax;
	// A texture smeared over a face (a brush side in Hammer whose texture runs
	// across it: one axis nearly vanishes, or both point the same way) has no
	// relief that fits it - the walk ran far along the smear (Julius: golden
	// streaky stripes on narrow faces at wall corners, gone with
	// r_fidelity_parallax 0). There the relief fades out (from 3 : 1; a
	// texture scaled 0.25 x 0.5 or a 60 degree slope keeps it).
	const float flShort = sqrt( min( dot( f.T, f.T ), dot( f.B, f.B ) ) );
	const float flSkew = abs( dot( f.T, f.B ) ) * rsqrt( max( dot( f.T, f.T ) * dot( f.B, f.B ), 1e-20 ) );
	f.flEven = smoothstep( 0.1, 0.3, flShort ) * ( 1.0 - smoothstep( 0.5, 0.85, flSkew ) );
	return f;
}

// The second layer of a blended surface (FIDELITY_BLEND)
struct FidelityParallaxBlend
{
	float2 vStart1;		// the first layer's height map coordinate before the walk
	float2 vStart2;		// the second layer's (height map and bump map)
	float4 vTo2;		// a step of (1, 0) in the first's coordinates in the second's (xy), of (0, 1) (zw)
	float4 vToBase2;	// the same into the second base texture's coordinates
	float2 d2a;			// screen derivatives of the second layer's coordinate
	float2 d2b;
	float flBlend;		// painted blend here (0 = first layer, 1 = second)
	float flDepth;		// height blend: how far the higher layer reaches through (0 = plain cross-fade)
	float flFlat2;		// a second layer without a height map: its height (on the reference plane), else -1
	float flH1;			// both heights where the walk ended (FidelityParallaxShift)
	float flH2;
};

// Call outside any branch (derivatives); vUV2 the second layer's height map
// coordinate, vBase2UV its base texture's; vTo2 and vToBase2 how a step of the
// first moves in them (constant per material: all come from one base
// coordinate through their transforms - the shader DLL works it out);
// flFlat2: see FidelityParallaxBlend
FidelityParallaxBlend FidelityParallaxBlendSetup( float2 vBumpUV, float2 vUV2, float4 vTo2, float4 vToBase2, float flBlend, float flDepth, float flFlat2 )
{
	FidelityParallaxBlend b;
	b.vStart1 = vBumpUV;
	b.vStart2 = vUV2;
	b.d2a = ddx( vUV2 );
	b.d2b = ddy( vUV2 );
	b.vTo2 = vTo2;
	b.vToBase2 = vToBase2;
	b.flBlend = flBlend;
	b.flDepth = flDepth;
	b.flFlat2 = flFlat2;
	b.flH1 = 1.0;
	b.flH2 = 1.0;
	return b;
}

// Where the second layer is for a point vUV of the first
float2 FidelityParallaxLayer2( FidelityParallaxBlend b, float2 vUV )
{
	const float2 d = vUV - b.vStart1;
	return b.vStart2 + b.vTo2.xy * d.x + b.vTo2.zw * d.y;
}

// How much of the second layer shows (0..1): the painted blend, tipped
// towards the higher of the two heights near the transition (height
// blending, as in terrain shaders); 0 or 1 painted stays that layer alone
// (for flDepth up to 0.5; the shader DLL holds it there)
// (no branches in these helpers: the old D3DX compiler of the SDK fails on
// some with "invalid read of more specific predicate")
float FidelityHeightBlend( float flBlend, float h1, float h2, float flDepth )
{
	const float d = max( flDepth, 0.001 );
	const float a1 = 1.0 - flBlend + h1 * d;
	const float a2 = flBlend + h2 * d;
	const float m = max( a1, a2 ) - d;
	const float w1 = max( a1 - m, 0.0 ), w2 = max( a2 - m, 0.0 );
	return flDepth < 0.001 ? flBlend : w2 / max( w1 + w2, 1e-5 );
}

// The relief's height at a point of the first layer (both layers blended),
// and each layer's own (h1, h2)
float FidelityParallaxHeights( FidelityParallaxFrame f, sampler HeightSampler, sampler Height2Sampler, FidelityParallaxBlend b, float2 vUV,
	out float h1, out float h2 )
{
	h1 = tex2Dgrad( HeightSampler, vUV, f.duv1, f.duv2 ).r;
	h2 = h1;
	float h = h1;
#if FIDELITY_BLEND
	// only read where the second layer is painted at all (most of a
	// displacement is one layer: the walk costs there what it costs on a wall)
	h2 = b.flFlat2 >= 0.0 ? b.flFlat2 : h1;
	[branch]
	if ( b.flBlend > 0.001 && b.flFlat2 < 0.0 )
		h2 = tex2Dgrad( Height2Sampler, FidelityParallaxLayer2( b, vUV ), b.d2a, b.d2b ).r;
	h = lerp( h1, h2, FidelityHeightBlend( b.flBlend, h1, h2, b.flDepth ) );
#endif
	return h;
}

float FidelityParallaxHeight( FidelityParallaxFrame f, sampler HeightSampler, sampler Height2Sampler, FidelityParallaxBlend b, float2 vUV )
{
	float h1, h2;
	return FidelityParallaxHeights( f, HeightSampler, Height2Sampler, b, vUV, h1, h2 );
}

// A world direction in the texture's own space (z along the normal)
float3 FidelityParallaxToTexture( FidelityParallaxFrame f, float3 v )
{
	return float3( dot( v, f.T ), dot( v, f.B ), dot( v, f.N ) );
}

// The reference plane here (0 = the bottom on the polygon .. 1 = the top):
// vPlane = walls, floors and ceilings (by the face's slope)
float FidelityParallaxPlane( FidelityParallaxFrame f, float2 vPlane )
{
	return lerp( vPlane.x, vPlane.y, smoothstep( 0.5, 0.8, abs( f.N.z ) ) );
}

// flPlane: FidelityParallaxPlane; flMaxShift: the most the texture moves, in world units
// (b: both heights where it ended go to b.flH1 and b.flH2 - read anyway,
// the passes need them for the blend; with no depth the height is 1)
float FidelityParallaxShift( FidelityParallaxFrame f, sampler HeightSampler, sampler Height2Sampler, inout FidelityParallaxBlend b,
	float3 vView, float flDepth, float flPlane, float flMaxShift, inout float2 vBaseUV, inout float2 vBumpUV )
{
	float2 vUV = vBumpUV;
	if ( flDepth > 0.0001 )
	{
		const float3 vViewTS = FidelityParallaxToTexture( f, vView );
		const float flSteps = lerp( PARALLAX_MAX_STEPS, PARALLAX_MIN_STEPS, saturate( vViewTS.z ) );
		const float flLayer = 1.0 / flSteps;
		// how far the texture point moves for the full depth: at flat angles
		// limited to 2.5 x the depth (5 x let the floor's joints run on under
		// the wall it meets - Julius: "als ob er einfach weiter drunter durchgeht";
		// the shader does not know where another surface begins)
		float2 vFull = vViewTS.xy / max( vViewTS.z, 0.4 ) * flDepth;
		vFull *= min( 1.0, flMaxShift * f.flUVPerUnit * rsqrt( max( dot( vFull, vFull ), 1e-20 ) ) );
		const float2 vStep = vFull * flLayer;
		// from where the view enters the relief: (1 - plane) x depth above the polygon
		vUV = vBumpUV + vFull * ( 1.0 - flPlane );
		float flLayerDepth = 0.0;
		float flMapDepth = 1.0 - FidelityParallaxHeight( f, HeightSampler, Height2Sampler, b, vUV );
		float flPrevMapDepth = flMapDepth;
		[loop]
		for ( int k = 0; k < PARALLAX_MAX_STEPS; k++ )
		{
			if ( flLayerDepth >= flMapDepth || k >= flSteps )
				break;
			vUV -= vStep;
			flPrevMapDepth = flMapDepth;
			flMapDepth = 1.0 - FidelityParallaxHeight( f, HeightSampler, Height2Sampler, b, vUV );
			flLayerDepth += flLayer;
		}
		// between the last two steps, where the view meets the height map
		const float flAfter = flMapDepth - flLayerDepth;
		const float flBefore = flPrevMapDepth - ( flLayerDepth - flLayer );
		const float flWeight = flAfter / min( flAfter - flBefore, -0.0001 );
		vUV = lerp( vUV, vUV + vStep, saturate( flWeight ) );
		// the base texture moves as far (in its own units)
		const float2 vShift = vUV - vBumpUV;
		vBaseUV += vShift * ( length( f.dBase1 ) + length( f.dBase2 ) ) / max( length( f.duv1 ) + length( f.duv2 ), 1e-8 );
	}
	vBumpUV = vUV;
	float h1, h2;
	const float flHeight = FidelityParallaxHeights( f, HeightSampler, Height2Sampler, b, vUV, h1, h2 );
	b.flH1 = h1;
	b.flH2 = h2;
	return flDepth > 0.0001 ? flHeight : 1.0;
}

// After the walk: the second layer's height map and base texture
// coordinates for the first layer's point vBumpUV
void FidelityParallaxLayer2Shift( FidelityParallaxBlend b, float2 vBumpUV, inout float2 vUV2, inout float2 vBase2UV )
{
	const float2 d = vBumpUV - b.vStart1;
	vUV2 = b.vStart2 + b.vTo2.xy * d.x + b.vTo2.zw * d.y;
	vBase2UV += b.vToBase2.xy * d.x + b.vToBase2.zw * d.y;
}

// The relief's normal (texture space) from the height map's slopes, for
// walls without a bump map: central differences 1.5 texels apart (one texel
// picked up single grains), slopes at most 1 (steeper edges caught the
// bright horizon in the reflection and the low sun, and glinted)
// (d1, d2: the screen derivatives of vUV; the second layer of a blended
// surface is taken as lying the same way round as the first)
float3 FidelityParallaxHeightNormal( sampler HeightSampler, float2 vUV, float2 d1, float2 d2, float2 vTexel, float flDepth )
{
	const float2 vStepU = float2( vTexel.x * 1.5, 0 ), vStepV = float2( 0, vTexel.y * 1.5 );
	float2 vSlope;
	vSlope.x = tex2Dgrad( HeightSampler, vUV + vStepU, d1, d2 ).r - tex2Dgrad( HeightSampler, vUV - vStepU, d1, d2 ).r;
	vSlope.y = tex2Dgrad( HeightSampler, vUV + vStepV, d1, d2 ).r - tex2Dgrad( HeightSampler, vUV - vStepV, d1, d2 ).r;
	vSlope *= flDepth / max( 3.0 * vTexel, 1e-6 );
	const float flSlope = length( vSlope );
	vSlope *= min( 1.0, 1.0 / max( flSlope, 1e-6 ) );
	return normalize( float3( -vSlope, 1.0 ) );
}

float FidelityParallaxShadow( FidelityParallaxFrame f, sampler HeightSampler, sampler Height2Sampler, FidelityParallaxBlend b,
	float3 vToLight, float flDepth, float2 vUV, float flHeight )
{
	float flShadow = 0.0;
	const float3 vLightTS = normalize( FidelityParallaxToTexture( f, vToLight ) );
	if ( flDepth > 0.0001 && vLightTS.z > 0.0 && flHeight < 0.99 )
	{
		// up from the point to the top, along the light (a light nearly along
		// the wall would sample far away: streaks - limited, and faded below)
		const float flRise = ( 1.0 - flHeight ) / PARALLAX_SHADOW_STEPS;
		const float2 vStep = vLightTS.xy / max( vLightTS.z, 0.35 ) * flDepth * flRise;
		[loop]
		for ( int k = 1; k <= PARALLAX_SHADOW_STEPS; k++ )
		{
			const float flAbove = FidelityParallaxHeight( f, HeightSampler, Height2Sampler, b, vUV + vStep * k ) - ( flHeight + flRise * k );
			// soft: higher above the ray and closer to the point darkens more
			flShadow = max( flShadow, flAbove * ( 1.0 - ( k - 1.0 ) / PARALLAX_SHADOW_STEPS ) );
		}
	}
	return 1.0 - saturate( flShadow * 6.0 ) * smoothstep( 0.08, 0.3, vLightTS.z );
}

#endif // FIDELITY_PARALLAX_COMMON_H
