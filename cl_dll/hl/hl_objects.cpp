/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "../hud.h"
#include "../cl_util.h"
#include "../demo.h"

#include "demo_api.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"

#include "pm_defs.h"
#include "event_api.h"
#include "entity_types.h"
#include "r_efx.h"

extern BEAM *pBeam;
extern BEAM *pBeam2;
extern TEMPENTITY *pFlare;	// Vit_amiN: egon's energy flare
void HUD_GetLastOrg( float *org );

// diagnostico del rayo del Gluon: se guarda en memoria y se muestra con "wii_egon"
static struct
{
    int count;
    vec3_t origin, angles, vecEnd, endpos, bsource, btarget, attach, porigin;
    float fraction;
    int allsolid, startsolid, ent;
} egon_diag;

void EgonDiag_f( void )
{
    gEngfuncs.Con_Printf( "wii_egon: actualizaciones=%d (0 = UpdateBeams nunca corrio)\n", egon_diag.count );
    if( !egon_diag.count )
        return;
    gEngfuncs.Con_Printf( "origen usado  %.0f %.0f %.0f\n", egon_diag.origin[0], egon_diag.origin[1], egon_diag.origin[2] );
    gEngfuncs.Con_Printf( "ent jugador   %.0f %.0f %.0f\n", egon_diag.porigin[0], egon_diag.porigin[1], egon_diag.porigin[2] );
    gEngfuncs.Con_Printf( "angulos vista %.1f %.1f %.1f\n", egon_diag.angles[0], egon_diag.angles[1], egon_diag.angles[2] );
    gEngfuncs.Con_Printf( "fin del trazo %.0f %.0f %.0f\n", egon_diag.vecEnd[0], egon_diag.vecEnd[1], egon_diag.vecEnd[2] );
    gEngfuncs.Con_Printf( "impacto       %.0f %.0f %.0f frac=%.3f ent=%d allsolid=%d startsolid=%d\n",
        egon_diag.endpos[0], egon_diag.endpos[1], egon_diag.endpos[2], egon_diag.fraction, egon_diag.ent, egon_diag.allsolid, egon_diag.startsolid );
    gEngfuncs.Con_Printf( "attachment 1  %.0f %.0f %.0f\n", egon_diag.attach[0], egon_diag.attach[1], egon_diag.attach[2] );
    gEngfuncs.Con_Printf( "rayo source   %.0f %.0f %.0f\n", egon_diag.bsource[0], egon_diag.bsource[1], egon_diag.bsource[2] );
    gEngfuncs.Con_Printf( "rayo target   %.0f %.0f %.0f\n", egon_diag.btarget[0], egon_diag.btarget[1], egon_diag.btarget[2] );
}

void UpdateBeams( void )
{
	vec3_t forward, vecSrc, vecEnd, origin, angles, right, up;
	vec3_t view_ofs;
	pmtrace_t tr;
	cl_entity_t *pthisplayer = gEngfuncs.GetLocalPlayer();
	int idx = pthisplayer->index;
		
	// Get our exact viewangles from engine
	gEngfuncs.GetViewAngles( (float *)angles );

	// Determine our last predicted origin
	HUD_GetLastOrg( (float *)&origin );

	// previousorigin solo lo guarda HUD_WeaponsPostThink, que unicamente corre con
	// CLIENT_WEAPONS. Sin eso se queda en (0,0,0) y el rayo del Gluon apuntaba siempre
	// al origen del mapa (se veia fijo y vertical). Se usa la posicion de ojos actual,
	// igual que EV_GetGunPosition para el jugador local.
	if( origin[0] == 0.0f && origin[1] == 0.0f && origin[2] == 0.0f )
	{
		VectorCopy( pthisplayer->origin, origin );
		gEngfuncs.pEventAPI->EV_LocalPlayerViewheight( view_ofs );
		VectorAdd( origin, view_ofs, origin );
	}

	AngleVectors( angles, forward, right, up );

	VectorCopy( origin, vecSrc );

	VectorMA( vecSrc, 2048, forward, vecEnd );

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );	
					
	// Store off the old count
	gEngfuncs.pEventAPI->EV_PushPMStates();

	// Now add in all of the players.
	gEngfuncs.pEventAPI->EV_SetSolidPlayers( idx - 1 );	

	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( vecSrc, vecEnd, PM_STUDIO_BOX, -1, &tr );

	gEngfuncs.pEventAPI->EV_PopPMStates();

	egon_diag.count++;
	VectorCopy( origin, egon_diag.origin );
	VectorCopy( pthisplayer->origin, egon_diag.porigin );
	VectorCopy( angles, egon_diag.angles );
	VectorCopy( vecEnd, egon_diag.vecEnd );
	VectorCopy( tr.endpos, egon_diag.endpos );
	VectorCopy( pthisplayer->attachment[0], egon_diag.attach );
	egon_diag.fraction = tr.fraction;
	egon_diag.allsolid = tr.allsolid;
	egon_diag.startsolid = tr.startsolid;
	egon_diag.ent = tr.ent;
	if( pBeam )
	{
		VectorCopy( pBeam->source, egon_diag.bsource );
		VectorCopy( pBeam->target, egon_diag.btarget );
	}

	if( pBeam )
	{
		pBeam->target = tr.endpos;
		pBeam->die = gEngfuncs.GetClientTime() + 0.1f; // We keep it alive just a little bit forward in the future, just in case.
	}

	if( pBeam2 )
	{
		pBeam2->target = tr.endpos;
		pBeam2->die = gEngfuncs.GetClientTime() + 0.1f; // We keep it alive just a little bit forward in the future, just in case.
	}

	if( pFlare )	// Vit_amiN: beam flare
	{
		pFlare->entity.origin = tr.endpos;
		pFlare->die = gEngfuncs.GetClientTime() + 0.1f; // We keep it alive just a little bit forward in the future, just in case.

		if( gEngfuncs.GetMaxClients() != 1 )	// Singleplayer always draws the egon's energy beam flare
		{
			pFlare->flags |= FTENT_NOMODEL;

			if( !( tr.allsolid || tr.ent <= 0 || tr.fraction == 1.0f ) )	// Beam hit some non-world entity
			{
				physent_t *pEntity = gEngfuncs.pEventAPI->EV_GetPhysent( tr.ent );

				// Not the world, let's assume that we hit something organic ( dog, cat, uncle joe, etc )
				if( pEntity && !( pEntity->solid == SOLID_BSP || pEntity->movetype == MOVETYPE_PUSHSTEP ) )
				{
					pFlare->flags &= ~FTENT_NOMODEL;
				}
			}
		}
	}
}

/*
=====================
Game_AddObjects

Add game specific, client-side objects here
=====================
*/
void Game_AddObjects( void )
{
	if( pBeam || pBeam2 || pFlare )	// Vit_amiN: egon flare added
		UpdateBeams();
}
