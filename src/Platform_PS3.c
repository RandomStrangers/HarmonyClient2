#include "Core.h"
#if defined PLAT_PS3

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
#include <sys/time.h>
#include <net/net.h>
#include <net/poll.h>
#include <ppu-lv2.h>
#include <sys/file.h>
#include <sys/mutex.h>
#include <sys/sem.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <sys/tty.h>
#include <sys/process.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; // not used
const hc_result ReturnCode_FileNotFound     = 0x80010006; // ENOENT;
const hc_result ReturnCode_DirectoryExists  = 0x80010014; // EEXIST
const hc_result ReturnCode_SocketInProgess  = NET_EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = NET_EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = NET_EPIPE;

const char* Platform_AppNameSuffix = " (PS3)";
hc_bool Platform_ReadonlyFilesystem;

SYS_PROCESS_PARAM(1001, 256 * 1024); // 256kb stack size

/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	u32 done = 0;
	sysTtyWrite(STDOUT_FILENO, msg,  len, &done);
	sysTtyWrite(STDOUT_FILENO, "\n", 1,   &done);
}

TimeMS DateTime_CurrentUTC(void) {
	u64 sec, nanosec;
	sysGetCurrentTime(&sec, &nanosec);
	return sec + UNIX_EPOCH_SECONDS;
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


/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
#define NS_PER_SEC 1000000000ULL

hc_uint64 Stopwatch_Measure(void) { 
	u64 sec, nsec;
	sysGetCurrentTime(&sec, &nsec);
	return sec * NS_PER_SEC + nsec;
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return (end - beg) / 1000;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("/dev_hdd0/HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	/* read/write/search permissions for owner and group, and with read/search permissions for others. */
	/* TODO: Is the default mode in all cases */
	return sysLv2FsMkdir(path->buffer, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

int File_Exists(const hc_filepath* path) {
	sysFSStat sb;
	return sysLv2FsStat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
	sysFSDirent entry;
	char* src;
	int dir_fd, res;

	Platform_EncodePath(&str, dirPath);
	if ((res = sysLv2FsOpenDir(str.buffer, &dir_fd))) return res;

	for (;;)
	{
		u64 read = 0;
		if ((res = sysLv2FsReadDir(dir_fd, &entry, &read))) return res;
		if (!read) break; // end of entries

		// ignore . and .. entry
		src = entry.d_name;
		if (src[0] == '.' && src[1] == '\0') continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;
	
		String_InitArray(path, pathBuffer);
		String_Format1(&path, "%s/", dirPath);
		
		int len = String_Length(src);	
		String_AppendUtf8(&path, src, len);

		int is_dir = entry.d_type == DT_DIR;
		callback(&path, obj, is_dir);
	}

	sysLv2FsCloseDir(dir_fd);
	return res;
}

static hc_result File_Do(hc_file* file, const char* path, int mode) {
	int fd = -1;
	
	int access = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int res    = sysLv2FsOpen(path, mode, &fd, access, NULL, 0);
	
	if (res) {
		*file = -1; return res;
	} else {
		// TODO: is this actually needed?
		if (mode & SYS_O_CREAT) sysLv2FsChmod(path, access);
		*file = fd; return 0;
	}
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SYS_O_RDONLY);
}
hc_result File_Create(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SYS_O_RDWR | SYS_O_CREAT | SYS_O_TRUNC);
}
hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, SYS_O_RDWR | SYS_O_CREAT);
}

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	u64 read = 0;
	int res  = sysLv2FsRead(file, data, count, &read);
	
	*bytesRead = read;
	return res;
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	u64 wrote = 0;
	int res   = sysLv2FsWrite(file, data, count, &wrote);
	
	*bytesWrote = wrote;
	return res;
}

hc_result File_Close(hc_file file) {
	return sysLv2FsClose(file);
}

hc_result File_Seek(hc_file file, int offset, int seekType) {
	static hc_uint8 modes[] = { SEEK_SET, SEEK_CUR, SEEK_END };
	u64 position = 0;
	return sysLv2FsLSeek64(file, offset, modes[seekType], &position);
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	u64 position = 0;
	int res = sysLv2FsLSeek64(file, 0, SEEK_CUR, &position);
	
	*pos = position;
	return res;
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	sysFSStat st;
	int res = sysLv2FsFStat(file, &st);
	
	*len = st.st_size;
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) { 
	sysUsleep(milliseconds * 1000); 
}

static void ExecThread(void* param) {
	((Thread_StartFunc)param)(); 
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	sys_ppu_thread_t* thread = (sys_ppu_thread_t*)Mem_Alloc(1, sizeof(sys_ppu_thread_t), "thread");
	*handle = thread;
	
	int res = sysThreadCreate(thread, ExecThread, (void*)func,
			0, stackSize, THREAD_JOINABLE, name);
	if (res) Logger_Abort2(res, "Creating thread");
}

void Thread_Detach(void* handle) {
	sys_ppu_thread_t* thread = (sys_ppu_thread_t*)handle;
	int res = sysThreadDetach(*thread);
	if (res) Logger_Abort2(res, "Detaching thread");
	Mem_Free(thread);
}

void Thread_Join(void* handle) {
	u64 retVal;
	sys_ppu_thread_t* thread = (sys_ppu_thread_t*)handle;
	int res = sysThreadJoin(*thread, &retVal);
	if (res) Logger_Abort2(res, "Joining thread");
	Mem_Free(thread);
}

void* Mutex_Create(const char* name) {
	sys_mutex_attr_t attr;	
	sysMutexAttrInitialize(attr);
	
	sys_mutex_t* mutex = (sys_mutex_t*)Mem_Alloc(1, sizeof(sys_mutex_t), "mutex");
	int res = sysMutexCreate(mutex, &attr);
	if (res) Logger_Abort2(res, "Creating mutex");
	return mutex;
}

void Mutex_Free(void* handle) {
	sys_mutex_t* mutex = (sys_mutex_t*)handle;
	int res = sysMutexDestroy(*mutex);
	if (res) Logger_Abort2(res, "Destroying mutex");
	Mem_Free(mutex);
}

void Mutex_Lock(void* handle) {
	sys_mutex_t* mutex = (sys_mutex_t*)handle;
	int res = sysMutexLock(*mutex, 0);
	if (res) Logger_Abort2(res, "Locking mutex");
}

void Mutex_Unlock(void* handle) {
	sys_mutex_t* mutex = (sys_mutex_t*)handle;
	int res = sysMutexUnlock(*mutex);
	if (res) Logger_Abort2(res, "Unlocking mutex");
}

void* Waitable_Create(const char* name) {
	sys_sem_attr_t attr = { 0 };
	attr.attr_protocol  = SYS_SEM_ATTR_PROTOCOL;
	attr.attr_pshared   = SYS_SEM_ATTR_PSHARED; 
	
	sys_sem_t* sem = (sys_sem_t*)Mem_Alloc(1, sizeof(sys_sem_t), "waitable");
	int res = sysSemCreate(sem, &attr, 0, 1000000);
	if (res) Logger_Abort2(res, "Creating waitable");
	
	return sem;
}

void Waitable_Free(void* handle) {
	sys_sem_t* sem = (sys_sem_t*)handle;

	int res = sysSemDestroy(*sem);
	if (res) Logger_Abort2(res, "Destroying waitable");
	Mem_Free(sem);
}

void Waitable_Signal(void* handle) {
	sys_sem_t* sem = (sys_sem_t*)handle;
	int res = sysSemPost(*sem, 1);
	if (res) Logger_Abort2(res, "Signalling event");
}

void Waitable_Wait(void* handle) {
	sys_sem_t* sem = (sys_sem_t*)handle;
	int res = sysSemWait(*sem, 0);
	if (res) Logger_Abort2(res, "Waitable wait");
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	sys_sem_t* sem = (sys_sem_t*)handle;
	int res = sysSemWait(*sem, milliseconds * 1000);
	if (res) Logger_Abort2(res, "Waitable wait for");
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
union SocketAddress {
	struct sockaddr raw;
	struct sockaddr_in v4;
};
static hc_result ParseHost(char* host, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	struct net_hostent* res = netGetHostByName(host);
	struct sockaddr_in* addr4;
	if (!res) return net_h_errno;
	
	// Must have at least one IPv4 address
	if (res->h_addrtype != AF_INET) return ERR_INVALID_ARGUMENT;
	if (!res->h_addr_list)          return ERR_INVALID_ARGUMENT;
	
	// each address pointer is only 4 bytes long
	u32* addr_list = (u32*)res->h_addr_list;
	char* src_addr;
	int i;
	
	for (i = 0; i < SOCKET_MAX_ADDRS; i++) 
	{
		src_addr = (char*)addr_list[i];
		if (!src_addr) break;
		addrs[i].size = sizeof(struct sockaddr_in);
		addr4 = (struct sockaddr_in*)addrs[i].data;
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		addr4->sin_addr   = *(struct in_addr*)src_addr;
	}
	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
}

hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	struct sockaddr_in* addr4 = (struct sockaddr_in*)addrs[0].data;
	char str[NATIVE_STR_LEN];
	String_EncodeUtf8(str, address);
	*numValidAddrs = 0;

	if (inet_aton(str, &addr4->sin_addr) > 0) {
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		
		addrs[0].size  = sizeof(*addr4);
		*numValidAddrs = 1;
		return 0;
	}
	
	return ParseHost(str, port, addrs, numValidAddrs);
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = netSocket(raw->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (*s < 0) return net_errno;

	if (nonblocking) {
		int on = 1;
		netSetSockOpt(*s, SOL_SOCKET, SO_NBIO, &on, sizeof(int));
	}
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;
	
	int res = netConnect(s, raw, addr->size);
	return res < 0 ? net_errno : 0;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int res = netRecv(s, data, count, 0);
	if (res < 0) return net_errno;
	
	*modified = res; return 0;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int res = netSend(s, data, count, 0);
	if (res < 0) return net_errno;
	
	*modified = res; return 0;
}

void Socket_Close(hc_socket s) {
	netShutdown(s, SHUT_RDWR);
	netClose(s);
}

static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	struct pollfd pfd;
	int flags;

	pfd.fd     = s;
	pfd.events = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	
	if (netPoll(&pfd, 1, 0) < 0) return net_errno;
	
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

	// https://stackoverflow.com/questions/29479953/so-error-value-after-successful-socket-operation
	netGetSockOpt(s, SOL_SOCKET, SO_ERROR, &res, &resultSize);
	return res;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) {
	netInitialize();
	
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
#define MACHINE_KEY "PS3_PS3_PS3_PS3_"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
