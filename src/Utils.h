#ifndef HC_UTILS_H
#define HC_UTILS_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Provides various utility functions
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

struct Bitmap;
struct StringsBuffer;
/* Represents a particular instance in time in some timezone. Not necessarily UTC time. */
/* NOTE: TimeMS and DateTime_CurrentUTC() should almost always be used instead. */
/* This struct should only be used when actually needed. (e.g. log message time) */
struct DateTime {
	int year;   /* Year,   ranges from 0 to 65535 */
	int month;  /* Month,  ranges from 1 to 12 */
	int day;    /* Day,    ranges from 1 to 31 */
	int hour;   /* Hour,   ranges from 0 to 23 */
	int minute; /* Minute, ranges from 0 to 59 */
	int second; /* Second, ranges from 0 to 59 */
	int __milli;/* Was milliseconds, unused now */
};

#define MILLIS_PER_SEC 1000
#define SECS_PER_MIN 60
#define SECS_PER_HOUR (60 * 60)
#define SECS_PER_DAY (60 * 60 * 24)

HC_NOINLINE int Utils_ParseEnum(const hc_string* text, int defValue, const char* const* names, int namesCount);
/* Returns whether value starts with http:// or https:// */
hc_bool Utils_IsUrlPrefix(const hc_string* value);

/* Creates the directory if it doesn't exist. (logs failure using Logger_SysWarn2) */
hc_bool Utils_EnsureDirectory(const char* dirName);
/* Gets the filename portion of a path. (e.g. "dir/file.txt" -> "file.txt") */
void Utils_UNSAFE_GetFilename(STRING_REF hc_string* path);
/* Gets rid of first directory in a path. (e.g. "dx/gl/aa.txt" -> "gl/aa.txt" */
void Utils_UNSAFE_TrimFirstDirectory(STRING_REF hc_string* path);
int Utils_AccumulateWheelDelta(float* accumulator, float delta);
#define Utils_AdjViewDist(value) ((int)(1.4142135f * (value)))

hc_uint8 Utils_CalcSkinType(const struct Bitmap* bmp);
hc_uint32 Utils_CRC32(const hc_uint8* data, hc_uint32 length);
/* CRC32 lookup table, for faster CRC32 calculations. */
/* NOTE: This cannot be just indexed by byte value - see Utils_CRC32 implementation. */
extern const hc_uint32 Utils_Crc32Table[256];
HC_NOINLINE void Utils_Resize(void** buffer, int* capacity, hc_uint32 elemSize, int defCapacity, int expandElems);
void Utils_SwapEndian16(hc_int16* values, int numValues);

/* Converts blocks of 3 bytes into 4 ASCII characters. (pads if needed) */
/* Returns the number of ASCII characters written. */
/* NOTE: You MUST ensure that dst is appropriately sized. */
int Convert_ToBase64(const void* data, int len, char* dst);
/* Converts blocks of 4 ASCII characters into 3 bytes. */
/* Returns the number of bytes written. */
/* NOTE: You MUST ensure that dst is appropriately sized. */
int Convert_FromBase64(const char* src, int len, hc_uint8* dst);

typedef hc_bool (*EntryList_Filter)(const hc_string* entry);
/* Loads the entries from disc. */
/* NOTE: If separator is \0, does NOT check for duplicate keys when loading. */
/* filter can be used to optionally skip loading some entries from the file. */
HC_NOINLINE hc_result EntryList_Load(struct StringsBuffer* list, const char* file, char separator, EntryList_Filter filter);
/* Shortcut for EntryList_Load with separator of \0 and filter of NULL */
/* NOTE: Does NOT check for duplicate keys */
HC_NOINLINE hc_result EntryList_UNSAFE_Load(struct StringsBuffer* list, const char* file);
/* Saves the entries in the given list to disc. */
HC_NOINLINE void EntryList_Save(struct StringsBuffer* list, const char* file);
/* Removes the entry whose key caselessly equals the given key. */
HC_NOINLINE hc_bool EntryList_Remove(struct StringsBuffer* list, const hc_string* key, char separator);
/* Replaces the entry whose key caselessly equals the given key, or adds a new entry. */
HC_NOINLINE void EntryList_Set(struct StringsBuffer* list, const hc_string* key, const hc_string* value, char separator);
/* Returns the value of the entry whose key caselessly equals the given key. */
HC_NOINLINE STRING_REF hc_string EntryList_UNSAFE_Get(struct StringsBuffer* list, const hc_string* key, char separator);
/* Finds the index of the entry whose key caselessly equals the given key. */
HC_NOINLINE int EntryList_Find(struct StringsBuffer* list, const hc_string* key, char separator);

hc_bool DirectUrl_Claims(const hc_string* STRING_REF input, hc_string* addr, hc_string* user, hc_string* mppass);
hc_bool DirectUrl_ExtractAddress(const hc_string* STRING_REF addr, hc_string* ip, hc_string* port, int* portNum);

HC_END_HEADER
#endif
