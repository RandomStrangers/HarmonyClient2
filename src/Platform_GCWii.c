#include "Core.h"
#if defined HC_BUILD_GCWII
#include "_PlatformBase.h"
#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "PackedCol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <network.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include <ogc/exi.h>
#ifdef HW_RVL
#include <ogc/wiilaunch.h>
#endif
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; /* TODO: not used apparently */
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = -EINPROGRESS; // net_XYZ error results are negative
const hc_result ReturnCode_SocketWouldBlock = -EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = -EPIPE;

#ifdef HW_RVL
const char* Platform_AppNameSuffix = " (Wii)";
#else
const char* Platform_AppNameSuffix = " (GameCube)";
#endif
hc_bool Platform_ReadonlyFilesystem;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
// To see these log messages:
//   1) In the UI, make sure 'Show log configuration' checkbox is checked in View menu
//   2) Make sure "OSReport EXI (OSREPORT)" log type is enabled
//   3) In the UI, make sure 'Show log' checkbox is checked in View menu
static void LogOverEXI(char* msg, int len) {
	u32 cmd = 0x80000000 | (0x800400 << 6); // write flag, UART base address

	// https://hitmen.c02.at/files/yagcd/yagcd/chap10.html
	// Try to acquire "MASK ROM"/"IPL" link
	// Writing to the IPL is used for debug message logging
	if (EXI_Lock(EXI_CHANNEL_0,   EXI_DEVICE_1, NULL) == 0) return;
	if (EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_1, EXI_SPEED8MHZ) == 0) {
		EXI_Unlock(EXI_CHANNEL_0); return;
	}

	EXI_Imm(     EXI_CHANNEL_0, &cmd, 4, EXI_WRITE, NULL);
	EXI_Sync(    EXI_CHANNEL_0);
	EXI_ImmEx(   EXI_CHANNEL_0, msg, len, EXI_WRITE);
	EXI_Deselect(EXI_CHANNEL_0);
	EXI_Unlock(  EXI_CHANNEL_0);
}

void Platform_Log(const char* msg, int len) {
	char tmp[256 + 1];
	len = min(len, 256);
	// See EXI_DeviceIPL.cpp in Dolphin, \r is what triggers buffered message to be logged
	Mem_Copy(tmp, msg, len); tmp[len] = '\r';

	LogOverEXI(tmp, len + 1);
}

#define GCWII_EPOCH_ADJUST 946684800ULL // GameCube/Wii time epoch is year 2000, not 1970

TimeMS DateTime_CurrentUTC(void) {
	u64 raw  = gettime();
	u64 secs = ticks_to_secs(raw);
	return secs + UNIX_EPOCH_SECONDS + GCWII_EPOCH_ADJUST;
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
	return gettime();
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return ticks_to_microsecs(end - beg);
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static char root_buffer[NATIVE_STR_LEN];
static hc_string root_path = String_FromArray(root_buffer);

static bool fat_available; 
// trying to call mkdir etc with no FAT device loaded seems to be broken (dolphin crashes due to trying to execute invalid instruction)
//   https://github.com/Patater/newlib/blob/8a9e3aaad59732842b08ad5fc19e0acf550a418a/libgloss/libsysbase/mkdir.c and
//   https://github.com/Patater/newlib/blob/8a9e3aaad59732842b08ad5fc19e0acf550a418a/newlib/libc/include/sys/iosupport.h
// FindDevice() returns -1 when no matching device, however the code still unconditionally does "if (devoptab_list[dev]->mkdir_r) {"
// - so will either attempt to access or execute invalid memory

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str   += root_path.length;
	*str++ = '/';
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	if (!fat_available) return ENOSYS;

	return mkdir(path->buffer, 0) == -1 ? errno : 0;
}

int File_Exists(const hc_filepath* path) {
	if (!fat_available) return false;
	
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	if (!fat_available) return ENOSYS;

	hc_string path; char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
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
	if (!fat_available) return ReturnCode_FileNotFound;
	return File_Do(file, path->buffer, O_RDONLY);
}

hc_result File_Create(hc_file* file, const hc_filepath* path) {
	if (!fat_available) return ENOTSUP;
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}

hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	if (!fat_available) return ENOTSUP;
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
void Thread_Sleep(hc_uint32 milliseconds) { usleep(milliseconds * 1000); }

static void* ExecThread(void* param) {
	((Thread_StartFunc)param)(); 
	return NULL;
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	lwp_t* thread = (lwp_t*)Mem_Alloc(1, sizeof(lwp_t), "thread");
	*handle = thread;
	
	int res = LWP_CreateThread(thread, ExecThread, (void*)func, NULL, stackSize, 80);
	if (res) Logger_Abort2(res, "Creating thread");
}

void Thread_Detach(void* handle) {
	// TODO: Leaks return value of thread ???
	lwp_t* ptr = (lwp_t*)handle;
	Mem_Free(ptr);
}

void Thread_Join(void* handle) {
	lwp_t* ptr = (lwp_t*)handle;
	int res = LWP_JoinThread(*ptr, NULL);
	if (res) Logger_Abort2(res, "Joining thread");
	Mem_Free(ptr);
}

void* Mutex_Create(const char* name) {
	mutex_t* ptr = (mutex_t*)Mem_Alloc(1, sizeof(mutex_t), "mutex");
	int res = LWP_MutexInit(ptr, false);
	if (res) Logger_Abort2(res, "Creating mutex");
	return ptr;
}

void Mutex_Free(void* handle) {
	mutex_t* mutex = (mutex_t*)handle;
	int res = LWP_MutexDestroy(*mutex);
	if (res) Logger_Abort2(res, "Destroying mutex");
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	mutex_t* mutex = (mutex_t*)handle;
	int res = LWP_MutexLock(*mutex);
	if (res) Logger_Abort2(res, "Locking mutex");
}

void Mutex_Unlock(void* handle) {
	mutex_t* mutex = (mutex_t*)handle;
	int res = LWP_MutexUnlock(*mutex);
	if (res) Logger_Abort2(res, "Unlocking mutex");
}

// should really use a semaphore with max 1.. too bad no 'TimedWait' though
struct WaitData {
	cond_t  cond;
	mutex_t mutex;
	int signalled; // For when Waitable_Signal is called before Waitable_Wait
};

void* Waitable_Create(const char* name) {
	struct WaitData* ptr = (struct WaitData*)Mem_Alloc(1, sizeof(struct WaitData), "waitable");
	int res;
	
	res = LWP_CondInit(&ptr->cond);
	if (res) Logger_Abort2(res, "Creating waitable");
	res = LWP_MutexInit(&ptr->mutex, false);
	if (res) Logger_Abort2(res, "Creating waitable mutex");

	ptr->signalled = false;
	return ptr;
}

void Waitable_Free(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;
	int res;
	
	res = LWP_CondDestroy(ptr->cond);
	if (res) Logger_Abort2(res, "Destroying waitable");
	res = LWP_MutexDestroy(ptr->mutex);
	if (res) Logger_Abort2(res, "Destroying waitable mutex");
	Mem_Free(handle);
}

void Waitable_Signal(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;
	int res;

	Mutex_Lock(&ptr->mutex);
	ptr->signalled = true;
	Mutex_Unlock(&ptr->mutex);

	res = LWP_CondSignal(ptr->cond);
	if (res) Logger_Abort2(res, "Signalling event");
}

void Waitable_Wait(void* handle) {
	struct WaitData* ptr = (struct WaitData*)handle;
	int res;

	Mutex_Lock(&ptr->mutex);
	if (!ptr->signalled) {
		res = LWP_CondWait(ptr->cond, ptr->mutex);
		if (res) Logger_Abort2(res, "Waitable wait");
	}
	ptr->signalled = false;
	Mutex_Unlock(&ptr->mutex);
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	struct WaitData* ptr = (struct WaitData*)handle;
	struct timespec ts;
	int res;

	ts.tv_sec  = milliseconds / 1000;
	ts.tv_nsec = milliseconds % 1000;

	Mutex_Lock(&ptr->mutex);
	if (!ptr->signalled) {
		res = LWP_CondTimedWait(ptr->cond, ptr->mutex, &ts);
		if (res && res != ETIMEDOUT) Logger_Abort2(res, "Waitable wait for");
	}
	ptr->signalled = false;
	Mutex_Unlock(&ptr->mutex);
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
static hc_result ParseHost(const char* host, int port, hc_sockaddr* addrs, int* numValidAddrs) {
#ifdef HW_RVL
	struct hostent* res = net_gethostbyname(host);
	struct sockaddr_in* addr4;
	char* src_addr;
	int i;
	
	// avoid confusion with SSL error codes
	// e.g. FFFF FFF7 > FF00 FFF7
	if (!res) return -0xFF0000 + errno;
	
	// Must have at least one IPv4 address
	if (res->h_addrtype != AF_INET) return ERR_INVALID_ARGUMENT;
	if (!res->h_addr_list)          return ERR_INVALID_ARGUMENT;

	for (i = 0; i < SOCKET_MAX_ADDRS; i++) 
	{
		src_addr = res->h_addr_list[i];
		if (!src_addr) break;
		addrs[i].size = sizeof(struct sockaddr_in);

		addr4 = (struct sockaddr_in*)addrs[i].data;
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		addr4->sin_addr   = *(struct in_addr*)src_addr;
	}

	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
#else
	// DNS resolution not implemented in gamecube libbba
	return ERR_NOT_SUPPORTED;
#endif
}
hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	struct sockaddr_in* addr4 = (struct sockaddr_in*)addrs[0].data;
	char str[NATIVE_STR_LEN];
	String_EncodeUtf8(str, address);
	*numValidAddrs = 1;
	if (inet_aton(str, &addr4->sin_addr) > 0) {
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		
		addrs[0].size = sizeof(*addr4);
		return 0;
	}
	
	return ParseHost(str, port, addrs, numValidAddrs);
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = net_socket(raw->sa_family, SOCK_STREAM, 0);
	if (*s < 0) return *s;

	if (nonblocking) {
		int blocking_raw = -1; /* non-blocking mode */
		net_ioctl(*s, FIONBIO, &blocking_raw);
	}
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;
	
	int res = net_connect(s, raw, addr->size);
	return res < 0 ? res : 0;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int res = net_recv(s, data, count, 0);
	if (res < 0) { *modified = 0; return res; }
	
	*modified = res; return 0;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	int res = net_send(s, data, count, 0);
	if (res < 0) { *modified = 0; return res; }
	
	*modified = res; return 0;
}

void Socket_Close(hc_socket s) {
	net_shutdown(s, 2); // SHUT_RDWR = 2
	net_close(s);
}

#ifdef HW_RVL
// libogc only implements net_poll for wii currently
static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	struct pollsd pfd;
	pfd.socket = s;
	pfd.events = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	
	int res = net_poll(&pfd, 1, 0);
	if (res < 0) { *success = false; return res; }
	
	// to match select, closed socket still counts as readable
	int flags = mode == SOCKET_POLL_READ ? (POLLIN | POLLHUP) : POLLOUT;
	*success  = (pfd.revents & flags) != 0;
	return 0;
}
#else
// libogc only implements net_select for gamecube currently
static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	fd_set set;
	struct timeval time = { 0 };
	int res; // number of 'ready' sockets
	FD_ZERO(&set);
	FD_SET(s, &set);
	if (mode == SOCKET_POLL_READ) {
		res = net_select(s + 1, &set, NULL, NULL, &time);
	} else {
		res = net_select(s + 1, NULL, &set, NULL, &time);
	}
	if (res < 0) { *success = false; return res; }
	*success = FD_ISSET(s, &set) != 0; return 0;
}
#endif

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	return Socket_Poll(s, SOCKET_POLL_READ, readable);
}

hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	u32 resultSize = sizeof(u32);
	hc_result res  = Socket_Poll(s, SOCKET_POLL_WRITE, writable);
	if (res || *writable) return res;

	return 0;
	// TODO FIX with updated devkitpro ???
	
	/* https://stackoverflow.com/questions/29479953/so-error-value-after-successful-socket-operation */
	net_getsockopt(s, SOL_SOCKET, SO_ERROR, &res, resultSize);
	return res;
}
static void InitSockets(void) {
#ifdef HW_RVL
	int ret = net_init();
	Platform_Log1("Network setup result: %i", &ret);
#else
	// https://github.com/devkitPro/wii-examples/blob/master/devices/network/sockettest/source/sockettest.c
	char localip[16] = {0};
	char netmask[16] = {0};
	char gateway[16] = {0};
	
	int ret = if_config(localip, netmask, gateway, TRUE, 20);
	if (ret >= 0) {
		Platform_Log3("Network ip: %c, gw: %c, mask %c", localip, gateway, netmask);
	} else {
		Platform_Log1("Network setup failed: %i", &ret);
	}
#endif
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
static void AppendDevice(hc_string* path, char* cwd) {
	// try to find device FAT mounted on, otherwise default to SD card
	if (!cwd) { String_AppendConst(path, "sd"); return;	}
	
	Platform_Log1("CWD: %c", cwd);
	hc_string cwd_ = String_FromReadonly(cwd);
	int deviceEnd  = String_IndexOf(&cwd_, ':');
		
	if (deviceEnd >= 0) {
		// e.g. "card0:/" becomes "card0"
		String_AppendAll(path, cwd, deviceEnd);
	} else {
		String_AppendConst(path, "sd");
	}
}

static void FindRootDirectory(void) {
	char cwdBuffer[NATIVE_STR_LEN] = { 0 };
	char* cwd = getcwd(cwdBuffer, NATIVE_STR_LEN);
	
	root_path.length = 0;
	AppendDevice(&root_path, cwd);
	String_AppendConst(&root_path, ":/HarmonyClient");
}

static void CreateRootDirectory(void) {
	if (!fat_available) return;
	root_buffer[root_path.length] = '\0';
	
	// irectory_Create(&String_Empty); just returns error 20
	int res = mkdir(root_buffer, 0);
	int err = res == -1 ? errno : 0;
	Platform_Log1("Created root directory: %i", &err);
}

void Platform_Init(void) {
	fat_available = fatInitDefault();
	Platform_ReadonlyFilesystem = !fat_available;

	FindRootDirectory();
	CreateRootDirectory();
	
	InitSockets();
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
#if defined HW_RVL
	#define MACHINE_KEY "Wii_Wii_Wii_Wii_"
#else
	#define MACHINE_KEY "GameCubeGameCube"
#endif

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
