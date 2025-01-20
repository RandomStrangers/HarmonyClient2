#include "Core.h"
#if defined HC_BUILD_MACCLASSIC

#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "SystemFonts.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "PackedCol.h"
#include <string.h>
#include <sys/time.h>

#undef true
#undef false
#include <MacMemory.h>
#include <Processes.h>
#include <Devices.h>
#include <Files.h>
#include <Gestalt.h>

const hc_result ReturnCode_FileShareViolation = 1000000000;
const hc_result ReturnCode_FileNotFound     = fnfErr;
const hc_result ReturnCode_DirectoryExists  = dupFNErr;
const hc_result ReturnCode_SocketInProgess  = 1000000;
const hc_result ReturnCode_SocketWouldBlock = 1000000;
const hc_result ReturnCode_SocketDropped    = 1000000;
static long sysVersion;

#if TARGET_CPU_68K
const char* Platform_AppNameSuffix = " (MAC 68k)";
#else
const char* Platform_AppNameSuffix = " (MAC PPC)";
#endif
hc_bool Platform_ReadonlyFilesystem;
hc_bool Platform_SingleProcess = true;


/*########################################################################################################################*
*---------------------------------------------------Imported headers------------------------------------------------------*
*#########################################################################################################################*/
// On 68k these are implemented using direct 68k opcodes
// On PPC these are implemented using function calls
#if TARGET_CPU_68K
	#define MAC_SYSAPI(_type) static _type
    #define MAC_ONEWORDINLINE(w1)           = w1
    #define MAC_TWOWORDINLINE(w1,w2)        = {w1, w2}
    #define MAC_THREEWORDINLINE(w1,w2,w3)   = {w1, w2, w3}
    #define MAC_FOURWORDINLINE(w1,w2,w3,w4) = {w1, w2, w3, w4}
#else
	#define MAC_SYSAPI(_type) extern pascal _type
    #define MAC_ONEWORDINLINE(w1)
    #define MAC_TWOWORDINLINE(w1,w2)
    #define MAC_THREEWORDINLINE(w1,w2,w3)
    #define MAC_FOURWORDINLINE(w1,w2,w3,w4)
#endif
typedef unsigned long MAC_FourCharCode;
static const int MAC_smSystemScript = -1;

// ==================================== IMPORTS FROM TIMER.H ====================================
// Availability: in InterfaceLib 7.1 and later
MAC_SYSAPI(void) Microseconds(hc_uint64* microTickCount) MAC_FOURWORDINLINE(0xA193, 0x225F, 0x22C8, 0x2280);

/*########################################################################################################################*
*---------------------------------------------------------Memory----------------------------------------------------------*
*#########################################################################################################################*/
void* Mem_Set(void*  dst, hc_uint8 value,  unsigned numBytes) { return memset( dst, value, numBytes); }
void* Mem_Copy(void* dst, const void* src, unsigned numBytes) { return memcpy( dst, src,   numBytes); }
void* Mem_Move(void* dst, const void* src, unsigned numBytes) { return memmove(dst, src,   numBytes); }

void* Mem_TryAlloc(hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? NewPtr(size) : NULL;
}

void* Mem_TryAllocCleared(hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? NewPtrClear(size) : NULL;
}

void* Mem_TryRealloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	if (!size) return NULL;
	if (!mem)  return NewPtr(size);

	// Try to resize in place
	MemError();
	SetPtrSize(mem, size);
	if (!MemError()) return mem;

	void* newMem = NewPtr(size);
	if (!newMem) return NULL;

	Mem_Copy(newMem, mem, GetPtrSize(mem));
	return newMem;
}

void Mem_Free(void* mem) {
	if (mem) DisposePtr(mem);
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Console_Write(const char* msg, int len);

void Platform_Log(const char* msg, int len) {
	Console_Write(msg, len);
}

// classic macOS uses an epoch of 1904
#define EPOCH_ADJUSTMENT 2082866400UL

static time_t gettod(void) {
    unsigned long secs;
    GetDateTime(&secs);
	return secs - EPOCH_ADJUSTMENT;
}

TimeMS DateTime_CurrentUTC(void) {
	time_t secs = gettod();
	return (cc_uint64)secs + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	struct tm loc_time;
	time_t secs = gettod();
	localtime_r(&secs, &loc_time);

	t->year   = loc_time.tm_year + 1900;
	t->month  = loc_time.tm_mon  + 1;
	t->day    = loc_time.tm_mday;
	t->hour   = loc_time.tm_hour;
	t->minute = loc_time.tm_min;
	t->second = loc_time.tm_sec;
}


/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
#define MS_PER_SEC 1000000ULL

hc_uint64 Stopwatch_Measure(void) {
	hc_uint64 count;
	if (sysVersion < 0x7000) {
		// 60 ticks a second
		count = TickCount();
		return count * MS_PER_SEC / 60;
	}

	Microseconds(&count);
	return count;
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static int retrievedWD, wd_refNum, wd_dirID;

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* buf = dst->buffer;
	char* str = dst->buffer;
	str++; // placeholder for length later
	*str++ = ':';
	
	// Classic Mac OS uses : to separate directories
	for (int i = 0; i < path->length; i++) 
	{
		char c = (char)path->buffer[i];
		if (c == '/') c = ':';
		*str++ = c;
	}
	*str   = '\0';
	buf[0] = String_Length(buf + 1); // pascal strings

	if (retrievedWD) return;
	retrievedWD = true;

	WDPBRec r = { 0 };
	PBHGetVolSync(&r);
	wd_refNum = r.ioWDVRefNum;
	wd_dirID  = r.ioWDDirID;

	int V = r.ioWDVRefNum, D = r.ioWDDirID;
	Platform_Log2("Working directory: %i, %i", &V, &D);
}

static int DoOpenDF(const char* name, char perm, hc_file* file) {
    HParamBlockRec pb;
	Mem_Set(&pb, 0, sizeof(pb));

	pb.fileParam.ioVRefNum = wd_refNum;
	pb.fileParam.ioDirID   = wd_dirID;
	pb.fileParam.ioNamePtr = name;
	pb.ioParam.ioPermssn   = perm;

	int err = PBHOpenDFSync(&pb);
	*file   = pb.ioParam.ioRefNum;
	return err;
}

static int DoCreateFile(const char* name) {
    HParamBlockRec pb;
	Mem_Set(&pb, 0, sizeof(pb));

	pb.fileParam.ioVRefNum = wd_refNum;
	pb.fileParam.ioDirID   = wd_dirID;
	pb.fileParam.ioNamePtr = name;

    return PBHCreateSync(&pb);
}

static int DoCreateFolder(const char* name) {
    HParamBlockRec pb;
	Mem_Set(&pb, 0, sizeof(pb));

	pb.fileParam.ioVRefNum = wd_refNum;
	pb.fileParam.ioDirID   = wd_dirID;
	pb.fileParam.ioNamePtr = name;

    return PBDirCreateSync(&pb);
}


void Directory_GetCachePath(hc_string* path) { }

hc_result Directory_Create(const hc_filepath* path) {
	return DoCreateFolder(path->buffer);
}

int File_Exists(const hc_filepath* path) {
	return false; // TODO
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return DoOpenDF(path->buffer, fsRdPerm, file);
}

hc_result File_Create(hc_file* file, const hc_filepath* path) {
	int res = DoCreateFile(path->buffer);
	if (res && res != dupFNErr) return res;

	return DoOpenDF(path->buffer, fsWrPerm, file);
}

hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	int res = DoCreateFile(path->buffer);
	if (res && res != dupFNErr) return res;

	return DoOpenDF(path->buffer, fsRdWrPerm, file);
}

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	ParamBlockRec pb;
	pb.ioParam.ioRefNum   = file;
	pb.ioParam.ioBuffer   = data;
	pb.ioParam.ioReqCount = count;
	pb.ioParam.ioPosMode  = fsAtMark;

	int err = PBReadSync(&pb);
	*bytesRead = pb.ioParam.ioActCount;
	return err;
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	ParamBlockRec pb;
	pb.ioParam.ioRefNum   = file;
	pb.ioParam.ioBuffer   = data;
	pb.ioParam.ioReqCount = count;
	pb.ioParam.ioPosMode  = fsAtMark;

	int err = PBWriteSync(&pb);
	*bytesWrote = pb.ioParam.ioActCount;
	return err;
}

hc_result File_Close(hc_file file) {
	ParamBlockRec pb;
	pb.ioParam.ioRefNum = file;

	return PBCloseSync(&pb);
}

hc_result File_Seek(hc_file file, int offset, int seekType) {
	static hc_uint8 modes[] = { fsFromStart, fsFromMark, fsFromLEOF };
	ParamBlockRec pb;
	pb.ioParam.ioRefNum    = file;
	pb.ioParam.ioPosMode   = modes[seekType];
	pb.ioParam.ioPosOffset = offset;

	return PBSetFPosSync(&pb);
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	ParamBlockRec pb;
	pb.ioParam.ioRefNum = file;

	int err = PBGetFPosSync(&pb);
	*pos    = pb.ioParam.ioPosOffset;
	return err;
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	ParamBlockRec pb;
	pb.ioParam.ioRefNum = file;

	int err = PBGetEOFSync(&pb);
	*len    = (hc_uint32)pb.ioParam.ioMisc;
	return err;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) {
	long delay = milliseconds * 1000 / 60;
	long final;
	Delay(delay, &final);
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	*handle = NULL;
	// TODO
}

void Thread_Detach(void* handle) {
	// TODO
}

void Thread_Join(void* handle) {
	// TODO
}

void* Mutex_Create(const char* name) {
	return NULL;
}

void Mutex_Free(void* handle) {
	// TODO
}

void Mutex_Lock(void* handle) {
	// TODO
}

void Mutex_Unlock(void* handle) {
	// TODO
}

void* Waitable_Create(const char* name) {
	return NULL;
}

void Waitable_Free(void* handle) {
	// TODO
}

void Waitable_Signal(void* handle) {
	// TODO
}

void Waitable_Wait(void* handle) {
	// TODO
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	// TODO
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	return ERR_NOT_SUPPORTED;
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	return ERR_NOT_SUPPORTED;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	return ERR_NOT_SUPPORTED;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	return ERR_NOT_SUPPORTED;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	return ERR_NOT_SUPPORTED;
}

void Socket_Close(hc_socket s) {

}

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	return ERR_NOT_SUPPORTED;
}

hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
hc_bool Process_OpenSupported = false;
static char gameArgs[GAME_MAX_CMDARGS][STRING_SIZE];
static int gameNumArgs;

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, hc_string* args) {
	int count = gameNumArgs;
	for (int i = 0; i < count; i++) 
	{
		args[i] = String_FromRawArray(gameArgs[i]);
	}
	
	// clear arguments so after game is closed, launcher is started
	gameNumArgs = 0;
	return count;
}

hc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) {
	return 0;
}

hc_result Process_StartGame2(const hc_string* args, int numArgs) {
	for (int i = 0; i < numArgs; i++) 
	{
		String_CopyToRawArray(gameArgs[i], &args[i]);
	}

	gameNumArgs = numArgs;
	return 0;
}

void Process_Exit(hc_result code) { 
	ExitToShell();
    for(;;) { }
}

hc_result Process_StartOpen(const hc_string* args) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*--------------------------------------------------------Updater----------------------------------------------------------*
*#########################################################################################################################*/
hc_bool Updater_Supported = false;
hc_bool Updater_Clean(void) { return true; }

const struct UpdaterInfo Updater_Info = { "&eCompile latest source code to update", 0 };

hc_result Updater_Start(const char** action) {
	return ERR_NOT_SUPPORTED;
}

hc_result Updater_GetBuildTime(hc_uint64* timestamp) {
	return ERR_NOT_SUPPORTED;
}

hc_result Updater_MarkExecutable(void) {
	return ERR_NOT_SUPPORTED;
}

hc_result Updater_SetNewBuildTime(hc_uint64 timestamp) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*-------------------------------------------------------Dynamic lib-------------------------------------------------------*
*#########################################################################################################################*/
const hc_string DynamicLib_Ext = String_FromConst(".dylib");

void* DynamicLib_Load2(const hc_string* path) {
	return NULL;
}

void* DynamicLib_Get2(void* lib, const char* name) {
	return NULL;
}

hc_bool DynamicLib_DescribeError(hc_string* dst) {
	return false;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Free(void) { }

hc_bool Platform_DescribeError(hc_result res, hc_string* dst) {
	// TODO
	return false;
}

void Platform_Init(void) {
	Gestalt(gestaltSystemVersion, &sysVersion);
	Platform_Log1("Running on Mac OS %h", &sysVersion);

	hc_string path = String_FromConst("aB.txt");
	hc_filepath str;
	Platform_EncodePath(&str, &path);

	int ERR2 = DoCreateFile(&str);
	Platform_Log1("TEST FILE: %i", &ERR2);
}

hc_result Platform_Encrypt(const void* data, int len, hc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

hc_result Platform_Decrypt(const void* data, int len, hc_string* dst) {
	return ERR_NOT_SUPPORTED;
}
#endif
