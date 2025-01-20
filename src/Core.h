#ifndef HC_CORE_H
#define HC_CORE_H
/* 
Core fixed-size integer types, automatic platform detection, and common small structs
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
#define CC_UPDATE
#if _MSC_VER
	typedef signed __int8  hc_int8;
	typedef signed __int16 hc_int16;
	typedef signed __int32 hc_int32;
	typedef signed __int64 hc_int64;
	typedef unsigned __int8  hc_uint8;
	typedef unsigned __int16 hc_uint16;
	typedef unsigned __int32 hc_uint32;
	typedef unsigned __int64 hc_uint64;

	#ifdef _WIN64
	typedef unsigned __int64 hc_uintptr;
	#else
	typedef unsigned __int32 hc_uintptr;
	#endif

#if _MSC_VER <= 1500
	#define HC_INLINE
	#define HC_NOINLINE
#else
	#define HC_INLINE   inline
	#define HC_NOINLINE __declspec(noinline)
#endif

	#ifndef HC_API
	#define HC_API __declspec(dllexport, noinline)
	#define HC_VAR __declspec(dllexport)
	#endif
	
	#define HC_HAS_TYPES
	#define HC_HAS_MISC

#elif __GNUC__
	/* really old GCC/clang might not have these defined */
	#ifdef __INT8_TYPE__
	/* avoid including <stdint.h> because it breaks defining UNICODE in Platform.c with MinGW */
	typedef __INT8_TYPE__  hc_int8;
	typedef __INT16_TYPE__ hc_int16;
	typedef __INT32_TYPE__ hc_int32;
	typedef __INT64_TYPE__ hc_int64;
	
	#ifdef __UINT8_TYPE__
	typedef __UINT8_TYPE__   hc_uint8;
	typedef __UINT16_TYPE__  hc_uint16;
	typedef __UINT32_TYPE__  hc_uint32;
	typedef __UINT64_TYPE__  hc_uint64;
	typedef __UINTPTR_TYPE__ hc_uintptr;
	#else
	/* clang doesn't define the __UINT8_TYPE__ */
	typedef unsigned __INT8_TYPE__   hc_uint8;
	typedef unsigned __INT16_TYPE__  hc_uint16;
	typedef unsigned __INT32_TYPE__  hc_uint32;
	typedef unsigned __INT64_TYPE__  hc_uint64;
	typedef unsigned __INTPTR_TYPE__ hc_uintptr;
	#endif
	#define HC_HAS_TYPES
	#endif
	
	#define HC_INLINE inline
	#define HC_NOINLINE __attribute__((noinline))
	
	#ifndef HC_API
	#ifdef _WIN32
	#define HC_API __attribute__((dllexport, noinline))
	#define HC_VAR __attribute__((dllexport))
	#else
	#define HC_API __attribute__((visibility("default"), noinline))
	#define HC_VAR __attribute__((visibility("default")))
	#endif
	#endif
	
	#define HC_HAS_MISC
	#ifdef __BIG_ENDIAN__
	#define HC_BIG_ENDIAN
	#endif
#elif __MWERKS__
	/* TODO: Is there actual attribute support for HC_API etc somewhere? */
	#define HC_BIG_ENDIAN
#endif

/* Unrecognised compiler, so just go with some sensible default typdefs */
/* Don't use <stdint.h>, as good chance such a compiler doesn't support it */
#ifndef HC_HAS_TYPES
typedef signed char  hc_int8;
typedef signed short hc_int16;
typedef signed int   hc_int32;
typedef signed long long hc_int64;

typedef unsigned char  hc_uint8;
typedef unsigned short hc_uint16;
typedef unsigned int   hc_uint32;
typedef unsigned long long hc_uint64;
typedef unsigned long  hc_uintptr;
#endif
#ifndef HC_HAS_MISC
#define HC_INLINE
#define HC_NOINLINE
#define HC_API
#define HC_VAR
#endif

typedef hc_uint32 hc_codepoint;
typedef hc_uint16 hc_unichar;
typedef hc_uint8  hc_bool;

#ifdef __APPLE__
/* TODO: REMOVE THIS AWFUL AWFUL HACK */
#include <stdbool.h>
#elif __cplusplus
#else
#define true 1
#define false 0
#endif

#ifndef NULL
#if __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

#define HC_WIN_BACKEND_TERMINAL 1
#define HC_WIN_BACKEND_SDL2     2
#define HC_WIN_BACKEND_SDL3     3
#define HC_WIN_BACKEND_X11      4
#define HC_WIN_BACKEND_WIN32    5
#define HC_WIN_BACKEND_COCOA    6
#define HC_WIN_BACKEND_BEOS     7
#define HC_WIN_BACKEND_ANDROID  8

#define HC_GFX_BACKEND_SOFTGPU   1
#define HC_GFX_BACKEND_GL1       2
#define HC_GFX_BACKEND_GL2       3
#define HC_GFX_BACKEND_D3D9      4
#define HC_GFX_BACKEND_D3D11     5
#define HC_GFX_BACKEND_VULKAN    6

#define HC_SSL_BACKEND_NONE     1
#define HC_SSL_BACKEND_BEARSSL  2
#define HC_SSL_BACKEND_SCHANNEL 3

#define HC_NET_BACKEND_BUILTIN  1
#define HC_NET_BACKEND_LIBCURL  2

#define HC_AUD_BACKEND_OPENAL   1
#define HC_AUD_BACKEND_WINMM    2
#define HC_AUD_BACKEND_OPENSLES 3

#define HC_GFX_BACKEND_IS_GL() (HC_GFX_BACKEND == HC_GFX_BACKEND_GL1 ||HC_GFX_BACKEND == HC_GFX_BACKEND_GL2)

#define HC_BUILD_NETWORKING
#define HC_BUILD_FREETYPE
#define HC_BUILD_RESOURCES
#define HC_BUILD_PLUGINS
#define HC_BUILD_ANIMATIONS
#define HC_BUILD_HELDBLOCK
#define HC_BUILD_FILESYSTEM
#define HC_BUILD_ADVLIGHTING
/*#define HC_BUILD_GL11*/

#ifndef HC_BUILD_MANUAL
#if defined NXDK
	/* XBox also defines _WIN32 */
	#define HC_BUILD_XBOX
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#define HC_BUILD_SPLITSCREEN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#ifndef CC_UPDATE
	#define CC_UPDATE
	#endif
#elif defined XENON
	/* libxenon also defines __linux__ (yes, really) */
	#define HC_BUILD_XBOX360
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
#elif defined _WIN32
	#define HC_BUILD_WIN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_SCHANNEL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_WINMM
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_D3D9
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_WIN32
	#undef  CC_UPDATE
#elif defined __ANDROID__
	#define HC_BUILD_ANDROID
	#define HC_BUILD_MOBILE
	#define HC_BUILD_POSIX
	#define HC_BUILD_GLES
	#define HC_BUILD_EGL
	#define HC_BUILD_TOUCH
	#define HC_BUILD_OPENSLES
	#undef  CC_UPDATE
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENSLES
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL2
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_ANDROID
#elif defined __serenity__
	#define HC_BUILD_SERENITY
	#define HC_BUILD_POSIX
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_SDL2
#elif defined __linux__
	#define HC_BUILD_LINUX
	#define HC_BUILD_POSIX
	#define HC_BUILD_XINPUT2
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
	#undef  CC_UPDATE
	#if defined HC_BUILD_RPI
		#define HC_BUILD_GLES
		#define HC_BUILD_EGL
		#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL2
	#else
		#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1	
	#endif
#elif defined __APPLE__
	#define HC_BUILD_DARWIN
	#define HC_BUILD_POSIX
	#if __x86_64__
		#undef  CC_UPDATE
	#endif
	#if defined __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__
		#define HC_BUILD_MOBILE
		#define HC_BUILD_GLES
		#define HC_BUILD_IOS
		#define HC_BUILD_TOUCH
		#define HC_BUILD_CFNETWORK
		#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL2
	#else
		#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
		#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_COCOA
		#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
		#define HC_BUILD_MACOS
	#endif
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined Macintosh
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_SOFTGPU
	#define HC_BUILD_MACCLASSIC
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#undef  HC_BUILD_RESOURCES
	#undef  HC_BUILD_FREETYPE
	#undef  HC_BUILD_NETWORKING
	#undef  CC_UPDATE
#elif defined __sun__
	#define HC_BUILD_SOLARIS
	#define HC_BUILD_POSIX
	#define HC_BUILD_XINPUT2
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
#elif defined __FreeBSD__ || defined __DragonFly__
	#define HC_BUILD_FREEBSD
	#define HC_BUILD_POSIX
	#define HC_BUILD_BSD
	#define HC_BUILD_XINPUT2
	#undef  CC_UPDATE
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
#elif defined __OpenBSD__
	#define HC_BUILD_OPENBSD
	#define HC_BUILD_POSIX
	#define HC_BUILD_BSD
	#define HC_BUILD_XINPUT2
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
#elif defined __NetBSD__
	#define HC_BUILD_NETBSD
	#define HC_BUILD_POSIX
	#define HC_BUILD_BSD
	#define HC_BUILD_XINPUT2
	#undef  CC_UPDATE
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
#elif defined __HAIKU__
	#define HC_BUILD_HAIKU
	#define HC_BUILD_POSIX
	#undef  CC_UPDATE
	#define HC_BACKTRACE_BUILTIN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_BEOS
#elif defined __BEOS__
	#define HC_BUILD_BEOS
	#define HC_BUILD_POSIX
	#define HC_BUILD_GL11
	#define HC_BACKTRACE_BUILTIN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_BEOS
#elif defined __sgi
	#define HC_BUILD_IRIX
	#define HC_BUILD_POSIX
	#define HC_BIG_ENDIAN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL1
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_X11
#elif defined __EMSCRIPTEN__
	#define HC_BUILD_WEB
	#define HC_BUILD_GLES
	#define HC_BUILD_TOUCH
	#define HC_BUILD_WEBAUDIO
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_MINFILES
	#define HC_BUILD_COOPTHREADED
	#undef  HC_BUILD_FREETYPE
	#undef  HC_BUILD_RESOURCES
	#undef  HC_BUILD_PLUGINS
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL2
#elif defined __psp__
	#define HC_BUILD_PSP
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_COOPTHREADED
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined __3DS__
	#define HC_BUILD_3DS
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_TOUCH
	#define HC_BUILD_DUALSCREEN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
#elif defined GEKKO
	#define HC_BUILD_GCWII
	#define HC_BUILD_CONSOLE
	#ifndef HW_RVL
		#define HC_BUILD_LOWMEM
	#endif
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_SPLITSCREEN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
#elif defined __vita__
	#define HC_BUILD_PSVITA
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_TOUCH
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined _arch_dreamcast
	#define HC_BUILD_DREAMCAST
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_SPLITSCREEN
	#define HC_BUILD_SMALLSTACK
	#undef  HC_BUILD_RESOURCES
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
#elif defined PLAT_PS3
	#define HC_BUILD_PS3
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_SPLITSCREEN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined N64
	#define HC_BIG_ENDIAN
	#define HC_BUILD_N64
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#define HC_BUILD_SMALLSTACK
	#undef  HC_BUILD_RESOURCES
	#undef  HC_BUILD_NETWORKING
	#undef  HC_BUILD_FILESYSTEM
#elif defined PLAT_PS2
	#define HC_BUILD_PS2
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_SPLITSCREEN
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined PLAT_NDS
	#define HC_BUILD_NDS
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#define HC_BUILD_TOUCH
	#define HC_BUILD_SMALLSTACK
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#undef  HC_BUILD_ANIMATIONS /* Very costly in FPU less system */
	#undef  HC_BUILD_ADVLIGHTING
#elif defined __WIIU__
	#define HC_BUILD_WIIU
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_SPLITSCREEN
	#define HC_BUILD_TOUCH
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_AUD_BACKEND HC_AUD_BACKEND_OPENAL
#elif defined __SWITCH__
	#define HC_BUILD_SWITCH
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_TOUCH
	#define HC_BUILD_GLES
	#define HC_BUILD_EGL
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_BUILTIN
	#define DEFAULT_SSL_BACKEND HC_SSL_BACKEND_BEARSSL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_GL2
#elif defined PLAT_PS1
	#define HC_BUILD_PS1
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_TINYMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#undef  HC_BUILD_RESOURCES
	#undef  HC_BUILD_NETWORKING
	#undef  HC_BUILD_ANIMATIONS /* Very costly in FPU less system */
	#undef  HC_BUILD_HELDBLOCK  /* Very costly in FPU less system */
	#undef  HC_BUILD_ADVLIGHTING
	#undef  HC_BUILD_FILESYSTEM
#elif defined OS2
	#define HC_BUILD_OS2
	#define HC_BUILD_POSIX
	#define HC_BUILD_FREETYPE
	#define DEFAULT_NET_BACKEND HC_NET_BACKEND_LIBCURL
	#define DEFAULT_GFX_BACKEND HC_GFX_BACKEND_SOFTGPU
	#define DEFAULT_WIN_BACKEND HC_WIN_BACKEND_SDL2
#elif defined PLAT_SATURN
	#define HC_BUILD_SATURN
	#define HC_BUILD_CONSOLE
	#define HC_BUILD_LOWMEM
	#define HC_BUILD_TINYMEM
	#define HC_BUILD_COOPTHREADED
	#define HC_BUILD_NOMUSIC
	#define HC_BUILD_NOSOUNDS
	#define HC_BUILD_SMALLSTACK
	#define HC_BUILD_TINYSTACK
	#undef  HC_BUILD_RESOURCES
	#undef  HC_BUILD_NETWORKING
	#undef  HC_BUILD_ANIMATIONS /* Very costly in FPU less system */
	#undef  HC_BUILD_HELDBLOCK  /* Very costly in FPU less system */
	#undef  HC_BUILD_ADVLIGHTING
	#undef  HC_BUILD_FILESYSTEM
#endif
#endif

/* Use platform default unless override is provided via command line/makefile/etc */
#if defined DEFAULT_WIN_BACKEND && !defined HC_WIN_BACKEND
	#define HC_WIN_BACKEND DEFAULT_WIN_BACKEND
#endif
#if defined DEFAULT_GFX_BACKEND && !defined HC_GFX_BACKEND
	#define HC_GFX_BACKEND DEFAULT_GFX_BACKEND
#endif
#if defined DEFAULT_SSL_BACKEND && !defined HC_SSL_BACKEND
	#define HC_SSL_BACKEND DEFAULT_SSL_BACKEND
#endif
#if defined DEFAULT_NET_BACKEND && !defined HC_NET_BACKEND
	#define HC_NET_BACKEND DEFAULT_NET_BACKEND
#endif
#if defined DEFAULT_AUD_BACKEND && !defined HC_AUD_BACKEND
	#define HC_AUD_BACKEND DEFAULT_AUD_BACKEND
#endif

#ifdef HC_BUILD_CONSOLE
#undef HC_BUILD_FREETYPE
#undef HC_BUILD_PLUGINS
#ifndef CC_UPDATE
#define CC_UPDATE
#endif
#endif

#ifdef HC_BUILD_NETWORKING
#define CUSTOM_MODELS
#endif
#ifndef HC_BUILD_LOWMEM
#define EXTENDED_BLOCKS
#endif
#define EXTENDED_TEXTURES

#ifdef EXTENDED_BLOCKS
typedef hc_uint16 BlockID;
#else
typedef hc_uint8 BlockID;
#endif

#ifdef EXTENDED_TEXTURES
typedef hc_uint16 TextureLoc;
#else
typedef hc_uint8 TextureLoc;
#endif

typedef hc_uint8 BlockRaw;
typedef hc_uint8 EntityID;
typedef hc_uint8 Face;
typedef hc_uint32 hc_result;

typedef hc_uint64 TimeMS;
typedef union hc_pointer_ { hc_uintptr val; void* ptr; } hc_pointer;

typedef struct Rect2D_  { int x, y, width, height; } Rect2D;
typedef struct TextureRec_ { float u1, v1, u2, v2; } TextureRec;

typedef struct hc_string_ {
	char* buffer;       /* Pointer to characters, NOT NULL TERMINATED */
	hc_uint16 length;   /* Number of characters used */
	hc_uint16 capacity; /* Max number of characters  */
} hc_string;
/* Indicates that a reference to the buffer in a string argument is persisted after the function has completed.
Thus it is **NOT SAFE** to allocate a string on the stack. */
#define STRING_REF

typedef void* GfxResourceID;

/* Contains the information to describe a 2D textured quad. */
struct Texture {
	GfxResourceID ID;
	short x, y; hc_uint16 width, height;
	TextureRec uv;
};

#ifdef __cplusplus
	#define HC_BEGIN_HEADER extern "C" {
	#define HC_END_HEADER }
#else
	#define HC_BEGIN_HEADER
	#define HC_END_HEADER
#endif
typedef hc_int8 cc_int8;
typedef hc_int16 cc_int16;
typedef hc_int32 cc_int32;
typedef hc_int64 cc_int64;
typedef hc_uint8 cc_uint8;
typedef hc_uint16 cc_uint16;
typedef hc_uint32 cc_uint32;
typedef hc_uint64 cc_uint64;
typedef hc_uintptr cc_uintptr;
typedef hc_string cc_string;
typedef hc_pointer cc_pointer;
typedef hc_result cc_result;
typedef hc_codepoint cc_codepoint;
typedef hc_unichar cc_unichar;
typedef hc_bool cc_bool;
#define CC_INLINE HC_INLINE
#define CC_NOINLINE HC_NOINLINE
#define CC_API HC_API
#define CC_VAR HC_VAR
#define CC_HAS_TYPES HC_HAS_TYPES
#define CC_HAS_MISC HC_HAS_MISC
#endif

