#ifndef HC_AXISLINESRENDERER_H
#define HC_AXISLINESRENDERER_H
#include "Core.h"
/* Renders 3 lines showing direction of each axis.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
HC_BEGIN_HEADER

struct IGameComponent;
extern struct IGameComponent AxisLinesRenderer_Component;
/* Whether the 3 axis lines should be rendered */
extern hc_bool AxisLinesRenderer_Enabled;

void AxisLinesRenderer_Render(void);

HC_END_HEADER
#endif
