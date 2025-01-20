#include "Core.h"
#if defined HC_BUILD_SWITCH
#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "Options.h"
#include <switch.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; // not used
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = " (Switch)";
hc_bool Platform_ReadonlyFilesystem;

alignas(16) u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    int i;
    FILE *f = fopen("sdmc:/exception_dump", "w");
    if(f==NULL)return;

    fprintf(f, "error_desc: 0x%x\n", ctx->error_desc);//You can also parse this with ThreadExceptionDesc.
    //This assumes AArch64, however you can also use threadExceptionIsAArch64().
    for(i=0; i<29; i++)fprintf(f, "[X%d]: 0x%lx\n", i, ctx->cpu_gprs[i].x);
    fprintf(f, "fp: 0x%lx\n", ctx->fp.x);
    fprintf(f, "lr: 0x%lx\n", ctx->lr.x);
    fprintf(f, "sp: 0x%lx\n", ctx->sp.x);
    fprintf(f, "pc: 0x%lx\n", ctx->pc.x);

    //You could print fpu_gprs if you want.

    fprintf(f, "pstate: 0x%x\n", ctx->pstate);
    fprintf(f, "afsr0: 0x%x\n", ctx->afsr0);
    fprintf(f, "afsr1: 0x%x\n", ctx->afsr1);
    fprintf(f, "esr: 0x%x\n", ctx->esr);

    fprintf(f, "far: 0x%lx\n", ctx->far.x);

    fclose(f);
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	
	// See include/switch/arm/counter.h
	//   static inline u64 armTicksToNs(u64 tick) { return (tick * 625) / 12; }
	return ((end - beg) * 625) / 12000;
}

hc_uint64 Stopwatch_Measure(void) {
	return armGetSystemTick();
}

void Platform_Log(const char* msg, int len) {
	svcOutputDebugString(msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	u64 timestamp = 0;
	timeGetCurrentTime(TimeType_Default, &timestamp);
	return timestamp + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	u64 timestamp = 0;
	TimeCalendarTime calTime = { 0 };
	timeGetCurrentTime(TimeType_Default, &timestamp);
	timeToCalendarTimeWithMyRule(timestamp, &calTime, NULL);

	t->year   = calTime.year;
	t->month  = calTime.month;
	t->day    = calTime.day;
	t->hour   = calTime.hour;
	t->minute = calTime.minute;
	t->second = calTime.second;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("sdmc:/switch/HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	return mkdir(path->buffer, 0) == -1 ? errno : 0;
}

int File_Exists(const hc_filepath* path) {
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;;
	struct dirent* entry;
	int res;

	Platform_EncodePath(&str, dirPath);
	DIR* dirPtr = opendir(str.buffer);
	if (!dirPtr) return errno;

	// POSIX docs: "When the end of the directory is encountered, a null pointer is returned and errno is not changed."
	// errno is sometimes leftover from previous calls, so always reset it before readdir gets called
	errno = 0;
	String_InitArray(path, pathBuffer);

	while ((entry = readdir(dirPtr))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		// ignore . and .. entry
		char* src = entry->d_name;
		if (src[0] == '.' && src[1] == '\0') continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;

		int len = String_Length(src);
		String_AppendUtf8(&path, src, len);
		int is_dir = entry->d_type == DT_DIR;
		// TODO: fallback to stat when this fails

		callback(&path, obj, is_dir);
		errno = 0;
	}

	res = errno; // return code from readdir
	closedir(dirPtr);
	return res;
}

static hc_result File_Do(hc_file* file, const char* path, int mode) {
	*file = open(path, mode, 0);
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
	hc_uint64 timeout_ns = (hc_uint64)milliseconds * (1000 * 1000); // to nanoseconds
	svcSleepThread(timeout_ns);
}

static void ExecSwitchThread(void* param) {
	((Thread_StartFunc)param)(); 
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	Thread* thread = (Thread*)Mem_Alloc(1, sizeof(Thread), name);
	*handle = thread;

	threadCreate(thread, ExecSwitchThread, (void*)func, NULL, stackSize, 0x2C, -2);
	threadStart(thread);
}

void Thread_Detach(void* handle) {
	// threadClose frees up resources, **including the stack of the thread**
	//  Which obviously completely breaks the thread - so instead just accept
	//  that there will be a small memory leak when non-joined threads exit
}

void Thread_Join(void* handle) {
	Thread* thread = (Thread*)handle;
	threadWaitForExit(thread);
	threadClose(thread);
	Mem_Free(thread);
}

void* Mutex_Create(const char* name) {
	Mutex* mutex = (Mutex*)Mem_Alloc(1, sizeof(Mutex), "mutex");
	mutexInit(mutex);
	return mutex;
}

void Mutex_Free(void* handle) {
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	mutexLock((Mutex*)handle);
}

void Mutex_Unlock(void* handle) {
	mutexUnlock((Mutex*)handle);
}


struct WaitData {
	CondVar cond;
	Mutex mutex;
	int signalled; // For when Waitable_Signal is called before Waitable_Wait
};

void* Waitable_Create(const char* name) {
	struct WaitData* ptr = (struct WaitData*)Mem_Alloc(1, sizeof(struct WaitData), "waitable");
	
	mutexInit(&ptr->mutex);
	condvarInit(&ptr->cond);

	ptr->signalled = false;
	return ptr;
}

void Waitable_Free(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;
	Mem_Free(ptr);
}

void Waitable_Signal(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;

	Mutex_Lock(&ptr->mutex);
	condvarWakeOne(&ptr->cond);
	Mutex_Unlock(&ptr->mutex);

	ptr->signalled = true;
}

void Waitable_Wait(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;

	Mutex_Lock(&ptr->mutex);
	if (!ptr->signalled) {
		condvarWait(&ptr->cond, &ptr->mutex);
	}
	ptr->signalled = false;
	Mutex_Unlock(&ptr->mutex);
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	struct WaitData* ptr = (struct WaitData*)handle;
	hc_uint64 timeout_ns = (hc_uint64)milliseconds * (1000 * 1000); // to nanoseconds

	Mutex_Lock(&ptr->mutex);
	if (!ptr->signalled) {
		condvarWaitTimeout(&ptr->cond, &ptr->mutex, timeout_ns);
	}
	ptr->signalled = false;
	Mutex_Unlock(&ptr->mutex);
}
/*

void* Waitable_Create(const char* name) {
	LEvent* ptr = (LEvent*)Mem_Alloc(1, sizeof(LEvent), "waitable");
	leventInit(ptr, false, true);
	return ptr;
}

void Waitable_Free(void* handle) {
	LEvent* ptr = (LEvent*)handle;
	leventClear(ptr);
	Mem_Free(ptr);
}

void Waitable_Signal(void* handle) {
	//leventSignal((LEvent*)handle);
}

void Waitable_Wait(void* handle) {
	leventWait((LEvent*)handle, UINT64_MAX);
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	hc_uint64 timeout_ns = milliseconds * (1000 * 1000); // to nanoseconds
	leventWait((LEvent*)handle, timeout_ns);
}
*/

/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
union SocketAddress {
	struct sockaddr raw;
	struct sockaddr_in  v4;
	struct sockaddr_in6 v6;
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
	
	if (inet_pton(AF_INET6, str, &addr->v6.sin6_addr) > 0) {
		addr->v6.sin6_family = AF_INET6;
		addr->v6.sin6_port   = htons(port);
		
		addrs[0].size  = sizeof(addr->v6);
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
		fcntl(*s, F_SETFL, O_NONBLOCK);
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
static void CreateRootDirectory(void) {
	mkdir("sdmc:/switch", 0);
	int res = mkdir(root_path.buffer, 0);
	int err = res == -1 ? errno : 0;
	Platform_Log1("Created root directory: %i", &err);
}

void Platform_Init(void) {
	CreateRootDirectory();
	socketInitializeDefault();
}

void Platform_Free(void) {
	socketExit();
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
#define MACHINE_KEY "Nintendo__Switch"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
