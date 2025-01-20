#ifndef HC_HELDBLOCKRENDERER_H
#define HC_HELDBLOCKRENDERER_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Renders the held block/arm at bottom right of game
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct IGameComponent;
extern struct IGameComponent HeldBlockRenderer_Component;
/* Whether held block/arm should be shown at all. */
extern hc_bool HeldBlockRenderer_Show;

void HeldBlockRenderer_ClickAnim(hc_bool digging);
void HeldBlockRenderer_Render(float delta);

HC_END_HEADER
#endif
