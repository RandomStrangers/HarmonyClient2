#ifndef HC_SELOUTLINERENDERER_H
#define HC_SELOUTLINERENDERER_H
#include "Core.h"
HC_BEGIN_HEADER

/* Renders an outline around the block the player is looking at.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct RayTracer;
struct IGameComponent;
extern struct IGameComponent SelOutlineRenderer_Component;

void SelOutlineRenderer_Render(struct RayTracer* selected, hc_bool dirty);

HC_END_HEADER
#endif
