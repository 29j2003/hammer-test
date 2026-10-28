//========= mapbase-fidelity ==================================================//
//
// Purpose: Baked ambient occlusion for VRAD (-fidelityao): corners, joints and
//          the floor under things get darker in the bounced (indirect) light,
//          as CS:GO's and Portal 2: Community Edition's lightmapped AO.
//
// Per luxel 16 rays over the hemisphere above it (cosine-weighted, the same
// fixed set for every luxel - no noise between luxels) up to a radius: the
// share that hits nothing is how open the luxel is. The bounced light of the
// luxel is multiplied by lerp( 1, openness, strength ); direct light (sun,
// lamps) stays as it is - so sunlit floors stay bright, shadowed corners get
// depth. Called from FinalLightFace (radial.cpp, lines marked
// "mapbase-fidelity"). On by default (Julius compiles from XBLAH and should
// not have to add anything), set in vrad.cpp's argument parsing:
//
//   -nofidelityao               off (VRAD as before)
//   -fidelityao                 on, also with -fast (which otherwise skips it)
//   -fidelityao_radius <units>  reach of the rays, default 48
//   -fidelityao_strength <0..1> how much darker, default 0.8
//
// Also here: deploy.bat's fidelity_lights.rad (glowing wall textures, leaf
// shadows of foliage) is read by itself when it is in the mod folder - no
// -lights needed - and switches -textureshadows on (its forcetextureshadow
// lines need it); -nofidelitylights leaves it out.
//
// And the release settings of docs/ENTWICKLUNG.md, so a compile from XBLAH
// without parameters looks as it should: -StaticPropLighting (light per
// vertex on prop_static) and -StaticPropPolys (prop shadows by polygon) -
// not for a -fast compile, not with -nofidelitydefaults. (-LargeDispSampleRadius
// is left out: SDK 2013's VRAD parses it but never uses it.)
//
// World space and baked: costs nothing in game, more compile time (16 rays
// per luxel).
//
//=============================================================================//

#ifndef FIDELITY_AO_H
#define FIDELITY_AO_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/ssemath.h"

#define FIDELITY_AO_RAYS	16

struct FidelityAOSettings_t
{
	bool m_bOn;			// in effect (set once the arguments are read: FidelityAOResolve)
	float m_flRadius;
	float m_flStrength;
	int m_nMode;		// -1 by itself (on, off with -fast), 0 off (-nofidelityao), 1 on (-fidelityao)
	bool m_bLights;		// read fidelity_lights.rad when it is there (-nofidelitylights: no)
	bool m_bDefaults;	// the release settings by themselves (-nofidelitydefaults: no)
};

// One set for the whole compile (an inline function's static: one instance for all files)
inline FidelityAOSettings_t &FidelityAO()
{
	static FidelityAOSettings_t s_Settings = { false, 48.0f, 0.8f, -1, true, true };
	return s_Settings;
}

// After the arguments: on unless switched off; a -fast compile (a quick look
// at a map) skips it unless asked for
inline void FidelityAOResolve( bool bFast )
{
	FidelityAOSettings_t &settings = FidelityAO();
	settings.m_bOn = settings.m_nMode > 0 || ( settings.m_nMode < 0 && !bFast );
	if ( settings.m_bOn )
		Msg( "[fidelity] baked ambient occlusion: radius %.0f, strength %.2f (-nofidelityao: off)\n", settings.m_flRadius, settings.m_flStrength );
}

// The share of the hemisphere above vecPos (facing vecNormal) that is open within the radius, 0..1
inline float Fidelity_LuxelOpenness( const Vector &vecPos, const Vector &vecNormal )
{
	Vector vecTangent, vecBinormal;
	VectorVectors( vecNormal, vecTangent, vecBinormal );
	// a little off the surface (the luxel lies on it)
	const Vector vecStart = vecPos + vecNormal * 1.0f;
	const float flRadius = FidelityAO().m_flRadius;
	float flOpen = 0.0f;
	for ( int nBatch = 0; nBatch < FIDELITY_AO_RAYS / 4; nBatch++ )
	{
		FourVectors start, stop;
		start.DuplicateVector( vecStart );
		for ( int k = 0; k < 4; k++ )
		{
			// Hammersley point i: (i + 0.5) / n and the radical inverse of i, mapped cosine-weighted
			const int i = nBatch * 4 + k;
			float flInverse = 0.0f, flDigit = 0.5f;
			for ( int n = i; n > 0; n >>= 1, flDigit *= 0.5f )
				flInverse += ( n & 1 ) ? flDigit : 0.0f;
			const float u = ( i + 0.5f ) / FIDELITY_AO_RAYS;
			const float r = sqrtf( u ), phi = 2.0f * M_PI_F * flInverse;
			const Vector vecDir = vecTangent * ( r * cosf( phi ) ) + vecBinormal * ( r * sinf( phi ) ) + vecNormal * sqrtf( 1.0f - u );
			const Vector vecEnd = vecStart + vecDir * flRadius;
			stop.X( k ) = vecEnd.x;
			stop.Y( k ) = vecEnd.y;
			stop.Z( k ) = vecEnd.z;
		}
		fltx4 visible;
		TestLine( start, stop, &visible );
		flOpen += SubFloat( visible, 0 ) + SubFloat( visible, 1 ) + SubFloat( visible, 2 ) + SubFloat( visible, 3 );
	}
	return flOpen / FIDELITY_AO_RAYS;
}

// How much of the bounced light a luxel keeps (1 with -fidelityao off)
inline float Fidelity_LuxelAO( const Vector &vecPos, const Vector &vecNormal )
{
	if ( !FidelityAO().m_bOn )
		return 1.0f;
	const float flStrength = clamp( FidelityAO().m_flStrength, 0.0f, 1.0f );
	return 1.0f - flStrength + flStrength * Fidelity_LuxelOpenness( vecPos, vecNormal );
}

#endif // FIDELITY_AO_H
