#ifndef HC_BLOCKPHYSICS_H
#define HC_BLOCKPHYSICS_H
#include "Core.h"
HC_BEGIN_HEADER

/* Implements simple block physics.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
typedef void (*PhysicsHandler)(int index, BlockID block);

HC_VAR extern struct Physics_ {
	/* Whether block physics are enabled at all. */
	hc_bool Enabled;
	/* Called when block is activated by a neighbouring block change. */
	/* e.g. trigger sand falling, water flooding */
	PhysicsHandler OnActivate[256];
	/* Called when this block is randomly activated. */
	/* e.g. grass eventually fading to dirt in darkness */
	PhysicsHandler OnRandomTick[256];
	/* Called when user manually places a block. */
	PhysicsHandler OnPlace[256];
	/* Called when user manually deletes a block. */
	PhysicsHandler OnDelete[256];
} Physics;

void Physics_SetEnabled(hc_bool enabled);
void Physics_OnBlockChanged(int x, int y, int z, BlockID old, BlockID now);
void Physics_Init(void);
void Physics_Free(void);
void Physics_Tick(void);

HC_END_HEADER
#endif
