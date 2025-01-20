#ifndef HC_PACKEDCOL_H
#define HC_PACKEDCOL_H
#include "Core.h"
HC_BEGIN_HEADER

/* Manipulates a packed 32 bit RGBA colour, in a format suitable for the native 3D graphics API vertex colours.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

typedef hc_uint32 PackedCol;
#if (HC_GFX_BACKEND == HC_GFX_BACKEND_D3D9) || defined HC_BUILD_XBOX || defined HC_BUILD_DREAMCAST
	#define PACKEDCOL_B_SHIFT  0
	#define PACKEDCOL_G_SHIFT  8
	#define PACKEDCOL_R_SHIFT 16
	#define PACKEDCOL_A_SHIFT 24
#elif defined HC_BIG_ENDIAN
	#define PACKEDCOL_R_SHIFT 24
	#define PACKEDCOL_G_SHIFT 16
	#define PACKEDCOL_B_SHIFT  8
	#define PACKEDCOL_A_SHIFT  0
#else
	#define PACKEDCOL_R_SHIFT  0
	#define PACKEDCOL_G_SHIFT  8
	#define PACKEDCOL_B_SHIFT 16
	#define PACKEDCOL_A_SHIFT 24
#endif

#define PACKEDCOL_R_MASK (0xFFU << PACKEDCOL_R_SHIFT)
#define PACKEDCOL_G_MASK (0xFFU << PACKEDCOL_G_SHIFT)
#define PACKEDCOL_B_MASK (0xFFU << PACKEDCOL_B_SHIFT)
#define PACKEDCOL_A_MASK (0xFFU << PACKEDCOL_A_SHIFT)

#define PackedCol_R(col) ((hc_uint8)(col >> PACKEDCOL_R_SHIFT))
#define PackedCol_G(col) ((hc_uint8)(col >> PACKEDCOL_G_SHIFT))
#define PackedCol_B(col) ((hc_uint8)(col >> PACKEDCOL_B_SHIFT))
#define PackedCol_A(col) ((hc_uint8)(col >> PACKEDCOL_A_SHIFT))

#define PackedCol_R_Bits(col) ((hc_uint8)(col) << PACKEDCOL_R_SHIFT)
#define PackedCol_G_Bits(col) ((hc_uint8)(col) << PACKEDCOL_G_SHIFT)
#define PackedCol_B_Bits(col) ((hc_uint8)(col) << PACKEDCOL_B_SHIFT)
#define PackedCol_A_Bits(col) ((hc_uint8)(col) << PACKEDCOL_A_SHIFT)

#define PackedCol_Make(r, g, b, a) (PackedCol_R_Bits(r) | PackedCol_G_Bits(g) | PackedCol_B_Bits(b) | PackedCol_A_Bits(a))
#define PACKEDCOL_WHITE PackedCol_Make(255, 255, 255, 255)
#define PACKEDCOL_RGB_MASK (PACKEDCOL_R_MASK | PACKEDCOL_G_MASK | PACKEDCOL_B_MASK)

/* Scales RGB components of the given colour. */
HC_API PackedCol PackedCol_Scale(PackedCol value, float t);
/* Linearly interpolates RGB components of the two given colours. */
HC_API PackedCol PackedCol_Lerp(PackedCol a, PackedCol b, float t);
/* Multiplies RGB components of the two given colours. */
HC_API PackedCol PackedCol_Tint(PackedCol a, PackedCol b);
/* Adds the two colors together in a way that gives a brighter result. */
HC_API PackedCol PackedCol_ScreenBlend(PackedCol a, PackedCol b);

HC_NOINLINE int PackedCol_DeHex(char hex);
HC_NOINLINE hc_bool PackedCol_Unhex(const char* src, int* dst, int count);
HC_NOINLINE void PackedCol_ToHex(hc_string* str, PackedCol value);
HC_NOINLINE hc_bool PackedCol_TryParseHex(const hc_string* str, hc_uint8* rgb);

#define PACKEDCOL_SHADE_X 0.6f
#define PACKEDCOL_SHADE_Z 0.8f
#define PACKEDCOL_SHADE_YMIN 0.5f
/* Retrieves shaded colours for ambient block face lighting */
void PackedCol_GetShaded(PackedCol normal, PackedCol* xSide, PackedCol* zSide, PackedCol* yMin);

HC_END_HEADER
#endif
