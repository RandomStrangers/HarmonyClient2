#ifndef HC_AUDIO_H
#define HC_AUDIO_H
#include "Core.h"
/* Manages playing sound and music.
   Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
HC_BEGIN_HEADER

struct IGameComponent;
extern struct IGameComponent Audio_Component; 
struct AudioContext;

#ifdef HC_BUILD_WEBAUDIO
#define DEFAULT_SOUNDS_VOLUME   0
#else
#define DEFAULT_SOUNDS_VOLUME 100
#endif

#ifdef HC_BUILD_NOMUSIC
#define DEFAULT_MUSIC_VOLUME   0
#else
#define DEFAULT_MUSIC_VOLUME 100
#endif

union AudioChunkMeta { void* ptr; hc_uintptr val; };
struct AudioChunk {
	void* data; /* the raw 16 bit integer samples */
	hc_uint32 size;
	union AudioChunkMeta meta;
};

struct AudioData {
	struct AudioChunk chunk;
	int channels;
	int sampleRate; /* frequency / sample rate */
	int volume; /* volume data played at (100 = normal volume) */
	int rate;   /* speed/pitch played at (100 = normal speed) */
};

/* Volume sounds are played at, from 0-100. */
/* NOTE: Use Audio_SetSounds, don't change this directly. */
extern int Audio_SoundsVolume;
/* Volume music is played at, from 0-100. */
/* NOTE: Use Audio_SetMusic, don't change this directly. */
extern int Audio_MusicVolume;
extern const hc_string Sounds_ZipPathMC;
extern const hc_string Sounds_ZipPathCC;

void Audio_SetMusic(int volume);
void Audio_SetSounds(int volume);
void Audio_PlayDigSound(hc_uint8 type);
void Audio_PlayStepSound(hc_uint8 type);
#define AUDIO_MAX_BUFFERS 4

hc_bool AudioBackend_Init(void);
void    AudioBackend_Tick(void);
void    AudioBackend_Free(void);

/* Initialises an audio context. */
hc_result Audio_Init(struct AudioContext* ctx, int buffers);
/* Stops any playing audio and then frees the audio context. */
void Audio_Close(struct AudioContext* ctx);
/* Sets the format of the audio data to be played. */
/* NOTE: Changing the format can be expensive, depending on the backend. */
hc_result Audio_SetFormat(struct AudioContext* ctx, int channels, int sampleRate, int playbackRate);
/* Sets the volume audio data is played at */
void Audio_SetVolume(struct AudioContext* ctx, int volume);
/* Queues the given audio chunk for playing. */
/* NOTE: You MUST ensure Audio_Poll indicates a buffer is free before calling this. */
/* NOTE: Some backends directly read from the chunk data - therefore you MUST NOT modify it */
hc_result Audio_QueueChunk(struct AudioContext* ctx, struct AudioChunk* chunk);
/* Begins playing audio. Audio_QueueChunk must have been used before this. */
hc_result Audio_Play(struct AudioContext* ctx);
/* Polls the audio context and then potentially unqueues buffer */
/* Returns the number of buffers being played or queued */
/* (e.g. if inUse is 0, no audio buffers are being played or queued) */
hc_result Audio_Poll(struct AudioContext* ctx, int* inUse);
hc_result Audio_Pause(struct AudioContext* ctx); /* Only implemented with OpenSL ES backend */

/* Outputs more detailed information about errors with audio. */
hc_bool Audio_DescribeError(hc_result res, hc_string* dst);
/* Allocates a group of chunks of data to store audio samples */
hc_result Audio_AllocChunks(hc_uint32 size, struct AudioChunk* chunks, int numChunks);
/* Frees a previously allocated group of chunks of data */
void Audio_FreeChunks(struct AudioChunk* chunks, int numChunks);

extern struct AudioContext music_ctx;
void Audio_Warn(hc_result res, const char* action);

hc_result AudioPool_Play(struct AudioData* data);
void AudioPool_Close(void);

HC_END_HEADER
#endif
