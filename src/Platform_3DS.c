#include "Core.h"
#if defined HC_BUILD_3DS
#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "PackedCol.h"

#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <stdio.h>
#include <malloc.h>
#include <netdb.h>
#include <3ds.h>
#include <citro3d.h>
#include "_PlatformConsole.h"

#define US_PER_SEC 1000000LL
#define NS_PER_MS 1000000LL

const hc_result ReturnCode_FileShareViolation = 1000000000; /* TODO: not used apparently */
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = " (3DS)";
hc_bool Platform_ReadonlyFilesystem;

// https://gbatemp.net/threads/homebrew-development.360646/page-245
// 3DS defaults to stack size of *32 KB*.. way too small
unsigned int __stacksize__ = 256 * 1024;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	// output to debug service (visible in Citra with log level set to "Debug.Emulated:Debug", or on console via remote gdb)
	svcOutputDebugString(msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	struct timeval cur;
	gettimeofday(&cur, NULL);
	return (cc_uint64)cur.tv_sec + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	struct timeval cur; 
	struct tm loc_time;
	gettimeofday(&cur, NULL);
	localtime_r(&cur.tv_sec, &loc_time);

	t->year   = loc_time.tm_year + 1900;
	t->month  = loc_time.tm_mon  + 1;
	t->day    = loc_time.tm_mday;
	t->hour   = loc_time.tm_hour;
	t->minute = loc_time.tm_min;
	t->second = loc_time.tm_sec;
}

hc_uint64 Stopwatch_Measure(void) {
	return svcGetSystemTick();
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;	
	// TODO: This doesn't seem to accurately measure time in Citra.
	// hopefully it works better on a real 3DS?
	
	// See CPU_TICKS_PER_USEC in libctru/include/3ds/os.h
	return (end - beg) * US_PER_SEC / SYSCLOCK_ARM11;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("sdmc:/3ds/HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	return mkdir(path->buffer, 0666) == -1 ? errno : 0; // FS has no permissions anyways
}

int File_Exists(const hc_filepath* path) {
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
	struct dirent* entry;
	char* src;
	int len, res, is_dir;

	Platform_EncodePath(&str, dirPath);
	DIR* dirPtr = opendir(str.buffer);
	if (!dirPtr) return errno;

	/* POSIX docs: "When the end of the directory is encountered, a null pointer is returned and errno is not changed." */
	/* errno is sometimes leftover from previous calls, so always reset it before readdir gets called */
	errno = 0;
	String_InitArray(path, pathBuffer);

	while ((entry = readdir(dirPtr))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);
		src = entry->d_name;

		len = String_Length(src);
		String_AppendUtf8(&path, src, len);
		is_dir = entry->d_type == DT_DIR;
		/* TODO: fallback to stat when this fails */

		callback(&path, obj, is_dir);
		errno = 0;
	}

	res = errno; /* return code from readdir */
	closedir(dirPtr);
	return res;
}

static hc_result File_Do(hc_file* file, const char* path, int mode) {
	*file = open(path, mode, 0666); // FS has no permissions anyways
	return *file == -1 ? errno : 0;
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

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	*bytesRead = read(file, data, count);
	return *bytesRead == -1 ? errno : 0;
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	*bytesWrote = write(file, data, count);
	return *bytesWrote == -1 ? errno : 0;
}

hc_result File_Close(hc_file file) {
	return close(file) == -1 ? errno : 0;
}

hc_result File_Seek(hc_file file, int offset, int seekType) {
	static hc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return lseek(file, offset, modes[seekType]) == -1 ? errno : 0;
}

hc_result File_Position(hc_file file, cc_uint32* pos) {
	*pos = lseek(file, 0, SEEK_CUR);
	return *pos == -1 ? errno : 0;
}

hc_result File_Length(hc_file file, cc_uint32* len) {
	struct stat st;
	if (fstat(file, &st) == -1) { *len = -1; return errno; }
	*len = st.st_size; return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) { 
	svcSleepThread(milliseconds * NS_PER_MS); 
}

static void Exec3DSThread(void* param) {
	((Thread_StartFunc)param)(); 
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	//TODO: Not quite correct, but eh
	*handle = threadCreate(Exec3DSThread, (void*)func, stackSize, 0x3f, -2, false);
}

void Thread_Detach(void* handle) {
	Thread thread = (Thread)handle;
	threadDetach(thread);
}

void Thread_Join(void* handle) {
	Thread thread = (Thread)handle;
	threadJoin(thread, U64_MAX);
	threadFree(thread);
}

void* Mutex_Create(const char* name) {
	LightLock* lock = (LightLock*)Mem_Alloc(1, sizeof(LightLock), "mutex");
	LightLock_Init(lock);
	return lock;
}

void Mutex_Free(void* handle) {
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	LightLock_Lock((LightLock*)handle);
}

void Mutex_Unlock(void* handle) {
	LightLock_Unlock((LightLock*)handle);
}

void* Waitable_Create(const char* name) {
	LightEvent* event = (LightEvent*)Mem_Alloc(1, sizeof(LightEvent), "waitable");
	LightEvent_Init(event, RESET_ONESHOT);
	return event;
}

void Waitable_Free(void* handle) {
	Mem_Free(handle);
}

void Waitable_Signal(void* handle) {
	LightEvent_Signal((LightEvent*)handle);
}

void Waitable_Wait(void* handle) {
	LightEvent_Wait((LightEvent*)handle);
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	s64 timeout_ns = milliseconds * (1000 * 1000); // milliseconds to nanoseconds
	LightEvent_WaitTimeout((LightEvent*)handle, timeout_ns);
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	char str[NATIVE_STR_LEN];
	char portRaw[32]; cc_string portStr;
	struct addrinfo hints = { 0 };
	struct addrinfo* result;
	struct addrinfo* cur;
	int i = 0;
	
	String_EncodeUtf8(str, address);
	*numValidAddrs = 0;
	struct sockaddr_in* addr4 = (struct sockaddr_in*)addrs[0].data;
	if (inet_aton(str, &addr4->sin_addr) > 0) {
		// TODO eliminate this path?
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		
		addrs[0].size  = sizeof(*addr4);
		*numValidAddrs = 1;
		return 0;
	}

	hints.ai_family   = AF_INET; // TODO: you need this, otherwise resolving dl.dropboxusercontent.com crashes in Citra. probably something to do with IPv6 addresses
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	String_InitArray(portStr,  portRaw);
	String_AppendInt(&portStr, port);
	portRaw[portStr.length] = '\0';

	int res = getaddrinfo(str, portRaw, &hints, &result);
	if (res == -NO_DATA) return SOCK_ERR_UNKNOWN_HOST;
	if (res) return res;

	for (cur = result; cur && i < SOCKET_MAX_ADDRS; cur = cur->ai_next, i++) 
	{
		if (!cur->ai_addrlen) break; 
		// TODO citra returns empty addresses past first one? does that happen on real hardware too?
		
		SocketAddr_Set(&addrs[i], cur->ai_addr, cur->ai_addrlen);
	}

	freeaddrinfo(result);
	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = socket(raw->sa_family, SOCK_STREAM, 0); // https://www.3dbrew.org/wiki/SOCU:socket
	if (*s == -1) return errno;

	if (nonblocking) {
		int flags = fcntl(*s, F_GETFL, 0);
		if (flags >= 0) fcntl(*s, F_SETFL, flags | O_NONBLOCK);
	}
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;
	
	int res = connect(s, raw, addr->size);
	return res == -1 ? errno : 0;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int recvCount = recv(s, data, count, 0);
	if (recvCount != -1) { *modified = recvCount; return 0; }
	*modified = 0; return errno;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int sentCount = send(s, data, count, 0);
	if (sentCount != -1) { *modified = sentCount; return 0; }
	
	*modified = 0; return errno;
}

void Socket_Close(hc_socket s) {
	shutdown(s, SHUT_RDWR);
	close(s);
}

static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	struct pollfd pfd;

	pfd.fd     = s;
	pfd.events = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	if (poll(&pfd, 1, 0) == -1) { *success = false; return errno; }
	
	/* to match select, closed socket still counts as readable */
	int flags = mode == SOCKET_POLL_READ ? (POLLIN | POLLHUP) : POLLOUT;
	*success  = (pfd.revents & flags) != 0;
	
	return 0;
}

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	return Socket_Poll(s, SOCKET_POLL_READ, readable);
}

hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	socklen_t resultSize = sizeof(socklen_t);
	hc_result res = Socket_Poll(s, SOCKET_POLL_WRITE, writable);
	if (res || *writable) return res;

	// Actual 3DS hardware returns INPROGRESS error code if connect is still in progress
	// Which is different from POSIX:
	//   https://stackoverflow.com/questions/29479953/so-error-value-after-successful-socket-operation
	getsockopt(s, SOL_SOCKET, SO_ERROR, &res, &resultSize);
	Platform_Log1("--write poll failed-- = %i", &res);
	if (res == -26) res = 0;
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
#define SOC_CTX_ALIGN 0x1000
#define SOC_CTX_SIZE  0x1000 * 128

static void CreateRootDirectory(const char* path) {
	// create root directories (no permissions anyways)
	int res = mkdir(path, 0666);
	if (res >= 0) return;
	
	int err = errno;
	Platform_Log2("mkdir %c FAILED: %i", path, &err);
}

void Platform_Init(void) {
	// Take full advantage of new 3DS if running on it
	osSetSpeedupEnable(true);
	
	// create root directories (no permissions anyways)
	CreateRootDirectory("sdmc:/3ds");
	CreateRootDirectory("sdmc:/3ds/HarmonyClient");
	
	// See https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/services/soc.h
	//  * @param context_addr Address of a page-aligned (0x1000) buffer to be used.
	//  * @param context_size Size of the buffer, a multiple of 0x1000.
	//  * @note The specified context buffer can no longer be accessed by the process which called this function, since the userland permissions for this block are set to no-access.
	void* buffer = memalign(SOC_CTX_ALIGN, SOC_CTX_SIZE);
	if (!buffer) return;
	socInit(buffer, SOC_CTX_SIZE);
}

void Platform_Free(void) {
	socExit();
	// TODO free soc buffer? probably no point
}

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
#define MACHINE_KEY "3DS_3DS_3DS_3DS_"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
