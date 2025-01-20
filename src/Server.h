#ifndef HC_SERVERCONNECTION_H
#define HC_SERVERCONNECTION_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Represents a connection to either a multiplayer or an internal singleplayer server
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

struct IGameComponent;
struct ScheduledTask;
extern struct IGameComponent Server_Component;

/* Prepares a ping entry for sending to the server, then returns its ID */
int Ping_NextPingId(void);
/* Updates received time for ping entry with matching ID */
void Ping_Update(int id);
/* Calculates average ping time based on most recent ping entries */
int Ping_AveragePingMS(void);

/* Data for currently active connection to a server */
HC_VAR extern struct _ServerConnectionData {
	/* Begins connecting to the server */
	/* NOTE: Usually asynchronous, but not always */
	void (*BeginConnect)(void);
	/* Ticks state of the server. */
	void (*Tick)(struct ScheduledTask* task);
	/* Sends a block update to the server */
	void (*SendBlock)(int x, int y, int z, BlockID old, BlockID now);
	/* Sends a chat message to the server */
	void (*SendChat)(const hc_string* text);
	/* NOTE: Deprecated and removed - change LocalPlayer's position instead */
	/*  Was a function pointer to send a position update to the multiplayer server */
	void (*__Unused)(void);
	/* Sends raw data to the server. */
	/* NOTE: Prefer SendBlock/SendChat instead, this does NOT work in singleplayer */
	void (*SendData)(const hc_uint8* data, hc_uint32 len);

	/* The current name of the server (Shows as first line when loading) */
	hc_string Name;
	/* The current MOTD of the server (Shows as second line when loading) */
	hc_string MOTD;
	/* The software name the client identifies itself as being to the server */
	/* By default this is GAME_APP_NAME */
	hc_string AppName;

	/* NOTE: Drprecated, was a pointer to a temp buffer  */
	hc_uint8* ___unused;
	/* Whether the player is connected to singleplayer/internal server */
	hc_bool IsSinglePlayer;
	/* Whether the player has been disconnected from the server */
	hc_bool Disconnected;

	/* Whether the server supports separate tab list from entities in world */
	hc_bool SupportsExtPlayerList;
	/* Whether the server supports packet with detailed info on mouse clicks */
	hc_bool SupportsPlayerClick;
	/* Whether the server supports combining multiple chat packets into one */
	hc_bool SupportsPartialMessages;
	/* Whether the server supports all of code page 437, not just ASCII */
	hc_bool SupportsFullCP437;

	/* Address of the server if multiplayer, empty string if singleplayer */
	hc_string Address;
	/* Port of the server if multiplayer, 0 if singleplayer */
	int Port;
} Server;

/* If user hasn't previously accepted url, displays a dialog asking to confirm downloading it */
/* Otherwise just calls TexturePack_Extract */
void Server_RetrieveTexturePack(const hc_string* url);

/* Path of map to automatically load in singleplayer */
extern hc_string SP_AutoloadMap;

HC_END_HEADER
#endif
