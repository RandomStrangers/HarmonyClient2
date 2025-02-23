#ifndef HC_STREAM_H
#define HC_STREAM_H
#include "Constants.h"
#include "Platform.h"
HC_BEGIN_HEADER

/* Defines an abstract way of reading and writing data in a streaming manner.
   Also provides common helper methods for reading/writing data to/from streams.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

struct Stream;
/* Represents a stream that can be written to and/or read from. */
struct Stream {
	/* Attempts to read some bytes from this stream. */
	hc_result (*Read)(struct Stream* s, hc_uint8* data, hc_uint32 count, hc_uint32* modified);
	/* Attempts to efficiently read a single byte from this stream. (falls back to Read) */
	hc_result (*ReadU8)(struct Stream* s, hc_uint8* data);
	/* Attempts to write some bytes to this stream. */
	hc_result (*Write)(struct Stream* s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified);
	/* Attempts to quickly advance the position in this stream. (falls back to reading then discarding) */
	hc_result (*Skip)(struct Stream* s, hc_uint32 count);

	/* Attempts to seek to the given position in this stream. (may not be supported) */
	hc_result (*Seek)(struct Stream* s, hc_uint32 position);
	/* Attempts to find current position this stream. (may not be supported) */
	hc_result (*Position)(struct Stream* s, hc_uint32* position);
	/* Attempts to find total length of this stream. (may not be supported) */
	hc_result (*Length)(struct Stream* s, hc_uint32* length);
	/* Attempts to close this stream, freeing associated resources. */
	hc_result (*Close)(struct Stream* s);
	
	union {
		hc_file file;
		void* inflate;
		struct { hc_uint8* cur; hc_uint32 left, length; hc_uint8* base; } mem;
		struct { struct Stream* source; hc_uint32 left, length; } portion;
		struct { hc_uint8* cur; hc_uint32 left, length; hc_uint8* base; struct Stream* source; hc_uint32 end; } buffered;
		struct { struct Stream* source; hc_uint32 crc32; } crc32;
	} meta;
};

/* Attempts to fully read up to count bytes from the stream. */
HC_API  hc_result Stream_Read(      struct Stream* s, hc_uint8* buffer, hc_uint32 count);
typedef hc_result (*FP_Stream_Read)(struct Stream* s, hc_uint8* buffer, hc_uint32 count);
/* Attempts to fully write up to count bytes from the stream. */
HC_API  hc_result Stream_Write(      struct Stream* s, const hc_uint8* buffer, hc_uint32 count);
typedef hc_result (*FP_Stream_Write)(struct Stream* s, const hc_uint8* buffer, hc_uint32 count);
/* Initialises default function pointers for a stream. (all read, write, seeks return an error) */
void Stream_Init(struct Stream* s);

/* Wrapper for File_Open() then Stream_FromFile() */
HC_API  hc_result Stream_OpenFile(      struct Stream* s, const hc_string* path);
typedef hc_result (*FP_Stream_OpenFile)(struct Stream* s, const hc_string* path);
/* Wrapper for File_Create() then Stream_FromFile() */
HC_API  hc_result Stream_CreateFile(      struct Stream* s, const hc_string* path);
typedef hc_result (*FP_Stream_CreateFile)(struct Stream* s, const hc_string* path);
/* Wrapper for File_OpenOrCreate, then File_Seek(END), then Stream_FromFile() */
hc_result Stream_AppendFile(struct Stream* s, const hc_string* path);
/* Creates or overwrites a file, setting the contents to the given data. */
hc_result Stream_WriteAllTo(const hc_string* path, const hc_uint8* data, hc_uint32 length);
/* Wraps a file, allowing reading from/writing to/seeking in the file. */
HC_API void Stream_FromFile(struct Stream* s, hc_file file);

/* Wraps another Stream, only allows reading up to 'len' bytes from the wrapped stream. */
HC_API void Stream_ReadonlyPortion(struct Stream* s, struct Stream* source, hc_uint32 len);
/* Wraps a block of memory, allowing reading from and seeking in the block. */
HC_API void Stream_ReadonlyMemory(struct Stream* s, void* data, hc_uint32 len);
/* Wraps another Stream, reading through an intermediary buffer. (Useful for files, since each read call is expensive) */
HC_API void Stream_ReadonlyBuffered(struct Stream* s, struct Stream* source, void* data, hc_uint32 size);

/* Wraps another Stream, calculating a running CRC32 as data is written. */
/* To get the final CRC32, xor it with 0xFFFFFFFFUL */
void Stream_WriteonlyCrc32(struct Stream* s, struct Stream* source);

/* Reads a little-endian 16 bit unsigned integer from memory. */
hc_uint16 Stream_GetU16_LE(const hc_uint8* data);
/* Reads a big-endian 16 bit unsigned integer from memory. */
hc_uint16 Stream_GetU16_BE(const hc_uint8* data);
/* Reads a little-endian 32 bit unsigned integer from memory. */
hc_uint32 Stream_GetU32_LE(const hc_uint8* data);
/* Reads a big-endian 32 bit unsigned integer from memory. */
hc_uint32 Stream_GetU32_BE(const hc_uint8* data);

/* Writes a little-endian 16 bit unsigned integer to memory. */
void Stream_SetU16_LE(hc_uint8* data, hc_uint16 value);
/* Writes a big-endian 16 bit unsigned integer to memory. */
void Stream_SetU16_BE(hc_uint8* data, hc_uint16 value);
/* Writes a little-endian 32 bit unsigned integer to memory. */
void Stream_SetU32_LE(hc_uint8* data, hc_uint32 value);
/* Writes a big-endian 32 bit unsigned integer to memory. */
void Stream_SetU32_BE(hc_uint8* data, hc_uint32 value);
/* Reads a little-endian 32 bit unsigned integer from the stream. */
hc_result Stream_ReadU32_LE(struct Stream* s, hc_uint32* value);
/* Reads a big-endian 32 bit unsigned integer from the stream. */
hc_result Stream_ReadU32_BE(struct Stream* s, hc_uint32* value);

/* Reads a line of UTF8 encoded character from the stream. */
/* NOTE: Reads one byte at a time. May want to use Stream_ReadonlyBuffered. */
HC_API hc_result Stream_ReadLine(struct Stream* s, hc_string* text);
/* Writes a line of UTF8 encoded text to the stream. */
HC_API hc_result Stream_WriteLine(struct Stream* s, hc_string* text);

HC_END_HEADER
#endif
