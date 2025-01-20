#include "Core.h"
#if defined PLAT_PS1

#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "Options.h"
#include "PackedCol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <hwregs_c.h>
void exit(int code) { _boot(); }

// The SDK calloc doesn't zero memory, so need to override it
void* calloc(size_t num, size_t size) {
	void* ptr = malloc(num * size);
	if (ptr) memset(ptr, 0, num * size);
	return ptr;
}
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; // not used
const hc_result ReturnCode_FileNotFound       = 99999;
const hc_result ReturnCode_DirectoryExists    = 99999;

const hc_result ReturnCode_SocketInProgess  = -1;
const hc_result ReturnCode_SocketWouldBlock = -1;
const hc_result ReturnCode_SocketDropped    = -1;

const char* Platform_AppNameSuffix  = " (PS1)";
hc_bool Platform_ReadonlyFilesystem = true;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char tmp[2048 + 1];
	len = min(len, 2048);
	Mem_Copy(tmp, msg, len); tmp[len] = '\0';
	
	printf("%s\n", tmp);
}

TimeMS DateTime_CurrentUTC(void) {
	return 0;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	Mem_Set(t, 0, sizeof(struct DateTime));
}


/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
static volatile hc_uint32 irq_count;

hc_uint64 Stopwatch_Measure(void) {
	return irq_count;
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return (end - beg) * 1000;
}

static void timer2_handler(void) { irq_count++; }

static void Stopwatch_Init(void) {
	TIMER_CTRL(2)	= 0x0258;				// CLK/8 input, IRQ on reload
	TIMER_RELOAD(2)	= (F_CPU / 8) / 1000;	// 1000 Hz

	EnterCriticalSection();
	InterruptCallback(IRQ_TIMER2, &timer2_handler);
	ExitCriticalSection();
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("cdrom:/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	return ERR_NOT_SUPPORTED;
}

int File_Exists(const hc_filepath* path) {
	return false;
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Create(hc_file* file, const hc_filepath* path) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Close(hc_file file) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Seek(hc_file file, int offset, int seekType) {	
	return ERR_NOT_SUPPORTED;
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) {
	// TODO sleep a bit
	VSync(0);
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	*handle = NULL;
}

void Thread_Detach(void* handle) {
}

void Thread_Join(void* handle) {
}

void* Mutex_Create(const char* name) {
	return NULL;
}

void Mutex_Free(void* handle) {
}

void Mutex_Lock(void* handle) {
}

void Mutex_Unlock(void* handle) {
}

void* Waitable_Create(const char* name) {
	return NULL;
}

void Waitable_Free(void* handle) {
}

void Waitable_Signal(void* handle) {
}

void Waitable_Wait(void* handle) {
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
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
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) {
	ResetGraph(0);
	Stopwatch_Init();
}

void Platform_Free(void) { }

hc_bool Platform_DescribeError(hc_result res, hc_string* dst) {
	return false;
}

hc_bool Process_OpenSupported = false;
hc_result Process_StartOpen(const hc_string* args) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*-------------------------------------------------------Encryption--------------------------------------------------------*
*#########################################################################################################################*/
#define MACHINE_KEY "PS1_PS1_PS1_PS1_"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
