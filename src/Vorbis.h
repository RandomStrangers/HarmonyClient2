#ifndef HC_VORBIS_H
#define HC_VORBIS_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Decodes ogg vorbis audio into 16 bit PCM samples
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct Stream;
#define VORBIS_MAX_CHANS 8
#define VORBIS_MAX_BLOCK_SIZE 8192
#define OGG_BUFFER_SIZE (255 * 256)

struct OggState {
	hc_uint8* cur; 
	hc_uint32 left, last;
	struct Stream* source;
	int segmentsRead, numSegments;
	hc_uint8 buffer[OGG_BUFFER_SIZE];
	hc_uint8 segments[255];
};

/* Wraps an OGG container around an existing stream. */
void Ogg_Init(struct OggState* ctx, struct Stream* source);
struct Codebook; struct Floor; struct Residue; struct Mapping; struct Mode;

struct imdct_state {
	int n, log2_n;
	float a[VORBIS_MAX_BLOCK_SIZE / 2];
	float b[VORBIS_MAX_BLOCK_SIZE / 2];
	float c[VORBIS_MAX_BLOCK_SIZE / 4];
	hc_uint32 reversed[VORBIS_MAX_BLOCK_SIZE / 8];
};

struct VorbisWindow { float* Prev; float* Cur; };
struct VorbisState {
	hc_uint32 Bits;    /* Holds bits across byte boundaries*/
	hc_uint32 NumBits; /* Number of bits in Bits buffer*/
	struct OggState* source; /* Source for filling Input buffer */

	hc_uint8 channels, modeNumBits;
	hc_uint16 curBlockSize, prevBlockSize, dataSize, numCodebooks;
	int sampleRate; int blockSizes[2];
	float* temp; /* temp array reused in places */
	float* values[2]; /* swapped each frame */
	float* prevOutput[VORBIS_MAX_CHANS];
	float* curOutput[VORBIS_MAX_CHANS];

	struct Codebook* codebooks;
	struct Floor* floors;
	struct Residue* residues;
	struct Mapping* mappings;
	struct Mode* modes;

	float* windowRaw;
	struct VorbisWindow windows[2];
	struct imdct_state imdct[2];
};

/* Initialises the given context to defaults */
void Vorbis_Init(struct VorbisState* ctx);
/* Frees all memory dynamically allocated by the given context */
void Vorbis_Free(struct VorbisState* ctx);
/* Reads and decodes the initial vorbis headers and setup data. */
hc_result Vorbis_DecodeHeaders(struct VorbisState* ctx);
/* Reads and decodes the current frame's audio data. */
hc_result Vorbis_DecodeFrame(struct VorbisState* ctx);
/* Produces final interleaved audio samples for the current frame. */
int Vorbis_OutputFrame(struct VorbisState* ctx, hc_int16* data);

HC_END_HEADER
#endif
