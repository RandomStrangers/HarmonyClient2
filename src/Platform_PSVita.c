#include "Core.h"
#if defined HC_BUILD_PSVITA
#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vitasdk.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; // not used
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = SCE_NET_ERROR_EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = SCE_NET_ERROR_EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = SCE_NET_ERROR_EPIPE;
static int epoll_id;
static int stdout_fd;

const char* Platform_AppNameSuffix = " (PS Vita)";
hc_bool Platform_ReadonlyFilesystem;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}

void Platform_Log(const char* msg, int len) {
	if (!stdout_fd) stdout_fd = sceKernelGetStdout();
	sceIoWrite(stdout_fd, msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	struct SceKernelTimeval cur;
	sceKernelLibcGettimeofday(&cur, NULL);
	return (hc_uint64)cur.sec + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	SceDateTime curTime;
	sceRtcGetCurrentClockLocalTime(&curTime);

	t->year   = curTime.year;
	t->month  = curTime.month;
	t->day    = curTime.day;
	t->hour   = curTime.hour;
	t->minute = curTime.minute;
	t->second = curTime.second;
}

#define US_PER_SEC 1000000ULL
hc_uint64 Stopwatch_Measure(void) {
	// TODO: sceKernelGetSystemTimeWide
	struct SceKernelTimeval cur;
	sceKernelLibcGettimeofday(&cur, NULL);
	return (hc_uint64)cur.sec * US_PER_SEC + cur.usec;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("ux0:data/HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

#define GetSCEResult(result) (result >= 0 ? 0 : result & 0xFFFF)

hc_result Directory_Create(const hc_filepath* path) {
	int result = sceIoMkdir(path->buffer, 0777);
	return GetSCEResult(result);
}

int File_Exists(const hc_filepath* path) {
	SceIoStat sb;
	return sceIoGetstat(path->buffer, &sb) == 0 && SCE_S_ISREG(sb.st_mode) != 0;
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
	int res;

	Platform_EncodePath(&str, dirPath);
	SceUID uid = sceIoDopen(str.buffer);
	if (uid < 0) return GetSCEResult(uid); // error

	String_InitArray(path, pathBuffer);
	SceIoDirent entry;

	while ((res = sceIoDread(uid, &entry)) > 0) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		// ignore . and .. entry (PSP does return them)
		char* src = entry.d_name;
		if (src[0] == '.' && src[1] == '\0')                  continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;
		
		int len = String_Length(src);
		String_AppendUtf8(&path, src, len);

		int is_dir = entry.d_stat.st_attr & SCE_SO_IFDIR;
		callback(&path, obj, is_dir);
	}

	sceIoDclose(uid);
	return GetSCEResult(res);
}

static hc_result File_Do(hc_file* file, const char* path, int mode) {
	int result = sceIoOpen(path, mode, 0777);
	*file      = result;
	return GetSCEResult(result);
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SCE_O_RDONLY);
}
hc_result File_Create(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC);
}
hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SCE_O_RDWR | SCE_O_CREAT);
}

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	int result = sceIoRead(file, data, count);
	*bytesRead = result;
	return GetSCEResult(result);
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	int result  = sceIoWrite(file, data, count);
	*bytesWrote = result;
	return GetSCEResult(result);
}

hc_result File_Close(hc_file file) {
	int result = sceIoClose(file);
	return GetSCEResult(result);
}

hc_result File_Seek(hc_file file, int offset, int seekType) {
	static hc_uint8 modes[3] = { SCE_SEEK_SET, SCE_SEEK_CUR, SCE_SEEK_END };
	
	int result = sceIoLseek32(file, offset, modes[seekType]);
	return GetSCEResult(result);
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	int result = sceIoLseek32(file, 0, SCE_SEEK_CUR);
	*pos       = result;
	return GetSCEResult(result);
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	int curPos = sceIoLseek32(file, 0, SCE_SEEK_CUR);
	if (curPos < 0) { *len = -1; return GetSCEResult(curPos); }
	
	*len = sceIoLseek32(file, 0, SCE_SEEK_END);
	sceIoLseek32(file, curPos, SCE_SEEK_SET); // restore position
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
// !!! NOTE: PSP uses cooperative multithreading (not preemptive multithreading) !!!
void Thread_Sleep(hc_uint32 milliseconds) { 
	sceKernelDelayThread(milliseconds * 1000); 
}

static int ExecThread(unsigned int argc, void *argv) {
	Thread_StartFunc* func = (Thread_StartFunc*)argv;
	(*func)();
	return 0;
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	#define HC_THREAD_PRIORITY 0x10000100
	#define HC_THREAD_ATTRS 0 // TODO PSP_THREAD_ATTR_VFPU?
	Thread_StartFunc func_ = func;
	
	int threadID = sceKernelCreateThread(name, ExecThread, HC_THREAD_PRIORITY, 
										stackSize, HC_THREAD_ATTRS, 0, NULL);
																				
	*handle = (int)threadID;
	sceKernelStartThread(threadID, sizeof(func_), (void*)&func_);
}

void Thread_Detach(void* handle) {
	sceKernelDeleteThread((int)handle); // TODO don't call this??
}

void Thread_Join(void* handle) {
	sceKernelWaitThreadEnd((int)handle, NULL, NULL);
	sceKernelDeleteThread((int)handle);
}

void* Mutex_Create(const char* name) {
	SceKernelLwMutexWork* ptr = (SceKernelLwMutexWork*)Mem_Alloc(1, sizeof(SceKernelLwMutexWork), "mutex");
	int res = sceKernelCreateLwMutex(ptr, name, 0, 0, NULL);
	if (res) Logger_Abort2(res, "Creating mutex");
	return ptr;
}

void Mutex_Free(void* handle) {
	int res = sceKernelDeleteLwMutex((SceKernelLwMutexWork*)handle);
	if (res) Logger_Abort2(res, "Destroying mutex");
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	int res = sceKernelLockLwMutex((SceKernelLwMutexWork*)handle, 1, NULL);
	if (res) Logger_Abort2(res, "Locking mutex");
}

void Mutex_Unlock(void* handle) {
	int res = sceKernelUnlockLwMutex((SceKernelLwMutexWork*)handle, 1);
	if (res) Logger_Abort2(res, "Unlocking mutex");
}

void* Waitable_Create(const char* name) {
	int evid = sceKernelCreateEventFlag(name, SCE_EVENT_WAITMULTIPLE, 0, NULL);
	if (evid < 0) Logger_Abort2(evid, "Creating waitable");
	return (void*)evid;
}

void Waitable_Free(void* handle) {
	sceKernelDeleteEventFlag((int)handle);
}

void Waitable_Signal(void* handle) {
	int res = sceKernelSetEventFlag((int)handle, 0x1);
	if (res < 0) Logger_Abort2(res, "Signalling event");
}

void Waitable_Wait(void* handle) {
	unsigned int match;
	int res = sceKernelWaitEventFlag((int)handle, 0x1, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR, &match, NULL);
	if (res < 0) Logger_Abort2(res, "Event wait");
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	SceUInt timeout = milliseconds * 1000;
	unsigned int match;
	int res = sceKernelWaitEventFlag((int)handle, 0x1, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR, &match, &timeout);
	if (res < 0) Logger_Abort2(res, "Event timed wait");
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	struct SceNetSockaddrIn* addr4 = (struct SceNetSockaddrIn*)addrs[0].data;
	char str[NATIVE_STR_LEN];
	char buf[1024];
	int rid, ret;
	
	String_EncodeUtf8(str, address);
	*numValidAddrs = 1;

	if (sceNetInetPton(SCE_NET_AF_INET, str, &addr4->sin_addr) <= 0) {
		/* Fallback to resolving as DNS name */
		rid = sceNetResolverCreate("HC resolver", NULL, 0);
		if (rid < 0) return ERR_INVALID_ARGUMENT;

		ret = sceNetResolverStartNtoa(rid, str, &addr4->sin_addr, 0, 0, 0);
		sceNetResolverDestroy(rid);
		if (ret) return ret;
	}
	
	addr4->sin_family = SCE_NET_AF_INET;
	addr4->sin_port   = sceNetHtons(port);
		
	addrs[0].size = sizeof(*addr4);
	return 0;
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct SceNetSockaddr* raw = (struct SceNetSockaddr*)addr->data;

	*s = sceNetSocket("HC socket", raw->sa_family, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
	if (*s < 0) return *s;

	if (nonblocking) {
		int on = 1;
		sceNetSetsockopt(*s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &on, sizeof(int));
	}
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	struct SceNetSockaddr* raw = (struct SceNetSockaddr*)addr->data;
	
	int res = sceNetConnect(s, raw, addr->size);
	return res;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int recvCount = sceNetRecv(s, data, count, 0);
	if (recvCount >= 0) { *modified = recvCount; return 0; }
	
	*modified = 0; 
	return recvCount;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int sentCount = sceNetSend(s, data, count, 0);
	if (sentCount >= 0) { *modified = sentCount; return 0; }
	
	*modified = 0; 
	return sentCount;
}

void Socket_Close(hc_socket s) {
	sceNetShutdown(s, SCE_NET_SHUT_RDWR);
	sceNetSocketClose(s);
}

static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	SceNetEpollEvent ev = { 0 };
	// to match select, closed socket still counts as readable
	int flags = mode == SOCKET_POLL_READ ? (SCE_NET_EPOLLIN | SCE_NET_EPOLLHUP) : SCE_NET_EPOLLOUT;
	int res, num_events;
	ev.data.fd = s;
	ev.events  = flags;
	
	if ((res = sceNetEpollControl(epoll_id, SCE_NET_EPOLL_CTL_ADD, s, &ev))) return res;	
	num_events = sceNetEpollWait(epoll_id, &ev, 1, 0);
	sceNetEpollControl(epoll_id, SCE_NET_EPOLL_CTL_DEL, s, NULL);

	if (num_events < 0)  return num_events;
	if (num_events == 0) { *success = false; return 0; }
	
	*success = (ev.events & flags) != 0;
	return 0;
}

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	return Socket_Poll(s, SOCKET_POLL_READ, readable);
}

hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	uint32_t resultSize = sizeof(uint32_t);
	hc_result res = Socket_Poll(s, SOCKET_POLL_WRITE, writable);
	if (res || *writable) return res;

	// https://stackoverflow.com/questions/29479953/so-error-value-after-successful-socket-operation
	sceNetGetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &res, &resultSize);
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
static char net_memory[512 * 1024] __attribute__ ((aligned (16))); // TODO is just 256 kb enough ?

static void InitNetworking(void) {
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);	
	SceNetInitParam param;
	
	param.memory = net_memory;
	param.size   = sizeof(net_memory);
	param.flags  = 0;
	
	int ret = sceNetInit(&param);
	if (ret < 0) Platform_Log1("Network init failed: %i", &ret);
}

void Platform_Init(void) {
	/*pspDebugSioInit();*/ 
	InitNetworking();
	epoll_id = sceNetEpollCreate("HC poll", 0);
	
	hc_filepath* root = FILEPATH_RAW(root_path.buffer);
	Directory_Create(root);
}
void Platform_Free(void) { }

hc_bool Platform_DescribeError(hc_result res, hc_string* dst) {
	char chars[NATIVE_STR_LEN];
	int len;

	/* For unrecognised error codes, strerror_r might return messages */
	/*  such as 'No error information', which is not very useful */
	/* (could check errno here but quicker just to skip entirely) */
	if (res >= 1000) return false;

	len = strerror_r(res, chars, NATIVE_STR_LEN);
	if (len == -1) return false;

	len = String_CalcLen(chars, NATIVE_STR_LEN);
	String_AppendUtf8(dst, chars, len);
	return true;
}

hc_bool Process_OpenSupported = false;
hc_result Process_StartOpen(const hc_string* args) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*-------------------------------------------------------Encryption--------------------------------------------------------*
*#########################################################################################################################*/
#define MACHINE_KEY "VitaVitaVitaVita"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
