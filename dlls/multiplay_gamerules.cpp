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
//
// teamplay_gamerules.cpp
//

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"player.h"
#include	"weapons.h"
#include	"gamerules.h"
 
#include	"skill.h"
#include	"game.h"
#include	"items.h"
#if !NO_VOICEGAMEMGR
#include	"voice_gamemgr.h"
#endif
#include	"hltv.h"
#include	"trains.h"

extern DLL_GLOBAL CGameRules *g_pGameRules;
extern DLL_GLOBAL BOOL	g_fGameOver;
extern int gmsgDeathMsg;	// client dll messages
extern int gmsgScoreInfo;
extern int gmsgMOTD;
extern int gmsgServerName;

extern int g_teamplay;

#define ITEM_RESPAWN_TIME	30
#define WEAPON_RESPAWN_TIME	20
#define AMMO_RESPAWN_TIME	20

float g_flIntermissionStartTime = 0;

#if !NO_VOICEGAMEMGR
CVoiceGameMgr	g_VoiceGameMgr;

class CMultiplayGameMgrHelper : public IVoiceGameMgrHelper
{
public:
	virtual bool CanPlayerHearPlayer(CBasePlayer *pListener, CBasePlayer *pTalker)
	{
		if( g_teamplay )
		{
			if( g_pGameRules->PlayerRelationship( pListener, pTalker ) != GR_TEAMMATE )
			{
				return false;
			}
		}

		return true;
	}
};

static CMultiplayGameMgrHelper g_GameMgrHelper;
#endif
//*********************************************************
// Rules for the half-life multiplayer game.
//*********************************************************
CHalfLifeMultiplay::CHalfLifeMultiplay()
{
#if !NO_VOICEGAMEMGR
	g_VoiceGameMgr.Init( &g_GameMgrHelper, gpGlobals->maxClients );
#endif
	RefreshSkillData();
	m_flIntermissionEndTime = 0;
	g_flIntermissionStartTime = 0;
	
	// 11/8/98
	// Modified by YWB:  Server .cfg file is now a cvar, so that 
	//  server ops can run multiple game servers, with different server .cfg files,
	//  from a single installed directory.
	// Mapcyclefile is already a cvar.

	// 3/31/99
	// Added lservercfg file cvar, since listen and dedicated servers should not
	// share a single config file. (sjb)
	if( IS_DEDICATED_SERVER() )
	{
		// dedicated server
		/*const char *servercfgfile = CVAR_GET_STRING( "servercfgfile" );

		if( servercfgfile && servercfgfile[0] )
		{
			char szCommand[256];
			
			ALERT( at_console, "Executing dedicated server config file\n" );
			sprintf( szCommand, "exec %s\n", servercfgfile );
			SERVER_COMMAND( szCommand );
		}
		*/
		// this code has been moved into engine, to only run server.cfg once
	}
	else
	{
		// listen server
		const char *lservercfgfile = CVAR_GET_STRING( "lservercfgfile" );

		if( lservercfgfile && lservercfgfile[0] )
		{
			char szCommand[256];
			
			ALERT( at_console, "Executing listen server config file\n" );
			sprintf( szCommand, "exec %s\n", lservercfgfile );
			SERVER_COMMAND( szCommand );
		}
	}
}

BOOL CHalfLifeMultiplay::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
#if !NO_VOICEGAMEMGR
	if( g_VoiceGameMgr.ClientCommand( pPlayer, pcmd ) )
		return TRUE;
#endif
	return CGameRules::ClientCommand( pPlayer, pcmd );
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::RefreshSkillData( void )
{
	// load all default values
	CGameRules::RefreshSkillData();

	// override some values for multiplay.

	// suitcharger
	gSkillData.suitchargerCapacity = 30;

	// Crowbar whack
	gSkillData.plrDmgCrowbar = 25;

	// Glock Round
	gSkillData.plrDmg9MM = 12;

	// 357 Round
	gSkillData.plrDmg357 = 50;

	// MP5 Round
	gSkillData.plrDmgMP5 = 12;

	// M203 grenade
	gSkillData.plrDmgM203Grenade = 100;

	// Shotgun buckshot
	gSkillData.plrDmgBuckshot = 20;// fewer pellets in deathmatch

	// Crossbow
	gSkillData.plrDmgCrossbowClient = 20;

	// RPG
	gSkillData.plrDmgRPG = 120;

	// Egon
	gSkillData.plrDmgEgonWide = 20;
	gSkillData.plrDmgEgonNarrow = 10;

	// Hand Grendade
	gSkillData.plrDmgHandGrenade = 100;

	// Satchel Charge
	gSkillData.plrDmgSatchel = 120;

	// Tripmine
	gSkillData.plrDmgTripmine = 150;

	// hornet
	gSkillData.plrDmgHornet = 10;
}

// longest the intermission can last, in seconds
#define MAX_INTERMISSION_TIME		120

extern cvar_t timeleft, fragsleft;

extern cvar_t mp_chattime;

//=========================================================
//=========================================================
void CHalfLifeMultiplay::Think( void )
{
#if !NO_VOICEGAMEMGR
	g_VoiceGameMgr.Update( gpGlobals->frametime );
#endif

	///// Check game rules /////
	static int last_frags;
	static int last_time;

	int frags_remaining = 0;
	int time_remaining = 0;

	if( g_fGameOver )   // someone else quit the game already
	{
		// bounds check
		int time = (int)CVAR_GET_FLOAT( "mp_chattime" );
		if( time < 1 )
			CVAR_SET_STRING( "mp_chattime", "1" );
		else if( time > MAX_INTERMISSION_TIME )
			CVAR_SET_STRING( "mp_chattime", UTIL_dtos1( MAX_INTERMISSION_TIME ) );

		m_flIntermissionEndTime = g_flIntermissionStartTime + mp_chattime.value;

		// check to see if we should change levels now
		if( m_flIntermissionEndTime < gpGlobals->time )
		{
			if( m_iEndIntermissionButtonHit  // check that someone has pressed a key, or the max intermission time is over
				|| ( ( g_flIntermissionStartTime + MAX_INTERMISSION_TIME ) < gpGlobals->time ) ) 
				ChangeLevel(); // intermission is over
		}

		return;
	}

	float flTimeLimit = timelimit.value * 60;
	float flFragLimit = fraglimit.value;

	time_remaining = (int)( flTimeLimit ? ( flTimeLimit - gpGlobals->time ) : 0);

	if( flTimeLimit != 0 && gpGlobals->time >= flTimeLimit )
	{
		GoToIntermission();
		return;
	}

	if( flFragLimit )
	{
		int bestfrags = 9999;
		int remain;

		// check if any player is over the frag limit
		for( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBaseEntity *pPlayer = UTIL_PlayerByIndex( i );

			if( pPlayer && pPlayer->pev->frags >= flFragLimit )
			{
				GoToIntermission();
				return;
			}

			if( pPlayer )
			{
				remain = (int)( flFragLimit - pPlayer->pev->frags );
				if( remain < bestfrags )
				{
					bestfrags = remain;
				}
			}
		}
		frags_remaining = bestfrags;
	}

	// Updates when frags change
	if( frags_remaining != last_frags )
	{
		g_engfuncs.pfnCvar_DirectSet( &fragsleft, UTIL_VarArgs( "%i", frags_remaining ) );
	}

	// Updates once per second
	if( timeleft.value != last_time )
	{
		g_engfuncs.pfnCvar_DirectSet( &timeleft, UTIL_VarArgs( "%i", time_remaining ) );
	}

	last_frags = frags_remaining;
	last_time = time_remaining;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::IsMultiplayer( void )
{
	return TRUE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::IsDeathmatch( void )
{
	return TRUE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::IsCoOp( void )
{
	return gpGlobals->coop ? TRUE : FALSE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::FShouldSwitchWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if( !pWeapon->CanDeploy() )
	{
		// that weapon can't deploy anyway.
		return FALSE;
	}

	if( !pPlayer->m_pActiveItem )
	{
		// player doesn't have an active item!
		return TRUE;
	}

	if( !pPlayer->m_iAutoWepSwitch )
	{
		return FALSE;
	}

	if( pPlayer->m_iAutoWepSwitch == 2
	    && pPlayer->m_afButtonLast & ( IN_ATTACK | IN_ATTACK2 ) )
	{
		return FALSE;
	}

	if( !pPlayer->m_pActiveItem->CanHolster() )
	{
		// can't put away the active item.
		return FALSE;
	}

	if( pWeapon->iWeight() > pPlayer->m_pActiveItem->iWeight() )
	{
		return TRUE;
	}

	return FALSE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::GetNextBestWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pCurrentWeapon )
{
	return HLGetNextBestWeapon( pPlayer, pCurrentWeapon );
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::ClientConnected( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[128] )
{
#if !NO_VOICEGAMEMGR
	g_VoiceGameMgr.ClientConnected( pEntity );
#endif
	return TRUE;
}

extern int gmsgSayText;
extern int gmsgGameMode;

void CHalfLifeMultiplay::UpdateGameMode( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgGameMode, NULL, pPlayer->edict() );
		WRITE_BYTE( 0 );  // game mode none
	MESSAGE_END();
}

void CHalfLifeMultiplay::InitHUD( CBasePlayer *pl )
{
	// notify other clients of player joining the game
	UTIL_ClientPrintAll( HUD_PRINTNOTIFY, UTIL_VarArgs( "%s has joined the game\n", 
		( pl->pev->netname && ( STRING( pl->pev->netname ) )[0] != 0 ) ? STRING( pl->pev->netname ) : "unconnected" ) );

	// team match?
	if( g_teamplay )
	{
		UTIL_LogPrintf( "\"%s<%i><%s><%s>\" entered the game\n",  
			STRING( pl->pev->netname ), 
			GETPLAYERUSERID( pl->edict() ),
			GETPLAYERAUTHID( pl->edict() ),
			g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( pl->edict() ), "model" ) );
	}
	else
	{
		UTIL_LogPrintf( "\"%s<%i><%s><%i>\" entered the game\n",  
			STRING( pl->pev->netname ), 
			GETPLAYERUSERID( pl->edict() ),
			GETPLAYERAUTHID( pl->edict() ),
			GETPLAYERUSERID( pl->edict() ) );
	}

	UpdateGameMode( pl );

	// sending just one score makes the hud scoreboard active;  otherwise
	// it is just disabled for single play
	//fix a bug in the information about the player's score when he left the server, so that his score would not be transferred to another player(seems to work)
	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( ENTINDEX(pl->edict()) );
		WRITE_SHORT( (int)pl->pev->frags );
		WRITE_SHORT( pl->m_iDeaths );
		WRITE_SHORT( 0 );
		WRITE_SHORT( GetTeamIndex( pl->m_szTeamName ) + 1 );
	MESSAGE_END();

	SendMOTDToClient( pl->edict() );

	// loop through all active players and send their score info to the new client
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		// FIXME:  Probably don't need to cast this just to read m_iDeaths
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );

		if( plr )
		{
			MESSAGE_BEGIN( MSG_ONE, gmsgScoreInfo, NULL, pl->edict() );
				WRITE_BYTE( i );	// client number
				WRITE_SHORT( (int)plr->pev->frags );
				WRITE_SHORT( plr->m_iDeaths );
				WRITE_SHORT( 0 );
				WRITE_SHORT( GetTeamIndex( plr->m_szTeamName ) + 1 );
			MESSAGE_END();
		}
	}

	if( g_fGameOver )
	{
		MESSAGE_BEGIN( MSG_ONE, SVC_INTERMISSION, NULL, pl->edict() );
		MESSAGE_END();
	}
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::ClientDisconnected( edict_t *pClient )
{
	if( pClient )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( pClient );

		if( pPlayer )
		{
			FireTargets( "game_playerleave", pPlayer, pPlayer, USE_TOGGLE, 0 );

			// team match?
			if( g_teamplay )
			{
				UTIL_LogPrintf( "\"%s<%i><%s><%s>\" disconnected\n",  
					STRING( pPlayer->pev->netname ), 
					GETPLAYERUSERID( pPlayer->edict() ),
					GETPLAYERAUTHID( pPlayer->edict() ),
					g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model" ) );
			}
			else
			{
				UTIL_LogPrintf( "\"%s<%i><%s><%i>\" disconnected\n",  
					STRING( pPlayer->pev->netname ), 
					GETPLAYERUSERID( pPlayer->edict() ),
					GETPLAYERAUTHID( pPlayer->edict() ),
					GETPLAYERUSERID( pPlayer->edict() ) );
			}

			pPlayer->RemoveAllItems( TRUE );// destroy all of the players weapons and items
		}
	}
}

//=========================================================
//=========================================================
float CHalfLifeMultiplay::FlPlayerFallDamage( CBasePlayer *pPlayer )
{
	int iFallDamage = (int)falldamage.value;

	switch( iFallDamage )
	{
	case 1:
		//progressive
		pPlayer->m_flFallVelocity -= PLAYER_MAX_SAFE_FALL_SPEED;
		return pPlayer->m_flFallVelocity * DAMAGE_FOR_FALL_SPEED;
		break;
	default:
	case 0:
		// fixed
		return 10;
		break;
	}
} 

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::FPlayerCanTakeDamage( CBasePlayer *pPlayer, CBaseEntity *pAttacker )
{
	return TRUE;
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::PlayerThink( CBasePlayer *pPlayer )
{
	if( g_fGameOver )
	{
		// check for button presses
		if( pPlayer->m_afButtonPressed & ( IN_DUCK | IN_ATTACK | IN_ATTACK2 | IN_USE | IN_JUMP ) )
			m_iEndIntermissionButtonHit = TRUE;

		// clear attack/use commands from player
		pPlayer->m_afButtonPressed = 0;
		pPlayer->pev->button = 0;
		pPlayer->m_afButtonReleased = 0;
	}
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::PlayerSpawn( CBasePlayer *pPlayer )
{
	BOOL		addDefault;
	CBaseEntity	*pWeaponEntity = NULL;
	int 		iOldAutoWepSwitch;

	iOldAutoWepSwitch = pPlayer->m_iAutoWepSwitch;

	pPlayer->m_iAutoWepSwitch = 1;
	pPlayer->pev->weapons |= ( 1 << WEAPON_SUIT );

	addDefault = TRUE;

	while( ( pWeaponEntity = UTIL_FindEntityByClassname( pWeaponEntity, "game_player_equip" ) ) )
	{
		pWeaponEntity->Touch( pPlayer );
		addDefault = FALSE;
	}

	if( addDefault )
	{
		pPlayer->GiveNamedItem( "weapon_crowbar" );
		pPlayer->GiveNamedItem( "weapon_9mmhandgun" );
		pPlayer->GiveAmmo( 68, "9mm", _9MM_MAX_CARRY );// 4 full reloads
	}

	pPlayer->m_iAutoWepSwitch = iOldAutoWepSwitch;
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	return TRUE;
}

//=========================================================
//=========================================================
float CHalfLifeMultiplay::FlPlayerSpawnTime( CBasePlayer *pPlayer )
{
	return gpGlobals->time;//now!
}

BOOL CHalfLifeMultiplay::AllowAutoTargetCrosshair( void )
{
	return ( aimcrosshair.value != 0 );
}

//=========================================================
// IPointsForKill - how many points awarded to anyone
// that kills this player?
//=========================================================
int CHalfLifeMultiplay::IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled )
{
	return 1;
}

//=========================================================
// PlayerKilled - someone/something killed this player
//=========================================================
void CHalfLifeMultiplay::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	CBasePlayer *peKiller = NULL;
	CBaseEntity *ktmp = CBaseEntity::Instance( pKiller );
	if( ktmp && (ktmp->Classify() == CLASS_PLAYER ) )
		peKiller = (CBasePlayer*)ktmp;
	else if( ktmp && ktmp->Classify() == CLASS_VEHICLE )
	{
		CBasePlayer *pDriver = (CBasePlayer *)( (CFuncVehicle *)ktmp )->m_pDriver;

		if( pDriver != NULL )
		{
			pKiller = pDriver->pev;
			peKiller = (CBasePlayer *)pDriver;
		}
	}

	DeathNotice( pVictim, pKiller, pInflictor );

	pVictim->m_iDeaths += 1;

	FireTargets( "game_playerdie", pVictim, pVictim, USE_TOGGLE, 0 );

	if( pVictim->pev == pKiller )
	{
		// killed self
		pKiller->frags -= 1;
	}
	else if( ktmp && ktmp->IsPlayer() )
	{
		// if a player dies in a deathmatch game and the killer is a client, award the killer some points
		pKiller->frags += IPointsForKill( peKiller, pVictim );

		FireTargets( "game_playerkill", ktmp, ktmp, USE_TOGGLE, 0 );
	}
	else
	{
		// killed by the world
		pKiller->frags -= 1;
	}

	// update the scores
	// killed scores
	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( ENTINDEX(pVictim->edict()) );
		WRITE_SHORT( (int)pVictim->pev->frags );
		WRITE_SHORT( pVictim->m_iDeaths );
		WRITE_SHORT( 0 );
		WRITE_SHORT( GetTeamIndex( pVictim->m_szTeamName ) + 1 );
	MESSAGE_END();

	// killers score, if it's a player
	CBaseEntity *ep = CBaseEntity::Instance( pKiller );
	if( ep && ep->Classify() == CLASS_PLAYER )
	{
		CBasePlayer *PK = (CBasePlayer*)ep;

		MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
			WRITE_BYTE( ENTINDEX( PK->edict() ) );
			WRITE_SHORT( (int)PK->pev->frags );
			WRITE_SHORT( PK->m_iDeaths );
			WRITE_SHORT( 0 );
			WRITE_SHORT( GetTeamIndex( PK->m_szTeamName ) + 1 );
		MESSAGE_END();

		// let the killer paint another decal as soon as he'd like.
		PK->m_flNextDecalTime = gpGlobals->time;
	}
}

//=========================================================
// Deathnotice. 
//=========================================================
void CHalfLifeMultiplay::DeathNotice( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pevInflictor )
{
	// Work out what killed the player, and send a message to all clients about it
	CBaseEntity::Instance( pKiller );

	const char *killer_weapon_name = "world";		// by default, the player is killed by the world
	int killer_index = 0;

	// Hack to fix name change
	const char *tau = "tau_cannon";
	const char *gluon = "gluon gun";

	if( pevInflictor )
	{
		if( pKiller->flags & FL_CLIENT )
		{
			killer_index = ENTINDEX( ENT( pKiller ) );

			if( pevInflictor == pKiller )
			{
				// If the inflictor is the killer,  then it must be their current weapon doing the damage
				CBasePlayer *pPlayer = (CBasePlayer*)CBaseEntity::Instance( pKiller );

				if( pPlayer->m_pActiveItem )
				{
					killer_weapon_name = pPlayer->m_pActiveItem->pszName();
				}
			}
			else
			{
				killer_weapon_name = STRING( pevInflictor->classname );  // it's just that easy
			}
		}
		else
		{
			killer_weapon_name = STRING( pevInflictor->classname );
		}
	}

	// strip the monster_* or weapon_* from the inflictor's classname
	if( strncmp( killer_weapon_name, "weapon_", 7 ) == 0 )
		killer_weapon_name += 7;
	else if( strncmp( killer_weapon_name, "monster_", 8 ) == 0 )
		killer_weapon_name += 8;
	else if( strncmp( killer_weapon_name, "func_", 5 ) == 0 )
		killer_weapon_name += 5;

	MESSAGE_BEGIN( MSG_ALL, gmsgDeathMsg );
		WRITE_BYTE( killer_index );						// the killer
		WRITE_BYTE( ENTINDEX( pVictim->edict() ) );		// the victim
		WRITE_STRING( killer_weapon_name );		// what they were killed by (should this be a string?)
	MESSAGE_END();

	// replace the code names with the 'real' names
	if( !strcmp( killer_weapon_name, "egon" ) )
		killer_weapon_name = gluon;
	else if( !strcmp( killer_weapon_name, "gauss" ) )
		killer_weapon_name = tau;

	if( pVictim->pev == pKiller )  
	{
		// killed self

		// team match?
		if( g_teamplay )
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%s>\" committed suicide with \"%s\"\n",  
				STRING( pVictim->pev->netname ), 
				GETPLAYERUSERID( pVictim->edict() ),
				GETPLAYERAUTHID( pVictim->edict() ),
				g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( pVictim->edict() ), "model" ),
				killer_weapon_name );		
		}
		else
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%i>\" committed suicide with \"%s\"\n",  
				STRING( pVictim->pev->netname ), 
				GETPLAYERUSERID( pVictim->edict() ),
				GETPLAYERAUTHID( pVictim->edict() ),
				GETPLAYERUSERID( pVictim->edict() ),
				killer_weapon_name );		
		}
	}
	else if( pKiller->flags & FL_CLIENT )
	{
		// team match?
		if( g_teamplay )
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%s>\" killed \"%s<%i><%s><%s>\" with \"%s\"\n",  
				STRING( pKiller->netname ),
				GETPLAYERUSERID( ENT(pKiller) ),
				GETPLAYERAUTHID( ENT(pKiller) ),
				g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( ENT(pKiller) ), "model" ),
				STRING( pVictim->pev->netname ),
				GETPLAYERUSERID( pVictim->edict() ),
				GETPLAYERAUTHID( pVictim->edict() ),
				g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( pVictim->edict() ), "model" ),
				killer_weapon_name );
		}
		else
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%i>\" killed \"%s<%i><%s><%i>\" with \"%s\"\n",  
				STRING( pKiller->netname ),
				GETPLAYERUSERID( ENT(pKiller) ),
				GETPLAYERAUTHID( ENT(pKiller) ),
				GETPLAYERUSERID( ENT(pKiller) ),
				STRING( pVictim->pev->netname ),
				GETPLAYERUSERID( pVictim->edict() ),
				GETPLAYERAUTHID( pVictim->edict() ),
				GETPLAYERUSERID( pVictim->edict() ),
				killer_weapon_name );
		}
	}
	else
	{ 
		// killed by the world

		// team match?
		if( g_teamplay )
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%s>\" committed suicide with \"%s\" (world)\n",
				STRING( pVictim->pev->netname ), 
				GETPLAYERUSERID( pVictim->edict() ), 
				GETPLAYERAUTHID( pVictim->edict() ),
				g_engfuncs.pfnInfoKeyValue( g_engfuncs.pfnGetInfoKeyBuffer( pVictim->edict() ), "model" ),
				killer_weapon_name );		
		}
		else
		{
			UTIL_LogPrintf( "\"%s<%i><%s><%i>\" committed suicide with \"%s\" (world)\n",
				STRING( pVictim->pev->netname ), 
				GETPLAYERUSERID( pVictim->edict() ), 
				GETPLAYERAUTHID( pVictim->edict() ),
				GETPLAYERUSERID( pVictim->edict() ),
				killer_weapon_name );		
		}
	}

	MESSAGE_BEGIN( MSG_SPEC, SVC_DIRECTOR );
		WRITE_BYTE( 9 );	// command length in bytes
		WRITE_BYTE( DRC_CMD_EVENT );	// player killed
		WRITE_SHORT( ENTINDEX( pVictim->edict() ) );	// index number of primary entity
		if( pevInflictor )
			WRITE_SHORT( ENTINDEX( ENT( pevInflictor ) ) );	// index number of secondary entity
		else
			WRITE_SHORT( ENTINDEX( ENT( pKiller ) ) );	// index number of secondary entity
		WRITE_LONG( 7 | DRC_FLAG_DRAMATIC );   // eventflags (priority and flags)
	MESSAGE_END();

//  Print a standard message
	// TODO: make this go direct to console
	return; // just remove for now
/*
	char szText[128];

	if( pKiller->flags & FL_MONSTER )
	{
		// killed by a monster
		strcpy( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, " was killed by a monster.\n" );
		return;
	}

	if( pKiller == pVictim->pev )
	{
		strcpy( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, " commited suicide.\n" );
	}
	else if( pKiller->flags & FL_CLIENT )
	{
		strcpy( szText, STRING( pKiller->netname ) );

		strcat( szText, " : " );
		strcat( szText, killer_weapon_name );
		strcat( szText, " : " );

		strcat( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, "\n" );
	}
	else if( FClassnameIs( pKiller, "worldspawn" ) )
	{
		strcpy( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, " fell or drowned or something.\n" );
	}
	else if( pKiller->solid == SOLID_BSP )
	{
		strcpy( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, " was mooshed.\n" );
	}
	else
	{
		strcpy( szText, STRING( pVictim->pev->netname ) );
		strcat( szText, " died mysteriously.\n" );
	}

	UTIL_ClientPrintAll( szText );
*/
}

//=========================================================
// PlayerGotWeapon - player has grabbed a weapon that was
// sitting in the world
//=========================================================
void CHalfLifeMultiplay::PlayerGotWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
}

//=========================================================
// FlWeaponRespawnTime - what is the time in the future
// at which this weapon may spawn?
//=========================================================
float CHalfLifeMultiplay::FlWeaponRespawnTime( CBasePlayerItem *pWeapon )
{
	if( weaponstay.value > 0 )
	{
		// make sure it's only certain weapons
		if( !(pWeapon->iFlags() & ITEM_FLAG_LIMITINWORLD ) )
		{
			return gpGlobals->time + 0;		// weapon respawns almost instantly
		}
	}

	return gpGlobals->time + WEAPON_RESPAWN_TIME;
}

// when we are within this close to running out of entities,  items 
// marked with the ITEM_FLAG_LIMITINWORLD will delay their respawn
#define ENTITY_INTOLERANCE	100

//=========================================================
// FlWeaponRespawnTime - Returns 0 if the weapon can respawn 
// now,  otherwise it returns the time at which it can try
// to spawn again.
//=========================================================
float CHalfLifeMultiplay::FlWeaponTryRespawn( CBasePlayerItem *pWeapon )
{
	if( pWeapon && pWeapon->m_iId && ( pWeapon->iFlags() & ITEM_FLAG_LIMITINWORLD ) )
	{
		if( NUMBER_OF_ENTITIES() < ( gpGlobals->maxEntities - ENTITY_INTOLERANCE ) )
			return 0;

		// we're past the entity tolerance level,  so delay the respawn
		return FlWeaponRespawnTime( pWeapon );
	}

	return 0;
}

//=========================================================
// VecWeaponRespawnSpot - where should this weapon spawn?
// Some game variations may choose to randomize spawn locations
//=========================================================
Vector CHalfLifeMultiplay::VecWeaponRespawnSpot( CBasePlayerItem *pWeapon )
{
	return pWeapon->pev->origin;
}

//=========================================================
// WeaponShouldRespawn - any conditions inhibiting the
// respawning of this weapon?
//=========================================================
int CHalfLifeMultiplay::WeaponShouldRespawn( CBasePlayerItem *pWeapon )
{
	if( pWeapon->pev->spawnflags & SF_NORESPAWN )
	{
		return GR_WEAPON_RESPAWN_NO;
	}

	return GR_WEAPON_RESPAWN_YES;
}

//=========================================================
// CanHaveWeapon - returns FALSE if the player is not allowed
// to pick up this weapon
//=========================================================
BOOL CHalfLifeMultiplay::CanHavePlayerItem( CBasePlayer *pPlayer, CBasePlayerItem *pItem )
{
	if( weaponstay.value > 0 )
	{
		if( pItem->iFlags() & ITEM_FLAG_LIMITINWORLD )
			return CGameRules::CanHavePlayerItem( pPlayer, pItem );

		// check if the player already has this weapon
		for( int i = 0; i < MAX_ITEM_TYPES; i++ )
		{
			CBasePlayerItem *it = pPlayer->m_rgpPlayerItems[i];

			while( it != NULL )
			{
				if( it->m_iId == pItem->m_iId )
				{
					return FALSE;
				}

				it = it->m_pNext;
			}
		}
	}

	return CGameRules::CanHavePlayerItem( pPlayer, pItem );
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::CanHaveItem( CBasePlayer *pPlayer, CItem *pItem )
{
	return TRUE;
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::PlayerGotItem( CBasePlayer *pPlayer, CItem *pItem )
{
}

//=========================================================
//=========================================================
int CHalfLifeMultiplay::ItemShouldRespawn( CItem *pItem )
{
	if( pItem->pev->spawnflags & SF_NORESPAWN )
	{
		return GR_ITEM_RESPAWN_NO;
	}

	return GR_ITEM_RESPAWN_YES;
}

//=========================================================
// At what time in the future may this Item respawn?
//=========================================================
float CHalfLifeMultiplay::FlItemRespawnTime( CItem *pItem )
{
	return gpGlobals->time + ITEM_RESPAWN_TIME;
}

//=========================================================
// Where should this item respawn?
// Some game variations may choose to randomize spawn locations
//=========================================================
Vector CHalfLifeMultiplay::VecItemRespawnSpot( CItem *pItem )
{
	return pItem->pev->origin;
}

//=========================================================
//=========================================================
void CHalfLifeMultiplay::PlayerGotAmmo( CBasePlayer *pPlayer, char *szName, int iCount )
{
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::IsAllowedToSpawn( CBaseEntity *pEntity )
{
//	if( pEntity->pev->flags & FL_MONSTER )
//		return FALSE;

	return TRUE;
}

//=========================================================
//=========================================================
int CHalfLifeMultiplay::AmmoShouldRespawn( CBasePlayerAmmo *pAmmo )
{
	if( pAmmo->pev->spawnflags & SF_NORESPAWN )
	{
		return GR_AMMO_RESPAWN_NO;
	}

	return GR_AMMO_RESPAWN_YES;
}

//=========================================================
//=========================================================
float CHalfLifeMultiplay::FlAmmoRespawnTime( CBasePlayerAmmo *pAmmo )
{
	return gpGlobals->time + AMMO_RESPAWN_TIME;
}

//=========================================================
//=========================================================
Vector CHalfLifeMultiplay::VecAmmoRespawnSpot( CBasePlayerAmmo *pAmmo )
{
	return pAmmo->pev->origin;
}

//=========================================================
//=========================================================
float CHalfLifeMultiplay::FlHealthChargerRechargeTime( void )
{
	return 60;
}

float CHalfLifeMultiplay::FlHEVChargerRechargeTime( void )
{
	return 30;
}

//=========================================================
//=========================================================
int CHalfLifeMultiplay::DeadPlayerWeapons( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_GUN_ACTIVE;
}

//=========================================================
//=========================================================
int CHalfLifeMultiplay::DeadPlayerAmmo( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_AMMO_ACTIVE;
}

edict_t *CHalfLifeMultiplay::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	edict_t *pentSpawnSpot = CGameRules::GetPlayerSpawnSpot( pPlayer );	
	if( IsMultiplayer() && pentSpawnSpot->v.target )
	{
		FireTargets( STRING( pentSpawnSpot->v.target ), pPlayer, pPlayer, USE_TOGGLE, 0 );
	}

	return pentSpawnSpot;
}

//=========================================================
//=========================================================
int CHalfLifeMultiplay::PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget )
{
	// half life deathmatch has only enemies
	return GR_NOTTEAMMATE;
}

BOOL CHalfLifeMultiplay::PlayFootstepSounds( CBasePlayer *pl, float fvol )
{
	if( g_footsteps && g_footsteps->value == 0 )
		return FALSE;

	if( pl->IsOnLadder() || pl->pev->velocity.Length2D() > 220 )
		return TRUE;  // only make step sounds in multiplayer if the player is moving fast enough

	return FALSE;
}

BOOL CHalfLifeMultiplay::FAllowFlashlight( void )
{
	return flashlight.value != 0; 
}

//=========================================================
//=========================================================
BOOL CHalfLifeMultiplay::FAllowMonsters( void )
{
	return ( allowmonsters.value != 0 );
}

//=========================================================
//======== CHalfLifeMultiplay private functions ===========
#define INTERMISSION_TIME		6

void CHalfLifeMultiplay::GoToIntermission( void )
{
	if( g_fGameOver )
		return;  // intermission has already been triggered, so ignore.

	MESSAGE_BEGIN( MSG_ALL, SVC_INTERMISSION );
	MESSAGE_END();

	// bounds check
	int time = (int)CVAR_GET_FLOAT( "mp_chattime" );
	if( time < 1 )
		CVAR_SET_STRING( "mp_chattime", "1" );
	else if( time > MAX_INTERMISSION_TIME )
		CVAR_SET_STRING( "mp_chattime", UTIL_dtos1( MAX_INTERMISSION_TIME ) );

	m_flIntermissionEndTime = gpGlobals->time + ( (int)mp_chattime.value );
	g_flIntermissionStartTime = gpGlobals->time;

	g_fGameOver = TRUE;
	m_iEndIntermissionButtonHit = FALSE;
}

#define MAX_RULE_BUFFER 1024

typedef struct mapcycle_item_s
{
	struct mapcycle_item_s *next;

	char mapname[32];
	int minplayers, maxplayers;
	char rulebuffer[MAX_RULE_BUFFER];
} mapcycle_item_t;

typedef struct mapcycle_s
{
	struct mapcycle_item_s *items;
	struct mapcycle_item_s *next_item;
} mapcycle_t;

/*
==============
DestroyMapCycle

Clean up memory used by mapcycle when switching it
==============
*/
void DestroyMapCycle( mapcycle_t *cycle )
{
	mapcycle_item_t *p, *n, *start;
	p = cycle->items;
	if( p )
	{
		start = p;
		p = p->next;
		while( p != start )
		{
			n = p->next;
			delete p;
			p = n;
		}
		
		delete cycle->items;
	}
	cycle->items = NULL;
	cycle->next_item = NULL;
}

static char com_token[1500];

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char *COM_Parse( const char *data )
{
	int c;
	int len;

	len = 0;
	com_token[0] = 0;

	if( !data )
		return NULL;

// skip whitespace
skipwhite:
	while( ( c = *data ) <= ' ')
	{
		if( c == 0 )
			return NULL;                    // end of file;
		data++;
	}

	// skip // comments
	if( c=='/' && data[1] == '/' )
	{
		while( *data && *data != '\n' )
			data++;
		goto skipwhite;
	}

	// handle quoted strings specially
	if( c == '\"' )
	{
		data++;
		while( 1 )
		{
			c = *data++;
			if( c=='\"' || !c )
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

	// parse single characters
	if( c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c == ',' )
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
	if( c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c == ',' )
			break;
	} while ( c > 32 );

	com_token[len] = 0;
	return data;
}

/*
==============
COM_TokenWaiting

Returns 1 if additional data is waiting to be processed on this line
==============
*/
int COM_TokenWaiting( const char *buffer )
{
	const char *p;

	p = buffer;
	while( *p && *p!='\n')
	{
		if( !isspace( *p ) || isalnum( *p ) )
			return 1;

		p++;
	}

	return 0;
}

/*
==============
ReloadMapCycleFile

Parses mapcycle.txt file into mapcycle_t structure
==============
*/
int ReloadMapCycleFile( const char *filename, mapcycle_t *cycle )
{
	char szMap[32];
	int length;
	const char *pFileList;
	const char *aFileList = pFileList = (const char *)LOAD_FILE_FOR_ME( filename, &length );
	int hasbuffer;
	mapcycle_item_s *item, *newlist = NULL, *next;

	if( pFileList && length )
	{
		// the first map name in the file becomes the default
		while( 1 )
		{
			char szBuffer[MAX_RULE_BUFFER] = {0};
			hasbuffer = 0;

			pFileList = COM_Parse( pFileList );

			if( com_token[0] == '\0' )
				break;

			strlcpy( szMap, com_token, sizeof( szMap ));

			// Any more tokens on this line?
			if( COM_TokenWaiting( pFileList ) )
			{
				pFileList = COM_Parse( pFileList );

				if( com_token[0] != '\0' )
				{
					hasbuffer = 1;
					strlcpy( szBuffer, com_token, sizeof( szBuffer ));
				}
			}

			// Check map
			if( IS_MAP_VALID( szMap ) )
			{
				// Create entry
				char *s;

				item = new mapcycle_item_s;

				strcpy( item->mapname, szMap );

				item->minplayers = 0;
				item->maxplayers = 0;

				memset( item->rulebuffer, 0, MAX_RULE_BUFFER );

				if( hasbuffer )
				{
					s = g_engfuncs.pfnInfoKeyValue( szBuffer, "minplayers" );
					if( s && s[0] )
					{
						item->minplayers = atoi( s );
						item->minplayers = Q_max( item->minplayers, 0 );
						item->minplayers = Q_min( item->minplayers, gpGlobals->maxClients );
					}
					s = g_engfuncs.pfnInfoKeyValue( szBuffer, "maxplayers" );
					if( s && s[0] )
					{
						item->maxplayers = atoi( s );
						item->maxplayers = Q_max( item->maxplayers, 0 );
						item->maxplayers = Q_min( item->maxplayers, gpGlobals->maxClients );
					}

					// Remove keys
					//
					g_engfuncs.pfnInfo_RemoveKey( szBuffer, "minplayers" );
					g_engfuncs.pfnInfo_RemoveKey( szBuffer, "maxplayers" );

					strcpy( item->rulebuffer, szBuffer );
				}

				item->next = cycle->items;
				cycle->items = item;
			}
			else
			{
				ALERT( at_console, "Skipping %s from mapcycle, not a valid map\n", szMap );
			}
		}

		FREE_FILE( (void*)aFileList );
	}

	// Fixup circular list pointer
	item = cycle->items;

	// Reverse it to get original order
	while( item )
	{
		next = item->next;
		item->next = newlist;
		newlist = item;
		item = next;
	}
	cycle->items = newlist;
	item = cycle->items;

	// Didn't parse anything
	if( !item )
	{
		return 0;
	}

	while( item->next )
	{
		item = item->next;
	}
	item->next = cycle->items;

	cycle->next_item = item->next;

	return 1;
}

/*
==============
CountPlayers

Determine the current # of active players on the server for map cycling logic
==============
*/
int CountPlayers( void )
{
	int num = 0;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );

		if( pEnt )
		{
			num = num + 1;
		}
	}

	return num;
}

/*
==============
ExtractCommandString

Parse commands/key value pairs to issue right after map xxx command is issued on server
 level transition
==============
*/
void ExtractCommandString( char *s, char *szCommand )
{
	// Now make rules happen
	char pkey[512];
	char value[512];	// use two buffers so compares
				// work without stomping on each other
	char *o;
	
	if( *s == '\\' )
		s++;

	while( 1 )
	{
		o = pkey;
		while( *s != '\\' )
		{
			if ( !*s )
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;

		while( *s != '\\' && *s )
		{
			if( !*s )
				return;
			*o++ = *s++;
		}
		*o = 0;

		strcat( szCommand, pkey );

		if( value[0] != '\0' )
		{
			strcat( szCommand, " " );
			strcat( szCommand, value );
		}
		strcat( szCommand, "\n" );

		if( !*s )
			return;
		s++;
	}
}

/*
==============
ChangeLevel

Server is changing to a new level, check mapcycle.txt for map name and setup info
==============
*/
void CHalfLifeMultiplay::ChangeLevel( void )
{
	static char szPreviousMapCycleFile[256];
	static mapcycle_t mapcycle;

	char szNextMap[32];
	char szFirstMapInList[32];
	char szCommands[1500];
	char szRules[1500];
	int minplayers = 0, maxplayers = 0;
	strcpy( szFirstMapInList, "hldm1" );  // the absolute default level is hldm1

	int curplayers;
	BOOL do_cycle = TRUE;

	// find the map to change to
	const char *mapcfile = CVAR_GET_STRING( "mapcyclefile" );
	ASSERT( mapcfile != NULL );

	szCommands[0] = '\0';
	szRules[0] = '\0';

	curplayers = CountPlayers();

	// Has the map cycle filename changed?
	if( stricmp( mapcfile, szPreviousMapCycleFile ) )
	{
		strcpy( szPreviousMapCycleFile, mapcfile );

		DestroyMapCycle( &mapcycle );

		if( !ReloadMapCycleFile( mapcfile, &mapcycle ) || ( !mapcycle.items ) )
		{
			ALERT( at_console, "Unable to load map cycle file %s\n", mapcfile );
			do_cycle = FALSE;
		}
	}

	if( do_cycle && mapcycle.items )
	{
		BOOL keeplooking = FALSE;
		BOOL found = FALSE;
		mapcycle_item_s *item;

		// Assume current map
		strcpy( szNextMap, STRING( gpGlobals->mapname ) );
		strcpy( szFirstMapInList, STRING( gpGlobals->mapname ) );

		// Traverse list
		for( item = mapcycle.next_item; item->next != mapcycle.next_item; item = item->next )
		{
			keeplooking = FALSE;

			ASSERT( item != NULL );

			if( item->minplayers != 0 )
			{
				if( curplayers >= item->minplayers )
				{
					found = TRUE;
					minplayers = item->minplayers;
				}
				else
				{
					keeplooking = TRUE;
				}
			}

			if( item->maxplayers != 0 )
			{
				if( curplayers <= item->maxplayers )
				{
					found = TRUE;
					maxplayers = item->maxplayers;
				}
				else
				{
					keeplooking = TRUE;
				}
			}

			if( keeplooking )
				continue;

			found = TRUE;
			break;
		}

		if( !found )
		{
			item = mapcycle.next_item;
		}		

		// Increment next item pointer
		mapcycle.next_item = item->next;

		// Perform logic on current item
		strcpy( szNextMap, item->mapname );

		ExtractCommandString( item->rulebuffer, szCommands );
		strcpy( szRules, item->rulebuffer );
	}

	if( !IS_MAP_VALID( szNextMap ) )
	{
		strcpy( szNextMap, szFirstMapInList );
	}

	g_fGameOver = TRUE;

	ALERT( at_console, "CHANGE LEVEL: %s\n", szNextMap );
	if( minplayers || maxplayers )
	{
		ALERT( at_console, "PLAYER COUNT:  min %i max %i current %i\n", minplayers, maxplayers, curplayers );
	}

	if( szRules[0] != '\0' )
	{
		ALERT( at_console, "RULES:  %s\n", szRules );
	}

	CHANGE_LEVEL( szNextMap, NULL );

	if( szCommands[0] != '\0' )
	{
		SERVER_COMMAND( szCommands );
	}
}

#define MAX_MOTD_CHUNK	  60
#define MAX_MOTD_LENGTH   1536 // (MAX_MOTD_CHUNK * 4)

void CHalfLifeMultiplay::SendMOTDToClient( edict_t *client )
{
	// read from the MOTD.txt file
	int length, char_count = 0;
	char *pFileList;
	char *aFileList = pFileList = (char*)LOAD_FILE_FOR_ME( CVAR_GET_STRING( "motdfile" ), &length );

	// send the server name
	MESSAGE_BEGIN( MSG_ONE, gmsgServerName, NULL, client );
		WRITE_STRING( CVAR_GET_STRING( "hostname" ) );
	MESSAGE_END();

	// Send the message of the day
	// read it chunk-by-chunk,  and send it in parts

	while( pFileList && *pFileList && char_count < MAX_MOTD_LENGTH )
	{
		char chunk[MAX_MOTD_CHUNK + 1];

		size_t size = strlcpy( chunk, pFileList, MAX_MOTD_CHUNK + 1 );
		char_count += Q_min( size, MAX_MOTD_CHUNK );
		if( char_count < MAX_MOTD_LENGTH )
			pFileList = aFileList + char_count; 
		else
			*pFileList = 0;

		MESSAGE_BEGIN( MSG_ONE, gmsgMOTD, NULL, client );
			WRITE_BYTE( *pFileList ? FALSE : TRUE );	// FALSE means there is still more message to come
			WRITE_STRING( chunk );
		MESSAGE_END();
	}

	FREE_FILE( (void*)aFileList );
}

int CMultiplayBusters::WeaponShouldRespawn( CBasePlayerItem *pWeapon )
{
	if( pWeapon->m_iId == WEAPON_EGON )
		return GR_WEAPON_RESPAWN_NO;

	return CHalfLifeMultiplay::WeaponShouldRespawn( pWeapon );
}

BOOL CMultiplayBusters::CanHaveItem( CBasePlayer *pPlayer, CItem *pItem )
{
	return BustingCanHaveItem( pPlayer, pItem );
}

BOOL CMultiplayBusters::CanHavePlayerItem( CBasePlayer *pPlayer, CBasePlayerItem *pItem )
{
	if( !BustingCanHaveItem( pPlayer, pItem ))
		return FALSE;

	return CHalfLifeMultiplay::CanHavePlayerItem( pPlayer, pItem );
}

int CMultiplayBusters::IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled )
{
	if( IsPlayerBusting( pAttacker ))
		return 1;

	if( IsPlayerBusting( pKilled ))
		return 2;

	return 0;
}
void CMultiplayBusters::DeathNotice( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pevInflictor )
{
	if( IsPlayerBusting( pVictim )
	    || IsPlayerBusting( CBaseEntity::Instance( pKiller )))
		CHalfLifeMultiplay::DeathNotice( pVictim, pKiller, pevInflictor );
}

void CMultiplayBusters::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	if( IsPlayerBusting( pVictim ))
	{
		UTIL_ClientPrintAll( HUD_PRINTCENTER, "The Buster is dead!!" );

		m_flEgonBustingCheckTime = -1.0f;

		CBasePlayer *peKiller = NULL;
		CBaseEntity *ktmp = CBaseEntity::Instance( pKiller );
		if( ktmp && ( ktmp->Classify() == CLASS_PLAYER ) )
			peKiller = (CBasePlayer*)ktmp;
		else if( ktmp && ktmp->Classify() == CLASS_VEHICLE )
		{
			CBasePlayer *pDriver = (CBasePlayer *)( (CFuncVehicle *)ktmp )->m_pDriver;

			if( pDriver != NULL )
			{
				pKiller = pDriver->pev;
				peKiller = (CBasePlayer *)pDriver;
			}
		}

		if( peKiller && peKiller->IsPlayer() )
		{
			UTIL_ClientPrintAll( HUD_PRINTTALK, UTIL_VarArgs( "%s has killed the Buster!", STRING( peKiller->pev->netname )));
		}

		pVictim->pev->renderfx = 0;
		pVictim->pev->rendercolor = g_vecZero;
	}

	CHalfLifeMultiplay::PlayerKilled( pVictim, pKiller, pInflictor );
}

void CMultiplayBusters::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	SetPlayerModel( pPlayer, FALSE );
	CHalfLifeMultiplay::ClientUserInfoChanged( pPlayer, infobuffer );
}

void CMultiplayBusters::PlayerSpawn( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	SetPlayerModel( pPlayer, FALSE );
}

bool IsPlayerBusting( CBaseEntity *pPlayer )
{
	if( g_pGameRules->IsBustingGame()
	    && pPlayer && pPlayer->IsPlayer()
	    && ((CBasePlayer*)pPlayer)->HasPlayerItemFromID( WEAPON_EGON ))
		return true;

	return false;
}

BOOL BustingCanHaveItem( CBasePlayer *pPlayer, CBaseEntity *pItem )
{
	if( IsPlayerBusting( pPlayer )
	    && !( strncmp( STRING( pItem->pev->classname ), "weapon_", 7 )
	    && strncmp( STRING( pItem->pev->classname ), "ammo_", 5 )))
		return FALSE;

	return TRUE;
}

CMultiplayBusters::CMultiplayBusters()
{
	CHalfLifeMultiplay();

	m_flEgonBustingCheckTime = -1.0;
}

void CMultiplayBusters::CheckForEgons( void )
{
	CBaseEntity *pPlayer;
	CWeaponBox *pWeaponBox = NULL;
	CBasePlayerItem *pWeapon;
	CBasePlayer *pNewBuster = NULL;
	int i, bestfrags = 9999;

	if( m_flEgonBustingCheckTime <= 0.0f )
	{
		m_flEgonBustingCheckTime = gpGlobals->time + 10.0f;
		return;
	}

	if( gpGlobals->time < m_flEgonBustingCheckTime )
		return;

	m_flEgonBustingCheckTime = -1.0f;

	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		pPlayer = UTIL_PlayerByIndex( i );
		if( IsPlayerBusting( pPlayer ))
			return;
	}

	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		pPlayer = UTIL_PlayerByIndex( i );

		if( pPlayer && pPlayer->pev->frags < bestfrags )
		{
			pNewBuster = (CBasePlayer*)pPlayer;
			bestfrags = pPlayer->pev->frags;
		}
	}

	if( !pNewBuster )
		return;

	pNewBuster->GiveNamedItem( "weapon_egon" );

	while( ( pWeaponBox = (CWeaponBox*)UTIL_FindEntityByClassname( pWeaponBox, "weaponbox" )))
	{
		// destroy weaponboxes with egons
		for( i = 0; i < MAX_ITEM_TYPES; i++ )
		{
			pWeapon = pWeaponBox->m_rgpPlayerItems[i];

			while( pWeapon )
			{
				if( pWeapon->m_iId != WEAPON_EGON )
				{
					pWeapon = pWeapon->m_pNext;
					continue;
				}

				pWeaponBox->Kill();
				pWeapon = 0;
				i = MAX_ITEM_TYPES;
			}
		}
	}
}

void CMultiplayBusters::Think( void )
{
	CheckForEgons();
	CHalfLifeMultiplay::Think();
}

void CMultiplayBusters::SetPlayerModel( CBasePlayer *pPlayer, BOOL bKnownBuster )
{
	const char *pszModel = NULL;

	if( bKnownBuster || IsPlayerBusting( pPlayer ))
	{
		pszModel = "ivan";
	}
	else
	{
		pszModel = "skeleton";
	}

	g_engfuncs.pfnSetClientKeyValue( ENTINDEX( pPlayer->edict()), g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict()), "model", pszModel );
}

void CMultiplayBusters::PlayerGotWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if( pWeapon->m_iId != WEAPON_EGON )
		return;

	pPlayer->RemoveAllItems( FALSE );
	UTIL_ClientPrintAll( HUD_PRINTCENTER, "Long live the new Buster!" );
	UTIL_ClientPrintAll( HUD_PRINTTALK, UTIL_VarArgs( "%s is busting!\n", STRING( pPlayer->pev->netname )));
	SetPlayerModel( pPlayer, TRUE );
	pPlayer->pev->health = pPlayer->pev->max_health;
	pPlayer->pev->armorvalue = MAX_NORMAL_BATTERY;
	pPlayer->pev->renderfx = kRenderFxGlowShell;
	pPlayer->pev->renderamt = 25;
	pPlayer->pev->rendercolor = Vector( 0, 75, 250 );
	pPlayer->m_rgAmmo[pWeapon->PrimaryAmmoIndex()] = pPlayer->ammo_uranium = 100;
}

//*********************************************************
// Campaign co-op (split screen)
//
// The single player maps played by up to four players. The rules are the
// multiplayer ones (clients, prediction and weapons agree) with these changes:
//  - player 1 is the leader: only he changes the level (triggers.cpp)
//  - a player that dies comes back near the closest living player after a
//    few seconds; the level restarts only when everybody is dead
//  - the players regenerate (all the way in at most 5 seconds after the last
//    hit), the monsters and the other players hurt them, they hurt themselves
//  - the health, armor, weapons and ammo go from level to level and are kept
//    in the cvars coop_p1 to coop_p4; the engine writes them to its co-op save
//    file (command coop_save) and puts them back with coop_load
//  - everybody is a Freeman clone, in a different color
//*********************************************************
#define COOP_MAX		4
#define COOP_BLOB		768
#define COOP_REGEN_DELAY	1.5f	// seconds without being hit before the regeneration starts
#define COOP_REGEN_TIME		3.5f	// seconds it takes to go from 0 to 100 (delay + this <= 5)
#define COOP_RESPAWN_DELAY	3.0f	// seconds a dead player waits
#define COOP_ALLDEAD_DELAY	4.0f	// seconds before the level restarts when everybody is dead

extern void respawn( entvars_t *pev, BOOL fCopyCorpse );	// client.cpp

static char g_szCoopStart[COOP_MAX][COOP_BLOB];	// what each player had when this level began
static BOOL g_fCoopFresh[COOP_MAX];			// that state has not been given yet
static char g_szCoopDeath[COOP_MAX][COOP_BLOB];	// what a player had when he died
static BOOL g_fCoopDead[COOP_MAX];
static float g_flCoopLastAlive;
static BOOL g_fCoopRestarting;

// Freeman in four hues (HL hue scale 0 to 255): orange, blue, green, purple
static const char *g_szCoopHue[COOP_MAX] = { "20", "170", "85", "215" };

static void Coop_Copy( char *dst, const char *src, int size )
{
	strncpy( dst, src, size - 1 );
	dst[size - 1] = 0;
}

static CBasePlayer *Coop_Player( int index )
{
	CBaseEntity *pEnt = UTIL_PlayerByIndex( index );

	if( !pEnt || !pEnt->IsPlayer() )
		return NULL;

	if( !( pEnt->pev->flags & FL_CLIENT ) )
		return NULL;

	return (CBasePlayer *)pEnt;
}

void Coop_Log( const char *fmt, ... )
{
	static char lastMap[64];
	static int lines;
	char buffer[512];
	va_list args;

	if( !g_pGameRules || !g_pGameRules->IsCoOp() )
		return;

	// the cap counts per map, so a level that fires thousands of targets cannot flood the log
	if( strncmp( lastMap, STRING( gpGlobals->mapname ), sizeof( lastMap ) - 1 ) != 0 )
	{
		strncpy( lastMap, STRING( gpGlobals->mapname ), sizeof( lastMap ) - 1 );
		lastMap[sizeof( lastMap ) - 1] = '\0';
		lines = 0;
	}

	if( ++lines > 600 )
		return;

	va_start( args, fmt );
	vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );

	g_engfuncs.pfnServerPrint( buffer );
}

BOOL Coop_IsLeader( CBaseEntity *pPlayer )
{
	return pPlayer && pPlayer->edict() && ENTINDEX( pPlayer->edict() ) == 1;
}

static int Coop_Slot( CBasePlayer *pPlayer )
{
	int slot = ENTINDEX( pPlayer->edict() ) - 1;

	return ( slot >= 0 && slot < COOP_MAX ) ? slot : -1;
}

// the player looks like Freeman, in the color of his slot
static void Coop_Look( CBasePlayer *pPlayer, char *infobuffer )
{
	const int slot = Coop_Slot( pPlayer );
	const int index = ENTINDEX( pPlayer->edict() );

	if( slot < 0 || !infobuffer )
		return;

	if( !FStrEq( g_engfuncs.pfnInfoKeyValue( infobuffer, "model" ), "gordon" ) )
		g_engfuncs.pfnSetClientKeyValue( index, infobuffer, "model", "gordon" );

	if( !FStrEq( g_engfuncs.pfnInfoKeyValue( infobuffer, "topcolor" ), g_szCoopHue[slot] ) )
		g_engfuncs.pfnSetClientKeyValue( index, infobuffer, "topcolor", g_szCoopHue[slot] );

	if( !FStrEq( g_engfuncs.pfnInfoKeyValue( infobuffer, "bottomcolor" ), g_szCoopHue[slot] ) )
		g_engfuncs.pfnSetClientKeyValue( index, infobuffer, "bottomcolor", g_szCoopHue[slot] );
}

/*
Snapshot of what a player carries, as text:
  H<health> A<armor> S<suit 0/1> a<ammo index>=<count> ... w<classname>=<clip> ... c<active weapon>
*/
static void Coop_Snapshot( CBasePlayer *pPlayer, char *out, int size )
{
	int len;

	len = safe_snprintf( out, size, "H%d A%d S%d", (int)pPlayer->pev->health, (int)pPlayer->pev->armorvalue,
		( pPlayer->pev->weapons & ( 1 << WEAPON_SUIT ) ) ? 1 : 0 );

	for( int i = 1; i < MAX_AMMO_SLOTS && len < size - 24; i++ )
	{
		if( pPlayer->m_rgAmmo[i] > 0 )
			len += safe_snprintf( out + len, size - len, " a%d=%d", i, pPlayer->m_rgAmmo[i] );
	}

	for( int i = 0; i < MAX_ITEM_TYPES; i++ )
	{
		CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i];

		while( pItem && len < size - 48 )
		{
			CBasePlayerItem *pW = pItem->GetWeaponPtr();
			int clip = pW ? ( (CBasePlayerWeapon *)pW )->m_iClip : -1;

			if( pItem->pev->classname )
				len += safe_snprintf( out + len, size - len, " w%s=%d", STRING( pItem->pev->classname ), clip );

			pItem = pItem->m_pNext;
		}
	}

	if( pPlayer->m_pActiveItem && pPlayer->m_pActiveItem->pev->classname && len < size - 48 )
		safe_snprintf( out + len, size - len, " c%s", STRING( pPlayer->m_pActiveItem->pev->classname ) );
}

// puts a snapshot back (health only for the level to level carry)
static void Coop_Apply( CBasePlayer *pPlayer, const char *blob, BOOL withHealth )
{
	char buf[COOP_BLOB], active[48] = "";
	struct { char name[40]; int clip; } weapons[24];
	int ammo[MAX_AMMO_SLOTS], nweapons = 0;
	char *tok;

	if( !blob || !blob[0] )
		return;

	Coop_Copy( buf, blob, sizeof( buf ) );
	memset( ammo, 0, sizeof( ammo ) );

	for( tok = strtok( buf, " " ); tok; tok = strtok( NULL, " " ) )
	{
		switch( tok[0] )
		{
		case 'H':
			if( withHealth )
				pPlayer->pev->health = Q_max( 1, Q_min( 100, atoi( tok + 1 ) ) );
			break;
		case 'A':
			pPlayer->pev->armorvalue = Q_max( 0, Q_min( 100, atoi( tok + 1 ) ) );
			break;
		case 'S':
			if( atoi( tok + 1 ) )
				pPlayer->pev->weapons |= ( 1 << WEAPON_SUIT );
			break;
		case 'a':
		{
			int idx = atoi( tok + 1 );
			const char *eq = strchr( tok, '=' );

			if( eq && idx > 0 && idx < MAX_AMMO_SLOTS )
				ammo[idx] = atoi( eq + 1 );
			break;
		}
		case 'w':
		{
			char *eq = strchr( tok, '=' );

			if( eq && nweapons < 24 && ( eq - tok - 1 ) < (int)sizeof( weapons[0].name ) )
			{
				*eq = 0;
				Coop_Copy( weapons[nweapons].name, tok + 1, sizeof( weapons[0].name ) );
				weapons[nweapons].clip = atoi( eq + 1 );
				nweapons++;
			}
			break;
		}
		case 'c':
			Coop_Copy( active, tok + 1, sizeof( active ) );
			break;
		}
	}

	// the weapons come with their own starting ammo: the real one is put after them
	{
		const int oldswitch = pPlayer->m_iAutoWepSwitch;

		pPlayer->m_iAutoWepSwitch = 1;

		for( int i = 0; i < nweapons; i++ )
			pPlayer->GiveNamedItem( weapons[i].name );

		pPlayer->m_iAutoWepSwitch = oldswitch;
	}

	for( int i = 1; i < MAX_AMMO_SLOTS; i++ )
	{
		if( ammo[i] > 0 )
			pPlayer->m_rgAmmo[i] = ammo[i];
	}

	for( int n = 0; n < nweapons; n++ )
	{
		for( int i = 0; i < MAX_ITEM_TYPES; i++ )
		{
			for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
			{
				CBasePlayerItem *pW = pItem->GetWeaponPtr();

				if( pW && pItem->pev->classname && FStrEq( STRING( pItem->pev->classname ), weapons[n].name ) && weapons[n].clip >= 0 )
					( (CBasePlayerWeapon *)pW )->m_iClip = weapons[n].clip;
			}
		}
	}

	if( active[0] )
		pPlayer->SelectItem( active );
}

// the state each player carries is kept in the cvars so the engine can write it to the co-op save
static void Coop_StoreCvar( int slot, const char *blob )
{
	char name[16];

	safe_snprintf( name, sizeof( name ), "coop_p%d", slot + 1 );
	CVAR_SET_STRING( name, blob ? blob : "" );
}

static void Coop_LoadCvars( void )
{
	for( int i = 0; i < COOP_MAX; i++ )
	{
		char name[16];
		const char *value;

		safe_snprintf( name, sizeof( name ), "coop_p%d", i + 1 );
		value = CVAR_GET_STRING( name );

		g_szCoopStart[i][0] = 0;
		g_fCoopFresh[i] = FALSE;
		g_fCoopDead[i] = FALSE;

		if( value && value[0] )
		{
			Coop_Copy( g_szCoopStart[i], value, sizeof( g_szCoopStart[i] ) );
			g_fCoopFresh[i] = TRUE;
		}
	}
}

// called from trigger_changelevel: the players' state for the next level, and the co-op save
void Coop_BeforeChangeLevel( const char *nextmap )
{
	char blob[COOP_BLOB];

	if( !g_pGameRules || !g_pGameRules->IsCoOp() )
		return;

	for( int i = 0; i < COOP_MAX; i++ )
	{
		CBasePlayer *pPlayer = Coop_Player( i + 1 );

		if( pPlayer && pPlayer->IsAlive() )
		{
			Coop_Snapshot( pPlayer, blob, sizeof( blob ) );
			Coop_StoreCvar( i, blob );
		}
		else if( pPlayer )
		{
			// dead at the time: he goes on with what he began the level with
			Coop_StoreCvar( i, g_szCoopStart[i] );
		}
		else Coop_StoreCvar( i, "" );
	}

	CVAR_SET_STRING( "coop_map", nextmap ? nextmap : "" );
	SERVER_COMMAND( "coop_save\n" );
	SERVER_EXECUTE();
}

CHalfLifeCoop::CHalfLifeCoop() : CHalfLifeMultiplay()
{
	g_fCoopRestarting = FALSE;
	g_flCoopLastAlive = gpGlobals->time;
	Coop_LoadCvars();

	// the base class loaded the deathmatch skill values
	RefreshSkillData();
}

void CHalfLifeCoop::RefreshSkillData( void )
{
	// the single player values
	CGameRules::RefreshSkillData();
}

void CHalfLifeCoop::InitHUD( CBasePlayer *pl )
{
	// like the multiplayer one, without the message of the day
	UTIL_ClientPrintAll( HUD_PRINTNOTIFY, UTIL_VarArgs( "%s has joined the game\n",
		( pl->pev->netname && ( STRING( pl->pev->netname ) )[0] != 0 ) ? STRING( pl->pev->netname ) : "unconnected" ) );

	UpdateGameMode( pl );

	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( ENTINDEX( pl->edict() ) );
		WRITE_SHORT( (int)pl->pev->frags );
		WRITE_SHORT( pl->m_iDeaths );
		WRITE_SHORT( 0 );
		WRITE_SHORT( 0 );
	MESSAGE_END();

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );

		if( plr )
		{
			MESSAGE_BEGIN( MSG_ONE, gmsgScoreInfo, NULL, pl->edict() );
				WRITE_BYTE( i );
				WRITE_SHORT( (int)plr->pev->frags );
				WRITE_SHORT( plr->m_iDeaths );
				WRITE_SHORT( 0 );
				WRITE_SHORT( 0 );
			MESSAGE_END();
		}
	}
}

void CHalfLifeCoop::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	CGameRules::ClientUserInfoChanged( pPlayer, infobuffer );
	Coop_Look( pPlayer, infobuffer );
}

BOOL CHalfLifeCoop::ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target )
{
	CBaseEntity *pTarget = CBaseEntity::Instance( target );

	// no autoaim on a partner
	if( pTarget && pTarget->IsPlayer() )
		return FALSE;

	return TRUE;
}

int CHalfLifeCoop::PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget )
{
	if( pPlayer && pTarget && pPlayer->IsPlayer() && pTarget->IsPlayer() )
		return GR_TEAMMATE;

	return GR_NOTTEAMMATE;
}

// the map's weapons stay where they are so everybody can take one
int CHalfLifeCoop::WeaponShouldRespawn( CBasePlayerItem *pWeapon )
{
	if( pWeapon->pev->spawnflags & SF_NORESPAWN )
		return GR_WEAPON_RESPAWN_NO;

	return GR_WEAPON_RESPAWN_YES;
}

float CHalfLifeCoop::FlWeaponRespawnTime( CBasePlayerItem *pWeapon )
{
	if( !( pWeapon->iFlags() & ITEM_FLAG_LIMITINWORLD ) )
		return gpGlobals->time;

	return gpGlobals->time + WEAPON_RESPAWN_TIME;
}

BOOL CHalfLifeCoop::CanHavePlayerItem( CBasePlayer *pPlayer, CBasePlayerItem *pItem )
{
	if( !( pItem->iFlags() & ITEM_FLAG_LIMITINWORLD ) )
	{
		for( int i = 0; i < MAX_ITEM_TYPES; i++ )
		{
			for( CBasePlayerItem *it = pPlayer->m_rgpPlayerItems[i]; it; it = it->m_pNext )
			{
				if( it->m_iId == pItem->m_iId )
					return FALSE;
			}
		}
	}

	return CGameRules::CanHavePlayerItem( pPlayer, pItem );
}

int CHalfLifeCoop::ItemShouldRespawn( CItem *pItem )
{
	return GR_ITEM_RESPAWN_NO;
}

int CHalfLifeCoop::AmmoShouldRespawn( CBasePlayerAmmo *pAmmo )
{
	return GR_AMMO_RESPAWN_NO;
}

// the closest living player to a point, other than "except"
static CBasePlayer *Coop_Nearest( const Vector &origin, CBasePlayer *except )
{
	CBasePlayer *pBest = NULL;
	float best = 0;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = Coop_Player( i );

		if( !pPlayer || pPlayer == except || !pPlayer->IsAlive() )
			continue;

		float d = ( pPlayer->pev->origin - origin ).Length();

		if( !pBest || d < best )
		{
			pBest = pPlayer;
			best = d;
		}
	}

	return pBest;
}

// what is under a point: the world or the entity that carries the player (a tram, a lift)
static edict_t *Coop_FloorUnder( const Vector &pos, edict_t *ignore )
{
	TraceResult tr;

	UTIL_TraceLine( pos, pos - Vector( 0, 0, 96 ), dont_ignore_monsters, ignore, &tr );

	if( tr.flFraction >= 1.0f )
		return NULL;	// nothing to stand on (outside a vehicle, over a pit)

	return tr.pHit ? tr.pHit : INDEXENT( 0 );
}

edict_t *CHalfLifeCoop::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	CBasePlayer *pNear = Coop_Nearest( pPlayer->pev->origin, pPlayer );

	// nobody else is in the game yet: the level's start
	if( !pNear )
		return CHalfLifeMultiplay::GetPlayerSpawnSpot( pPlayer );

	const int slot = Coop_Slot( pPlayer ) < 0 ? 0 : Coop_Slot( pPlayer );
	Vector spot = pNear->pev->origin;
	BOOL found = FALSE;
	static const float rings[] = { 40, 56, 80, 112 };

	// the spot has to be on what the partner stands on: in the tram of the first level
	// a place beyond the cabin's walls would leave the player on the tracks
	edict_t *pFloor = Coop_FloorUnder( pNear->pev->origin, pNear->edict() );

	for( int pass = 0; pass < 2 && !found; pass++ )
	{
		for( int r = 0; r < 4 && !found; r++ )
		{
			for( int k = 0; k < 8 && !found; k++ )
			{
				const float ang = ( ( k + slot * 2 ) % 8 ) * 45.0f * 0.01745329f;
				Vector pos = pNear->pev->origin + Vector( cos( ang ) * rings[r], sin( ang ) * rings[r], 0 );
				TraceResult tr;

				// the way there has to be free and the player has to fit
				UTIL_TraceHull( pNear->pev->origin, pos, dont_ignore_monsters, human_hull, pNear->edict(), &tr );

				if( tr.flFraction < 1.0f || tr.fStartSolid || tr.fAllSolid )
					continue;

				UTIL_TraceHull( pos, pos, dont_ignore_monsters, human_hull, pPlayer->edict(), &tr );

				if( tr.fStartSolid || tr.fAllSolid )
					continue;

				// first pass: only where the partner's floor is; second: anywhere free
				if( pass == 0 && pFloor && Coop_FloorUnder( pos, pPlayer->edict() ) != pFloor )
					continue;

				spot = pos;
				found = TRUE;
			}
		}
	}

	pPlayer->pev->origin = spot + Vector( 0, 0, 1 );
	pPlayer->pev->v_angle = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = pNear->pev->angles;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;

	return pNear->edict();
}

void CHalfLifeCoop::PlayerSpawn( CBasePlayer *pPlayer )
{
	const int slot = Coop_Slot( pPlayer );
	char blob[COOP_BLOB];
	BOOL given = FALSE;

	Coop_Look( pPlayer, g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ) );

	if( slot >= 0 && g_fCoopDead[slot] )
	{
		// he died: back with what he had, at full health
		g_fCoopDead[slot] = FALSE;
		Coop_Apply( pPlayer, g_szCoopDeath[slot], FALSE );
		given = TRUE;
	}
	else if( slot >= 0 && g_fCoopFresh[slot] )
	{
		// the state the previous level (or a loaded save) left for him
		g_fCoopFresh[slot] = FALSE;
		Coop_Apply( pPlayer, g_szCoopStart[slot], TRUE );
		given = TRUE;
	}
	else
	{
		// joins late: the same as the closest partner has
		CBasePlayer *pNear = Coop_Nearest( pPlayer->pev->origin, pPlayer );

		if( pNear )
		{
			Coop_Snapshot( pNear, blob, sizeof( blob ) );
			Coop_Apply( pPlayer, blob, FALSE );
			given = TRUE;
		}
	}

	if( !given )
	{
		// the equipment the map hands out, if it has any
		CBaseEntity *pEquip = NULL;

		while( ( pEquip = UTIL_FindEntityByClassname( pEquip, "game_player_equip" ) ) )
			pEquip->Touch( pPlayer );
	}

	// what he has now is what a restart of the level gives back
	if( slot >= 0 && !g_szCoopStart[slot][0] )
		Coop_Snapshot( pPlayer, g_szCoopStart[slot], sizeof( g_szCoopStart[slot] ) );
}

void CHalfLifeCoop::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	const int slot = Coop_Slot( pVictim );

	if( slot >= 0 )
	{
		Coop_Snapshot( pVictim, g_szCoopDeath[slot], sizeof( g_szCoopDeath[slot] ) );
		g_fCoopDead[slot] = TRUE;
	}

	CHalfLifeMultiplay::PlayerKilled( pVictim, pKiller, pInflictor );
}

void CHalfLifeCoop::Think( void )
{
	static float lastHealth[COOP_MAX], lastHurt[COOP_MAX], deadSince[COOP_MAX];
	int alive = 0, total = 0;

#if !NO_VOICEGAMEMGR
	g_VoiceGameMgr.Update( gpGlobals->frametime );
#endif

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = Coop_Player( i );
		const int slot = i - 1;

		if( !pPlayer || slot >= COOP_MAX )
			continue;

		total++;

		if( pPlayer->IsAlive() )
		{
			alive++;
			deadSince[slot] = 0;

			if( pPlayer->pev->health < lastHealth[slot] )
				lastHurt[slot] = gpGlobals->time;

			if( pPlayer->pev->health < 100 && gpGlobals->time > lastHurt[slot] + COOP_REGEN_DELAY )
			{
				pPlayer->pev->health += gpGlobals->frametime * ( 100.0f / COOP_REGEN_TIME );

				if( pPlayer->pev->health > 100 )
					pPlayer->pev->health = 100;
			}

			lastHealth[slot] = pPlayer->pev->health;
		}
		else
		{
			lastHealth[slot] = 100;
			lastHurt[slot] = 0;

			if( deadSince[slot] == 0 )
				deadSince[slot] = gpGlobals->time;
		}
	}

	if( alive > 0 )
	{
		g_flCoopLastAlive = gpGlobals->time;

		// the dead come back near the closest living player
		for( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pPlayer = Coop_Player( i );
			const int slot = i - 1;

			if( !pPlayer || slot >= COOP_MAX || pPlayer->IsAlive() || deadSince[slot] == 0 )
				continue;

			if( pPlayer->pev->deadflag >= DEAD_DEAD && gpGlobals->time > deadSince[slot] + COOP_RESPAWN_DELAY )
			{
				deadSince[slot] = 0;
				pPlayer->pev->button = 0;
				respawn( pPlayer->pev, !( pPlayer->m_afPhysicsFlags & PFLAG_OBSERVER ) );
			}
		}
	}
	else if( total > 0 && !g_fCoopRestarting && gpGlobals->time > g_flCoopLastAlive + COOP_ALLDEAD_DELAY )
	{
		// everybody is dead: the level starts again with what each one began it with
		g_fCoopRestarting = TRUE;

		for( int i = 0; i < COOP_MAX; i++ )
			Coop_StoreCvar( i, g_szCoopStart[i] );

		SERVER_COMMAND( UTIL_VarArgs( "changelevel %s\n", STRING( gpGlobals->mapname ) ) );
	}
}
