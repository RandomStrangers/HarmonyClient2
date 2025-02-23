#include "Core.h"
#if defined HC_BUILD_WIIU

#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "SystemFonts.h"
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
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <signal.h>
#include <stdio.h>
#include <poll.h>
#include <netdb.h>
#include <coreinit/debug.h>
#include <coreinit/event.h>
#include <coreinit/fastmutex.h>
#include <coreinit/thread.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include <whb/proc.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; /* TODO: not used apparently */
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = " (Wii U)";
hc_bool Platform_ReadonlyFilesystem;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char tmp[256 + 1];
	len = min(len, 256);
	Mem_Copy(tmp, msg, len); tmp[len] = '\0';
	
	OSReport("%s\n", tmp);
}
#define WIIU_EPOCH_ADJUST 946684800ULL // Wii U time epoch is year 2000, not 1970

TimeMS DateTime_CurrentUTC(void) {
	OSTime time   = OSGetTime();
	hc_int64 secs = (time_t)OSTicksToSeconds(time);
	return secs + UNIX_EPOCH_SECONDS + WIIU_EPOCH_ADJUST;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	struct OSCalendarTime loc_time;
	OSTicksToCalendarTime(OSGetTime(), &loc_time);

	t->year   = loc_time.tm_year;
	t->month  = loc_time.tm_mon  + 1;
	t->day    = loc_time.tm_mday;
	t->hour   = loc_time.tm_hour;
	t->minute = loc_time.tm_min;
	t->second = loc_time.tm_sec;
}


/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
hc_uint64 Stopwatch_Measure(void) {
	return OSGetSystemTime(); // TODO OSGetSystemTick ??
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return OSTicksToMicroseconds(end - beg);
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
// fs:/vol/external01
//const char* sd_root = WHBGetSdCardMountPath();
	//int sd_length = String_Length(sd_root);
	//Mem_Copy(str, sd_root, sd_length);
	//str   += sd_length;
	
static const hc_string root_path = String_FromConst("HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	/* read/write/search permissions for owner and group, and with read/search permissions for others. */
	/* TODO: Is the default mode in all cases */
	return mkdir(path->buffer, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1 ? errno : 0;
}

int File_Exists(const hc_filepath* path) {
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
	DIR* dirPtr;
	struct dirent* entry;
	char* src;
	int len, res, is_dir;

	Platform_EncodePath(&str, dirPath);
	dirPtr = opendir(str.buffer);
	if (!dirPtr) return errno;

	/* POSIX docs: "When the end of the directory is encountered, a null pointer is returned and errno is not changed." */
	/* errno is sometimes leftover from previous calls, so always reset it before readdir gets called */
	errno = 0;
	String_InitArray(path, pathBuffer);

	while ((entry = readdir(dirPtr))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		/* ignore . and .. entry */
		src = entry->d_name;
		if (src[0] == '.' && src[1] == '\0') continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;

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
	*file = open(path, mode, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	return *file == -1 ? errno : 0;
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path, O_RDONLY);
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
	static hc_uint8 modes[] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return lseek(file, offset, modes[seekType]) == -1 ? errno : 0;
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	*pos = lseek(file, 0, SEEK_CUR);
	return *pos == -1 ? errno : 0;
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	struct stat st;
	if (fstat(file, &st) == -1) { *len = -1; return errno; }
	*len = st.st_size; return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) { 
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
}

static int ExecThread(int argc, const char **argv) {
	((Thread_StartFunc)argv)(); 
	return 0;
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	OSThread* thread = (OSThread*)Mem_Alloc(1, sizeof(OSThread), "thread");
	void* stack = memalign(16, stackSize);
	
	OSCreateThread(thread, ExecThread,
                       1, (Thread_StartFunc)func,
                       stack + stackSize, stackSize,
                       16, OS_THREAD_ATTRIB_AFFINITY_ANY);

	*handle = thread;
	// TODO revisit this
	OSSetThreadRunQuantum(thread, 1000); // force yield after 1 millisecond
	OSResumeThread(thread);
}

void Thread_Detach(void* handle) {
	OSDetachThread((OSThread*)handle);
}

void Thread_Join(void* handle) {
	int result;
	OSJoinThread((OSThread*)handle, &result);
}

void* Mutex_Create(const char* name) {
	OSFastMutex* mutex = (OSFastMutex*)Mem_Alloc(1, sizeof(OSFastMutex), "mutex");
	
	OSFastMutex_Init(mutex, name);
	return mutex;
}

void Mutex_Free(void* handle) {
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	OSFastMutex_Lock((OSFastMutex*)handle);
}

void Mutex_Unlock(void* handle) {
	OSFastMutex_Unlock((OSFastMutex*)handle);
}

void* Waitable_Create(const char* name) {
	OSEvent* event = (OSEvent*)Mem_Alloc(1, sizeof(OSEvent), "waitable");

	OSInitEvent(event, false, OS_EVENT_MODE_AUTO);
	return event;
}

void Waitable_Free(void* handle) {
	OSResetEvent((OSEvent*)handle);
	Mem_Free(handle);
}

void Waitable_Signal(void* handle) {
	OSSignalEvent((OSEvent*)handle);
}

void Waitable_Wait(void* handle) {
	OSWaitEvent((OSEvent*)handle);
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	OSTime timeout = OSMillisecondsToTicks(milliseconds);
	OSWaitEventWithTimeout((OSEvent*)handle, timeout);
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
union SocketAddress {
	struct sockaddr raw;
	struct sockaddr_in v4;
	struct sockaddr_storage total;
};

static hc_result ParseHost(const char* host, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	char portRaw[32]; hc_string portStr;
	struct addrinfo hints = { 0 };
	struct addrinfo* result;
	struct addrinfo* cur;
	int res, i = 0;

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	String_InitArray(portStr,  portRaw);
	String_AppendInt(&portStr, port);
	portRaw[portStr.length] = '\0';

	res = getaddrinfo(host, portRaw, &hints, &result);
	if (res == EAI_AGAIN) return SOCK_ERR_UNKNOWN_HOST;
	if (res) return res;

	/* Prefer IPv4 addresses first */
	// TODO: Wii U has no ipv6 support anyways?
	for (cur = result; cur && i < SOCKET_MAX_ADDRS; cur = cur->ai_next) 
	{
		if (cur->ai_family != AF_INET) continue;
		SocketAddr_Set(&addrs[i], cur->ai_addr, cur->ai_addrlen); i++;
	}
	
	for (cur = result; cur && i < SOCKET_MAX_ADDRS; cur = cur->ai_next) 
	{
		if (cur->ai_family == AF_INET) continue;
		SocketAddr_Set(&addrs[i], cur->ai_addr, cur->ai_addrlen); i++;
	}

	freeaddrinfo(result);
	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
}

hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	union SocketAddress* addr = (union SocketAddress*)addrs[0].data;
	char str[NATIVE_STR_LEN];

	String_EncodeUtf8(str, address);
	*numValidAddrs = 0;

	if (inet_pton(AF_INET,  str, &addr->v4.sin_addr)  > 0) {
		addr->v4.sin_family = AF_INET;
		addr->v4.sin_port   = htons(port);
		
		addrs[0].size  = sizeof(addr->v4);
		*numValidAddrs = 1;
		return 0;
	}
	return ParseHost(str, port, addrs, numValidAddrs);
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = socket(raw->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (*s == -1) return errno;

	if (nonblocking) {
		int blocking_raw = -1; /* non-blocking mode */
		ioctl(*s, FIONBIO, &blocking_raw);
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
	int flags;

	pfd.fd     = s;
	pfd.events = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	if (poll(&pfd, 1, 0) == -1) { *success = false; return errno; }
	
	/* to match select, closed socket still counts as readable */
	flags    = mode == SOCKET_POLL_READ ? (POLLIN | POLLHUP) : POLLOUT;
	*success = (pfd.revents & flags) != 0;
	return 0;
}

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	return Socket_Poll(s, SOCKET_POLL_READ, readable);
}

hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	socklen_t resultSize = sizeof(socklen_t);
	hc_result res = Socket_Poll(s, SOCKET_POLL_WRITE, writable);
	if (res || *writable) return res;

	/* https://stackoverflow.com/questions/29479953/so-error-value-after-successful-socket-operation */
	getsockopt(s, SOL_SOCKET, SO_ERROR, &res, &resultSize);
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
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

void Platform_Init(void) {
	WHBProcInit();
	mkdir("HarmonyClient", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

hc_bool Process_OpenSupported = false;
hc_result Process_StartOpen(const hc_string* args) {
	return ERR_NOT_SUPPORTED;
}


/*########################################################################################################################*
*-------------------------------------------------------Encryption--------------------------------------------------------*
*#########################################################################################################################*/
#define MACHINE_KEY "WiiUWiiUWiiUWiiU"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
