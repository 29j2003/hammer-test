//========= mapbase-fidelity ==================================================//
//
// Purpose: Glowing things light their surroundings: models with glowing
//          materials (health and suit chargers, the crossbow, Combine lights,
//          energy balls ...), glow sprites on moving things (grenade blink,
//          Manhack and Scanner eyes, lights on weapons and NPCs) and muzzle
//          flashes.
//
// Nothing to set up, everything is found automatically:
//   - models: materials with $selfillum or $additive, or drawn unlit
//     (UnlitGeneric: Combine screens and monitors), per skin (the
//     crossbow's bolt only glows in its "glow" skin, so the light goes out
//     while reloading). Only what really glows counts: the selfillum mask
//     (the base texture's alpha, or $selfillummask), read from the .vtf
//     (fidelity_texture.cpp); for $additive and unlit ones the bright parts. The light sits
//     at the middle of the glowing vertices, a little out of the surface, in
//     their colour (times $selfillumtint, more saturated). Vertices and points
//     on the triangles count (a flat screen has its few vertices on the dark
//     frame). Plants never glow. A tiny glowing
//     part gives a small dim light (the lamps of a Combine console, NPC
//     eyes) - none on weapons (a red dot sight lit the room); a few percent
//     of the model full light. Unreadable textures: the colourful parts of
//     the thumbnail.
//   - sprites: only ones attached to something or parented to a moving
//     entity - a sprite on a lamp in the map belongs to light that is
//     already baked. Colour from the sprite and its Render Color, brightness
//     follows the sprite (a blinking sprite gives a blinking light).
//   - afterglow: a crossbow bolt stuck in a wall glows on hot yellow-orange
//     and cools down to a deep orange. Started by the bolt's impact effect
//     ("BoltImpact", c_stickybolt.cpp, line marked "mapbase-fidelity"): the
//     bolt in flight is models/weapons/w_missile_closed.mdl in Mapbase and
//     the stuck one a temporary model, so there is nothing to find by name.
//   - grenades: a red light that grows stronger while the grenade is live
//     and flashes with every blip (its tick sound, read from the playing
//     sounds, so light and sound agree); its glow sprite gets no extra light.
//   - Combine balls: a bright blue-white light.
//   - fixed colours for some models (s_GlowColors: gravity gun orange ...).
//   - lights of weapons in the hand and of a stuck bolt are kept in front of
//     walls and away from them (Fidelity_KeepInOpen: behind or right at a
//     wall they hardly light it).
//   - fire: burning NPCs and props (entityflame) give a warm, flickering
//     light, unless the engine lights that flame itself (EF_DIMLIGHT). env_fire
//     is left out: mappers light their fires with flickering baked lights.
//   - muzzle flashes: a short flash of light on the world (the engine's own
//     muzzle flash light only lights models). Called from CTempEnts::
//     MuzzleFlash (c_te_legacytempents.cpp) and the muzzle flashes of NPC and
//     vehicle guns (fx_hl2_tracers.cpp, c_strider.cpp; lines marked
//     "mapbase-fidelity").
//   - static props of the map with a glowing model (Combine consoles), read
//     from the .bsp's static prop lump.
//   - effects: RPG rocket flame, NPC lasers where they hit, explosion
//     fireballs, tracers flying with their light, sparks on metal, electric
//     sparks, vortigaunt zaps, open rollermines (hooks in fx_explosion.cpp,
//     fx_impact.cpp, fx_sparks.cpp, fx.cpp, fx_hl2_tracers.cpp,
//     c_vort_charge_token.cpp).
//
// Every light fades in and out (weapon switch, reload, coming into range)
// instead of switching on and off.
//
// Brightness and reach follow Valve's own engine lights (explosion: 255 with
// radius 255, flare: 255 with radius ~120, EF_DIMLIGHT: 100 with radius 200):
// below a radius of about 150 a light hardly shows on walls, so no glow
// light is smaller than that.
//
// r_fidelity_glow_debug 1 shows every light in the world (kind, brightness,
// radius) and prints the list once a second, with the engine lights in use.
//
// A material can steer it with its own keys:
//   "$fidelityglow"       "0"            no light for this material
//   "$fidelityglowcolor"  "[1 0.5 0.1]"  this colour instead of the computed one
//
// World-space engine lights (like a muzzle flash), so the result is the same
// from every view direction. Cost: only the nearest r_fidelity_glow_count
// things light at once, fading out with distance; the list of glowing
// entities (and static props in reach) is rebuilt twice a second, a frame
// only sorts that short list. Each model is analysed once. Static props of
// the map (no entities) are read from the .bsp when the map starts.
//
// Only 32 engine lights exist for everything (an array in engine.dll): lights
// close together share one, the most important get world lights, the rest
// light only models (elights) - see GiveLights.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_glow.h"
#include "fidelity_audio.h"
#include "fidelity_effects.h"
#include "fidelity_texture.h"
#include "fidelity_model.h"
#include "fidelity_flashes.h"
#include "igamesystem.h"
#include "dlight.h"
#include "iefx.h"
#include "fx.h"
#include "view.h"
#include "Sprite.h"
#include "c_fire_smoke.h"
#include "c_smoke_trail.h"
#include "beam_shared.h"
#include "engine/IEngineSound.h"
#include "engine/ivdebugoverlay.h"
#include "filesystem.h"
#include "datacache/imdlcache.h"
#include "studio.h"
#include "bspfile.h"
#include "gamebspfile.h"
#include "model_types.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "utlmap.h"
#include "utlstring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_glow( "r_fidelity_glow", "1", FCVAR_ARCHIVE, "Glowing models and sprites on moving things light their surroundings. 0 = off." );
ConVar r_fidelity_glow_count( "r_fidelity_glow_count", "16", FCVAR_ARCHIVE, "How many glowing things light their surroundings at once (the nearest ones)." );
ConVar r_fidelity_glow_elights( "r_fidelity_glow_elights", "16", FCVAR_ARCHIVE, "When the engine's 32 dynamic lights are taken: how many more glow lights "
	"may light only models (props, NPCs, weapons), not the world. 0 = none." );
ConVar r_fidelity_glow_distance( "r_fidelity_glow_distance", "1500", FCVAR_ARCHIVE, "Distance from the camera at which the lights have faded out." );
ConVar r_fidelity_glow_brightness( "r_fidelity_glow_brightness", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Brightness of the light from glowing things." );
ConVar r_fidelity_glow_radius( "r_fidelity_glow_radius", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Reach of the light from glowing things (scales the size-based radius)." );
static void Fidelity_GlowDebugChanged( IConVar *pVar, const char *pszOld, float flOld );
ConVar r_fidelity_glow_debug( "r_fidelity_glow_debug", "0", 0, "Shows the glow lights in the world and lists them in the console once a second; "
	"models are analysed again and their glowing parts listed. 2 = also every material checked, with its shader "
	"(set it before loading the map to see the static props).", Fidelity_GlowDebugChanged );
ConVar r_fidelity_muzzlelight( "r_fidelity_muzzlelight", "1", FCVAR_ARCHIVE, "Muzzle flashes light the world for a moment. 0 = off." );
ConVar r_fidelity_sparklight( "r_fidelity_sparklight", "1", FCVAR_ARCHIVE, "Bullets hitting metal light it up for a moment (sparks). 0 = off." );
ConVar r_fidelity_muzzlelight_brightness( "r_fidelity_muzzlelight_brightness", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Brightness of the light from muzzle flashes." );

// Keys of our engine lights: far away from entity indices (the engine's own
// lights use those). Each kind has its own range, so two lights never share
// an engine light by accident:
//   GLOW_LIGHT_KEY_BASE +   0x0000 - 0x1FFF  entities (their entity list slot,
//                                            client-only entities too)
//                       +   0xA000 - 0xAFFF  static props of the map
//                       +   0xC000 ...       crossbow afterglows
//                       +   0xD000 ...       explosion fireballs
//                       +   0xE000 ...       tracers
//   MUZZLE_LIGHT_KEY_BASE + 0x0000 - 0x1FFF  muzzle flashes (entity index)
//                       +   0x8000 ...       sparks
//                       +   0x9000 ...       vortigaunt zaps
#define GLOW_LIGHT_KEY_BASE		0x46440000
#define MUZZLE_LIGHT_KEY_BASE	0x46450000
#define STATIC_KEY_BASE			0xA000
#define AFTERGLOW_KEY_BASE		0xC000
#define EXPLOSION_KEY_BASE		0xD000
#define TRACER_KEY_BASE			0xE000
#define SPARK_KEY_BASE			( MUZZLE_LIGHT_KEY_BASE + 0x8000 )
#define ZAP_KEY_BASE			( MUZZLE_LIGHT_KEY_BASE + 0x9000 )
// Engine light colours are 0..255 (100 is the engine's own "dim light").
#define GLOW_LIGHT_SCALE		220.0f
#define MUZZLE_LIGHT_SCALE		300.0f
#define MUZZLE_RAPID_GAP			0.25f	// seconds: a shot this soon after the last one is sustained fire
#define MUZZLE_RAPID_BRIGHTNESS		0.35f	// its flash's light (both lights)
#define MUZZLE_LIGHT_WITH_PROJECTED	0.35f	// the engine light of a flash that has a projected light too (0.5: "harsch")
// How far the light sits in front of the glowing surface, in units.
#define GLOW_SURFACE_OFFSET		6.0f
// Least distance from walls of a weapon light (three lightmap texels; at 32
// the light still went away at walls in game) and of the small lights on
// surfaces (afterglow, explosion, beam hit, laser dot, zap): closer, the wall
// hardly shows the light.
#define GLOW_WALL_DISTANCE		48.0f
#define LIGHT_WALL_DISTANCE		20.0f
// Seconds to fade a model light in / out, and a sprite light (fast, so
// blinking stays a blink).
#define GLOW_FADE_IN			0.25f
#define GLOW_FADE_OUT			0.4f
#define SPRITE_FADE_IN			0.03f
#define SPRITE_FADE_OUT			0.08f
#define MAX_GLOW_SKINS			8
// Share of the vertices of a model's glowing materials that really glow: below
// the first no light (a red dot sight), from the second on full light
// (charger lights, gravity gun core).
#define GLOW_SHARE_MIN			0.004f
#define GLOW_SHARE_FULL			0.025f
#define GLOW_DEBUG_POINTS		24
#define GLOW_MAX_BONES			4		// bones the glowing part hangs on (the heaviest)
// Reach of model glows: from the model's size, but never so small that walls
// stay dark; weapons in the hand light the wall in front of the player.
#define GLOW_RADIUS_MIN			220.0f	// (150 / 320 and 2 x the size: a charger's light only showed close by -
#define GLOW_RADIUS_MAX			380.0f	// Julius 26.10.2026: "Die Range sollte deutlich hoeher sein")
#define GLOW_RADIUS_SIZE		2.5f	// times the model's size
#define VIEWMODEL_RADIUS		160.0f	// (240 lit a whole wall - Julius 26.10.2026, bugbait)
// Weapons in the hand on the world: a constant share of their light. It used to
// dim with a wall close (Fidelity_WallDimming, against glaring corners), but then
// the light got darker the closer the player came (Julius 26.10.2026: "naeher
// dunkler, weiter weg heller"); kept out of the wall (Fidelity_KeepInOpen) and
// this much weaker it cannot glare either.
#define VIEWMODEL_WORLD_BRIGHTNESS	0.6f
// The model-only light of a weapon in the hand, at its glowing part (see m_bSplit)
// (weak: it sits a few units from the weapon's own surfaces - at 0.5 the AR2
// was still blinding in game)
#define VIEWMODEL_MODEL_RADIUS		100.0f
#define VIEWMODEL_MODEL_BRIGHTNESS	0.15f
// Burning things: warm, flickering, a bit brighter than glows; radius from
// the size of what burns.
static const float s_vecFireColor[ 3 ] = { 1.0f, 0.55f, 0.18f };
#define FIRE_BRIGHTNESS			1.3f
#define FIRE_RADIUS_MIN			128.0f
#define FIRE_RADIUS_MAX			320.0f
// The engine has MAX_DLIGHTS (32) dynamic lights for everything (flares,
// explosions, sparks, Gunship, Strider ...); when all are taken, a new one
// overwrites an old one. Our lights only use free slots and leave these free.
#define GLOW_RESERVED_SLOTS		6
// Beyond the 32 (an engine array, can not grow): lights near each other share
// one engine light (closer than GLOW_MERGE_SHARE of the smaller radius), the
// most important ones (bright, wide, near the camera) get world lights, the
// rest model-only lights (elights, an own array of the engine; muzzle flashes
// use some, so at most r_fidelity_glow_elights).
#define GLOW_MERGE_SHARE		0.3f
#define MAX_GLOW_ELIGHTS		32

//-----------------------------------------------------------------------------
// How many engine lights are taken by others than us (glow and muzzle keys).
//-----------------------------------------------------------------------------
static int Fidelity_OtherLights()
{
	dlight_t *pList[ MAX_DLIGHTS ];
	const int nActive = effects->CL_GetActiveDLights( pList );
	int nOthers = 0;
	for ( int i = 0; i < nActive; i++ )
	{
		if ( pList[ i ]->key < GLOW_LIGHT_KEY_BASE || pList[ i ]->key >= MUZZLE_LIGHT_KEY_BASE + 0x10000 )
			nOthers++;
	}
	return nOthers;
}

//-----------------------------------------------------------------------------
// Short flashes (muzzle, sparks, zaps) only start while fewer than
// MAX_DLIGHTS - nKeepFree engine lights are taken (by anyone, us included):
// many flashes at once (a firefight) never push out a flare or an explosion.
//-----------------------------------------------------------------------------
static bool Fidelity_FlashFits( int nKeepFree )
{
	dlight_t *pList[ MAX_DLIGHTS ];
	return effects->CL_GetActiveDLights( pList ) < MAX_DLIGHTS - nKeepFree;
}

//-----------------------------------------------------------------------------
// Afterglow of a crossbow bolt stuck in a wall: hot yellow-orange, cooling
// down to a deep orange.
//-----------------------------------------------------------------------------
static const float s_vecAfterglowHot[ 3 ] = { 1.0f, 0.72f, 0.3f };
static const float s_vecAfterglowCool[ 3 ] = { 1.0f, 0.35f, 0.08f };
#define AFTERGLOW_SECONDS		5.0f
#define AFTERGLOW_RADIUS		170.0f
#define AFTERGLOW_BRIGHTNESS	1.6f
#define MAX_AFTERGLOWS			16

struct FidelityGlowSkin_t
{
	bool m_bGlows;
	Vector m_vecColor;		// linear, brightest channel 1
	Vector m_vecLocalPos;	// where the light sits, in model space (models without bones in the draw)
	// Where the glowing vertices are, per bone they hang on (bone space, as the
	// renderer skins them): the light follows the bones as they are drawn.
	int m_nBones;
	int m_nBone[ GLOW_MAX_BONES ];
	Vector m_vecBonePos[ GLOW_MAX_BONES ];		// weighted middle, bone space
	Vector m_vecBoneNormal[ GLOW_MAX_BONES ];	// weighted normal sum, bone space
	float m_flBoneWeight[ GLOW_MAX_BONES ];
	float m_flRadius;		// light radius before r_fidelity_glow_radius
	float m_flStrength;		// 0..1 by the size of the glowing part
	int m_nColorOverride;	// index into s_GlowColors (by model name), -1 = none
	Vector m_vecDebugPoints[ GLOW_DEBUG_POINTS ];	// some glowing vertices (bone space), for r_fidelity_glow_debug
	int m_nDebugBone[ GLOW_DEBUG_POINTS ];
	int m_nDebugPoints;
};

struct FidelityGlowModel_t
{
	bool m_bAnySkinGlows;
	// Things that can run empty (health and suit chargers: a sequence "empty"):
	// their "idle" sequence shows the charge used up as its cycle (0 full,
	// 1 empty). -1 = none.
	int m_nEmptySequence;
	int m_nEmptyClickSequence;
	int m_nIdleSequence;
	int m_nSkins;
	FidelityGlowSkin_t m_Skins[ MAX_GLOW_SKINS ];
};

enum FidelityGlowKind_t
{
	GLOW_KIND_MODEL,
	GLOW_KIND_SPRITE,
	GLOW_KIND_FIRE,
	GLOW_KIND_GRENADE,
	GLOW_KIND_COMBINEBALL,
	GLOW_KIND_AFTERGLOW,
	GLOW_KIND_ROCKET,
	GLOW_KIND_BEAM,
	GLOW_KIND_EXPLOSION,
	GLOW_KIND_TRACER,
	GLOW_KIND_STATIC,
	GLOW_KIND_ROLLERMINE,
	GLOW_KIND_COUNT
};

static const char *s_pszGlowKinds[] = { "model", "sprite", "fire", "grenade", "combineball", "afterglow", "rocket", "beam", "explosion", "tracer", "static", "rollermine" };
COMPILE_TIME_ASSERT( ARRAYSIZE( s_pszGlowKinds ) == GLOW_KIND_COUNT );

// Pulse tracers (AR2, Combine soldiers, gunship): a blue-white light flying along
static const float s_vecTracerColor[ 3 ] = { 0.55f, 0.75f, 1.0f };
#define TRACER_BRIGHTNESS		0.9f
#define TRACER_RADIUS			120.0f
#define MAX_TRACERS				8

// RPG rocket (its flame, RocketTrail while it emits): warm, flickering
static const float s_vecRocketColor[ 3 ] = { 1.0f, 0.62f, 0.28f };
#define ROCKET_BRIGHTNESS		2.2f
#define ROCKET_RADIUS			320.0f
// Beams of NPCs (the stalker's laser): a small light where they hit
#define BEAM_BRIGHTNESS			1.5f
#define BEAM_RADIUS				140.0f
// Explosions: the engine's own light lasts 0.1 s; a fireball light fades
// from hot yellow over orange to red
static const float s_vecExplosionHot[ 3 ] = { 1.0f, 0.85f, 0.5f };
static const float s_vecExplosionCool[ 3 ] = { 0.9f, 0.3f, 0.08f };
#define EXPLOSION_SECONDS		0.9f
#define EXPLOSION_BRIGHTNESS	3.0f
#define EXPLOSION_RADIUS		450.0f
#define MAX_EXPLOSIONS			8
// An open rollermine: the electric arcs around it, flickering blue-white
static const float s_vecRollermineColor[ 3 ] = { 0.75f, 0.85f, 1.0f };
#define ROLLERMINE_BRIGHTNESS	0.9f
#define ROLLERMINE_RADIUS		160.0f
// NPCs with a tiny glowing part (eyes, lenses): a small dim light
#define NPC_EYE_BRIGHTNESS		0.4f
#define NPC_EYE_RADIUS			72.0f
#define NPC_LIGHT_FORWARD		14.0f	// NPC glow light this far in front of them (units)
#define NPC_LIGHT_COOL			0.35f	// ... and this much towards a cool blue
// Tiny glowing parts of things that are no weapon (console lamps): a small, dim glow
#define SMALL_GLOW_BRIGHTNESS	0.3f
#define SMALL_GLOW_RADIUS		96.0f
// Sprites a player or NPC carries without a parent (the RPG's laser dot):
// mostly red, a little of the sprite's own (white) colour
#define DOT_RADIUS				80.0f
#define DOT_FADE				0.35f
#define DOT_RED_SHARE			0.8f
static const float s_vecDotRed[ 3 ] = { 1.0f, 0.1f, 0.06f };

// One engine light: one glow light or several close together
struct FidelityGlowCluster_t
{
	int m_nKey;				// key of its most important light
	bool m_bElight;			// lights models only
	bool m_bWorldOnly;		// lights only the world (weapon lights kept off walls, see m_bSplit)
	Vector m_vecOriginSum;	// origins weighted with brightness
	Vector m_vecEnergy;		// colours times brightness, summed
	float m_flWeight;		// brightness summed
	float m_flRadius;
};

struct FidelityGlowOrder_t
{
	int m_nLight;
	float m_flScore;
};

struct FidelityGlowCandidate_t
{
	EHANDLE m_hEntity;
	int m_nKind;		// FidelityGlowKind_t
	int m_nStatic;		// GLOW_KIND_STATIC: index into m_StaticProps
	float m_flDistSqr;
};

// A static prop of the map with a glowing model (prop_static has no entity:
// read from the map's static prop lump). VRAD does not bake light from models,
// so without this a Combine console in a map lights nothing.
struct FidelityStaticGlow_t
{
	const model_t *m_pModel;
	matrix3x4_t m_ToWorld;
	Vector m_vecCenter;
	int m_nSkin;
};
#define MAX_STATIC_GLOWS		0x1000

// One light that is on or fading
struct FidelityGlowLight_t
{
	EHANDLE m_hEntity;
	int m_nKey;
	Vector m_vecOrigin;
	Vector m_vecColor;
	float m_flRadius;
	float m_flIntensity;		// what is shown, follows m_flTarget
	float m_flTarget;
	float m_flFadeIn;
	float m_flFadeOut;
	float m_flStartTime;		// when it was made (afterglow, explosion, tracer: their age)
	int m_nKind;				// FidelityGlowKind_t, for r_fidelity_glow_debug
	float m_flFirstSeen;		// grenades: when the light started (the warning gets stronger)
	float m_flPulse;			// grenades: flash of the last blip, 1 .. 0
	int m_nLastBlipGuid;		// grenades: the blip sound already flashed for
	Vector m_vecTravel;			// tracers: direction times speed (units/s)
	float m_flTravelTime;		// tracers: seconds until it arrives
	float m_flBrightness;		// tracers: brightness (thick tracers brighter)
	// Weapons in the hand: the light for the world is kept off walls (m_vecOrigin,
	// see Fidelity_KeepInOpen) and lights only the world - there it would sit
	// right in front of the weapon and blind it (seen in game: crossbow, AR2).
	// The weapon and what is near get a weaker light for models only where it
	// really glows (m_vecModelOrigin).
	bool m_bSplit;
	Vector m_vecModelOrigin;
	float m_flModelScale;		// brightness of that light (VIEWMODEL_MODEL_BRIGHTNESS, more while reloading)
	float m_flWorldScale;		// and of the light for the world (VIEWMODEL_WORLD_BRIGHTNESS)
	// Lights only the world: the glow of an NPC (its eyes) lit its own face -
	// a bright yellow spot on the Combine soldier's gas mask (seen in game)
	bool m_bNoModels;
	bool m_bSeen;
};

//-----------------------------------------------------------------------------
// Keeps a light in the open and away from walls. A light inside or behind a
// surface does not light its front, and one just in front of it hardly
// either: the engine lights walls per lightmap texel (usually 16 units) with
// 1/r^2 from the texel's centre, so a light a few units off the wall reaches
// one or two texels (seen in game: weapon light at a wall almost gone, the
// bolt's impact dark). vecFrom must be in the open (the eye, the flight path);
// the light ends up at least flMin from the wall it would hit and from walls
// to its sides.
//-----------------------------------------------------------------------------
static void Fidelity_PushOff( Vector &vecLight, const trace_t &tr, float flMin )
{
	// flMin off the surface along its normal, as far as there is room
	trace_t trOut;
	UTIL_TraceLine( tr.endpos + tr.plane.normal, tr.endpos + tr.plane.normal * flMin, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &trOut );
	vecLight = trOut.endpos - tr.plane.normal * ( trOut.fraction < 1.0f ? 2.0f : 0.0f );
}

// Off a wall found by a trace from the light: along the wall's normal until
// flMin from its plane, as far as there is room
static void Fidelity_PushOffPlane( Vector &vecLight, const trace_t &tr, float flMin )
{
	const float flDist = DotProduct( vecLight - tr.endpos, tr.plane.normal );
	if ( flDist >= flMin )
		return;
	trace_t trOut;
	UTIL_TraceLine( vecLight, vecLight + tr.plane.normal * ( flMin - flDist ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &trOut );
	vecLight = trOut.endpos - tr.plane.normal * ( trOut.fraction < 1.0f ? 2.0f : 0.0f );
}

static void Fidelity_KeepInOpen( const Vector &vecFrom, Vector &vecLight, float flMin )
{
	// The wall ahead on the line from vecFrom through the light: the light goes
	// back along that line until it is flMin from the wall's plane. Continuous -
	// it moves smoothly as the wall comes near (it used to jump onto the wall's
	// normal when the wall came into reach, seen in game as a sudden switch).
	// Close to the wall that can be behind vecFrom (where the player stands).
	Vector vecDir = vecLight - vecFrom;
	const float flLength = VectorNormalize( vecDir );
	const float flReach = flLength + flMin / 0.3f;
	trace_t tr;
	UTIL_TraceLine( vecFrom, vecFrom + vecDir * flReach, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction < 1.0f && !tr.startsolid )
	{
		const float flCos = MAX( -DotProduct( vecDir, tr.plane.normal ), 0.3f );
		const float flMax = tr.fraction * flReach - flMin / flCos;
		if ( flLength > flMax )
		{
			if ( flMax >= 0.0f )
			{
				vecLight = vecFrom + vecDir * flMax;
			}
			else
			{
				// Behind vecFrom: as far as there is room
				trace_t trBack;
				UTIL_TraceLine( vecFrom, vecFrom + vecDir * flMax, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &trBack );
				vecLight = trBack.endpos - vecDir * ( trBack.fraction < 1.0f ? -2.0f : 0.0f );
			}
		}
	}

	// Walls to the sides (strafing along a wall) and corners (diagonals): the
	// edge of a thin wall or pillar next to the light lay between the four
	// side traces, and its narrow face lit up glaring (Julius: gravity gun at
	// small walls). Each pushes the light along the wall's normal just as far
	// as needed, without moving it sideways.
	static const Vector s_vecSides[] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ),
		Vector( 0.7071f, 0.7071f, 0 ), Vector( 0.7071f, -0.7071f, 0 ), Vector( -0.7071f, 0.7071f, 0 ), Vector( -0.7071f, -0.7071f, 0 ) };
	for ( int i = 0; i < ARRAYSIZE( s_vecSides ); i++ )
	{
		UTIL_TraceLine( vecLight, vecLight + s_vecSides[ i ] * flMin, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 1.0f && !tr.startsolid )
			Fidelity_PushOffPlane( vecLight, tr, flMin );
	}
}

//-----------------------------------------------------------------------------
// Colours that should not come from the texture average (one line per model,
// matched as part of the model name).
//-----------------------------------------------------------------------------
struct FidelityGlowColor_t
{
	const char *m_pszModel;
	float m_vecColor[ 3 ];
	float m_flBrightness;
	float m_flReloadFlare;	// in the hand: the weapon's own light this much brighter halfway through a reload (0 = not)
};

static const FidelityGlowColor_t s_GlowColors[] =
{
	{ "physcannon", { 1.0f, 0.5f, 0.12f }, 1.5f, 0.0f },	// gravity gun: orange (Julius)
	{ "crossbow", { 1.0f, 0.45f, 0.1f }, 1.3f, 0.0f },		// glowing bolt
	// AR2: red, weak on the walls (Julius: "was an Wänden abgeht, ist so
	// stark", 0.55 still "etwas zu stark"); while reloading the weapon glows up
	// red ("vorher so schön kräftig rot kurz"; x 6 was too visible)
	{ "irifle", { 1.0f, 0.22f, 0.07f }, 0.4f, 3.0f },
	// bugbait: its few blue specks lit a whole wall deep blue (Julius 26.10.2026: "viel zu hell");
	// at 0.2 and 0.45 it gave hardly any light ("immer noch kein Licht": fidelity_report
	// showed 0.1 .. 0.23 of full at a wall): full strength again, but only as far as any
	// weapon in the hand reaches (VIEWMODEL_RADIUS 160, not the 220 .. 380 that lit a wall)
	{ "bugbait", { 0.45f, 0.65f, 1.0f }, 1.0f, 0.0f },
};

// The entry of s_GlowColors for a model, -1 = none (looked up once per model)
static int Fidelity_ColorOverrideOf( const model_t *pModel )
{
	const char *pszModel = modelinfo->GetModelName( pModel );
	for ( int n = 0; n < ARRAYSIZE( s_GlowColors ); n++ )
	{
		if ( Q_stristr( pszModel, s_GlowColors[ n ].m_pszModel ) )
			return n;
	}
	return -1;
}

// A fixed colour and brightness for some models (s_GlowColors)
static void Fidelity_ApplyColorOverride( const FidelityGlowSkin_t &skin, FidelityGlowLight_t &light )
{
	if ( skin.m_nColorOverride < 0 )
		return;
	const FidelityGlowColor_t &entry = s_GlowColors[ skin.m_nColorOverride ];
	light.m_vecColor.Init( entry.m_vecColor[ 0 ], entry.m_vecColor[ 1 ], entry.m_vecColor[ 2 ] );
	light.m_flTarget *= entry.m_flBrightness;
}

// A weapon in the hand that glows up while reloading (s_GlowColors): 0 at
// the start and end of the reload, the full flare halfway
static float Fidelity_ReloadFlare( C_BaseAnimating *pAnimating, const FidelityGlowSkin_t &skin )
{
	if ( skin.m_nColorOverride < 0 || s_GlowColors[ skin.m_nColorOverride ].m_flReloadFlare <= 0.0f )
		return 0.0f;
	if ( pAnimating->GetSequenceActivity( pAnimating->GetSequence() ) != ACT_VM_RELOAD )
		return 0.0f;
	return s_GlowColors[ skin.m_nColorOverride ].m_flReloadFlare * sinf( M_PI_F * clamp( pAnimating->GetCycle(), 0.0f, 1.0f ) );
}

// Grenades and Combine balls (fixed looks)
static const float s_vecGrenadeColor[ 3 ] = { 1.0f, 0.06f, 0.03f };
static const float s_vecCombineBallColor[ 3 ] = { 0.3f, 0.6f, 1.0f };	// (Julius: "ein bisschen mehr blauer")
#define GRENADE_RAMP_SECONDS		2.5f	// from a faint glow to full warning
#define GRENADE_PULSE_SECONDS		0.18f	// a blip's flash fades in this time
#define COMBINEBALL_BRIGHTNESS		2.0f
#define COMBINEBALL_RADIUS			320.0f

//-----------------------------------------------------------------------------
// Makes a colour more saturated and scales it so the brightest channel is 1.
//-----------------------------------------------------------------------------
static Vector Fidelity_GlowSaturate( const Vector &vecColor, float flSaturation )
{
	const float flGray = ( vecColor.x + vecColor.y + vecColor.z ) / 3.0f;
	Vector vecResult = Vector( flGray, flGray, flGray ) + ( vecColor - Vector( flGray, flGray, flGray ) ) * flSaturation;
	vecResult.x = MAX( vecResult.x, 0.0f );
	vecResult.y = MAX( vecResult.y, 0.0f );
	vecResult.z = MAX( vecResult.z, 0.0f );
	const float flMax = MAX( vecResult.x, MAX( vecResult.y, vecResult.z ) );
	return ( flMax > 0.0001f ) ? vecResult / flMax : Vector( 1, 1, 1 );
}

//-----------------------------------------------------------------------------
// Colour of an effect material (a tracer, a muzzle flash): its texture's
// average, saturated, brightest channel 1 - so the light has the colour the
// player sees. vecDefault without a material or when it is missing. Looked up
// once per name (a handful of effect materials).
//-----------------------------------------------------------------------------
struct FidelityMaterialColor_t
{
	CUtlString m_Name;
	Vector m_vecColor;
};
static CUtlVector< FidelityMaterialColor_t > s_MaterialColors;

static Vector Fidelity_MaterialColor( const char *pszMaterial, const Vector &vecDefault )
{
	if ( !pszMaterial )
		return vecDefault;
	for ( int i = 0; i < s_MaterialColors.Count(); i++ )
	{
		if ( !Q_stricmp( s_MaterialColors[ i ].m_Name.Get(), pszMaterial ) )
			return s_MaterialColors[ i ].m_vecColor;
	}
	FidelityMaterialColor_t &entry = s_MaterialColors[ s_MaterialColors.AddToTail() ];
	entry.m_Name = pszMaterial;
	entry.m_vecColor = vecDefault;
	IMaterial *pMaterial = materials->FindMaterial( pszMaterial, TEXTURE_GROUP_CLIENT_EFFECTS, false );
	if ( pMaterial && !pMaterial->IsErrorMaterial() )
	{
		Vector vecReflectivity;
		pMaterial->GetReflectivity( vecReflectivity );
		if ( vecReflectivity.LengthSqr() > 0.0001f )
			entry.m_vecColor = Fidelity_GlowSaturate( vecReflectivity, 1.5f );
	}
	return entry.m_vecColor;
}

//-----------------------------------------------------------------------------
// How a material glows: its tint ($selfillumtint, or the $fidelityglowcolor
// override), its base texture and where it glows (the mask). $selfillum
// glows where the selfillum mask says: the base texture's alpha, or the
// $selfillummask texture. $additive and unlit materials (UnlitGeneric: drawn
// at full brightness whatever the light, like Combine screens) glow where
// they are bright.
//-----------------------------------------------------------------------------
struct FidelityGlowMaterial_t
{
	Vector m_vecTint;
	bool m_bOverride;
	bool m_bAdditive;					// glows where bright (no mask)
	const CFidelityImage *m_pBase;		// NULL if it can not be read
	const CFidelityImage *m_pMask;		// selfillum mask, NULL: base alpha (or none readable)
};

static const CFidelityImage *Fidelity_MaterialImage( IMaterial *pMaterial, const char *pszVar )
{
	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
	if ( !bFound || !pVar->IsTexture() || !pVar->GetTextureValue() || pVar->GetTextureValue()->IsError() )
		return NULL;
	return Fidelity_TextureImage( pVar->GetTextureValue()->GetName() );
}

// Does this material glow? Fills info.
static bool Fidelity_MaterialGlows( IMaterial *pMaterial, FidelityGlowMaterial_t &info )
{
	info.m_vecTint.Init( 1, 1, 1 );
	info.m_bOverride = false;
	info.m_bAdditive = false;
	info.m_pBase = info.m_pMask = NULL;
	if ( !pMaterial || pMaterial->IsErrorMaterial() )
		return false;
	// Plants never glow (grass3 lit the ground around it - seen in game)
	if ( Q_stristr( pMaterial->GetName(), "foliage" ) || Q_stristr( pMaterial->GetName(), "grass" ) )
		return false;
	const bool bSelfIllum = pMaterial->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM );
	// Unlit: screens and monitors (scrolling ones use UnlitTwoTexture), not
	// cut-out or see-through ones.
	const char *pszShader = pMaterial->GetShaderName();
	const char *pszUnlit = pszShader ? ( !Q_strnicmp( pszShader, "SDK_", 4 ) ? pszShader + 4 : pszShader ) : "";
	const bool bUnlit = ( !Q_stricmp( pszUnlit, "UnlitGeneric" ) || !Q_stricmp( pszUnlit, "UnlitTwoTexture" ) || !Q_stricmp( pszUnlit, "MonitorScreen" ) ) &&
		!pMaterial->GetMaterialVarFlag( MATERIAL_VAR_ALPHATEST ) && !pMaterial->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT );
	if ( r_fidelity_glow_debug.GetInt() >= 2 )
		Msg( "[fidelity] glow material %s: shader %s%s%s\n", pMaterial->GetName(), pszShader ? pszShader : "?",
			bSelfIllum ? ", $selfillum" : "", bUnlit ? ", unlit" : "" );
	if ( !bSelfIllum && !bUnlit && !pMaterial->GetMaterialVarFlag( MATERIAL_VAR_ADDITIVE ) )
		return false;

	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( "$fidelityglow", &bFound, false );
	if ( bFound && pVar->GetFloatValue() == 0.0f )
		return false;

	info.m_bAdditive = !bSelfIllum;
	info.m_pBase = Fidelity_MaterialImage( pMaterial, "$basetexture" );
	if ( bSelfIllum )
		info.m_pMask = Fidelity_MaterialImage( pMaterial, "$selfillummask" );

	pVar = pMaterial->FindVar( "$fidelityglowcolor", &bFound, false );
	if ( bFound )
	{
		pVar->GetVecValue( info.m_vecTint.Base(), 3 );
		info.m_bOverride = true;
		return true;
	}
	pVar = pMaterial->FindVar( "$selfillumtint", &bFound, false );
	if ( bFound )
		pVar->GetVecValue( info.m_vecTint.Base(), 3 );
	return true;
}

// How much a point at u, v glows (0..1) and its colour (gamma, 0..1)
static float Fidelity_GlowAt( const FidelityGlowMaterial_t &info, IMaterial *pMaterial, float u, float v, Vector &vecColor )
{
	float rgba[ 4 ];
	if ( info.m_pBase )
	{
		info.m_pBase->Sample( u, v, rgba );
		vecColor.Init( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ] );
		const float flBright = MAX( rgba[ 0 ], MAX( rgba[ 1 ], rgba[ 2 ] ) );
		if ( info.m_bAdditive )
			return clamp( ( flBright - 0.15f ) / 0.35f, 0.0f, 1.0f );
		if ( info.m_pMask )
		{
			float mask[ 4 ];
			info.m_pMask->Sample( u, v, mask );
			return MAX( mask[ 0 ], MAX( mask[ 1 ], mask[ 2 ] ) );
		}
		return rgba[ 3 ];
	}
	// Texture not readable: the 16x16 colour thumbnail, weighted by how colourful and bright it is
	pMaterial->GetLowResColorSample( u, v, rgba );
	vecColor.Init( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ] );
	const float flMax = MAX( rgba[ 0 ], MAX( rgba[ 1 ], rgba[ 2 ] ) );
	const float flMin = MIN( rgba[ 0 ], MIN( rgba[ 1 ], rgba[ 2 ] ) );
	return clamp( ( flMax - flMin ) * 2.0f + 0.2f * flMax, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// Where, in which colour and how strongly one skin of a model glows. Only the
// glowing parts count (seen in game: the health charger's light sat between
// all its colourful parts instead of at its blue lights, the MP7's tiny red
// dot and the revolver's sights lit the room).
//-----------------------------------------------------------------------------
static void Fidelity_AnalyseSkin( const model_t *pModel, studiohdr_t *pStudioHdr, const studiohwdata_t *pHardware,
	const CUtlVector< const mstudiovertex_t * > &vertices, int nSkin, FidelityGlowSkin_t &skin )
{
	skin.m_bGlows = false;
	skin.m_vecColor.Init( 1, 1, 1 );
	skin.m_vecLocalPos.Init();
	skin.m_flRadius = 0.0f;
	skin.m_flStrength = 0.0f;
	skin.m_nDebugPoints = 0;
	skin.m_nBones = 0;

	if ( pStudioHdr->numbones <= 0 )
		return;	// a broken model (every studio model has a root bone)

	const studioloddata_t &lod = pHardware->m_pLODs[ pHardware->m_RootLOD ];
	const short *pSkin = pStudioHdr->pSkinref( nSkin * pStudioHdr->numskinref );

	// Which of the skin's textures glow
	bool bGlows[ MAXSTUDIOSKINS ];
	FidelityGlowMaterial_t infos[ MAXSTUDIOSKINS ];
	Vector vecOverride( 0, 0, 0 );
	int nOverrides = 0;
	bool bAny = false;
	const int nRefs = MIN( pStudioHdr->numskinref, MAXSTUDIOSKINS );
	for ( int i = 0; i < nRefs; i++ )
	{
		bGlows[ i ] = false;
		const int nTexture = pSkin[ i ];
		if ( nTexture < 0 || nTexture >= lod.numMaterials )
			continue;
		bGlows[ i ] = Fidelity_MaterialGlows( lod.ppMaterials[ nTexture ], infos[ i ] );
		bAny |= bGlows[ i ];
		if ( bGlows[ i ] && infos[ i ].m_bOverride )
		{
			vecOverride += infos[ i ].m_vecTint;
			nOverrides++;
		}
	}
	if ( !bAny || vertices.Count() == 0 )
		return;

	// Every vertex of the model, the glowing ones weighted by how much they glow
	Vector vecPosSum( 0, 0, 0 ), vecNormalSum( 0, 0, 0 ), vecColorSum( 0, 0, 0 );
	float flColorWeightSum = 0.0f;	// colourful parts count more for the colour
	// Per bone: glowing vertices in that bone's space (vertex * poseToBone, like the skinning)
	int nBoneIndex[ 16 ];
	Vector vecBonePos[ 16 ], vecBoneNormal[ 16 ];
	float flBoneWeight[ 16 ];
	int nBoneSlots = 0;
	float flWeightSum = 0.0f;
	int nAll = 0, nGlowing = 0;
	const int nVertices = vertices.Count();
	if ( nVertices <= 0 )
		return;

	// One point of a glowing material (model space, bone it hangs on): adds
	// to the sums if it glows
	auto AddSample = [&]( const Vector &vecPos, const Vector &vecNormal, const Vector2D &vecUV, int nVertexBone,
		const FidelityGlowMaterial_t &info, IMaterial *pMaterial )
	{
		Vector vecColor;
		const float flGlow = Fidelity_GlowAt( info, pMaterial, vecUV.x, vecUV.y, vecColor );
		if ( flGlow < 0.3f )
			return;
		nGlowing++;
		const float flWeight = flGlow * ( 0.25f + MAX( vecColor.x, MAX( vecColor.y, vecColor.z ) ) );
		vecPosSum += vecPos * flWeight;
		vecNormalSum += vecNormal * flWeight;

		// In the space of the bone it hangs on most
		const int nBone = clamp( nVertexBone, 0, pStudioHdr->numbones - 1 );
		const matrix3x4_t &poseToBone = pStudioHdr->pBone( nBone )->poseToBone;
		Vector vecInBone, vecNormalInBone;
		VectorTransform( vecPos, poseToBone, vecInBone );
		VectorRotate( vecNormal, poseToBone, vecNormalInBone );
		int nSlot = 0;
		while ( nSlot < nBoneSlots && nBoneIndex[ nSlot ] != nBone )
			nSlot++;
		if ( nSlot == nBoneSlots && nBoneSlots < ARRAYSIZE( nBoneIndex ) )
		{
			nBoneIndex[ nSlot ] = nBone;
			vecBonePos[ nSlot ].Init();
			vecBoneNormal[ nSlot ].Init();
			flBoneWeight[ nSlot ] = 0.0f;
			nBoneSlots++;
		}
		if ( nSlot < nBoneSlots )
		{
			vecBonePos[ nSlot ] += vecInBone * flWeight;
			vecBoneNormal[ nSlot ] += vecNormalInBone * flWeight;
			flBoneWeight[ nSlot ] += flWeight;
		}
		// The colour mostly from the colourful parts: the white text on a blue
		// Combine screen made the light white (Julius: "bläulicher")
		const float flChroma = MAX( vecColor.x, MAX( vecColor.y, vecColor.z ) ) - MIN( vecColor.x, MIN( vecColor.y, vecColor.z ) );
		const float flColorWeight = flWeight * ( 0.15f + 2.0f * flChroma );
		vecColorSum += vecColor * info.m_vecTint * flColorWeight;
		flColorWeightSum += flColorWeight;
		flWeightSum += flWeight;
		// A few of them for r_fidelity_glow_debug (spread over the model)
		if ( skin.m_nDebugPoints < GLOW_DEBUG_POINTS && ( nGlowing % 3 ) == 1 )
		{
			skin.m_nDebugBone[ skin.m_nDebugPoints ] = nBone;
			skin.m_vecDebugPoints[ skin.m_nDebugPoints++ ] = vecInBone;
		}
	};
	for ( int nPart = 0; nPart < pStudioHdr->numbodyparts; nPart++ )
	{
		mstudiobodyparts_t *pPart = pStudioHdr->pBodypart( nPart );
		if ( pPart->nummodels <= 0 )
			continue;
		mstudiomodel_t *pSubModel = pPart->pModel( 0 );
		const int nBase = pSubModel->vertexindex / sizeof( mstudiovertex_t );
		for ( int nMesh = 0; nMesh < pSubModel->nummeshes; nMesh++ )
		{
			mstudiomesh_t *pMesh = pSubModel->pMesh( nMesh );
			if ( pMesh->material < 0 || pMesh->material >= nRefs || !bGlows[ pMesh->material ] )
				continue;
			// The share counts within the glowing materials only: a view model's arms and hands
			// would make a weapon's glowing part look tiny (seen in game: no gravity gun light).
			const FidelityGlowMaterial_t &info = infos[ pMesh->material ];
			IMaterial *pMaterial = lod.ppMaterials[ pSkin[ pMesh->material ] ];
			for ( int i = 0; i < pMesh->numvertices; i++ )
			{
				const int nIndex = nBase + pMesh->vertexoffset + i;
				if ( nIndex < 0 || nIndex >= nVertices )
					break;
				const mstudiovertex_t &vertex = *vertices[ nIndex ];
				nAll++;
				AddSample( vertex.m_vecPosition, vertex.m_vecNormal, vertex.m_vecTexCoord, vertex.m_BoneWeights.bone[ 0 ], info, pMaterial );
			}
		}
	}

	// Points on the triangles too (middle and towards each corner): a big flat
	// screen has its few vertices on the dark frame around it, so by its
	// vertices alone a Combine monitor did not glow at all
	for ( int nRef = 0; nRef < nRefs; nRef++ )
	{
		if ( !bGlows[ nRef ] )
			continue;
		IMaterial *pMaterial = lod.ppMaterials[ pSkin[ nRef ] ];
		bool bDone = false;	// a material on several skin references: once
		for ( int nOther = 0; nOther < nRef && !bDone; nOther++ )
			bDone = bGlows[ nOther ] && lod.ppMaterials[ pSkin[ nOther ] ] == pMaterial;
		CUtlVector< int > triangles;
		if ( bDone || !Fidelity_ModelTriangles( pModel, pStudioHdr, pHardware, pMaterial, triangles ) )
			continue;
		for ( int t = 0; t + 2 < triangles.Count(); t += 3 )
		{
			const mstudiovertex_t *pCorner[ 3 ];
			bool bValid = true;
			for ( int c = 0; c < 3 && bValid; c++ )
			{
				bValid = triangles[ t + c ] >= 0 && triangles[ t + c ] < nVertices;
				pCorner[ c ] = bValid ? vertices[ triangles[ t + c ] ] : NULL;
			}
			if ( !bValid )
				continue;
			const Vector vecMiddle = ( pCorner[ 0 ]->m_vecPosition + pCorner[ 1 ]->m_vecPosition + pCorner[ 2 ]->m_vecPosition ) / 3.0f;
			const Vector2D vecMiddleUV = ( pCorner[ 0 ]->m_vecTexCoord + pCorner[ 1 ]->m_vecTexCoord + pCorner[ 2 ]->m_vecTexCoord ) / 3.0f;
			Vector vecNormal = pCorner[ 0 ]->m_vecNormal + pCorner[ 1 ]->m_vecNormal + pCorner[ 2 ]->m_vecNormal;
			VectorNormalize( vecNormal );
			for ( int k = 0; k < 4; k++ )
			{
				// the middle, then halfway from it to each corner
				const Vector vecPos = k == 0 ? vecMiddle : ( vecMiddle + pCorner[ k - 1 ]->m_vecPosition ) * 0.5f;
				const Vector2D vecUV = k == 0 ? vecMiddleUV : ( vecMiddleUV + pCorner[ k - 1 ]->m_vecTexCoord ) * 0.5f;
				nAll++;
				AddSample( vecPos, vecNormal, vecUV, pCorner[ 0 ]->m_BoneWeights.bone[ 0 ], infos[ nRef ], pMaterial );
			}
		}
	}
	// Too small a glowing part (a sight, a dot) lights nothing; a few percent of the model, full light.
	const float flShare = nAll > 0 ? (float)nGlowing / nAll : 0.0f;
	skin.m_flStrength = clamp( ( flShare - GLOW_SHARE_MIN ) / ( GLOW_SHARE_FULL - GLOW_SHARE_MIN ), 0.0f, 1.0f );
	if ( r_fidelity_glow_debug.GetBool() )
	{
		bool bReadable = true;
		for ( int i = 0; i < nRefs; i++ )
			bReadable &= !bGlows[ i ] || infos[ i ].m_pBase != NULL;
		Vector vecMins, vecMaxs;
		modelinfo->GetModelRenderBounds( pModel, vecMins, vecMaxs );
		const Vector vecMiddle = flWeightSum > 0.0001f ? vecPosSum / flWeightSum : vec3_origin;
		Msg( "[fidelity] glow %s skin %d: %d of %d vertices glow (%.1f%%), strength %.2f, textures %s, middle %.0f %.0f %.0f in bounds %.0f %.0f %.0f .. %.0f %.0f %.0f\n",
			modelinfo->GetModelName( pModel ), nSkin, nGlowing, nAll, flShare * 100.0f, skin.m_flStrength, bReadable ? "read" : "NOT readable (thumbnail used)",
			vecMiddle.x, vecMiddle.y, vecMiddle.z, vecMins.x, vecMins.y, vecMins.z, vecMaxs.x, vecMaxs.y, vecMaxs.z );
	}
	// Tiny glowing parts are kept too (strength 0): NPC eyes get a small light.
	if ( flWeightSum <= 0.0001f || nGlowing == 0 )
		return;

	skin.m_vecLocalPos = vecPosSum / flWeightSum;

	// The heaviest bones (most glowing parts hang on one or two)
	while ( skin.m_nBones < GLOW_MAX_BONES )
	{
		int nBest = -1;
		for ( int i = 0; i < nBoneSlots; i++ )
		{
			if ( flBoneWeight[ i ] > 0.0f && ( nBest < 0 || flBoneWeight[ i ] > flBoneWeight[ nBest ] ) )
				nBest = i;
		}
		if ( nBest < 0 )
			break;
		skin.m_nBone[ skin.m_nBones ] = nBoneIndex[ nBest ];
		skin.m_vecBonePos[ skin.m_nBones ] = vecBonePos[ nBest ] / flBoneWeight[ nBest ];
		skin.m_vecBoneNormal[ skin.m_nBones ] = vecBoneNormal[ nBest ];
		skin.m_flBoneWeight[ skin.m_nBones ] = flBoneWeight[ nBest ];
		skin.m_nBones++;
		flBoneWeight[ nBest ] = 0.0f;
	}
	skin.m_vecColor = Fidelity_GlowSaturate( vecColorSum / MAX( flColorWeightSum, 0.0001f ), 1.5f );
	if ( nOverrides > 0 )
		skin.m_vecColor = Fidelity_GlowSaturate( vecOverride / nOverrides, 1.0f );

	// Out of the surface, so the light does not sit inside the model
	Vector vecNormal = vecNormalSum;
	if ( VectorNormalize( vecNormal ) > 0.0001f )
		skin.m_vecLocalPos += vecNormal * GLOW_SURFACE_OFFSET;

	Vector vecMins, vecMaxs;
	modelinfo->GetModelRenderBounds( pModel, vecMins, vecMaxs );
	skin.m_flRadius = clamp( ( vecMaxs - vecMins ).Length() * GLOW_RADIUS_SIZE, GLOW_RADIUS_MIN, GLOW_RADIUS_MAX );
	skin.m_bGlows = true;
}

//-----------------------------------------------------------------------------
// A bone's place in the world as it is drawn. The glow runs before this
// frame's rendering (game system update in HudUpdate): the bones the renderer
// set up last frame are still in the entity's cache - one frame old, like the
// rest of the light, and free. Only without them (not drawn last frame):
// SetupBones with all bones, once per entity and frame. Entities without
// bones: their own transform.
//-----------------------------------------------------------------------------
static void Fidelity_BoneToWorld( C_BaseEntity *pEntity, int nBone, matrix3x4_t &boneToWorld )
{
	static matrix3x4_t s_Bones[ MAXSTUDIOBONES ];
	static C_BaseEntity *s_pBonesOf = NULL;
	static int s_nBonesFrame = -1;
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	CStudioHdr *pHdr = pAnimating ? pAnimating->GetModelPtr() : NULL;
	if ( !pHdr || nBone < 0 || nBone >= pHdr->numbones() || nBone >= MAXSTUDIOBONES )
	{
		MatrixCopy( pEntity->RenderableToWorldTransform(), boneToWorld );
		return;
	}
	if ( pAnimating->IsBoneCacheValid() )
	{
		pAnimating->GetCachedBoneMatrix( nBone, boneToWorld );
		return;
	}
	if ( s_pBonesOf != pEntity || s_nBonesFrame != gpGlobals->framecount )
	{
		// View models too (HudUpdate allows only normal models)
		C_BaseAnimating::PushAllowBoneAccess( true, true, "fidelity_glow" );
		const bool bOk = pAnimating->SetupBones( s_Bones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime );
		C_BaseAnimating::PopBoneAccess( "fidelity_glow" );
		if ( !bOk )
		{
			MatrixCopy( pEntity->RenderableToWorldTransform(), boneToWorld );
			return;
		}
		s_pBonesOf = pEntity;
		s_nBonesFrame = gpGlobals->framecount;
	}
	MatrixCopy( s_Bones[ nBone ], boneToWorld );
}

//-----------------------------------------------------------------------------
// Where the light of a glowing model sits now: the middle of its glowing
// vertices, moved by their bones as they are drawn (seen in game: computed
// with the entity's own transform, the chargers' light sat beside them), a
// little out of the surface.
//-----------------------------------------------------------------------------
static void Fidelity_GlowOrigin( C_BaseEntity *pEntity, const FidelityGlowSkin_t &skin, Vector &vecOrigin )
{
	if ( skin.m_nBones <= 0 || !pEntity->GetBaseAnimating() )
	{
		VectorTransform( skin.m_vecLocalPos, pEntity->RenderableToWorldTransform(), vecOrigin );
		return;
	}
	Vector vecSum( 0, 0, 0 ), vecNormal( 0, 0, 0 );
	float flWeight = 0.0f;
	for ( int i = 0; i < skin.m_nBones; i++ )
	{
		matrix3x4_t boneToWorld;
		Fidelity_BoneToWorld( pEntity, skin.m_nBone[ i ], boneToWorld );
		Vector vecPoint, vecDir;
		VectorTransform( skin.m_vecBonePos[ i ], boneToWorld, vecPoint );
		VectorRotate( skin.m_vecBoneNormal[ i ], boneToWorld, vecDir );
		vecSum += vecPoint * skin.m_flBoneWeight[ i ];
		vecNormal += vecDir;
		flWeight += skin.m_flBoneWeight[ i ];
	}
	vecOrigin = vecSum / MAX( flWeight, 0.0001f );
	if ( VectorNormalize( vecNormal ) > 0.0001f )
		vecOrigin += vecNormal * GLOW_SURFACE_OFFSET;
}

class CFidelityGlow : public CAutoGameSystemPerFrame
{
public:
	CFidelityGlow() : CAutoGameSystemPerFrame( "CFidelityGlow" ), m_Models( DefLessFunc( const model_t * ) ), m_SpriteColors( DefLessFunc( const model_t * ) ), m_nSoundsFrame( -1 ), m_bStaticPropsLoaded( false ), m_flNextScan( 0.0f ), m_flNextDebug( 0.0f ), m_nAfterglows( 0 ), m_nExplosions( 0 ), m_nTracers( 0 ) {}

	virtual void LevelInitPostEntity() { m_flNextScan = 0.0f; m_bStaticPropsLoaded = false; }
	virtual void LevelShutdownPreEntity() { m_Models.RemoveAll(); m_SpriteColors.RemoveAll(); m_Candidates.RemoveAll(); m_Lights.RemoveAll(); m_StaticProps.RemoveAll(); m_Elights.RemoveAll(); Fidelity_ForgetTextureImages(); }
	virtual void Update( float frametime );
	void AddAfterglow( const Vector &vecOrigin, const Vector &vecDirection );
	void AddExplosion( const Vector &vecOrigin );
	void AddTracer( const Vector &vecStart, const Vector &vecDir, float flSpeed, float flDistance, const char *pszMaterial, float flScale );
	void ForgetModels() { m_Models.RemoveAll(); m_SpriteColors.RemoveAll(); m_flNextScan = 0.0f; m_bStaticPropsLoaded = false; }

private:
	const FidelityGlowModel_t &LookUpModel( const model_t *pModel );
	void LoadStaticProps();
	const FidelityGlowSkin_t *GlowOf( C_BaseEntity *pEntity );
	void Scan();
	FidelityGlowLight_t &Light( C_BaseEntity *pEntity, int nKey, bool bSprite );
	FidelityGlowLight_t &NewLight( int nKind, int nKey );
	int BlipOf( int nEntity );
	bool SpriteLight( C_Sprite *pSprite, FidelityGlowLight_t &light, float flBrightness );
	bool NearAfterglow( const Vector &vecOrigin ) const;
	void Debug( int nCandidates, int nFree );
	void GiveLights( int nFree, float flRadiusScale );
	bool LightStatic( int nStatic, float flBase );
	bool LightFire( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightRocket( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightBeam( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightRollermine( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightCombineBall( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightGrenade( C_BaseEntity *pEntity, int nKey, float flBase );
	bool LightSprite( C_BaseEntity *pEntity, int nKey, float flBase, const Vector &vecView );
	bool LightModel( C_BaseEntity *pEntity, int nKey, float flBase, const Vector &vecView );

	CUtlMap< const model_t *, FidelityGlowModel_t > m_Models;
	CUtlMap< const model_t *, Vector > m_SpriteColors;	// texture colour of sprite models
	CUtlVector< FidelityGlowCandidate_t > m_Candidates;
	CUtlVector< FidelityGlowLight_t > m_Lights;
	CUtlVector< FidelityStaticGlow_t > m_StaticProps;	// static props with glowing models
	CUtlVector< FidelityGlowOrder_t > m_Order;			// lights by importance (per frame, memory kept)
	CUtlVector< FidelityGlowCluster_t > m_Clusters;		// engine lights given this frame
	CUtlVector< int > m_Elights;						// elight keys given last frame
	CUtlVector< int > m_NewElights;						// ... and this frame (memory kept)
	CUtlVector< int > m_ModelLights;					// lights of weapons in the hand (index into m_Lights)
	CUtlVector< const model_t * > m_ViewModelsLit;		// this frame: weapon models in the hand that have a light
	CUtlVector< SndInfo_t > m_Sounds;	// playing sounds, read once per frame while a grenade glows
	int m_nSoundsFrame;
	bool m_bStaticPropsLoaded;	// this map's static props read (the first frame the glow is on)
	float m_flNextScan;
	float m_flNextDebug;
	int m_nAfterglows;		// afterglows started, for their keys
	int m_nExplosions;		// explosions, for their keys
	int m_nTracers;			// tracers, for their keys
};

static CFidelityGlow s_FidelityGlow;

//-----------------------------------------------------------------------------
// Analyses a model once (all skins).
//-----------------------------------------------------------------------------
const FidelityGlowModel_t &CFidelityGlow::LookUpModel( const model_t *pModel )
{
	unsigned short index = m_Models.Find( pModel );
	if ( index != m_Models.InvalidIndex() )
		return m_Models[ index ];

	FidelityGlowModel_t info;
	info.m_bAnySkinGlows = false;
	info.m_nSkins = 0;
	info.m_nEmptySequence = info.m_nEmptyClickSequence = info.m_nIdleSequence = -1;

	const MDLHandle_t hModel = modelinfo->GetCacheHandle( pModel );
	studiohwdata_t *pHardware = mdlcache->GetHardwareData( hModel );
	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( pModel );
	if ( pHardware && pHardware->m_NumLODs > 0 && pStudioHdr )
	{
		for ( int nSeq = 0; nSeq < pStudioHdr->numlocalseq; nSeq++ )
		{
			const char *pszLabel = pStudioHdr->pLocalSeqdesc( nSeq )->pszLabel();
			if ( !Q_stricmp( pszLabel, "empty" ) )
				info.m_nEmptySequence = nSeq;
			else if ( !Q_stricmp( pszLabel, "emptyclick" ) )
				info.m_nEmptyClickSequence = nSeq;
			else if ( !Q_stricmp( pszLabel, "idle" ) )
				info.m_nIdleSequence = nSeq;
		}

		// The vertices once for all skins
		CUtlVector< const mstudiovertex_t * > vertices;
		Fidelity_ModelVertices( mdlcache->GetVertexData( hModel ), pHardware->m_RootLOD, vertices );
		info.m_nSkins = clamp( pStudioHdr->numskinfamilies, 1, MAX_GLOW_SKINS );
		const int nColorOverride = Fidelity_ColorOverrideOf( pModel );
		for ( int nSkin = 0; nSkin < info.m_nSkins; nSkin++ )
		{
			Fidelity_AnalyseSkin( pModel, pStudioHdr, pHardware, vertices, nSkin, info.m_Skins[ nSkin ] );
			info.m_Skins[ nSkin ].m_nColorOverride = nColorOverride;
			info.m_bAnySkinGlows |= info.m_Skins[ nSkin ].m_bGlows;
		}
	}

	index = m_Models.Insert( pModel, info );
	return m_Models[ index ];
}

// How the entity glows right now (its current model and skin), or NULL
const FidelityGlowSkin_t *CFidelityGlow::GlowOf( C_BaseEntity *pEntity )
{
	const model_t *pModel = pEntity->GetModel();
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
		return NULL;
	const FidelityGlowModel_t &info = LookUpModel( pModel );
	if ( !info.m_bAnySkinGlows )
		return NULL;
	const FidelityGlowSkin_t &skin = info.m_Skins[ clamp( pEntity->GetSkin(), 0, info.m_nSkins - 1 ) ];
	return skin.m_bGlows ? &skin : NULL;
}

//-----------------------------------------------------------------------------
// Sprites that move with something: attached or parented.
//-----------------------------------------------------------------------------
// The frag grenade (its own light with blips, see Update)
static bool Fidelity_IsFragGrenade( const model_t *pModel )
{
	return pModel && Q_stristr( modelinfo->GetModelName( pModel ), "w_grenade" ) != NULL;
}

// How much a charger still holds, as a light factor: 1 for things that can not
// run empty; 0 when empty (sequence "empty"/"emptyclick", or "idle" at its
// last frame), otherwise at least a fifth so it does not fade to nothing early.
static float Fidelity_ChargeLeft( C_BaseAnimating *pAnimating, const FidelityGlowModel_t &info )
{
	if ( info.m_nEmptySequence < 0 )
		return 1.0f;
	const int nSequence = pAnimating->GetSequence();
	if ( nSequence == info.m_nEmptySequence || nSequence == info.m_nEmptyClickSequence )
		return 0.0f;
	if ( nSequence != info.m_nIdleSequence )
		return 1.0f;	// in use: the charging animations
	const float flCharge = 1.0f - clamp( pAnimating->GetCycle(), 0.0f, 1.0f );
	return flCharge < 0.01f ? 0.0f : MAX( flCharge, 0.2f );
}

// A view model (weapon in the hand) or a sprite on one
static bool Fidelity_OnViewModel( C_BaseEntity *pEntity )
{
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating && pAnimating->IsViewModel() )
		return true;
	C_Sprite *pSprite = dynamic_cast< C_Sprite * >( pEntity );
	C_BaseEntity *pOwner = pSprite ? ( pSprite->m_hAttachedToEntity.Get() ? pSprite->m_hAttachedToEntity.Get() : pSprite->GetMoveParent() ) : NULL;
	C_BaseAnimating *pOwnerAnimating = pOwner ? pOwner->GetBaseAnimating() : NULL;
	return pOwnerAnimating && pOwnerAnimating->IsViewModel();
}

// A ragdoll (client or server side) or an NPC that has died
bool Fidelity_IsDead( C_BaseEntity *pEntity )
{
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating && pAnimating->IsRagdoll() )
		return true;
	ClientClass *pClass = pEntity->GetClientClass();
	if ( pClass && Q_stristr( pClass->m_pNetworkName, "Ragdoll" ) )
		return true;
	return pEntity->IsNPC() && !pEntity->IsAlive();
}

static bool Fidelity_IsSpotlightEnd( C_BaseEntity *pEntity )
{
	ClientClass *pClass = pEntity ? pEntity->GetClientClass() : NULL;
	return pClass && !Q_strcmp( pClass->m_pNetworkName, "CSpotlightEnd" );
}

static C_Sprite *Fidelity_MovingSprite( C_BaseEntity *pEntity )
{
	C_Sprite *pSprite = dynamic_cast< C_Sprite * >( pEntity );
	if ( !pSprite )
		return NULL;
	C_BaseEntity *pOwner = pSprite->m_hAttachedToEntity.Get() ? pSprite->m_hAttachedToEntity.Get() : pSprite->GetMoveParent();
	if ( !pOwner )
	{
		// A sprite a player or NPC owns and moves (the RPG's laser dot)
		C_BaseEntity *pOwnerEntity = pSprite->GetOwnerEntity();
		return ( pOwnerEntity && ( pOwnerEntity->IsPlayer() || pOwnerEntity->IsNPC() ) ) ? pSprite : NULL;
	}
	// The grenade's glow sprite: the grenade has its own light.
	if ( Fidelity_IsFragGrenade( pOwner->GetModel() ) )
		return NULL;
	return pSprite;
}

//-----------------------------------------------------------------------------
// Collects the entities that can glow (twice a second). View models always
// count, so a weapon switch is seen at once.
//-----------------------------------------------------------------------------
void CFidelityGlow::Scan()
{
	m_Candidates.RemoveAll();
	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		FidelityGlowCandidate_t candidate;
		candidate.m_hEntity = pEntity;
		candidate.m_flDistSqr = 0.0f;
		candidate.m_nKind = GLOW_KIND_MODEL;
		candidate.m_nStatic = -1;

		// Burning things (the flame entity has no model)
		ClientClass *pClass = pEntity->GetClientClass();
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "CEntityFlame" ) )
		{
			candidate.m_nKind = GLOW_KIND_FIRE;
			m_Candidates.AddToTail( candidate );
			continue;
		}
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "RocketTrail" ) )
		{
			candidate.m_nKind = GLOW_KIND_ROCKET;
			m_Candidates.AddToTail( candidate );
			continue;
		}
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "CBeam" ) )
		{
			candidate.m_nKind = GLOW_KIND_BEAM;
			m_Candidates.AddToTail( candidate );
			continue;
		}
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "CNPC_RollerMine" ) )
		{
			candidate.m_nKind = GLOW_KIND_ROLLERMINE;
			m_Candidates.AddToTail( candidate );
			continue;
		}
		if ( pClass && !Q_strcmp( pClass->m_pNetworkName, "CPropCombineBall" ) )
		{
			candidate.m_nKind = GLOW_KIND_COMBINEBALL;
			m_Candidates.AddToTail( candidate );
			continue;
		}

		const model_t *pModel = pEntity->GetModel();
		if ( !pModel )
			continue;
		if ( Fidelity_IsFragGrenade( pModel ) )
		{
			candidate.m_nKind = GLOW_KIND_GRENADE;
			m_Candidates.AddToTail( candidate );
			continue;
		}
		const int nType = modelinfo->GetModelType( pModel );
		if ( nType == mod_studio )
		{
			C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
			if ( !LookUpModel( pModel ).m_bAnySkinGlows && !( pAnimating && pAnimating->IsViewModel() ) )
				continue;
		}
		else if ( nType == mod_sprite && Fidelity_MovingSprite( pEntity ) )
		{
			candidate.m_nKind = GLOW_KIND_SPRITE;
		}
		else
		{
			continue;
		}
		m_Candidates.AddToTail( candidate );
	}

	// Static props of the map with a glowing model (Combine consoles, lamps):
	// only those that can come into reach before the next scan (0.5 s of
	// running plus a margin), so a frame never sorts thousands of them.
	const float flReach = MAX( r_fidelity_glow_distance.GetFloat(), 1.0f ) + 512.0f;
	const Vector &vecView = MainViewOrigin();
	for ( int i = 0; i < m_StaticProps.Count(); i++ )
	{
		if ( m_StaticProps[ i ].m_vecCenter.DistToSqr( vecView ) > flReach * flReach )
			continue;
		FidelityGlowCandidate_t candidate;
		candidate.m_hEntity = NULL;
		candidate.m_flDistSqr = 0.0f;
		candidate.m_nKind = GLOW_KIND_STATIC;
		candidate.m_nStatic = i;
		m_Candidates.AddToTail( candidate );
	}
}

//-----------------------------------------------------------------------------
// Reads the static props of the map (game lump "sprp" of the .bsp): their
// model, place and skin, and keeps the ones whose model glows. All lump
// versions start the same (origin, angles, model index, ..., skin at byte
// 32); the size of an entry comes from the lump size.
//-----------------------------------------------------------------------------
// Reads a T at nPos of the lump; false if it does not fit (memcpy: no unaligned access)
template< typename T >
static bool Fidelity_LumpRead( const CUtlVector< unsigned char > &lump, int nPos, T &value )
{
	if ( nPos < 0 || nPos > lump.Count() - (int)sizeof( T ) )
		return false;
	V_memcpy( &value, lump.Base() + nPos, sizeof( T ) );
	return true;
}

void CFidelityGlow::LoadStaticProps()
{
	m_StaticProps.RemoveAll();
	const char *pszMap = engine->GetLevelName();
	if ( !pszMap || !pszMap[ 0 ] )
		return;
	FileHandle_t hFile = filesystem->Open( pszMap, "rb", "GAME" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return;

	CUtlVector< unsigned char > lump;
	dheader_t header;
	if ( filesystem->Read( &header, sizeof( header ), hFile ) == sizeof( header ) && header.ident == IDBSPHEADER )
	{
		// The game lump directory, then the static prop lump in it
		const lump_t &gameLump = header.lumps[ LUMP_GAME_LUMP ];
		int nGameLumps = 0;
		filesystem->Seek( hFile, gameLump.fileofs, FILESYSTEM_SEEK_HEAD );
		if ( gameLump.filelen >= 4 && filesystem->Read( &nGameLumps, 4, hFile ) == 4 )
		{
			for ( int i = 0; i < nGameLumps && i < 64; i++ )
			{
				dgamelump_t entry;
				if ( filesystem->Read( &entry, sizeof( entry ), hFile ) != sizeof( entry ) )
					break;
				if ( entry.id != GAMELUMP_STATIC_PROPS || ( entry.flags & GAMELUMPFLAG_COMPRESSED ) || entry.filelen <= 0 || entry.filelen > 32 * 1024 * 1024 )
					continue;
				lump.SetCount( entry.filelen );
				filesystem->Seek( hFile, entry.fileofs, FILESYSTEM_SEEK_HEAD );
				if ( filesystem->Read( lump.Base(), entry.filelen, hFile ) != entry.filelen )
					lump.RemoveAll();
				break;
			}
		}
	}
	filesystem->Close( hFile );
	if ( lump.Count() < 12 )
		return;

	// Model names, leaves, then the props. The map may come from anywhere:
	// every count is checked against the lump size before use (divisions, no
	// products that could wrap), every value read with a bounds check.
	const int nSize = lump.Count();
	int nNames = 0, nLeaves = 0, nProps = 0;
	if ( !Fidelity_LumpRead( lump, 0, nNames ) || nNames < 0 || nNames > ( nSize - 4 ) / STATIC_PROP_NAME_LENGTH )
		return;
	const int nNamesAt = 4;
	int nPos = nNamesAt + nNames * STATIC_PROP_NAME_LENGTH;
	if ( !Fidelity_LumpRead( lump, nPos, nLeaves ) || nLeaves < 0 || nLeaves > ( nSize - nPos - 4 ) / (int)sizeof( unsigned short ) )
		return;
	nPos += 4 + nLeaves * (int)sizeof( unsigned short );
	if ( !Fidelity_LumpRead( lump, nPos, nProps ) || nProps <= 0 || ( nSize - nPos - 4 ) / nProps < 36 )
		return;
	nPos += 4;
	const int nEntrySize = ( nSize - nPos ) / nProps;

	// Models: looked up once, kept if they glow
	CUtlVector< const model_t * > models;
	for ( int i = 0; i < nNames; i++ )
	{
		char szName[ STATIC_PROP_NAME_LENGTH + 1 ];
		V_memcpy( szName, lump.Base() + nNamesAt + i * STATIC_PROP_NAME_LENGTH, STATIC_PROP_NAME_LENGTH );
		szName[ STATIC_PROP_NAME_LENGTH ] = 0;
		const int nIndex = modelinfo->GetModelIndex( szName );
		const model_t *pModel = nIndex >= 0 ? modelinfo->GetModel( nIndex ) : modelinfo->FindOrLoadModel( szName );
		if ( pModel && modelinfo->GetModelType( pModel ) == mod_studio && LookUpModel( pModel ).m_bAnySkinGlows )
			models.AddToTail( pModel );
		else
			models.AddToTail( NULL );
	}

	for ( int i = 0; i < nProps && m_StaticProps.Count() < MAX_STATIC_GLOWS; i++ )
	{
		// Origin, angles, model index (24), skin (32); entries need not be aligned
		const int nEntry = nPos + i * nEntrySize;
		float flPlace[ 6 ];
		unsigned short nType = 0;
		int nSkin = 0;
		if ( !Fidelity_LumpRead( lump, nEntry, flPlace ) || !Fidelity_LumpRead( lump, nEntry + 24, nType ) || !Fidelity_LumpRead( lump, nEntry + 32, nSkin ) )
			break;
		if ( nType >= models.Count() || !models[ nType ] || !IsFinite( flPlace[ 0 ] ) || !IsFinite( flPlace[ 1 ] ) || !IsFinite( flPlace[ 2 ] ) )
			continue;
		const Vector vecOrigin( flPlace[ 0 ], flPlace[ 1 ], flPlace[ 2 ] );
		const QAngle angAngles( flPlace[ 3 ], flPlace[ 4 ], flPlace[ 5 ] );
		FidelityStaticGlow_t &prop = m_StaticProps[ m_StaticProps.AddToTail() ];
		prop.m_pModel = models[ nType ];
		AngleMatrix( angAngles, vecOrigin, prop.m_ToWorld );
		Vector vecMins, vecMaxs;
		modelinfo->GetModelRenderBounds( prop.m_pModel, vecMins, vecMaxs );
		VectorTransform( ( vecMins + vecMaxs ) * 0.5f, prop.m_ToWorld, prop.m_vecCenter );
		prop.m_nSkin = nSkin;
	}
	if ( m_StaticProps.Count() > 0 )
		DevMsg( "[fidelity] glow: %d static props with glowing models\n", m_StaticProps.Count() );
}

static int __cdecl Fidelity_SortGlow( const FidelityGlowCandidate_t *pA, const FidelityGlowCandidate_t *pB )
{
	return ( pA->m_flDistSqr < pB->m_flDistSqr ) ? -1 : ( pA->m_flDistSqr > pB->m_flDistSqr ) ? 1 : 0;
}

// The light of an entity, made if it has none yet
FidelityGlowLight_t &CFidelityGlow::Light( C_BaseEntity *pEntity, int nKey, bool bSprite )
{
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		// Entities by their handle, static props (no entity) by their key
		if ( pEntity ? m_Lights[ i ].m_hEntity.Get() == pEntity : ( m_Lights[ i ].m_nKind == GLOW_KIND_STATIC && m_Lights[ i ].m_nKey == nKey ) )
			return m_Lights[ i ];
	}
	FidelityGlowLight_t &light = NewLight( bSprite ? GLOW_KIND_SPRITE : GLOW_KIND_MODEL, nKey );
	light.m_hEntity = pEntity;
	light.m_vecOrigin = pEntity ? pEntity->WorldSpaceCenter() : vec3_origin;
	light.m_flFadeIn = bSprite ? SPRITE_FADE_IN : GLOW_FADE_IN;
	light.m_flFadeOut = bSprite ? SPRITE_FADE_OUT : GLOW_FADE_OUT;
	return light;
}

//-----------------------------------------------------------------------------
// A new light of nKind with nKey, every field set (off, white, sprite fades).
// Lights without an entity (afterglow, explosion, tracer) reuse their keys
// round robin: an old one of the same kind and key goes first.
//-----------------------------------------------------------------------------
FidelityGlowLight_t &CFidelityGlow::NewLight( int nKind, int nKey )
{
	for ( int i = m_Lights.Count() - 1; i >= 0; i-- )
	{
		if ( !m_Lights[ i ].m_hEntity.Get() && m_Lights[ i ].m_nKind == nKind && m_Lights[ i ].m_nKey == nKey )
			m_Lights.FastRemove( i );
	}
	FidelityGlowLight_t &light = m_Lights[ m_Lights.AddToTail() ];
	light.m_hEntity = NULL;
	light.m_nKey = nKey;
	light.m_vecOrigin.Init();
	light.m_vecColor.Init( 1, 1, 1 );
	light.m_flRadius = 0.0f;
	light.m_flIntensity = 0.0f;
	light.m_flTarget = 0.0f;
	light.m_flFadeIn = SPRITE_FADE_IN;
	light.m_flFadeOut = SPRITE_FADE_OUT;
	light.m_flStartTime = gpGlobals->curtime;
	light.m_nKind = nKind;
	light.m_flFirstSeen = gpGlobals->curtime;
	light.m_flPulse = 0.0f;
	light.m_nLastBlipGuid = -1;
	light.m_vecTravel.Init();
	light.m_flTravelTime = 0.0f;
	light.m_flBrightness = 1.0f;
	light.m_bSplit = false;
	light.m_vecModelOrigin.Init();
	light.m_flModelScale = VIEWMODEL_MODEL_BRIGHTNESS;
	light.m_flWorldScale = 1.0f;
	light.m_bNoModels = false;
	light.m_bSeen = false;
	return light;
}

//-----------------------------------------------------------------------------
// Id (GUID) of the grenade's tick sound if one is playing now, else -1. The
// list of playing sounds is read at most once a frame, only while a grenade
// is near.
//-----------------------------------------------------------------------------
int CFidelityGlow::BlipOf( int nEntity )
{
	if ( m_nSoundsFrame != gpGlobals->framecount )
	{
		m_nSoundsFrame = gpGlobals->framecount;
		m_Sounds.RemoveAll();
		enginesound->GetActiveSounds( m_Sounds );
	}
	char szName[ MAX_PATH ];
	for ( int i = 0; i < m_Sounds.Count(); i++ )
	{
		const SndInfo_t &info = m_Sounds[ i ];
		if ( info.m_nSoundSource == nEntity && filesystem->String( info.m_filenameHandle, szName, sizeof( szName ) ) && Q_stristr( szName, "tick" ) )
			return info.m_nGuid;
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Sprite light: colour of its material times its Render Color, brightness as
// drawn. False: no light.
//-----------------------------------------------------------------------------
bool CFidelityGlow::SpriteLight( C_Sprite *pSprite, FidelityGlowLight_t &light, float flBrightness )
{
	const float flSprite = pSprite->GetRenderBrightness() / 255.0f;
	if ( flSprite <= 0.01f )
		return false;
	// The glow sprite of a stuck crossbow bolt: its afterglow is the light.
	if ( NearAfterglow( pSprite->GetRenderOrigin() ) )
		return false;
	// The texture's colour, looked up once per sprite model
	const model_t *pModel = pSprite->GetModel();
	unsigned short index = m_SpriteColors.Find( pModel );
	if ( index == m_SpriteColors.InvalidIndex() )
	{
		IMaterial *pMaterial = NULL;
		modelinfo->GetModelMaterials( pModel, 1, &pMaterial );
		Vector vecColor( 1, 1, 1 );
		if ( pMaterial )
		{
			pMaterial->GetReflectivity( vecColor );
			vecColor = Fidelity_GlowSaturate( vecColor, 2.0f );
		}
		index = m_SpriteColors.Insert( pModel, vecColor );
	}
	const Vector vecTexture = m_SpriteColors[ index ];
	const color32 render = pSprite->GetRenderColor();
	light.m_vecColor = Fidelity_GlowSaturate( vecTexture * Vector( render.r / 255.0f, render.g / 255.0f, render.b / 255.0f ), 1.0f );

	Vector vecMins, vecMaxs;
	pSprite->GetRenderBounds( vecMins, vecMaxs );
	light.m_flRadius = clamp( ( vecMaxs.x - vecMins.x ) * 3.0f, 100.0f, 220.0f );
	light.m_vecOrigin = pSprite->GetRenderOrigin();
	light.m_flTarget = flBrightness * flSprite;
	return true;
}

bool CFidelityGlow::NearAfterglow( const Vector &vecOrigin ) const
{
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		if ( m_Lights[ i ].m_nKind == GLOW_KIND_AFTERGLOW && m_Lights[ i ].m_vecOrigin.DistToSqr( vecOrigin ) < 48.0f * 48.0f )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// A crossbow bolt stuck at vecOrigin (on the surface it hit), flying along
// vecDirection: it glows on there.
//-----------------------------------------------------------------------------
void CFidelityGlow::AddAfterglow( const Vector &vecOrigin, const Vector &vecDirection )
{
	if ( !r_fidelity_glow.GetBool() )
		return;
	Vector vecDir = vecDirection;
	if ( VectorNormalize( vecDir ) < 0.001f )
		return;
	// Only where it sticks in something solid (a bolt through an NPC with no
	// wall behind ends in the air).
	trace_t tr;
	UTIL_TraceLine( vecOrigin - vecDir * 24.0f, vecOrigin + vecDir * 24.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction >= 1.0f )
		return;

	// Only MAX_AFTERGLOWS glow at once: the oldest one's key is reused.
	FidelityGlowLight_t &light = NewLight( GLOW_KIND_AFTERGLOW, AFTERGLOW_KEY_BASE + ( m_nAfterglows++ % MAX_AFTERGLOWS ) );
	// Off the surface along its normal: a hot spot around the bolt
	Fidelity_PushOff( light.m_vecOrigin, tr, LIGHT_WALL_DISTANCE );
	light.m_vecColor.Init( s_vecAfterglowHot[ 0 ], s_vecAfterglowHot[ 1 ], s_vecAfterglowHot[ 2 ] );
	light.m_flRadius = AFTERGLOW_RADIUS;
	light.m_flFadeOut = GLOW_FADE_OUT;
	light.m_bSeen = true;
}

static void Fidelity_GlowDebugChanged( IConVar *pVar, const char *pszOld, float flOld )
{
	// Analyse again, so the console shows each model's glowing parts
	s_FidelityGlow.ForgetModels();
}

//-----------------------------------------------------------------------------
// An explosion at vecOrigin: a fireball light that fades over a second.
//-----------------------------------------------------------------------------
void CFidelityGlow::AddExplosion( const Vector &vecOrigin )
{
	if ( !r_fidelity_glow.GetBool() )
		return;
	FidelityGlowLight_t &light = NewLight( GLOW_KIND_EXPLOSION, EXPLOSION_KEY_BASE + ( m_nExplosions++ % MAX_EXPLOSIONS ) );
	// A little above the ground it went off on (a light in the floor lights nothing)
	light.m_vecOrigin = vecOrigin;
	Fidelity_KeepInOpen( vecOrigin + Vector( 0.0f, 0.0f, 32.0f ), light.m_vecOrigin, LIGHT_WALL_DISTANCE );
	light.m_vecColor.Init( s_vecExplosionHot[ 0 ], s_vecExplosionHot[ 1 ], s_vecExplosionHot[ 2 ] );
	light.m_flRadius = EXPLOSION_RADIUS;
	light.m_bSeen = true;
}

//-----------------------------------------------------------------------------
// A pulse tracer flying from vecStart along vecDir: a light that flies with it.
//-----------------------------------------------------------------------------
void CFidelityGlow::AddTracer( const Vector &vecStart, const Vector &vecDir, float flSpeed, float flDistance, const char *pszMaterial, float flScale )
{
	if ( !r_fidelity_glow.GetBool() || flSpeed <= 1.0f )
		return;
	FidelityGlowLight_t &light = NewLight( GLOW_KIND_TRACER, TRACER_KEY_BASE + ( m_nTracers++ % MAX_TRACERS ) );
	light.m_vecOrigin = vecStart;
	light.m_vecTravel = vecDir * flSpeed;
	light.m_flTravelTime = flDistance / flSpeed;
	light.m_vecColor = Fidelity_MaterialColor( pszMaterial, Vector( s_vecTracerColor[ 0 ], s_vecTracerColor[ 1 ], s_vecTracerColor[ 2 ] ) );
	light.m_flRadius = TRACER_RADIUS * flScale;
	light.m_flBrightness = flScale;
	light.m_bSeen = true;
}

void Fidelity_TracerLight( const Vector &vecStart, const Vector &vecDir, float flSpeed, float flDistance, const char *pszMaterial, float flScale )
{
	s_FidelityGlow.AddTracer( vecStart, vecDir, flSpeed, flDistance, pszMaterial, flScale );
}

void Fidelity_ExplosionLight( const Vector &vecOrigin, bool bLight )
{
	if ( bLight )
	{
		s_FidelityGlow.AddExplosion( vecOrigin );
		Fidelity_ProjectedExplosion( vecOrigin );
	}
	Fidelity_AudioExplosion( vecOrigin );	// the mixer numbs the ears nearby
	Fidelity_DistantSound( vecOrigin, -1 );	// far away: its echo follows
}

void Fidelity_BoltImpact( const Vector &vecOrigin, const Vector &vecDirection )
{
	s_FidelityGlow.AddAfterglow( vecOrigin, vecDirection );
}

//-----------------------------------------------------------------------------
// r_fidelity_glow_debug: every light as text in the world, and once a second
// the list in the console.
//-----------------------------------------------------------------------------
void CFidelityGlow::Debug( int nCandidates, int nFree )
{
	if ( !debugoverlay )
		return;
	const bool bPrint = gpGlobals->realtime >= m_flNextDebug;
	if ( bPrint )
	{
		m_flNextDebug = gpGlobals->realtime + 1.0f;
		int nWorld = 0;
		for ( int c = 0; c < m_Clusters.Count(); c++ )
			nWorld += m_Clusters[ c ].m_bElight ? 0 : 1;
		Msg( "[fidelity] glow: %d candidates, %d lights, %d engine lights used by others, %d free for glow; "
			"last frame %d world lights, %d model-only lights (lights close together share one)\n",
			nCandidates, m_Lights.Count(), Fidelity_OtherLights(), nFree, nWorld, m_Clusters.Count() - nWorld );
	}
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		const FidelityGlowLight_t &light = m_Lights[ i ];
		C_BaseEntity *pEntity = light.m_hEntity.Get();
		const char *pszWhat = ( pEntity && pEntity->GetModel() ) ? modelinfo->GetModelName( pEntity->GetModel() ) : "-";
		const int nStatic = light.m_nKey - STATIC_KEY_BASE;
		if ( light.m_nKind == GLOW_KIND_STATIC && nStatic >= 0 && nStatic < m_StaticProps.Count() )
			pszWhat = modelinfo->GetModelName( m_StaticProps[ nStatic ].m_pModel );
		const char *pszKind = s_pszGlowKinds[ clamp( light.m_nKind, 0, (int)ARRAYSIZE( s_pszGlowKinds ) - 1 ) ];
		debugoverlay->AddTextOverlay( light.m_vecOrigin, 0, 0.0f, "%s %.2f r%.0f%s", pszKind, light.m_flIntensity, light.m_flRadius, ( light.m_bSplit || light.m_bNoModels ) ? " (world only)" : "" );
		if ( light.m_bSplit )
		{
			// The weapon's own model light, where it really glows
			debugoverlay->AddTextOverlay( light.m_vecModelOrigin, 0, 0.0f, "weapon (models only)" );
			debugoverlay->AddBoxOverlay( light.m_vecModelOrigin, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), vec3_angle, 255, 255, 255, 200, 0.0f );
		}
		debugoverlay->AddBoxOverlay( light.m_vecOrigin, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), vec3_angle,
			(int)( light.m_vecColor.x * 255 ), (int)( light.m_vecColor.y * 255 ), (int)( light.m_vecColor.z * 255 ), 200, 0.0f );
		// The vertices found glowing, as small magenta boxes: shows whether the right parts count
		const FidelityGlowSkin_t *pGlow = ( light.m_nKind == GLOW_KIND_MODEL && pEntity ) ? GlowOf( pEntity ) : NULL;
		for ( int n = 0; pGlow && n < pGlow->m_nDebugPoints; n++ )
		{
			Vector vecPoint;
			matrix3x4_t boneToWorld;
			Fidelity_BoneToWorld( pEntity, pGlow->m_nDebugBone[ n ], boneToWorld );
			VectorTransform( pGlow->m_vecDebugPoints[ n ], boneToWorld, vecPoint );
			debugoverlay->AddBoxOverlay( vecPoint, Vector( -0.5f, -0.5f, -0.5f ), Vector( 0.5f, 0.5f, 0.5f ), vec3_angle, 255, 0, 255, 255, 0.0f );
		}
		if ( bPrint )
		{
			Msg( "  %-11s %-40s brightness %.2f (target %.2f) radius %.0f colour %.2f %.2f %.2f at %.0f %.0f %.0f\n", pszKind, pszWhat,
				light.m_flIntensity, light.m_flTarget, light.m_flRadius, light.m_vecColor.x, light.m_vecColor.y, light.m_vecColor.z,
				light.m_vecOrigin.x, light.m_vecOrigin.y, light.m_vecOrigin.z );
		}
	}
}

//-----------------------------------------------------------------------------
// The light of one candidate this frame, one function per kind: sets its
// place, colour and target brightness (flBase: brightness x distance fade)
// and returns true, or false if it gives no light now (a light it still has
// fades out).
//-----------------------------------------------------------------------------
// A static prop with a glowing model: like a model, with its fixed place
bool CFidelityGlow::LightStatic( int nStatic, float flBase )
{
	const FidelityStaticGlow_t &prop = m_StaticProps[ nStatic ];
	const FidelityGlowModel_t &info = LookUpModel( prop.m_pModel );
	const FidelityGlowSkin_t &skin = info.m_Skins[ clamp( prop.m_nSkin, 0, MAX( info.m_nSkins - 1, 0 ) ) ];
	if ( !skin.m_bGlows )
		return false;
	FidelityGlowLight_t &light = Light( NULL, STATIC_KEY_BASE + nStatic, false );
	light.m_nKind = GLOW_KIND_STATIC;
	VectorTransform( skin.m_vecLocalPos, prop.m_ToWorld, light.m_vecOrigin );
	light.m_vecColor = skin.m_vecColor;
	light.m_flRadius = skin.m_flStrength > 0.0f ? skin.m_flRadius : SMALL_GLOW_RADIUS;
	light.m_flTarget = flBase * ( skin.m_flStrength > 0.0f ? skin.m_flStrength : SMALL_GLOW_BRIGHTNESS );
	Fidelity_ApplyColorOverride( skin, light );
	light.m_bSeen = true;
	return true;
}

// Burning things: warm flicker at the middle of what burns
bool CFidelityGlow::LightFire( C_BaseEntity *pEntity, int nKey, float flBase )
{
	// The engine lights some flames itself (EF_DIMLIGHT in the episodes).
	if ( pEntity->IsEffectActive( EF_DIMLIGHT | EF_BRIGHTLIGHT ) )
		return false;
	C_BaseEntity *pBurning = static_cast< C_EntityFlame * >( pEntity )->m_hEntAttached.Get();
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );	// fast fades: the flicker stays
	light.m_nKind = GLOW_KIND_FIRE;
	light.m_vecOrigin = pBurning ? pBurning->WorldSpaceCenter() : pEntity->GetAbsOrigin() + Vector( 0.0f, 0.0f, 16.0f );
	light.m_flRadius = clamp( ( pBurning ? pBurning->BoundingRadius() : 32.0f ) * 4.0f, FIRE_RADIUS_MIN, FIRE_RADIUS_MAX );
	light.m_vecColor.Init( s_vecFireColor[ 0 ], s_vecFireColor[ 1 ], s_vecFireColor[ 2 ] );
	// Flicker: a few unrelated sines, every flame with its own phase
	const float t = gpGlobals->curtime + nKey * 1.7f;
	const float flFlicker = 0.78f + 0.12f * sinf( t * 9.1f ) + 0.06f * sinf( t * 17.3f + 1.3f ) + 0.04f * sinf( t * 31.7f + 2.1f );
	light.m_flTarget = flBase * flFlicker * FIRE_BRIGHTNESS;
	light.m_bSeen = true;
	return true;
}

// The flame of a flying rocket (the trail emits while it burns)
bool CFidelityGlow::LightRocket( C_BaseEntity *pEntity, int nKey, float flBase )
{
	C_RocketTrail *pTrail = static_cast< C_RocketTrail * >( pEntity );
	if ( !pTrail->m_bEmit )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	light.m_nKind = GLOW_KIND_ROCKET;
	light.m_vecOrigin = pEntity->GetAbsOrigin();
	light.m_flRadius = ROCKET_RADIUS;
	light.m_vecColor.Init( s_vecRocketColor[ 0 ], s_vecRocketColor[ 1 ], s_vecRocketColor[ 2 ] );
	const float t = gpGlobals->curtime + nKey * 0.7f;
	const float flFlicker = 0.82f + 0.1f * sinf( t * 23.0f ) + 0.08f * sinf( t * 41.0f + 1.1f );
	light.m_flTarget = flBase * ROCKET_BRIGHTNESS * flFlicker;
	light.m_bSeen = true;
	return true;
}

// Beams of NPCs (the stalker's laser) light where they hit; beams in the map belong to the baked light.
bool CFidelityGlow::LightBeam( C_BaseEntity *pEntity, int nKey, float flBase )
{
	CBeam *pBeam = static_cast< CBeam * >( pEntity );
	C_BaseEntity *pStart = pBeam->GetStartEntityPtr();
	C_BaseEntity *pEnd = pBeam->GetEndEntityPtr();
	const bool bFromStart = pStart && pStart->IsNPC();
	if ( ( !bFromStart && !( pEnd && pEnd->IsNPC() ) ) || !pEntity->ShouldDraw() || pBeam->GetBrightness() <= 2 )
		return false;
	// Spotlights (scanner, npc_spotlight): their end has its own engine light
	if ( Fidelity_IsSpotlightEnd( pStart ) || Fidelity_IsSpotlightEnd( pEnd ) )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	light.m_nKind = GLOW_KIND_BEAM;
	const Vector vecFrom = bFromStart ? pBeam->GetAbsStartPos() : pBeam->GetAbsEndPos();
	light.m_vecOrigin = bFromStart ? pBeam->GetAbsEndPos() : pBeam->GetAbsStartPos();
	Fidelity_KeepInOpen( vecFrom, light.m_vecOrigin, LIGHT_WALL_DISTANCE );
	light.m_flRadius = BEAM_RADIUS;
	const color32 render = pEntity->GetRenderColor();
	light.m_vecColor = Fidelity_GlowSaturate( Vector( render.r / 255.0f, render.g / 255.0f, render.b / 255.0f ), 1.3f );
	light.m_flTarget = flBase * BEAM_BRIGHTNESS * ( pBeam->GetBrightness() / 255.0f );
	light.m_bSeen = true;
	return true;
}

// An open rollermine (its arcs on): C_RollerMine draws itself translucent then (m_bIsOpen is private)
bool CFidelityGlow::LightRollermine( C_BaseEntity *pEntity, int nKey, float flBase )
{
	if ( !pEntity->ShouldDraw() || pEntity->GetRenderGroup() != RENDER_GROUP_TRANSLUCENT_ENTITY )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	light.m_nKind = GLOW_KIND_ROLLERMINE;
	light.m_vecOrigin = pEntity->WorldSpaceCenter() + Vector( 0.0f, 0.0f, 8.0f );
	light.m_flRadius = ROLLERMINE_RADIUS;
	light.m_vecColor.Init( s_vecRollermineColor[ 0 ], s_vecRollermineColor[ 1 ], s_vecRollermineColor[ 2 ] );
	light.m_flTarget = flBase * ROLLERMINE_BRIGHTNESS * random->RandomFloat( 0.6f, 1.0f );
	light.m_bSeen = true;
	return true;
}

// Combine energy ball: blue-white, a slight shimmer
bool CFidelityGlow::LightCombineBall( C_BaseEntity *pEntity, int nKey, float flBase )
{
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	light.m_nKind = GLOW_KIND_COMBINEBALL;
	light.m_vecOrigin = pEntity->WorldSpaceCenter();
	light.m_flRadius = COMBINEBALL_RADIUS;
	light.m_vecColor.Init( s_vecCombineBallColor[ 0 ], s_vecCombineBallColor[ 1 ], s_vecCombineBallColor[ 2 ] );
	const float t = gpGlobals->curtime * 13.0f + nKey;
	light.m_flTarget = flBase * COMBINEBALL_BRIGHTNESS * ( 0.9f + 0.1f * sinf( t ) );
	light.m_bSeen = true;
	return true;
}

// Frag grenade: red, stronger the longer it is live, a flash at every blip
bool CFidelityGlow::LightGrenade( C_BaseEntity *pEntity, int nKey, float flBase )
{
	if ( !pEntity->ShouldDraw() )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	light.m_nKind = GLOW_KIND_GRENADE;
	// Every blip (its tick sound, so light and sound agree) flashes
	const int nBlip = BlipOf( pEntity->entindex() );
	if ( nBlip >= 0 && nBlip != light.m_nLastBlipGuid )
	{
		light.m_nLastBlipGuid = nBlip;
		light.m_flPulse = 1.0f;
	}
	light.m_flPulse = MAX( light.m_flPulse - gpGlobals->frametime / GRENADE_PULSE_SECONDS, 0.0f );
	const float flRamp = clamp( ( gpGlobals->curtime - light.m_flFirstSeen ) / GRENADE_RAMP_SECONDS, 0.0f, 1.0f );
	light.m_vecOrigin = pEntity->WorldSpaceCenter() + Vector( 0.0f, 0.0f, 4.0f );	// off the floor it lies on
	light.m_flRadius = 160.0f + 80.0f * flRamp + 140.0f * light.m_flPulse;
	light.m_vecColor.Init( s_vecGrenadeColor[ 0 ], s_vecGrenadeColor[ 1 ], s_vecGrenadeColor[ 2 ] );
	light.m_flTarget = flBase * ( 0.6f + 0.6f * flRamp + 2.0f * light.m_flPulse );
	light.m_bSeen = true;
	return true;
}

// Sprites that move with something (see Fidelity_MovingSprite); hidden ones give no light
bool CFidelityGlow::LightSprite( C_BaseEntity *pEntity, int nKey, float flBase, const Vector &vecView )
{
	// Sprites on the weapon in the hand (the gravity gun's claws): the weapon's
	// own light stands for them (five lights for one gun were too many and
	// took the places of other things, e.g. a health charger).
	if ( !pEntity->ShouldDraw() || Fidelity_OnViewModel( pEntity ) )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, true );
	C_Sprite *pSprite = static_cast< C_Sprite * >( pEntity );
	if ( !SpriteLight( pSprite, light, flBase ) )
		return false;
	C_BaseEntity *pOwner = pSprite->m_hAttachedToEntity.Get() ? pSprite->m_hAttachedToEntity.Get() : pSprite->GetMoveParent();
	if ( !pOwner )
	{
		// A laser dot on a wall: a small light just in front of it, red like
		// the dot (its sprite's white core made the light nearly white -
		// Julius: "der muss rot auch haben, wie der Pointer")
		light.m_flRadius = DOT_RADIUS;
		// fades in and out softly (the sprite's fast fade made it pop on - Julius)
		light.m_flFadeIn = DOT_FADE;
		light.m_flFadeOut = DOT_FADE;
		const float flBright = MAX( light.m_vecColor.x, MAX( light.m_vecColor.y, light.m_vecColor.z ) );
		light.m_vecColor = Lerp( DOT_RED_SHARE, light.m_vecColor, Vector( s_vecDotRed[ 0 ], s_vecDotRed[ 1 ], s_vecDotRed[ 2 ] ) * MAX( flBright, 0.5f ) );
		Fidelity_KeepInOpen( vecView, light.m_vecOrigin, LIGHT_WALL_DISTANCE );
	}
	light.m_bSeen = true;
	return true;
}

// Models with glowing materials (chargers, weapons, Combine lamps, NPC eyes)
bool CFidelityGlow::LightModel( C_BaseEntity *pEntity, int nKey, float flBase, const Vector &vecView )
{
	// The dead give no light (their light fades out): Combine eyes go dark.
	if ( Fidelity_IsDead( pEntity ) )
		return false;
	const FidelityGlowSkin_t *pGlow = pEntity->ShouldDraw() ? GlowOf( pEntity ) : NULL;
	// One light per weapon in the hand: a second view model with the same
	// model (seen in game: two lights on the AR2) gives none.
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pGlow && pAnimating && pAnimating->IsViewModel() )
	{
		if ( m_ViewModelsLit.Find( pEntity->GetModel() ) != m_ViewModelsLit.InvalidIndex() )
			return false;
		m_ViewModelsLit.AddToTail( pEntity->GetModel() );
	}
	// Tiny glowing parts: a small light on NPCs (eyes) and things (console
	// lamps), none on weapons (sights)
	const bool bWeapon = pEntity->IsBaseCombatWeapon() || ( pAnimating && pAnimating->IsViewModel() );
	if ( !pGlow || ( pGlow->m_flStrength <= 0.0f && bWeapon ) )
		return false;
	// Things that run empty (chargers) dim with their charge and go dark when empty.
	const float flCharge = pAnimating ? Fidelity_ChargeLeft( pAnimating, LookUpModel( pEntity->GetModel() ) ) : 1.0f;
	if ( flCharge <= 0.0f )
		return false;
	FidelityGlowLight_t &light = Light( pEntity, nKey, false );
	Fidelity_GlowOrigin( pEntity, *pGlow, light.m_vecOrigin );
	light.m_vecColor = pGlow->m_vecColor;
	light.m_flRadius = pGlow->m_flRadius;
	light.m_flTarget = flBase * pGlow->m_flStrength * flCharge;
	if ( pGlow->m_flStrength <= 0.0f )
	{
		// Only a tiny glowing part: eyes and lenses of Combine soldiers, lamps of a console
		light.m_flRadius = pEntity->IsNPC() ? NPC_EYE_RADIUS : SMALL_GLOW_RADIUS;
		light.m_flTarget = flBase * ( pEntity->IsNPC() ? NPC_EYE_BRIGHTNESS : SMALL_GLOW_BRIGHTNESS );
	}
	Fidelity_ApplyColorOverride( *pGlow, light );
	// NPCs: the light a little in front of them, on the world only, and cooler
	// (the eyes of a Combine soldier lit his own mask yellow, right at the nose)
	light.m_bNoModels = pEntity->IsNPC();
	if ( light.m_bNoModels )
	{
		Vector vecForward;
		AngleVectors( pEntity->GetAbsAngles(), &vecForward );
		light.m_vecOrigin += vecForward * NPC_LIGHT_FORWARD;
		light.m_vecColor = Fidelity_GlowSaturate( Lerp( NPC_LIGHT_COOL, light.m_vecColor, Vector( 0.45f, 0.75f, 1.0f ) ), 1.0f );
	}
	// Weapons in the hand: a wider light, kept out of the wall in front of the player.
	light.m_bSplit = pAnimating && pAnimating->IsViewModel();
	if ( light.m_bSplit )
	{
		light.m_flRadius = VIEWMODEL_RADIUS;	// not the 220 .. 380 of glowing things in the world: it lit whole walls
		light.m_vecModelOrigin = light.m_vecOrigin;
		light.m_flModelScale = VIEWMODEL_MODEL_BRIGHTNESS * ( 1.0f + Fidelity_ReloadFlare( pAnimating, *pGlow ) );
		Fidelity_KeepInOpen( vecView, light.m_vecOrigin, GLOW_WALL_DISTANCE );
		light.m_flWorldScale = VIEWMODEL_WORLD_BRIGHTNESS;
	}
	light.m_bSeen = true;
	return true;
}

void CFidelityGlow::Update( float frametime )
{
	if ( !r_fidelity_glow.GetBool() || !effects || !engine->IsInGame() )
	{
		m_Lights.RemoveAll();
		return;
	}

	if ( !m_bStaticPropsLoaded )
	{
		m_bStaticPropsLoaded = true;
		LoadStaticProps();
	}
	if ( gpGlobals->realtime >= m_flNextScan )
	{
		m_flNextScan = gpGlobals->realtime + 0.5f;
		Scan();
	}

	// Nearest first
	const Vector &vecView = MainViewOrigin();
	const float flMaxDist = MAX( r_fidelity_glow_distance.GetFloat(), 1.0f );
	for ( int i = 0; i < m_Candidates.Count(); i++ )
	{
		if ( m_Candidates[ i ].m_nKind == GLOW_KIND_STATIC )
		{
			m_Candidates[ i ].m_flDistSqr = vecView.DistToSqr( m_StaticProps[ m_Candidates[ i ].m_nStatic ].m_vecCenter );
			continue;
		}
		C_BaseEntity *pEntity = m_Candidates[ i ].m_hEntity.Get();
		if ( !pEntity || pEntity->IsDormant() )
			m_Candidates[ i ].m_flDistSqr = FLT_MAX;
		else
			m_Candidates[ i ].m_flDistSqr = vecView.DistToSqr( pEntity->WorldSpaceCenter() );
	}
	m_Candidates.Sort( Fidelity_SortGlow );

	for ( int i = 0; i < m_Lights.Count(); i++ )
		m_Lights[ i ].m_bSeen = false;
	m_ViewModelsLit.RemoveAll();

	// The nearest r_fidelity_glow_count things that really glow right now get a
	// light (a weapon without glow, a sprite that is off, take no place).
	const float flBrightness = MAX( r_fidelity_glow_brightness.GetFloat(), 0.0f );
	const int nMaxLights = MAX( r_fidelity_glow_count.GetInt(), 0 );
	int nLit = 0;
	for ( int i = 0; i < m_Candidates.Count() && nLit < nMaxLights; i++ )
	{
		const FidelityGlowCandidate_t &candidate = m_Candidates[ i ];
		if ( candidate.m_flDistSqr >= flMaxDist * flMaxDist )
			break;
		C_BaseEntity *pEntity = candidate.m_hEntity.Get();

		// Fade out over the last third of the distance, so lights never pop.
		const float flDist = sqrtf( candidate.m_flDistSqr );
		const float flFade = clamp( ( flMaxDist - flDist ) / ( flMaxDist / 3.0f ), 0.0f, 1.0f );

		const float flBase = flBrightness * flFade;
		if ( candidate.m_nKind == GLOW_KIND_STATIC )
		{
			if ( LightStatic( candidate.m_nStatic, flBase ) )
				nLit++;
			continue;
		}
		if ( !pEntity )
			continue;

		// The entity list slot: unique among live entities, client-only ones too
		const int nKey = pEntity->GetRefEHandle().GetEntryIndex();
		bool bLit;
		const bool bWeapon = Fidelity_OnViewModel( pEntity );
		switch ( candidate.m_nKind )
		{
		case GLOW_KIND_FIRE:		bLit = LightFire( pEntity, nKey, flBase ); break;
		case GLOW_KIND_ROCKET:		bLit = LightRocket( pEntity, nKey, flBase ); break;
		case GLOW_KIND_BEAM:		bLit = LightBeam( pEntity, nKey, flBase ); break;
		case GLOW_KIND_ROLLERMINE:	bLit = LightRollermine( pEntity, nKey, flBase ); break;
		case GLOW_KIND_COMBINEBALL:	bLit = LightCombineBall( pEntity, nKey, flBase ); break;
		case GLOW_KIND_GRENADE:		bLit = LightGrenade( pEntity, nKey, flBase ); break;
		case GLOW_KIND_SPRITE:		bLit = LightSprite( pEntity, nKey, flBase, vecView ); break;
		default:					bLit = LightModel( pEntity, nKey, flBase, vecView ); break;
		}
		// The weapon in the hand is always lit and takes no place of the others
		if ( bLit && !bWeapon )
			nLit++;
	}

	// Fade, afterglow, give the engine the lights (only as many as are free)
	const float flRadiusScale = MAX( r_fidelity_glow_radius.GetFloat(), 0.0f );
	int nFree = MAX_DLIGHTS - GLOW_RESERVED_SLOTS - Fidelity_OtherLights();
	if ( r_fidelity_glow_debug.GetBool() )
		Debug( m_Candidates.Count(), nFree );
	for ( int i = m_Lights.Count() - 1; i >= 0; i-- )
	{
		FidelityGlowLight_t &light = m_Lights[ i ];
		if ( light.m_nKind == GLOW_KIND_TRACER )
		{
			// Flies from its start at its speed, gone when it arrives
			const float flAge = gpGlobals->curtime - light.m_flStartTime;
			light.m_vecOrigin += light.m_vecTravel * frametime;
			light.m_flTarget = ( flAge < light.m_flTravelTime ) ? flBrightness * TRACER_BRIGHTNESS * light.m_flBrightness : 0.0f;
			light.m_flIntensity = light.m_flTarget;
			if ( vecView.DistToSqr( light.m_vecOrigin ) > flMaxDist * flMaxDist )
				light.m_flIntensity = 0.0f;
		}
		else if ( light.m_nKind == GLOW_KIND_EXPLOSION )
		{
			// Fireball: hot yellow, over orange to red, gone after EXPLOSION_SECONDS
			const float flAge = clamp( ( gpGlobals->curtime - light.m_flStartTime ) / EXPLOSION_SECONDS, 0.0f, 1.0f );
			light.m_vecColor = Lerp( flAge, Vector( s_vecExplosionHot[ 0 ], s_vecExplosionHot[ 1 ], s_vecExplosionHot[ 2 ] ),
				Vector( s_vecExplosionCool[ 0 ], s_vecExplosionCool[ 1 ], s_vecExplosionCool[ 2 ] ) );
			light.m_flRadius = EXPLOSION_RADIUS * ( 1.0f - 0.35f * flAge );
			light.m_flTarget = flAge < 1.0f ? flBrightness * EXPLOSION_BRIGHTNESS * ( 1.0f - flAge ) * ( 1.0f - flAge ) : 0.0f;
			light.m_flIntensity = light.m_flTarget;
			if ( vecView.DistToSqr( light.m_vecOrigin ) > Square( flMaxDist + EXPLOSION_RADIUS ) )
				light.m_flIntensity = 0.0f;
		}
		else if ( light.m_nKind == GLOW_KIND_AFTERGLOW )
		{
			// Glows on where it stuck: hot, then cooling down.
			const float flAge = clamp( ( gpGlobals->curtime - light.m_flStartTime ) / AFTERGLOW_SECONDS, 0.0f, 1.0f );
			light.m_vecColor = Lerp( flAge, Vector( s_vecAfterglowHot[ 0 ], s_vecAfterglowHot[ 1 ], s_vecAfterglowHot[ 2 ] ),
				Vector( s_vecAfterglowCool[ 0 ], s_vecAfterglowCool[ 1 ], s_vecAfterglowCool[ 2 ] ) );
			light.m_flTarget = flAge < 1.0f ? flBrightness * AFTERGLOW_BRIGHTNESS * powf( 1.0f - flAge, 1.5f ) : 0.0f;
			light.m_flIntensity = light.m_flTarget;
			// Not too far away to be worth a light
			if ( vecView.DistToSqr( light.m_vecOrigin ) > flMaxDist * flMaxDist )
				light.m_flIntensity = 0.0f;
		}
		else
		{
			if ( !light.m_bSeen )
				light.m_flTarget = 0.0f;
			if ( light.m_flIntensity < light.m_flTarget )
				light.m_flIntensity = MIN( light.m_flTarget, light.m_flIntensity + frametime / light.m_flFadeIn );
			else
				light.m_flIntensity = MAX( light.m_flTarget, light.m_flIntensity - frametime / light.m_flFadeOut );
		}

		if ( light.m_flIntensity <= 0.0f && light.m_flTarget <= 0.0f )
			m_Lights.FastRemove( i );
	}
	GiveLights( nFree, flRadiusScale );
}

// One of our engine lights for this frame (kept alive by setting it again every frame)
static void Fidelity_SetLight( dlight_t *pLight, const Vector &vecOrigin, float flRadius, const Vector &vecColor )
{
	pLight->origin = vecOrigin;
	pLight->radius = flRadius;
	pLight->die = gpGlobals->curtime + 0.1f;
	pLight->decay = 0.0f;
	pLight->minlight = 0.0f;
	pLight->style = 0;
	pLight->flags = 0;
	VectorToColorRGBExp32( vecColor, pLight->color );
}

static int __cdecl Fidelity_SortOrder( const FidelityGlowOrder_t *pA, const FidelityGlowOrder_t *pB )
{
	return ( pA->m_flScore > pB->m_flScore ) ? -1 : ( pA->m_flScore < pB->m_flScore ) ? 1 : 0;
}

//-----------------------------------------------------------------------------
// Gives the lights to the engine. Most important first (brightness x reach,
// less with distance: the weapon in the hand and what is in front of the
// player); a light close to one already given joins it (one engine light at
// their weighted middle, their colours added); world lights while free, then
// model-only lights. Engine lights of ours not given again go out now (not
// 0.1 s later), so a light moving to another group is never there twice.
//-----------------------------------------------------------------------------
void CFidelityGlow::GiveLights( int nFree, float flRadiusScale )
{
	const Vector &vecView = MainViewOrigin();
	m_Order.RemoveAll();
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		const FidelityGlowLight_t &light = m_Lights[ i ];
		if ( light.m_flIntensity <= 0.0f )
			continue;
		FidelityGlowOrder_t &order = m_Order[ m_Order.AddToTail() ];
		order.m_nLight = i;
		order.m_flScore = light.m_flIntensity * light.m_flRadius / MAX( vecView.DistTo( light.m_vecOrigin ), 32.0f );
	}
	m_Order.Sort( Fidelity_SortOrder );

	const int nMaxElights = clamp( r_fidelity_glow_elights.GetInt(), 0, MAX_GLOW_ELIGHTS );
	int nElights = 0;
	m_Clusters.RemoveAll();
	for ( int n = 0; n < m_Order.Count(); n++ )
	{
		const FidelityGlowLight_t &light = m_Lights[ m_Order[ n ].m_nLight ];
		FidelityGlowCluster_t *pCluster = NULL;
		for ( int c = 0; c < m_Clusters.Count() && !pCluster; c++ )
		{
			if ( m_Clusters[ c ].m_bWorldOnly != ( light.m_bSplit || light.m_bNoModels ) )
				continue;	// a world-only light and one that lights models stay apart
			const float flMerge = GLOW_MERGE_SHARE * MIN( light.m_flRadius, m_Clusters[ c ].m_flRadius );
			const Vector vecCenter = m_Clusters[ c ].m_vecOriginSum / m_Clusters[ c ].m_flWeight;
			if ( vecCenter.DistToSqr( light.m_vecOrigin ) < flMerge * flMerge )
				pCluster = &m_Clusters[ c ];
		}
		if ( !pCluster )
		{
			const bool bElight = nFree <= 0;
			if ( bElight && ( nElights >= nMaxElights || light.m_bSplit || light.m_bNoModels ) )
				continue;	// (a weapon light without a world light still has its model light, below)
			if ( bElight )
				nElights++;
			else
				nFree--;
			pCluster = &m_Clusters[ m_Clusters.AddToTail() ];
			pCluster->m_nKey = light.m_nKey;
			pCluster->m_bElight = bElight;
			pCluster->m_bWorldOnly = light.m_bSplit || light.m_bNoModels;
			pCluster->m_vecOriginSum.Init();
			pCluster->m_vecEnergy.Init();
			pCluster->m_flWeight = 0.0f;
			pCluster->m_flRadius = 0.0f;
		}
		const float flWorld = light.m_flIntensity * light.m_flWorldScale;
		pCluster->m_vecOriginSum += light.m_vecOrigin * flWorld;
		pCluster->m_vecEnergy += light.m_vecColor * flWorld;
		pCluster->m_flWeight += flWorld;
		pCluster->m_flRadius = MAX( pCluster->m_flRadius, light.m_flRadius );
	}

	// Model-only lights of weapons in the hand, where they really glow (weaker:
	// a few units from the weapon's own surfaces)
	m_ModelLights.RemoveAll();
	for ( int n = 0; n < m_Order.Count(); n++ )
	{
		const FidelityGlowLight_t &light = m_Lights[ m_Order[ n ].m_nLight ];
		if ( light.m_bSplit )
			m_ModelLights.AddToTail( m_Order[ n ].m_nLight );
	}

	// Elight keys given this frame
	CUtlVector< int > &elights = m_NewElights;
	elights.RemoveAll();
	for ( int c = 0; c < m_Clusters.Count(); c++ )
	{
		if ( m_Clusters[ c ].m_bElight )
			elights.AddToTail( m_Clusters[ c ].m_nKey );
	}
	for ( int i = 0; i < m_ModelLights.Count(); i++ )
		elights.AddToTail( m_Lights[ m_ModelLights[ i ] ].m_nKey );

	// Our engine lights from last frame that are not given again: out now (not
	// 0.1 s later, so a light moving to another group is never there twice)
	dlight_t *pActive[ MAX_DLIGHTS ];
	const int nActive = effects->CL_GetActiveDLights( pActive );
	for ( int i = 0; i < nActive; i++ )
	{
		const int nKey = pActive[ i ]->key - GLOW_LIGHT_KEY_BASE;
		if ( nKey < 0 || nKey >= 0x10000 )
			continue;
		bool bGiven = false;
		for ( int c = 0; c < m_Clusters.Count() && !bGiven; c++ )
			bGiven = !m_Clusters[ c ].m_bElight && m_Clusters[ c ].m_nKey == nKey;
		if ( !bGiven )
		{
			pActive[ i ]->die = gpGlobals->curtime;
			pActive[ i ]->radius = 0.0f;
		}
	}
	for ( int i = 0; i < m_Elights.Count(); i++ )
	{
		dlight_t *pOld = elights.Find( m_Elights[ i ] ) != elights.InvalidIndex() ? NULL : effects->GetElightByKey( GLOW_LIGHT_KEY_BASE + m_Elights[ i ] );
		if ( pOld )
		{
			pOld->die = gpGlobals->curtime;
			pOld->radius = 0.0f;
		}
	}
	m_Elights.RemoveAll();
	m_Elights.AddVectorToTail( elights );

	for ( int c = 0; c < m_Clusters.Count(); c++ )
	{
		const FidelityGlowCluster_t &cluster = m_Clusters[ c ];
		dlight_t *pLight = cluster.m_bElight ? effects->CL_AllocElight( GLOW_LIGHT_KEY_BASE + cluster.m_nKey ) : effects->CL_AllocDlight( GLOW_LIGHT_KEY_BASE + cluster.m_nKey );
		if ( !pLight )
			continue;
		Fidelity_SetLight( pLight, cluster.m_vecOriginSum / cluster.m_flWeight, cluster.m_flRadius * flRadiusScale, cluster.m_vecEnergy * GLOW_LIGHT_SCALE );
		pLight->flags = cluster.m_bWorldOnly ? DLIGHT_NO_MODEL_ILLUMINATION : 0;
	}
	for ( int i = 0; i < m_ModelLights.Count(); i++ )
	{
		const FidelityGlowLight_t &light = m_Lights[ m_ModelLights[ i ] ];
		dlight_t *pLight = effects->CL_AllocElight( GLOW_LIGHT_KEY_BASE + light.m_nKey );
		if ( pLight )
		{
			Fidelity_SetLight( pLight, light.m_vecModelOrigin, MIN( light.m_flRadius, VIEWMODEL_MODEL_RADIUS ) * flRadiusScale,
				light.m_vecColor * ( GLOW_LIGHT_SCALE * light.m_flModelScale * light.m_flIntensity ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Muzzle flashes: a bright, very short light on the world. Combine weapons
// flash blue-white, all others warm.
//-----------------------------------------------------------------------------
static void Fidelity_FlashLight( const Vector &vecOrigin, const Vector &vecColor, float flRadius, ClientEntityHandle_t hEntity );

void Fidelity_MuzzleFlashAt( const Vector &vecOrigin, int nType, ClientEntityHandle_t hEntity, bool bFirstPerson )
{
	if ( bFirstPerson )
		Fidelity_MuzzleSmokeShot( nType );	// after sustained fire the barrel smokes (fidelity_effects.cpp)
	else
		Fidelity_DistantSound( vecOrigin, nType );	// far away: its echo follows (fidelity_effects.cpp)
	if ( !r_fidelity_muzzlelight.GetBool() || !effects )
		return;
	if ( nType == MUZZLEFLASH_RPG && bFirstPerson )
		return;	// the player's RPG has no flash either

	const bool bCombine = ( nType == MUZZLEFLASH_AR2 || nType == MUZZLEFLASH_COMBINE || nType == MUZZLEFLASH_COMBINE_TURRET );
	// Combine weapons blue-white, the RPG as before, all others a warm yellow (Julius: "etwas gelber")
	Vector vecColor = bCombine ? Vector( 0.55f, 0.75f, 1.0f ) : ( nType == MUZZLEFLASH_RPG ? Vector( 1.0f, 0.78f, 0.45f ) : Vector( 1.0f, 0.86f, 0.36f ) );
	// Sustained fire of the player's own weapon: the first shot flashes fully,
	// the shots right after it (closer than MUZZLE_RAPID_GAP) much weaker - ten
	// full flashes a second (AR2, SMG) strobed the whole room (Julius 26.10.2026:
	// "für Leute, die empfindlich sind, too much")
	static float s_flLastOwnShot = -1.0f;
	if ( bFirstPerson )
	{
		if ( gpGlobals->curtime - s_flLastOwnShot < MUZZLE_RAPID_GAP && gpGlobals->curtime >= s_flLastOwnShot )
			vecColor *= MUZZLE_RAPID_BRIGHTNESS;
		s_flLastOwnShot = gpGlobals->curtime;
	}
	float flRadius = ( nType == MUZZLEFLASH_SHOTGUN || nType == MUZZLEFLASH_357 ) ? 280.0f : 220.0f;
	flRadius *= random->RandomFloat( 0.9f, 1.1f ) * ( bFirstPerson ? 1.0f : 0.85f );

	// also as a projected light for a moment (bump maps, specular - fidelity_flashes.cpp); the engine
	// light stays for what lies beside and behind, dimmed
	const bool bProjected = Fidelity_ProjectedMuzzleFlash( vecOrigin, vecColor, hEntity, bFirstPerson );
	Fidelity_FlashLight( vecOrigin, vecColor * ( bProjected ? MUZZLE_LIGHT_WITH_PROJECTED : 1.0f ), flRadius, hEntity );
}

// A muzzle flash's light: 0.06 s on the world. A flash is short: it may use
// the reserve, but never the last two slots.
static void Fidelity_FlashLight( const Vector &vecOrigin, const Vector &vecColor, float flRadius, ClientEntityHandle_t hEntity )
{
	if ( !Fidelity_FlashFits( 2 ) )
		return;

	C_BaseEntity *pEntity = ClientEntityList().GetBaseEntityFromHandle( hEntity );
	const int nKey = ( pEntity && pEntity->entindex() >= 0 ) ? pEntity->entindex() : 0;
	dlight_t *pLight = effects->CL_AllocDlight( MUZZLE_LIGHT_KEY_BASE + nKey );
	if ( !pLight )
		return;
	pLight->origin = vecOrigin;
	pLight->radius = flRadius;
	pLight->die = gpGlobals->curtime + 0.06f;
	pLight->decay = flRadius / 0.06f;
	pLight->minlight = 0.0f;
	pLight->style = 0;
	pLight->flags = 0;
	VectorToColorRGBExp32( vecColor * ( MUZZLE_LIGHT_SCALE * MAX( r_fidelity_muzzlelight_brightness.GetFloat(), 0.0f ) ), pLight->color );
}

void Fidelity_MuzzleLight( const Vector &vecOrigin, const char *pszMaterial, const Vector &vecColor, float flRadius, ClientEntityHandle_t hEntity )
{
	if ( !r_fidelity_muzzlelight.GetBool() || !effects )
		return;
	Fidelity_FlashLight( vecOrigin, Fidelity_MaterialColor( pszMaterial, vecColor ), flRadius * random->RandomFloat( 0.9f, 1.1f ), hEntity );
}

//-----------------------------------------------------------------------------
// Sparks of a bullet on metal: a tiny, very short warm light in front of the
// surface; electric sparks (env_spark, broken Combine machines): a little
// bigger and blue-white. At most SPARK_LIGHTS at once (automatic fire on a
// metal wall).
//-----------------------------------------------------------------------------
#define SPARK_LIGHTS			4

void Fidelity_SparkLight( const Vector &vecOrigin, const Vector &vecNormal, bool bElectric )
{
	static int s_nSparks = 0;
	if ( !r_fidelity_sparklight.GetBool() || !effects || !Fidelity_FlashFits( 4 ) )
		return;
	dlight_t *pLight = effects->CL_AllocDlight( SPARK_KEY_BASE + ( s_nSparks++ % SPARK_LIGHTS ) );
	if ( !pLight )
		return;
	pLight->origin = vecOrigin + vecNormal * 20.0f;	// a little off the wall (lightmap texels)
	pLight->radius = bElectric ? random->RandomFloat( 90.0f, 140.0f ) : random->RandomFloat( 60.0f, 90.0f );
	const float flLife = bElectric ? 0.12f : 0.07f;
	pLight->die = gpGlobals->curtime + flLife;
	pLight->decay = pLight->radius / flLife;
	pLight->minlight = 0.0f;
	pLight->style = 0;
	pLight->flags = 0;
	VectorToColorRGBExp32( ( bElectric ? Vector( 0.75f, 0.88f, 1.0f ) : Vector( 1.0f, 0.75f, 0.35f ) ) * 220.0f, pLight->color );
}

//-----------------------------------------------------------------------------
// Vortigaunt zap: a green-white flash where it strikes (pulled back towards
// where it came from, off the surface) and, for the real zap, a smaller one
// at its hands. At most ZAP_LIGHTS at once.
//-----------------------------------------------------------------------------
#define ZAP_LIGHTS				4
static const float s_vecZapColor[ 3 ] = { 0.6f, 1.0f, 0.55f };

void Fidelity_ZapLight( const Vector &vecFrom, const Vector &vecHit, float flScale )
{
	static int s_nZaps = 0;
	if ( !r_fidelity_glow.GetBool() || !effects || !Fidelity_FlashFits( 2 ) )
		return;
	const Vector vecColor( s_vecZapColor[ 0 ], s_vecZapColor[ 1 ], s_vecZapColor[ 2 ] );
	Vector vecOrigin = vecHit;
	Fidelity_KeepInOpen( vecFrom, vecOrigin, LIGHT_WALL_DISTANCE );
	dlight_t *pLight = effects->CL_AllocDlight( ZAP_KEY_BASE + ( s_nZaps++ % ZAP_LIGHTS ) );
	if ( !pLight )
		return;
	const float flLife = 0.3f;
	pLight->origin = vecOrigin;
	pLight->radius = 300.0f * flScale;
	pLight->die = gpGlobals->curtime + flLife;
	pLight->decay = pLight->radius / flLife;
	pLight->minlight = 0.0f;
	pLight->style = 0;
	pLight->flags = 0;
	VectorToColorRGBExp32( vecColor * ( 320.0f * MAX( r_fidelity_glow_brightness.GetFloat(), 0.0f ) ), pLight->color );
	if ( flScale >= 1.0f && Fidelity_FlashFits( 2 ) )
	{
		const ColorRGBExp32 color = pLight->color;
		dlight_t *pHands = effects->CL_AllocDlight( ZAP_KEY_BASE + ( s_nZaps++ % ZAP_LIGHTS ) );
		if ( pHands )
		{
			pHands->origin = vecFrom;
			pHands->radius = 180.0f;
			pHands->die = gpGlobals->curtime + flLife;
			pHands->decay = pHands->radius / flLife;
			pHands->minlight = 0.0f;
			pHands->style = 0;
			pHands->flags = 0;
			pHands->color = color;
		}
	}
}

void Fidelity_MuzzleFlash( int nType, ClientEntityHandle_t hEntity, int nAttachment, bool bFirstPerson )
{
	Vector vecOrigin;
	// (no r_fidelity_muzzlelight test here: muzzle smoke and the far-away echo start in there too)
	if ( FX_GetAttachmentTransform( hEntity, nAttachment, &vecOrigin, NULL ) )
		Fidelity_MuzzleFlashAt( vecOrigin, nType, hEntity, bFirstPerson );
}
