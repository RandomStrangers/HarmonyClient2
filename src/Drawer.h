#ifndef HC_DRAWER_H
#define HC_DRAWER_H
#include "PackedCol.h"
#include "Vectors.h"
HC_BEGIN_HEADER

/* 
Draws the vertices for a cuboid region
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct VertexTextured;

HC_VAR extern struct _DrawerData {
	/* Whether a colour tinting effect should be applied to all faces. */
	hc_bool Tinted;
	/* The colour to multiply colour of faces by (tinting effect). */
	PackedCol TintCol;
	/* Minimum corner of base block bounding box. (For texture UV) */
	Vec3 MinBB;
	/* Maximum corner of base block bounding box. (For texture UV) */
	Vec3 MaxBB;
	/* Coordinate of minimum block bounding box corner in the world. */
	float X1, Y1, Z1;
	/* Coordinate of maximum block bounding box corner in the world. */
	float X2, Y2, Z2;
} Drawer;

/* Draws minimum X face of the cuboid. (i.e. at X1) */
HC_API void Drawer_XMin(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);
/* Draws maximum X face of the cuboid. (i.e. at X2) */
HC_API void Drawer_XMax(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);
/* Draws minimum Z face of the cuboid. (i.e. at Z1) */
HC_API void Drawer_ZMin(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);
/* Draws maximum Z face of the cuboid. (i.e. at Z2) */
HC_API void Drawer_ZMax(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);
/* Draws minimum Y face of the cuboid. (i.e. at Y1) */
HC_API void Drawer_YMin(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);
/* Draws maximum Y face of the cuboid. (i.e. at Y2) */
HC_API void Drawer_YMax(int count, PackedCol col, TextureLoc texLoc, struct VertexTextured** vertices);

HC_END_HEADER
#endif
