//========= mapbase-fidelity ==================================================//
//
// Purpose: Glowing eyes go dark when the body dies (Combine soldiers, metro
//          police, hunters): the "FidelityDeadEyes" material proxy.
//
// The eyes glow through the material's $selfillum; alive and dead bodies
// share the material. deploy.bat gives the materials of these characters the
// proxy (a copy of the game's material with a "Proxies" block). Bound for a
// dead body (a ragdoll, an NPC no longer alive - Fidelity_IsDead), it fades
// $selfillumtint to black over EYES_FADE_TIME, from the moment it first sees
// the body dead; for everything else it keeps the material's own tint.
//
// Cost: one lookup per draw of such a material.
//
//=============================================================================//

#include "cbase.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "fidelity_glow.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_dead_eyes( "r_fidelity_dead_eyes", "1", FCVAR_ARCHIVE, "Glowing eyes of Combine soldiers and others go dark when they die. 0 = off." );

#define EYES_FADE_TIME		1.5f	// seconds until the eyes are dark
#define EYES_FORGET_TIME	30.0f	// dead bodies not drawn for this long are forgotten

class CFidelityDeadEyesProxy : public IMaterialProxy
{
public:
	CFidelityDeadEyesProxy() : m_pTint( NULL ), m_Died( DefLessFunc( C_BaseEntity * ) ), m_flLastCleanup( 0.0f )
	{
		m_vecTint.Init( 1.0f, 1.0f, 1.0f );
	}

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool bFound = false;
		m_pTint = pMaterial->FindVar( "$selfillumtint", &bFound, false );
		if ( !bFound )
			m_pTint = NULL;
		if ( m_pTint && m_pTint->IsDefined() )
			m_pTint->GetVecValue( m_vecTint.Base(), 3 );
		return m_pTint != NULL;
	}

	virtual void OnBind( void *pArg );
	virtual void Release() { delete this; }
	virtual IMaterial *GetMaterial() { return m_pTint ? m_pTint->GetOwningMaterial() : NULL; }

private:
	struct Died_t
	{
		float m_flTime;		// when it was first seen dead
		float m_flLastSeen;
	};

	IMaterialVar *m_pTint;
	Vector m_vecTint;		// the material's own $selfillumtint
	CUtlMap< C_BaseEntity *, Died_t > m_Died;
	float m_flLastCleanup;
};

void CFidelityDeadEyesProxy::OnBind( void *pArg )
{
	IClientRenderable *pRenderable = (IClientRenderable *)pArg;
	IClientUnknown *pUnknown = pRenderable ? pRenderable->GetIClientUnknown() : NULL;
	C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
	float flLeft = 1.0f;	// share of the glow left
	if ( pEntity && r_fidelity_dead_eyes.GetBool() && Fidelity_IsDead( pEntity ) )
	{
		unsigned short index = m_Died.Find( pEntity );
		if ( index == m_Died.InvalidIndex() )
		{
			Died_t died = { gpGlobals->curtime, gpGlobals->curtime };
			index = m_Died.Insert( pEntity, died );
		}
		m_Died[ index ].m_flLastSeen = gpGlobals->curtime;
		flLeft = 1.0f - clamp( ( gpGlobals->curtime - m_Died[ index ].m_flTime ) / EYES_FADE_TIME, 0.0f, 1.0f );
	}
	m_pTint->SetVecValue( m_vecTint.x * flLeft, m_vecTint.y * flLeft, m_vecTint.z * flLeft );

	// Forget bodies long gone (a pointer may be used again by a new entity)
	if ( gpGlobals->curtime - m_flLastCleanup > 5.0f || gpGlobals->curtime < m_flLastCleanup )
	{
		m_flLastCleanup = gpGlobals->curtime;
		for ( unsigned short i = m_Died.FirstInorder(); i != m_Died.InvalidIndex(); )
		{
			const unsigned short next = m_Died.NextInorder( i );
			const float flAge = gpGlobals->curtime - m_Died[ i ].m_flLastSeen;
			if ( flAge > EYES_FORGET_TIME || flAge < 0.0f )
				m_Died.RemoveAt( i );
			i = next;
		}
	}
}

EXPOSE_INTERFACE( CFidelityDeadEyesProxy, IMaterialProxy, "FidelityDeadEyes" IMATERIAL_PROXY_INTERFACE_VERSION );
