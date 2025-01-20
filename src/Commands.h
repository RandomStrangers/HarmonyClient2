#ifndef HC_COMMANDS_H
#define HC_COMMANDS_H
#include "Core.h"
HC_BEGIN_HEADER

/* Executes actions in response to certain chat input
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct IGameComponent;
extern struct IGameComponent Commands_Component;

hc_bool Commands_Execute(const hc_string* input);

/* This command is only available in singleplayer */
#define COMMAND_FLAG_SINGLEPLAYER_ONLY 0x01
/* args is passed as a single string instead of being split by spaces */
#define COMMAND_FLAG_UNSPLIT_ARGS 0x02

struct ChatCommand;
/* Represents a client-side command/action */
struct ChatCommand {
	const char* name;         /* Full name of this command */
	/* Function pointer for the actual action the command performs */
	void (*Execute)(const hc_string* args, int argsCount);
	hc_uint8 flags;           /* Flags for handling this command (see COMMAND_FLAG defines) */
	const char* help[5];      /* Messages to show when a player uses /help on this command */
	struct ChatCommand* next; /* Next command in linked-list of client commands */
};

/* Registers a client-side command, allowing it to be used with /client [cmd name] */
HC_API  void Commands_Register(      struct ChatCommand* cmd);
typedef void (*FP_Commands_Register)(struct ChatCommand* cmd);

HC_END_HEADER
#endif
