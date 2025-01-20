#include "Core.h"
#if defined HC_BUILD_WEB

#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "SystemFonts.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <emscripten.h>

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREAT  0x040
#define O_EXCL   0x080
#define O_TRUNC  0x200

/* Unfortunately, errno constants are different in some older emscripten versions */
/*  (linux errno numbers compared to WASI errno numbers) */
/* So just use the same errono numbers as interop_web.js */
#define _ENOENT        2
#define _EAGAIN        6 /* same as EWOULDBLOCK */
#define _EEXIST       17
#define _EHOSTUNREACH 23
#define _EINPROGRESS  26

const hc_result ReturnCode_FileShareViolation = 1000000000; /* Not used in web filesystem backend */
const hc_result ReturnCode_FileNotFound     = _ENOENT;
const hc_result ReturnCode_SocketInProgess  = _EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = _EAGAIN;
const hc_result ReturnCode_DirectoryExists  = _EEXIST;

const char* Platform_AppNameSuffix = "";
hc_bool Platform_ReadonlyFilesystem;
hc_bool Platform_SingleProcess;


/*########################################################################################################################*
*---------------------------------------------------------Memory----------------------------------------------------------*
*#########################################################################################################################*/
void* Mem_Set(void*  dst, hc_uint8 value,  hc_uint32 numBytes) { return memset( dst, value, numBytes); }
void* Mem_Copy(void* dst, const void* src, hc_uint32 numBytes) { return memcpy( dst, src,   numBytes); }
void* Mem_Move(void* dst, const void* src, hc_uint32 numBytes) { return memmove(dst, src,   numBytes); }

void* Mem_TryAlloc(hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? malloc(size) : NULL;
}

void* Mem_TryAllocCleared(hc_uint32 numElems, hc_uint32 elemsSize) {
	return calloc(numElems, elemsSize);
}

void* Mem_TryRealloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? realloc(mem, size) : NULL;
}

void Mem_Free(void* mem) {
	if (mem) free(mem);
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}

hc_uint64 Stopwatch_Measure(void) {
	/* time is a milliseconds double */
	/*  convert to microseconds */
	return (hc_uint64)(emscripten_get_now() * 1000);
}

extern void interop_Log(const char* msg, int len);
void Platform_Log(const char* msg, int len) {
	interop_Log(msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	struct timeval cur;
	gettimeofday(&cur, NULL);
	return (hc_uint64)cur.tv_sec + UNIX_EPOCH_SECONDS;
}

extern void interop_GetLocalTime(struct DateTime* t);
void DateTime_CurrentLocal(struct DateTime* t) {
	interop_GetLocalTime(t);
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	String_EncodeUtf8(str, path);
}

void Directory_GetCachePath(hc_string* path) { }

extern void interop_InitFilesystem(void);
hc_result Directory_Create(const hc_filepath* path) {
	/* Web filesystem doesn't need directories */
	return 0;
}

extern int interop_FileExists(const char* path);
int File_Exists(const hc_filepath* path) {
	return interop_FileExists(path->buffer);
}

static void* enum_obj;
static Directory_EnumCallback enum_callback;
EMSCRIPTEN_KEEPALIVE void Directory_IterCallback(const char* src) {
	hc_string path; char pathBuffer[FILENAME_SIZE];

	String_InitArray(path, pathBuffer);
	String_AppendUtf8(&path, src, String_Length(src));
	enum_callback(&path, enum_obj, false);
}

extern int interop_DirectoryIter(const char* path);
hc_result Directory_Enum(const hc_string* path, void* obj, Directory_EnumCallback callback) {
	hc_filepath str;
	Platform_EncodePath(&str, path);

	enum_obj      = obj;
	enum_callback = callback;
	/* returned result is negative for error */
	return -interop_DirectoryIter(str.buffer);
}

extern int interop_FileCreate(const char* path, int mode);
static hc_result File_Do(hc_file* file, const char* path, int mode) {
	int res = interop_FileCreate(path, mode);

	/* returned result is negative for error */
	if (res >= 0) {
		*file = res; return 0;
	} else {
		*file = -1; return -res;
	}
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, O_RDONLY);
}
hc_result File_Create(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}
hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

extern int interop_FileRead(int fd, void* data, int count);
hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	int res = interop_FileRead(file, data, count);

	/* returned result is negative for error */
	if (res >= 0) {
		*bytesRead = res; return 0;
	} else {
		*bytesRead = -1;   return -res;
	}
}

extern int interop_FileWrite(int fd, const void* data, int count);
hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	int res = interop_FileWrite(file, data, count);

	/* returned result is negative for error */
	if (res >= 0) {
		*bytesWrote = res; return 0;
	} else {
		*bytesWrote = -1;  return -res;
	}
}

extern int interop_FileClose(int fd);
hc_result File_Close(hc_file file) {
	/* returned result is negative for error */
	return -interop_FileClose(file);
}

extern int interop_FileSeek(int fd, int offset, int whence);
hc_result File_Seek(hc_file file, int offset, int seekType) {
	/* returned result is negative for error */
	int res = interop_FileSeek(file, offset, seekType);
	/* FileSeek returns current position, discard that */
	return res >= 0 ? 0 : -res;
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	/* FILE_SEEKFROM_CURRENT is same as SEEK_CUR */
	int res = interop_FileSeek(file, 0, FILE_SEEKFROM_CURRENT);
	/* returned result is negative for error */
	if (res >= 0) {
		*pos = res; return 0;
	} else {
		*pos = -1;  return -res;
	}
}

extern int interop_FileLength(int fd);
hc_result File_Length(hc_file file, hc_uint32* len) {
	int res = interop_FileLength(file);
	/* returned result is negative for error */
	if (res >= 0) {
		*len = res; return 0;
	} else {
		*len = -1;  return -res;
	}
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
/* No real threading support with emscripten backend */
void  Thread_Sleep(hc_uint32 milliseconds) { }

void* Mutex_Create(const char* name) { return NULL; }
void  Mutex_Free(void* handle) { }
void  Mutex_Lock(void* handle) { }
void  Mutex_Unlock(void* handle) { }

void* Waitable_Create(const char* name) { return NULL; }
void  Waitable_Free(void* handle) { }
void  Waitable_Signal(void* handle) { }
void  Waitable_Wait(void* handle) { }
void  Waitable_WaitFor(void* handle, hc_uint32 milliseconds) { }


/*########################################################################################################################*
*--------------------------------------------------------Font/Text--------------------------------------------------------*
*#########################################################################################################################*/
void Platform_LoadSysFonts(void) { }


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
extern void interop_InitSockets(void);

hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	int len = String_EncodeUtf8(addrs[0].data, address);
	/* TODO can this ever happen */
	if (len >= HC_SOCKETADDR_MAXSIZE) Logger_Abort("Overrun in Socket_ParseAddress");

	addrs[0].size  = port;
	*numValidAddrs = 1;
	return 0;
}

extern int interop_SocketCreate(void);
extern int interop_SocketConnect(int sock, const hc_uint8* host, int port);

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	*s = interop_SocketCreate();
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	/* size is used to store port number instead */
	/* returned result is negative for error */
	int res = -interop_SocketConnect(s, addr->data, addr->size);

	/* error returned when invalid address provided */
	if (res == _EHOSTUNREACH) return ERR_INVALID_ARGUMENT;
	return res;
}

extern int interop_SocketRecv(int sock, void* data, int len);
hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* read) {
	int res; 
	*read = 0;

	/* interop_SocketRecv only reads one WebSocket frame at most, hence call it multiple times */
	while (count) {
		/* returned result is negative for error */
		res = interop_SocketRecv(s, data, count);

		if (res >= 0) {
			*read += res;
			data  += res; count -= res;
		} else {
			/* EAGAIN when no more data available */
			if (res == -_EAGAIN) return *read == 0 ? _EAGAIN : 0;

			return -res;
		}
	}
	return 0;
}

extern int interop_SocketSend(int sock, const void* data, int len);
hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	/* returned result is negative for error */
	int res = interop_SocketSend(s, data, count);

	if (res >= 0) {
		*modified = res; return 0;
	} else {
		*modified = 0; return -res;
	}
}

extern int interop_SocketClose(int sock);
void Socket_Close(hc_socket s) {
	interop_SocketClose(s);
}

extern int interop_SocketWritable(int sock, hc_bool* writable);
hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	/* returned result is negative for error */
	return -interop_SocketWritable(s, writable);
}


/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
hc_bool Process_OpenSupported = true;

void Process_Exit(hc_result code) {
	/* 'Window' (i.e. the web canvas) isn't implicitly closed when process is exited */
	if (code) Window_RequestClose();
	/* game normally calls exit with code = 0 due to async IndexedDB loading */
	if (code) exit(code);
}

extern int interop_OpenTab(const char* url);
hc_result Process_StartOpen(const hc_string* args) {
	char str[NATIVE_STR_LEN];
	hc_result res;
	String_EncodeUtf8(str, args);

	res = interop_OpenTab(str);
	/* interop error code -> ClassiCube error code */
	if (res == 1) res = ERR_INVALID_OPEN_URL;
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
hc_bool Platform_DescribeError(hc_result res, hc_string* dst) {
	char* str;
	int len;

	/* For unrecognised error codes, strerror might return messages */
	/*  such as 'No error information', which is not very useful */
	if (res >= 1000) return false;

	str = strerror(res);
	if (!str) return false;

	len = String_CalcLen(str, NATIVE_STR_LEN);
	String_AppendUtf8(dst, str, len);
	return true;
}

EMSCRIPTEN_KEEPALIVE void Platform_LogError(const char* msg) {
	hc_string str = String_FromReadonly(msg);
	Logger_WarnFunc(&str);
}

extern void interop_InitModule(void);
void Platform_Init(void) {
	interop_InitModule();
	interop_InitFilesystem();
	interop_InitSockets();
}
void Platform_Free(void) { }


/*########################################################################################################################*
*-------------------------------------------------------Encryption--------------------------------------------------------*
*#########################################################################################################################*/
hc_result Platform_Encrypt(const void* data, int len, hc_string* dst) { return ERR_NOT_SUPPORTED; }
hc_result Platform_Decrypt(const void* data, int len, hc_string* dst) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*------------------------------------------------------Main driver--------------------------------------------------------*
*#########################################################################################################################*/
int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, hc_string* args) {
	int i, count;
	argc--; argv++; /* skip executable path argument */

	count = min(argc, GAME_MAX_CMDARGS);
	for (i = 0; i < count; i++) { args[i] = String_FromReadonly(argv[i]); }
	return count;
}


hc_result Platform_SetDefaultCurrentDirectory(int argc, char** argv) { return 0; }
static int _argc;
static char** _argv;

extern void interop_FS_Init(void);
extern void interop_DirectorySetWorking(const char* path);
extern void interop_AsyncDownloadTexturePack(const char* path);

int main(int argc, char** argv) {
	_argc = argc; _argv = argv;

	/* Game loads resources asynchronously, then actually starts itself */
	/* main */
	/*  > texture pack download (async) */
	/*     > load indexedDB (async) */
	/*        > web_main (game actually starts) */
	interop_FS_Init();
	interop_DirectorySetWorking("/harmonyclient");
	interop_AsyncDownloadTexturePack("texpacks/default.zip");
}

extern void interop_LoadIndexedDB(void);
extern void interop_AsyncLoadIndexedDB(void);
/* Asynchronous callback after texture pack is downloaded */
EMSCRIPTEN_KEEPALIVE void main_phase1(void) {
	interop_LoadIndexedDB(); /* legacy compatibility */
	interop_AsyncLoadIndexedDB();
}

extern int web_main(int argc, char** argv);
/* Asynchronous callback after IndexedDB is loaded */
EMSCRIPTEN_KEEPALIVE void main_phase2(void) {
	web_main(_argc, _argv);
}
#endif
