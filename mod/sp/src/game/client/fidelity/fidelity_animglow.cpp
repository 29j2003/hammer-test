//========= mapbase-fidelity ==================================================//
//
// Purpose: Glow only while a model plays certain animations, with real light.
//
// Mapsmith's "Glow ..." (Models and Weapons pages) can make parts of a model
// glow only during some animations (a weapon's reload). It writes the glow
// into the material ($selfillum with its own mask) plus its settings in keys
// of its own (see fidelity_animglow.h) and this proxy:
//
//   "Proxies" { "FidelityAnimGlow" { } }
//
// The proxy sets $selfillumtint on every draw from the drawn model's current
// sequence and cycle - no script, no string work per frame (the name check
// is cached per model and sequence). fidelity_glow.cpp reads the same
// settings: the model's light follows the same amount, so the world is lit
// only while the animation plays and fades with it (like the AR2's red
// reload flare, s_GlowColors).
//
// Without this client.dll (a mod without mapbase-fidelity) the engine does
// not know the proxy: the tint stays 0, nothing glows - never a wrong glow.
// Mapsmith writes Mapbase's VScriptProxy for such mods instead.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_animglow.h"
#include "c_baseanimating.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialproxy.h"
#include "utlstring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define ANIMGLOW_MAX_NAMES		8
#define ANIMGLOW_NAME_LENGTH	64
#define ANIMGLOW_MAX			64		// glowing materials known at once (a few per mod)

struct FidelityAnimGlow_t
{
	IMaterial *m_pMaterial;
	CUtlString m_Name;					// the material's name (a freed material's pointer may come back for another)
	Vector m_vecColor;					// $mapsmith_glowcolor
	int m_nNames;
	char m_szNames[ ANIMGLOW_MAX_NAMES ][ ANIMGLOW_NAME_LENGTH ];	// activities or sequence names
	float m_flFrom, m_flTo, m_flFade;	// cycle window, fade at its inner edges
	bool m_bPulse;
	float m_flPeriod, m_flLow;
	// Cache of the last name check (one model plays one sequence for many frames)
	const CStudioHdr *m_pLastHdr;
	int m_nLastSequence;
	bool m_bLastMatch;
};

static CUtlVector< FidelityAnimGlow_t > s_AnimGlows;
static CUtlVector< IMaterial * > s_NotAnimGlows;	// materials checked and found without one

// "act_vm_reload, fire" -> the names (lower case is not needed: compared without case)
static void Fidelity_AnimGlowNames( const char *pszList, FidelityAnimGlow_t &glow )
{
	glow.m_nNames = 0;
	const char *p = pszList;
	while ( *p && glow.m_nNames < ANIMGLOW_MAX_NAMES )
	{
		while ( *p == ',' || *p == ' ' )
			p++;
		int n = 0;
		char *pszOut = glow.m_szNames[ glow.m_nNames ];
		while ( *p && *p != ',' )
		{
			if ( *p != ' ' && n < ANIMGLOW_NAME_LENGTH - 1 )
				pszOut[ n++ ] = *p;
			p++;
		}
		pszOut[ n ] = 0;
		if ( n > 0 )
			glow.m_nNames++;
	}
}

// Reads "key=value;key=value" (Mapsmith's encodeSettings). False if the glow
// is not tied to animations.
static bool Fidelity_ParseAnimGlow( const char *pszSettings, FidelityAnimGlow_t &glow )
{
	glow.m_nNames = 0;
	glow.m_flFrom = 0.0f;
	glow.m_flTo = 1.0f;
	glow.m_flFade = 0.0f;
	glow.m_bPulse = false;
	glow.m_flPeriod = 1.0f;
	glow.m_flLow = 0.25f;
	bool bAnimation = false;
	const char *p = pszSettings;
	while ( p && *p )
	{
		const char *pszEnd = strchr( p, ';' );
		const int nLength = pszEnd ? (int)( pszEnd - p ) : V_strlen( p );
		char szPart[ 512 ];
		V_strncpy( szPart, p, MIN( nLength + 1, (int)sizeof( szPart ) ) );
		char *pszValue = strchr( szPart, '=' );
		if ( pszValue )
		{
			*pszValue++ = 0;
			if ( !V_stricmp( szPart, "when" ) )
				bAnimation = !V_stricmp( pszValue, "animation" );
			else if ( !V_stricmp( szPart, "anims" ) )
				Fidelity_AnimGlowNames( pszValue, glow );
			else if ( !V_stricmp( szPart, "from" ) )
				glow.m_flFrom = clamp( (float)V_atof( pszValue ), 0.0f, 1.0f );
			else if ( !V_stricmp( szPart, "to" ) )
				glow.m_flTo = clamp( (float)V_atof( pszValue ), 0.0f, 1.0f );
			else if ( !V_stricmp( szPart, "fade" ) )
				glow.m_flFade = clamp( (float)V_atof( pszValue ), 0.0f, 0.5f );
			else if ( !V_stricmp( szPart, "pulse" ) )
				glow.m_bPulse = V_atoi( pszValue ) != 0;
			else if ( !V_stricmp( szPart, "period" ) )
				glow.m_flPeriod = clamp( (float)V_atof( pszValue ), 0.05f, 60.0f );
			else if ( !V_stricmp( szPart, "low" ) )
				glow.m_flLow = clamp( (float)V_atof( pszValue ), 0.0f, 1.0f );
		}
		p = pszEnd ? pszEnd + 1 : NULL;
	}
	glow.m_flTo = MAX( glow.m_flTo, glow.m_flFrom );
	return bAnimation && glow.m_nNames > 0;
}

int Fidelity_AnimGlowOf( IMaterial *pMaterial )
{
	if ( !pMaterial || pMaterial->IsErrorMaterial() )
		return -1;
	for ( int i = 0; i < s_AnimGlows.Count(); i++ )
	{
		if ( s_AnimGlows[ i ].m_pMaterial == pMaterial && !V_stricmp( s_AnimGlows[ i ].m_Name.Get(), pMaterial->GetName() ) )
			return i;
	}
	if ( s_NotAnimGlows.Find( pMaterial ) != s_NotAnimGlows.InvalidIndex() )
		return -1;

	bool bFound = false;
	IMaterialVar *pGlow = pMaterial->FindVar( "$mapsmith_glow", &bFound, false );
	IMaterialVar *pSettings = bFound && pGlow->GetIntValue() == 1 ? pMaterial->FindVar( "$mapsmith_glowsettings", &bFound, false ) : NULL;
	FidelityAnimGlow_t glow;
	if ( !bFound || !pSettings || !Fidelity_ParseAnimGlow( pSettings->GetStringValue(), glow ) || s_AnimGlows.Count() >= ANIMGLOW_MAX )
	{
		s_NotAnimGlows.AddToTail( pMaterial );
		return -1;
	}
	glow.m_pMaterial = pMaterial;
	glow.m_Name = pMaterial->GetName();
	glow.m_vecColor.Init( 1, 1, 1 );
	IMaterialVar *pColor = pMaterial->FindVar( "$mapsmith_glowcolor", &bFound, false );
	if ( bFound )
		pColor->GetVecValue( glow.m_vecColor.Base(), 3 );
	glow.m_pLastHdr = NULL;
	glow.m_nLastSequence = -1;
	glow.m_bLastMatch = false;
	return s_AnimGlows.AddToTail( glow );
}

const Vector &Fidelity_AnimGlowColor( int nGlow )
{
	static const Vector s_vecNone( 0, 0, 0 );
	return s_AnimGlows.IsValidIndex( nGlow ) ? s_AnimGlows[ nGlow ].m_vecColor : s_vecNone;
}

float Fidelity_AnimGlowAmount( int nGlow, C_BaseAnimating *pAnimating )
{
	if ( !pAnimating || !s_AnimGlows.IsValidIndex( nGlow ) )
		return 0.0f;
	FidelityAnimGlow_t &glow = s_AnimGlows[ nGlow ];
	const CStudioHdr *pHdr = pAnimating->GetModelPtr();
	const int nSequence = pAnimating->GetSequence();
	if ( !pHdr || nSequence < 0 )
		return 0.0f;
	if ( pHdr != glow.m_pLastHdr || nSequence != glow.m_nLastSequence )
	{
		glow.m_pLastHdr = pHdr;
		glow.m_nLastSequence = nSequence;
		glow.m_bLastMatch = false;
		const char *pszActivity = pAnimating->GetSequenceActivityName( nSequence );
		const char *pszSequence = pAnimating->GetSequenceName( nSequence );
		for ( int i = 0; i < glow.m_nNames && !glow.m_bLastMatch; i++ )
		{
			glow.m_bLastMatch = ( pszActivity && !V_stricmp( pszActivity, glow.m_szNames[ i ] ) ) ||
				( pszSequence && !V_stricmp( pszSequence, glow.m_szNames[ i ] ) );
		}
	}
	if ( !glow.m_bLastMatch )
		return 0.0f;

	const float flCycle = pAnimating->GetCycle();
	if ( flCycle < glow.m_flFrom || flCycle > glow.m_flTo )
		return 0.0f;
	float flAmount = 1.0f;
	if ( glow.m_flFade > 0.0f )
	{
		// Fades only at inner edges: a window that starts at 0 or ends at 1 is
		// already on (or still on) when the animation begins or ends.
		if ( glow.m_flFrom > 0.0f )
			flAmount = MIN( flAmount, ( flCycle - glow.m_flFrom ) / glow.m_flFade );
		if ( glow.m_flTo < 1.0f )
			flAmount = MIN( flAmount, ( glow.m_flTo - flCycle ) / glow.m_flFade );
	}
	if ( glow.m_bPulse )
		flAmount *= glow.m_flLow + ( 1.0f - glow.m_flLow ) * ( 0.5f + 0.5f * sinf( gpGlobals->curtime * 2.0f * M_PI_F / glow.m_flPeriod ) );
	return clamp( flAmount, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// The material proxy: $selfillumtint = colour x amount for the drawn model
//-----------------------------------------------------------------------------
class CFidelityAnimGlowProxy : public IMaterialProxy
{
public:
	CFidelityAnimGlowProxy() : m_pMaterial( NULL ), m_pTint( NULL ), m_nGlow( -1 ), m_bLookedUp( false ) {}

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		m_pMaterial = pMaterial;
		return true;
	}
	virtual void OnBind( void *pArg );
	virtual void Release() { delete this; }
	virtual IMaterial *GetMaterial() { return m_pMaterial; }

private:
	IMaterial *m_pMaterial;
	IMaterialVar *m_pTint;
	int m_nGlow;
	bool m_bLookedUp;
};

void CFidelityAnimGlowProxy::OnBind( void *pArg )
{
	// The shader's own parameters may not exist yet while proxies are set up:
	// looked up on first use.
	if ( !m_bLookedUp )
	{
		m_bLookedUp = true;
		bool bFound = false;
		m_pTint = m_pMaterial ? m_pMaterial->FindVar( "$selfillumtint", &bFound, false ) : NULL;
		if ( !bFound )
			m_pTint = NULL;
		m_nGlow = Fidelity_AnimGlowOf( m_pMaterial );
	}
	if ( !m_pTint )
		return;
	IClientRenderable *pRenderable = (IClientRenderable *)pArg;
	IClientUnknown *pUnknown = pRenderable ? pRenderable->GetIClientUnknown() : NULL;
	C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
	const float flAmount = Fidelity_AnimGlowAmount( m_nGlow, pEntity ? pEntity->GetBaseAnimating() : NULL );
	const Vector vecTint = Fidelity_AnimGlowColor( m_nGlow ) * flAmount;
	m_pTint->SetVecValue( vecTint.x, vecTint.y, vecTint.z );
}

EXPOSE_INTERFACE( CFidelityAnimGlowProxy, IMaterialProxy, "FidelityAnimGlow" IMATERIAL_PROXY_INTERFACE_VERSION );
