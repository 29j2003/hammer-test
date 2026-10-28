//========= mapbase-fidelity ==================================================//
//
// Purpose: fidelity_report - everything about what the player looks at, for
//          fixing a wall or an effect that looks wrong without guessing.
//
// Julius sees the game, Claude does not: a wall's relief was tuned from
// screenshots alone, several rounds over, and kept missing (the height map
// is made from textures nobody here could look at). Looking at the spot and
// typing the command writes into <mod>/fidelity_report/:
//
//   NN_info.txt         the map, where the player stands and looks; what the
//                       crosshair hits (entity, surface, its surfaceprop and
//                       game material - bullet holes and impact effects come
//                       from it); every material there (the wall's, or all of
//                       a model's) with all its parameters; the weapon in
//                       hand: its view model, attachments and the events of
//                       its sequences (muzzle flashes come from those);
//                       the sun (env_sun and env_global_light: where they
//                       point, sizes, colours) and whether server.dll was
//                       built with the add-on; the dynamic lights burning
//                       now (they light the walls per pixel); every fidelity
//                       setting; the decals the game has loaded (name and
//                       shader; scorch marks in full, with their textures)
//   NN_<material>.vmt   the material as the game loads it
//   NN_<param>_<name>.vtf  every texture it uses ($basetexture, $bumpmap,
//                       $parallaxmap - the height map deploy.bat made ...)
//
// and takes a screenshot (jpeg). fidelity\report.bat turns the textures
// into pictures and packs the folder into fidelity_report.zip to send.
//
// fidelity_compare does the same and then takes one screenshot with
// everything on and one with each effect off in turn (screenshots/
// fidelity_compare_NN_<step>_<effect>.jpg), to see which effect does what.
//
// Nothing runs unless a command is typed.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_volumetric.h"
#include "filesystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "vphysics_interface.h"
#include "physics_shared.h"
#include "view.h"
#include "c_baseviewmodel.h"
#include "tier1/utlbuffer.h"
#include "fidelity_godrays.h"
#include "iefx.h"
#include "dlight.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define REPORT_FOLDER		"fidelity_report"
#define REPORT_MAX_FILE		( 32 * 1024 * 1024 )	// textures larger than this are only named
#define REPORT_MAX_MATERIALS	16

// A name usable in a file name: letters, digits, '-' and '_' (slashes become '_')
static void FidelityReportSafeName( const char *pszName, char *pszOut, int nOut )
{
	int n = 0;
	for ( const char *p = pszName; *p && n < nOut - 1; p++ )
	{
		const char c = *p;
		const bool bKeep = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_';
		pszOut[ n++ ] = bKeep ? c : '_';
	}
	pszOut[ n ] = 0;
}

// Copies a file of the game (VPKs, the mod, the map's own files) into the report; false when it is not there
static bool FidelityReportCopy( const char *pszFrom, const char *pszTo, CUtlBuffer &info )
{
	if ( !g_pFullFileSystem->FileExists( pszFrom, "GAME" ) )
	{
		info.Printf( "      (file %s not found)\n", pszFrom );
		return false;
	}
	const unsigned int nSize = g_pFullFileSystem->Size( pszFrom, "GAME" );
	if ( nSize > REPORT_MAX_FILE )
	{
		info.Printf( "      (file %s: %u bytes, too large to copy)\n", pszFrom, nSize );
		return false;
	}
	CUtlBuffer data;
	if ( !g_pFullFileSystem->ReadFile( pszFrom, "GAME", data ) || !g_pFullFileSystem->WriteFile( pszTo, "MOD", data ) )
	{
		info.Printf( "      (file %s could not be copied)\n", pszFrom );
		return false;
	}
	info.Printf( "      -> %s\n", pszTo );
	return true;
}

// One material: its shader, every parameter set, and its files copied
static void FidelityReportMaterial( IMaterial *pMaterial, int nReport, CUtlBuffer &info )
{
	if ( !pMaterial )
		return;
	info.Printf( "\n  material %s%s\n    shader %s\n", pMaterial->GetName(), pMaterial->IsErrorMaterial() ? " (ERROR: not found)" : "", pMaterial->GetShaderName() );
	if ( pMaterial->IsErrorMaterial() )
		return;

	char szSafe[ MAX_PATH ], szFrom[ MAX_PATH ], szTo[ MAX_PATH ];
	FidelityReportSafeName( pMaterial->GetName(), szSafe, sizeof( szSafe ) );
	Q_snprintf( szFrom, sizeof( szFrom ), "materials/%s.vmt", pMaterial->GetName() );
	Q_snprintf( szTo, sizeof( szTo ), "%s/%02d_%s.vmt", REPORT_FOLDER, nReport, szSafe );
	info.Printf( "    file %s\n", szFrom );
	FidelityReportCopy( szFrom, szTo, info );

	const int nParams = pMaterial->ShaderParamCount();
	IMaterialVar **ppParams = pMaterial->GetShaderParams();
	for ( int i = 0; ppParams && i < nParams; i++ )
	{
		IMaterialVar *pVar = ppParams[ i ];
		if ( !pVar || !pVar->IsDefined() )
			continue;
		if ( pVar->GetType() != MATERIAL_VAR_TYPE_TEXTURE )
		{
			info.Printf( "    %s \"%s\"\n", pVar->GetName(), pVar->GetStringValue() );
			continue;
		}
		ITexture *pTexture = pVar->GetTextureValue();
		const char *pszTexture = pTexture ? pTexture->GetName() : "";
		info.Printf( "    %s \"%s\"%s\n", pVar->GetName(), pszTexture, pTexture && pTexture->IsError() ? " (ERROR: not found)" : "" );
		if ( !pTexture || pTexture->IsError() || !pszTexture[ 0 ] || !Q_strnicmp( pszTexture, "_rt_", 4 ) )
			continue;
		char szParam[ 64 ], szTexture[ MAX_PATH ];
		FidelityReportSafeName( pVar->GetName() + ( pVar->GetName()[ 0 ] == '$' ? 1 : 0 ), szParam, sizeof( szParam ) );
		FidelityReportSafeName( pszTexture, szTexture, sizeof( szTexture ) );
		Q_snprintf( szFrom, sizeof( szFrom ), "materials/%s.vtf", pszTexture );
		Q_snprintf( szTo, sizeof( szTo ), "%s/%02d_%s_%s.vtf", REPORT_FOLDER, nReport, szParam, szTexture );
		FidelityReportCopy( szFrom, szTo, info );
	}
}

// The weapon in hand: its view model's attachments and the events of its
// sequences (a muzzle flash is an event: AE_MUZZLEFLASH "SMG1 muzzle", or a
// particle effect)
static void FidelityReportWeapon( C_BasePlayer *pPlayer, CUtlBuffer &info )
{
	C_BaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : NULL;
	if ( !pWeapon )
	{
		info.Printf( "\nweapon: none\n" );
		return;
	}
	info.Printf( "\nweapon %s (\"%s\"), clip %d, ammo type %d\n", pWeapon->GetClassname(), pWeapon->GetPrintName(), pWeapon->Clip1(), pWeapon->GetPrimaryAmmoType() );
	C_BaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	CStudioHdr *pHdr = pViewModel ? pViewModel->GetModelPtr() : NULL;
	if ( !pHdr || !pViewModel->GetModel() )
	{
		info.Printf( "  view model: none\n" );
		return;
	}
	info.Printf( "  view model %s\n  attachments:", modelinfo->GetModelName( pViewModel->GetModel() ) );
	for ( int i = 0; i < pHdr->GetNumAttachments(); i++ )
		info.Printf( " %d=%s", i + 1, pHdr->pAttachment( i ).pszName() );
	info.Printf( "\n  sequences with events (cycle: event number name \"options\"):\n" );
	for ( int i = 0; i < pHdr->GetNumSeq(); i++ )
	{
		mstudioseqdesc_t &seq = pHdr->pSeqdesc( i );
		if ( seq.numevents <= 0 )
			continue;
		info.Printf( "    %s (%s)\n", seq.pszLabel(), seq.pszActivityName() );
		for ( int j = 0; j < seq.numevents; j++ )
		{
			const mstudioevent_t *pEvent = seq.pEvent( j );
			info.Printf( "      %.2f: %d %s \"%s\"\n", pEvent->cycle, pEvent->event, pEvent->szeventindex ? pEvent->pszEventName() : "", pEvent->pszOptions() );
		}
	}
}

//-----------------------------------------------------------------------------
// The engine's dynamic lights now, all of them, unsorted (fidelity_dynlights.cpp
// hands the nearest four to the walls' shader)
//-----------------------------------------------------------------------------
static void FidelityReportDynamicLights( const Vector &vecEye, CUtlBuffer &info )
{
	dlight_t *pActive[ MAX_DLIGHTS ];
	const int nActive = effects ? effects->CL_GetActiveDLights( pActive ) : 0;
	info.Printf( "\ndynamic lights: %d\n", nActive );
	for ( int i = 0; i < nActive; i++ )
	{
		const dlight_t *pLight = pActive[ i ];
		if ( !pLight )
			continue;
		Vector vecColor;
		ColorRGBExp32ToVector( pLight->color, vecColor );
		info.Printf( "  at %.0f %.0f %.0f (%.0f units away), radius %.0f, colour %.1f %.1f %.1f, flags %d, dies in %.2f s, key %d\n",
			pLight->origin.x, pLight->origin.y, pLight->origin.z, vecEye.DistTo( pLight->origin ), pLight->radius,
			vecColor.x, vecColor.y, vecColor.z, pLight->flags, pLight->die - gpGlobals->curtime, pLight->key );
	}
}

//-----------------------------------------------------------------------------
// The map's env_projectedtexture entities (also those without a shadow map)
//-----------------------------------------------------------------------------
static void FidelityReportProjectedTextures( const Vector &vecEye, CUtlBuffer &info )
{
	info.Printf( "\nenv_projectedtexture entities:\n" );
	int nCount = 0;
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		ClientClass *pClass = pEntity->GetClientClass();
		if ( !pClass || Q_strcmp( pClass->m_pNetworkName, "CEnvProjectedTexture" ) )
			continue;
		nCount++;
		const Vector &vecOrigin = pEntity->GetAbsOrigin();
		const QAngle &angles = pEntity->GetAbsAngles();
		info.Printf( "  at %.0f %.0f %.0f (%.0f units away), angles %.0f %.0f %.0f%s\n", vecOrigin.x, vecOrigin.y, vecOrigin.z, vecEye.DistTo( vecOrigin ),
			angles.x, angles.y, angles.z, pEntity->IsDormant() ? ", dormant (not sent: out of sight)" : "" );
	}
	if ( nCount == 0 )
		info.Printf( "  none\n" );
}

//-----------------------------------------------------------------------------
// Every setting of the add-on (and a few of the engine's it builds on), as
// the game runs with them now
//-----------------------------------------------------------------------------
static void FidelityReportSettings( CUtlBuffer &info )
{
	info.Printf( "\nsettings:\n" );
	static const char *s_pszEngine[] = { "mat_hdr_level", "r_flashlightdepthres", "r_shadowfromworldlights", "mat_specular", "r_waterforceexpensive" };
	for ( int i = 0; i < ARRAYSIZE( s_pszEngine ); i++ )
	{
		const ConVar *pVar = g_pCVar->FindVar( s_pszEngine[ i ] );
		if ( pVar )
			info.Printf( "  %s %s\n", pVar->GetName(), pVar->GetString() );
	}
	ICvar::Iterator iter( g_pCVar );
	for ( iter.SetFirst(); iter.IsValid(); iter.Next() )
	{
		const ConCommandBase *pBase = iter.Get();
		if ( !pBase || pBase->IsCommand() || !Q_stristr( pBase->GetName(), "_fidelity" ) )
			continue;
		info.Printf( "  %s %s\n", pBase->GetName(), static_cast<const ConVar *>( pBase )->GetString() );
	}
}

//-----------------------------------------------------------------------------
// The decals the game has loaded: name and shader each; scorch marks (from
// explosions) in full, with their textures
//-----------------------------------------------------------------------------
#define REPORT_MAX_SCORCH	4

static void FidelityReportDecals( int nReport, CUtlBuffer &info )
{
	info.Printf( "\ndecals loaded:\n" );
	int nScorch = 0;
	IMaterial *pScorch[ REPORT_MAX_SCORCH ] = {};
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || Q_strnicmp( pMaterial->GetName(), "decals/", 7 ) )
			continue;
		info.Printf( "  %s: %s\n", pMaterial->GetName(), pMaterial->GetShaderName() );
		if ( nScorch < REPORT_MAX_SCORCH && Q_stristr( pMaterial->GetName(), "scorch" ) )
			pScorch[ nScorch++ ] = pMaterial;
	}
	for ( int i = 0; i < nScorch; i++ )
		FidelityReportMaterial( pScorch[ i ], nReport, info );
}

CON_COMMAND( fidelity_report, "Writes everything about what you look at (materials, textures, surface, weapon) into fidelity_report/ in the mod, with a screenshot - to send when something looks wrong (fidelity\\report.bat packs it)." )
{
	if ( !engine->IsInGame() )
	{
		Msg( "[fidelity] fidelity_report: load a map first\n" );
		return;
	}
	g_pFullFileSystem->CreateDirHierarchy( REPORT_FOLDER, "MOD" );
	int nReport = 1;
	char szInfo[ MAX_PATH ];
	for ( ; nReport < 100; nReport++ )
	{
		Q_snprintf( szInfo, sizeof( szInfo ), "%s/%02d_info.txt", REPORT_FOLDER, nReport );
		if ( !g_pFullFileSystem->FileExists( szInfo, "MOD" ) )
			break;
	}
	if ( nReport >= 100 )
	{
		Msg( "[fidelity] fidelity_report: 99 reports already - send and empty the folder %s first\n", REPORT_FOLDER );
		return;
	}

	CUtlBuffer info( 0, 0, CUtlBuffer::TEXT_BUFFER );
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	const Vector vecEye = MainViewOrigin();
	QAngle angView;
	VectorAngles( MainViewForward(), angView );
	info.Printf( "fidelity_report %02d\nmap %s\nview %.1f %.1f %.1f, angles %.1f %.1f %.1f\n", nReport, engine->GetLevelName(),
		vecEye.x, vecEye.y, vecEye.z, angView.x, angView.y, angView.z );
	if ( args.ArgC() > 1 )
		info.Printf( "note: %s\n", args.ArgS() );

	// what the crosshair hits (as a bullet would: glass and grates too)
	trace_t tr;
	UTIL_TraceLine( vecEye, vecEye + MainViewForward() * MAX_TRACE_LENGTH, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction >= 1.0f )
		info.Printf( "\nlooking at: nothing\n" );
	else
	{
		info.Printf( "\nlooking at %.1f %.1f %.1f (%.0f units away), surface normal %.2f %.2f %.2f\n", tr.endpos.x, tr.endpos.y, tr.endpos.z,
			( tr.endpos - vecEye ).Length(), tr.plane.normal.x, tr.plane.normal.y, tr.plane.normal.z );
		C_BaseEntity *pEntity = tr.m_pEnt;
		const model_t *pModel = pEntity ? pEntity->GetModel() : NULL;
		info.Printf( "  entity %s%s, model %s\n", pEntity ? pEntity->GetClassname() : "none", pEntity && pEntity->IsWorld() ? " (the world)" : "",
			pModel ? modelinfo->GetModelName( pModel ) : "none" );
		// a static prop (a tree ...) is part of the world: hitbox = its index + 1
		if ( pEntity && pEntity->IsWorld() && tr.hitbox > 0 && staticpropmgr )
		{
			ICollideable *pProp = staticpropmgr->GetStaticPropByIndex( tr.hitbox - 1 );
			const model_t *pPropModel = pProp ? pProp->GetCollisionModel() : NULL;
			if ( pPropModel )
			{
				// (the collision model gave no materials: the model itself, by its name)
				const char *pszName = modelinfo->GetModelName( pPropModel );
				const model_t *pLoaded = modelinfo->FindOrLoadModel( pszName );
				pModel = pLoaded ? pLoaded : pPropModel;
				info.Printf( "  static prop %d, model %s (%d materials)\n", tr.hitbox - 1, pszName,
					modelinfo->GetModelType( pModel ) == mod_studio ? modelinfo->GetModelMaterialCount( pModel ) : -1 );
			}
		}
		// the light there as the map baked it (lightmaps and ambient): ~0 means the
		// map has no light reaching it (or was compiled without lighting)
		const Vector vecBaked = engine->GetLightForPoint( tr.endpos + tr.plane.normal * 2.0f, true );
		info.Printf( "  baked light there %.3f %.3f %.3f\n", vecBaked.x, vecBaked.y, vecBaked.z );
		const surfacedata_t *pSurface = physprops ? physprops->GetSurfaceData( tr.surface.surfaceProps ) : NULL;
		info.Printf( "  surface texture %s, surfaceprop %d %s, game material '%c'\n", tr.surface.name ? tr.surface.name : "?", tr.surface.surfaceProps,
			physprops ? physprops->GetPropName( tr.surface.surfaceProps ) : "?", pSurface && pSurface->game.material ? (char)pSurface->game.material : '?' );

		if ( pModel && modelinfo->GetModelType( pModel ) == mod_studio )
		{
			// a model: all its materials
			IMaterial *pMaterials[ REPORT_MAX_MATERIALS ] = {};
			const int nCount = MIN( modelinfo->GetModelMaterialCount( pModel ), REPORT_MAX_MATERIALS );
			modelinfo->GetModelMaterials( pModel, nCount, pMaterials );
			for ( int i = 0; i < nCount; i++ )
				FidelityReportMaterial( pMaterials[ i ], nReport, info );
		}
		else if ( tr.surface.name && tr.surface.name[ 0 ] && tr.surface.name[ 0 ] != '*' )
			FidelityReportMaterial( materials->FindMaterial( tr.surface.name, TEXTURE_GROUP_WORLD, false ), nReport, info );
	}

	FidelityReportWeapon( pPlayer, info );
	Fidelity_SunReport( info );
	FidelityReportDynamicLights( vecEye, info );
	Fidelity_VolumetricReport( vecEye, info );
	FidelityReportProjectedTextures( vecEye, info );
	// the server's half: built with the add-on (both DLLs from the same pull)?
	const ConVar *pTactics = g_pCVar->FindVar( "npc_fidelity_tactics" );
	info.Printf( "\nserver.dll: %s\n", pTactics ? "with mapbase-fidelity" : "WITHOUT mapbase-fidelity (old server.dll - run update.bat)" );
	if ( pTactics )
		info.Printf( "  npc_fidelity_tactics %s, npc_fidelity_gesture_blend %s\n", pTactics->GetString(),
			g_pCVar->FindVar( "npc_fidelity_gesture_blend" ) ? g_pCVar->FindVar( "npc_fidelity_gesture_blend" )->GetString() : "?" );
	FidelityReportSettings( info );
	FidelityReportDecals( nReport, info );

	g_pFullFileSystem->WriteFile( szInfo, "MOD", info );
	// the screenshot goes to <mod>/screenshots (report.bat takes the newest along)
	engine->ClientCmd_Unrestricted( "jpeg\n" );
	Msg( "[fidelity] report %02d written to %s/ (with a screenshot) - when done, run fidelity\\report.bat and send fidelity_report.zip\n", nReport, REPORT_FOLDER );
}

//-----------------------------------------------------------------------------
// fidelity_compare: a report (as fidelity_report), then one screenshot with
// everything on and one with each effect off in turn (r_fidelity_ssao 0, ...)
// - which effect makes a floor darker or a light pop shows at once, without
// guessing (Julius 26.10.2026: the wood got darker coming closer; a light and
// its shadow popped in). The player should stand still for the few seconds.
// Each setting is put back right after its screenshot (and on leaving the map).
//-----------------------------------------------------------------------------
static const char *s_pszCompare[][ 2 ] =
{
	{ "all_on", NULL },
	{ "ssao_off", "r_fidelity_ssao" },
	{ "godrays_off", "r_fidelity_godrays" },
	{ "volumetric_off", "r_fidelity_volumetric" },	// light in the air, mist
	{ "dynlights_off", "r_fidelity_dynlights" },
	{ "parallax_off", "r_fidelity_parallax" },
	{ "grade_off", "r_fidelity_grade" },
	{ "suncascade_off", "r_fidelity_sunshadow_cascade" },	// one sun shadow map instead of two
	{ "sky_off", "r_fidelity_sky" },				// clouds (also in front of the sun)
	{ "glow_off", "r_fidelity_glow" },				// last: its lights fade back in slowly
};

#define COMPARE_SETTLE	0.6f	// seconds after a change before the screenshot (glows fade out in 0.4)
#define COMPARE_SHOT	0.3f	// seconds for the screenshot before the setting goes back

class CFidelityCompare : public CAutoGameSystemPerFrame
{
public:
	CFidelityCompare() : CAutoGameSystemPerFrame( "CFidelityCompare" ), m_nStep( -1 ), m_nReport( 0 ), m_bShot( false ),
		m_bRestorePending( false ), m_flNext( 0.0f ), m_pChanged( NULL )
	{
		m_szOld[ 0 ] = 0;
	}

	void Start( float flDelay )
	{
		Restore();
		m_nStep = 0;
		m_bShot = false;
		m_flNext = gpGlobals->realtime + flDelay;
		m_nReport = 0;
		char szInfo[ MAX_PATH ];
		for ( int n = 1; n < 100; n++ )
		{
			Q_snprintf( szInfo, sizeof( szInfo ), "%s/%02d_info.txt", REPORT_FOLDER, n );
			if ( g_pFullFileSystem->FileExists( szInfo, "MOD" ) )
				m_nReport = n;
		}
		m_nReport++;	// the report fidelity_compare asked for comes next
	}

	virtual void Update( float frametime )
	{
		if ( m_nStep < 0 || gpGlobals->realtime < m_flNext )
			return;
		if ( m_nStep >= ARRAYSIZE( s_pszCompare ) || !engine->IsInGame() )
		{
			Restore();
			m_nStep = -1;
			Msg( "[fidelity] compare done - run fidelity\\report.bat and send the zip\n" );
			return;
		}
		const char *pszVar = s_pszCompare[ m_nStep ][ 1 ];
		if ( !m_bShot )
		{
			// switch this one off, the screenshot after it settled
			Restore();
			m_bRestorePending = false;
			ConVar *pVar = pszVar ? g_pCVar->FindVar( pszVar ) : NULL;
			if ( pVar )
			{
				m_pChanged = pVar;
				Q_strncpy( m_szOld, pVar->GetString(), sizeof( m_szOld ) );
				pVar->SetValue( 0 );
			}
			m_bShot = true;
			m_flNext = gpGlobals->realtime + COMPARE_SETTLE;
			return;
		}
		char szCommand[ 128 ];
		Q_snprintf( szCommand, sizeof( szCommand ), "jpeg fidelity_compare_%02d_%d_%s 90\n", m_nReport, m_nStep, s_pszCompare[ m_nStep ][ 0 ] );
		engine->ClientCmd_Unrestricted( szCommand );
		// (back on in the next round, after the screenshot was taken)
		m_nStep++;
		m_bShot = false;
		m_flNext = gpGlobals->realtime + COMPARE_SHOT;
		m_bRestorePending = true;
	}

	virtual void PreRender()
	{
		// a setting goes back once its screenshot is taken (the frame after the command)
		if ( m_bRestorePending && gpGlobals->realtime >= m_flNext - COMPARE_SHOT * 0.5f )
		{
			Restore();
			m_bRestorePending = false;
		}
	}

	virtual void LevelShutdownPreEntity() { Restore(); m_nStep = -1; }
	virtual void Shutdown() { Restore(); }

private:
	void Restore()
	{
		if ( m_pChanged )
			m_pChanged->SetValue( m_szOld );
		m_pChanged = NULL;
	}

	int m_nStep;
	int m_nReport;
	bool m_bShot;
	bool m_bRestorePending;
	float m_flNext;
	ConVar *m_pChanged;
	char m_szOld[ 64 ];
};

static CFidelityCompare s_FidelityCompare;

CON_COMMAND( fidelity_compare, "fidelity_report, then screenshots with each effect off in turn (stand still a few seconds) - shows which effect changes what you see." )
{
	if ( !engine->IsInGame() )
	{
		Msg( "[fidelity] fidelity_compare: load a map first\n" );
		return;
	}
	char szCommand[ 256 ];
	Q_snprintf( szCommand, sizeof( szCommand ), "fidelity_report compare %s\n", args.ArgC() > 1 ? args.ArgS() : "" );
	engine->ClientCmd_Unrestricted( szCommand );
	s_FidelityCompare.Start( 1.0f );
	Msg( "[fidelity] compare: stand still for about %d seconds\n", (int)( ARRAYSIZE( s_pszCompare ) * ( COMPARE_SETTLE + COMPARE_SHOT ) + 2.0f ) );
}
