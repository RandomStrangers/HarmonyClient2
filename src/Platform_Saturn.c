#include "Core.h"
#if defined PLAT_SATURN

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
#include <yaul.h>
#include <mm/tlsf.h>

//#define OVERRIDE_MEM_FUNCTIONS
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

const char* Platform_AppNameSuffix  = " (Saturn)";
hc_bool Platform_ReadonlyFilesystem = true;


/*########################################################################################################################*
*---------------------------------------------------------Memory----------------------------------------------------------*
*#########################################################################################################################*/
static tlsf_t lwram_mem; // Use LWRAM for 1 MB of memory

static void InitMemory(void) {
	lwram_mem = tlsf_pool_create(LWRAM(0), LWRAM_SIZE);
}

/*void* Mem_TryAlloc(hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? tlsf_malloc(&lwram_mem, size) : NULL;
}

void* Mem_TryAllocCleared(hc_uint32 numElems, hc_uint32 elemsSize) {
	void* ptr = Mem_TryAlloc(numElems, elemsSize);
	if (ptr) memset(ptr, 0, numElems * elemsSize);
	return ptr;
}

void* Mem_TryRealloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 size = CalcMemSize(numElems, elemsSize);
	return size ? tlsf_realloc(&lwram_mem, mem, size) : NULL;
}

void Mem_Free(void* mem) {
	tlsf_free(&lwram_mem, mem);
}*/


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
#define WRITE_ADDRESS CS0(0x00100001)
// in Medafen, patch DummyWrite in src/ss/Cart.cpp to instead log the value

void Platform_Log(const char* msg, int len) {
	for (int i = 0; i < len; i++) {
		MEMORY_WRITE(8, WRITE_ADDRESS, msg[i]);
	}
	MEMORY_WRITE(8, WRITE_ADDRESS, '\n');
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
static volatile hc_uint32 overflow_count;

hc_uint64 Stopwatch_Measure(void) {
	return cpu_frt_count_get() + (overflow_count * 65536);
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	hc_uint32 delta = end - beg;

	// TODO still wrong?? and overflows?? and PAL detection ???
	return (delta * 1000) / CPU_FRT_NTSC_320_128_COUNT_1MS;
}

static void ovf_handler(void) { overflow_count++; }

static void Stopwatch_Init(void) {
	//cpu_frt_init(CPU_FRT_CLOCK_DIV_8);
	cpu_frt_init(CPU_FRT_CLOCK_DIV_128);
	cpu_frt_ovi_set(ovf_handler);

	cpu_frt_interrupt_priority_set(15);
	cpu_frt_count_set(0);
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	return ReturnCode_DirectoryExists;
}

int File_Exists(const hc_filepath* path) {
	return false;
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	return ERR_NOT_SUPPORTED;
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return ReturnCode_FileNotFound;
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
	hc_uint32 cycles = 26846 * milliseconds;
	
	for (hc_uint32 i = 0; i < cycles; i++)
	{
		__asm__ volatile ("nop;");
	}
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
extern void *__end;

void Platform_Init(void) {
	hc_uint32 avail = HWRAM(HWRAM_SIZE) - (uint32_t)&__end;
	Platform_Log1("Free HWRAM: %i bytes", &avail);

	InitMemory();
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
#define MACHINE_KEY "SaturnSaturnSEGA"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
