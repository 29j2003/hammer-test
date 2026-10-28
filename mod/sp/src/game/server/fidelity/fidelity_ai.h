//========= mapbase-fidelity ==================================================//
//
// Purpose: Combine soldiers a little more like Half-Life 1's grunts
//          (npc_fidelity_tactics, on by default).
//
// Players often find Half-Life 1's soldiers livelier: they dash out of cover
// and back, go looking for a player who hid, and pull back when badly hurt.
// Half-Life 2's Combine (npc_combine.cpp) mostly wait in cover until one of
// the squad's two attack slots is free - with a hidden player a squad can
// sit still for a long time. Two small changes, in
// CNPC_Combine::SelectCombatSchedule and BuildScheduleTestBits (lines marked
// "mapbase-fidelity"):
//
//   - Hunting a hidden enemy: a soldier who would wait in cover, and has not
//     seen the enemy for npc_fidelity_hunt_after seconds (a little more for
//     each soldier, so a squad does not move at once), goes to find a line
//     of fire anyway - as the vanilla code already does for soldiers 3000
//     units away. Not more often than every npc_fidelity_hunt_interval.
//   - Falling back when badly hurt: a soldier just hit, below
//     npc_fidelity_fallback_health of his health, runs for cover (the
//     vanilla code only does that half the time, and only when standing).
//     At most every npc_fidelity_fallback_interval seconds. The choice is
//     only made when a schedule ends or is broken off, and most of the
//     soldiers' schedules (pressing on, finding a line of fire, waiting in
//     cover, a heavy hit's flinch) are not broken off by a hit - by their end
//     the hit was forgotten, and a hurt soldier did not fall back (Julius
//     25.10.2026: "rennt nicht in die Deckung, wenn er zu low ist"). So
//     while a soldier is that low, a hit breaks off what he does
//     (CNPC_Combine::BuildScheduleTestBits), and a hit up to
//     FIDELITY_FALLBACK_RECENT seconds ago still counts (the flinch first).
//     Not while a behaviour the mapper gave him leads (assault, func_tank,
//     standoff): those choose for themselves. Cover comes from the map's
//     nodes (info_node, hint nodes); without them only a step to the side.
//
// Both show it to the squad with a hand signal (gesture layered over the
// run - "advance", "take cover"), when the model has the gesture; the
// Combine already play full hand signals in other places.
//
// Header only (included once, by npc_combine.cpp): no new file in the
// server's project, nothing to configure in XBLAH. Mapbase's own AI fixes
// that are off by default (ai_enemy_memory_fixes, npc_combine_fixed_shootpos,
// ai_squad_broadcast_elusion) are switched on in fidelity.cfg.
//
//=============================================================================//

#ifndef FIDELITY_AI_H
#define FIDELITY_AI_H
#ifdef _WIN32
#pragma once
#endif

static ConVar npc_fidelity_tactics( "npc_fidelity_tactics", "1", FCVAR_ARCHIVE, "Combine soldiers hunt a hidden enemy and fall back when badly hurt, as Half-Life 1's grunts. 0 = as Half-Life 2." );
static ConVar npc_fidelity_hunt_after( "npc_fidelity_hunt_after", "6", FCVAR_NONE, "Seconds without seeing a hidden enemy before a soldier waiting in cover goes to find him." );
static ConVar npc_fidelity_hunt_interval( "npc_fidelity_hunt_interval", "8", FCVAR_NONE, "Seconds a soldier waits before going to find a hidden enemy again." );
static ConVar npc_fidelity_fallback_health( "npc_fidelity_fallback_health", "0.4", FCVAR_NONE, "Share of his health below which a soldier just hit runs for cover." );
static ConVar npc_fidelity_fallback_interval( "npc_fidelity_fallback_interval", "10", FCVAR_NONE, "Seconds before a soldier falls back again." );

// Should a soldier who would wait in cover go to find the hidden enemy? (flNext: his own timer)
inline bool Fidelity_CombineHunt( CAI_BaseNPC *pNPC, float &flNext )
{
	if ( !npc_fidelity_tactics.GetBool() || !pNPC->GetEnemy() || gpGlobals->curtime < flNext )
		return false;
	// a little later for each soldier: a squad does not leave its cover at once
	const float flAfter = npc_fidelity_hunt_after.GetFloat() + ( pNPC->entindex() % 4 ) * 1.5f;
	if ( gpGlobals->curtime - pNPC->GetEnemyLastTimeSeen() < flAfter )
		return false;
	flNext = gpGlobals->curtime + npc_fidelity_hunt_interval.GetFloat();
	// "advance" to the squad, as a gesture over the run (only if the model has one)
#if SHARED_COMBINE_ACTIVITIES
	if ( pNPC->IsInSquad() && pNPC->HaveSequenceForActivity( ACT_GESTURE_SIGNAL_ADVANCE ) )
		pNPC->AddGesture( ACT_GESTURE_SIGNAL_ADVANCE );
#endif
	return true;
}

#define FIDELITY_FALLBACK_RECENT	2.0f	// seconds a hit still counts for falling back (a flinch plays first)

// Is a soldier hurt so badly that he would fall back when hit? (flNext: his own timer)
inline bool Fidelity_CombineLow( CAI_BaseNPC *pNPC, float flNext )
{
	if ( !npc_fidelity_tactics.GetBool() || !pNPC->GetEnemy() || gpGlobals->curtime < flNext )
		return false;
	if ( pNPC->GetRunningBehavior() != NULL )
		return false;
	return pNPC->GetHealth() <= pNPC->GetMaxHealth() * npc_fidelity_fallback_health.GetFloat();
}

// Interrupts of the soldier's schedule: while he is low, a hit breaks off what he does
inline void Fidelity_CombineInterrupts( CAI_BaseNPC *pNPC, float flNext )
{
	if ( !Fidelity_CombineLow( pNPC, flNext ) )
		return;
	pNPC->SetCustomInterruptCondition( COND_LIGHT_DAMAGE );
	pNPC->SetCustomInterruptCondition( COND_HEAVY_DAMAGE );
}

// Should a soldier just hit badly run for cover? (flNext: his own timer)
inline bool Fidelity_CombineFallBack( CAI_BaseNPC *pNPC, float &flNext )
{
	if ( !Fidelity_CombineLow( pNPC, flNext ) )
		return false;
	if ( gpGlobals->curtime - pNPC->GetLastDamageTime() > FIDELITY_FALLBACK_RECENT )
		return false;
	flNext = gpGlobals->curtime + npc_fidelity_fallback_interval.GetFloat();
#if SHARED_COMBINE_ACTIVITIES
	if ( pNPC->IsInSquad() && pNPC->HaveSequenceForActivity( ACT_GESTURE_SIGNAL_TAKECOVER ) )
		pNPC->AddGesture( ACT_GESTURE_SIGNAL_TAKECOVER );
#endif
	return true;
}

#endif // FIDELITY_AI_H
