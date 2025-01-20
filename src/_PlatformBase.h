#include "Platform.h"
#include "String.h"
#include "Logger.h"
#include "Constants.h"
#include "Errors.h"

/*########################################################################################################################*
*---------------------------------------------------------Memory----------------------------------------------------------*
*#########################################################################################################################*/
int Mem_Equal(const void* a, const void* b, hc_uint32 numBytes) {
	const hc_uint8* src = (const hc_uint8*)a;
	const hc_uint8* dst = (const hc_uint8*)b;

	while (numBytes--) { 
		if (*src++ != *dst++) return false; 
	}
	return true;
}

HC_NOINLINE static void AbortOnAllocFailed(const char* place) {	
	hc_string log; char logBuffer[STRING_SIZE+20 + 1];
	String_InitArray_NT(log, logBuffer);

	String_Format1(&log, "Out of memory! (when allocating %c)", place);
	log.buffer[log.length] = '\0';
	Logger_Abort(log.buffer);
}

void* Mem_Alloc(hc_uint32 numElems, hc_uint32 elemsSize, const char* place) {
	void* ptr = Mem_TryAlloc(numElems, elemsSize);
	if (!ptr) AbortOnAllocFailed(place);
	return ptr;
}

void* Mem_AllocCleared(hc_uint32 numElems, hc_uint32 elemsSize, const char* place) {
	void* ptr = Mem_TryAllocCleared(numElems, elemsSize);
	if (!ptr) AbortOnAllocFailed(place);
	return ptr;
}

void* Mem_Realloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize, const char* place) {
	void* ptr = Mem_TryRealloc(mem, numElems, elemsSize);
	if (!ptr) AbortOnAllocFailed(place);
	return ptr;
}

static HC_NOINLINE hc_uint32 CalcMemSize(hc_uint32 numElems, hc_uint32 elemsSize) {
	hc_uint32 numBytes;
	if (!numElems || !elemsSize) return 1; /* treat 0 size as 1 byte */
	
	numBytes = numElems * elemsSize;
	if (numBytes / elemsSize != numElems) return 0; /* Overflow */
	return numBytes;
}


/*########################################################################################################################*
*--------------------------------------------------------Logging----------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log1(const char* format, const void* a1) {
	Platform_Log4(format, a1, NULL, NULL, NULL);
}
void Platform_Log2(const char* format, const void* a1, const void* a2) {
	Platform_Log4(format, a1, a2, NULL, NULL);
}
void Platform_Log3(const char* format, const void* a1, const void* a2, const void* a3) {
	Platform_Log4(format, a1, a2, a3, NULL);
}

void Platform_Log4(const char* format, const void* a1, const void* a2, const void* a3, const void* a4) {
	hc_string msg; char msgBuffer[512];
	String_InitArray(msg, msgBuffer);

	String_Format4(&msg, format, a1, a2, a3, a4);
	Platform_Log(msg.buffer, msg.length);
}

void Platform_LogConst(const char* message) {
	Platform_Log(message, String_Length(message));
}

/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
static char gameArgs[GAME_MAX_CMDARGS][STRING_SIZE];
static int gameNumArgs;
static hc_bool gameHasArgs;

static hc_result SetGameArgs(const hc_string* args, int numArgs) {
	int i;
	for (i = 0; i < numArgs; i++) 
	{
		String_CopyToRawArray(gameArgs[i], &args[i]);
	}
	
	gameHasArgs = true;
	gameNumArgs = numArgs;
	return 0;
}

static int GetGameArgs(hc_string* args) {
	int i, count = gameNumArgs;
	for (i = 0; i < count; i++) 
	{
		args[i] = String_FromRawArray(gameArgs[i]);
	}
	
	/* clear arguments so after game is closed, launcher is started */
	gameNumArgs = 0;
	return count;
}


/*########################################################################################################################*
*----------------------------------------------------------Misc-----------------------------------------------------------*
*#########################################################################################################################*/
int Stopwatch_ElapsedMS(hc_uint64 beg, hc_uint64 end) {
	hc_uint64 raw = Stopwatch_ElapsedMicroseconds(beg, end);
	if (raw > Int32_MaxValue) return Int32_MaxValue / 1000;
	return (int)raw / 1000;
}

static HC_INLINE void SocketAddr_Set(hc_sockaddr* addr, const void* src, unsigned srcLen) {
	if (srcLen > HC_SOCKETADDR_MAXSIZE) Logger_Abort("Attempted to copy too large socket");

	Mem_Copy(addr->data, src, srcLen);
	addr->size = srcLen;
}

hc_result Socket_WriteAll(hc_socket socket, const hc_uint8* data, hc_uint32 count) {
	hc_uint32 sent;
	hc_result res;

	while (count)
	{
		if ((res = Socket_Write(socket, data, count, &sent))) return res;
		if (!sent) return ERR_END_OF_STREAM;

		data  += sent;
		count -= sent;
	}
	return 0;
}


/*########################################################################################################################*
*-------------------------------------------------------Dynamic lib-------------------------------------------------------*
*#########################################################################################################################*/
hc_result DynamicLib_Load(const hc_string* path, void** lib) {
	*lib = DynamicLib_Load2(path);
	return *lib == NULL;
}
hc_result DynamicLib_Get(void* lib, const char* name, void** symbol) {
	*symbol = DynamicLib_Get2(lib, name);
	return *symbol == NULL;
}


hc_bool DynamicLib_LoadAll(const hc_string* path, const struct DynamicLibSym* syms, int count, void** _lib) {
	int i, loaded = 0;
	void* addr;
	void* lib;

	lib   = DynamicLib_Load2(path);
	*_lib = lib;
	if (!lib) { Logger_DynamicLibWarn("loading", path); return false; }

	for (i = 0; i < count; i++) {
		addr = DynamicLib_Get2(lib, syms[i].name);
		if (addr) loaded++;
		*syms[i].symAddr = addr;
	}
	return loaded == count;
}
