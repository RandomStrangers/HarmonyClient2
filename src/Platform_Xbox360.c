#include "Core.h"
#if defined CH_BUILD_XBOX360
#include "_PlatformBase.h"
#include "Stream.h"
#include "Funcs.h"
#include "Utils.h"
#include "Errors.h"

#define LWIP_SOCKET 1
#include <lwip/sockets.h>
#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <console/console.h>
#include <network/network.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; /* TODO: not used apparently */
const hc_result ReturnCode_FileNotFound     = ENOENT;
const hc_result ReturnCode_DirectoryExists  = EEXIST;
const hc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = " (XBox 360)";
hc_bool Platform_ReadonlyFilesystem;


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char tmp[2048 + 1];
	len = min(len, 2048);
	Mem_Copy(tmp, msg, len); tmp[len] = '\0';
	
	puts(tmp);
}

TimeMS DateTime_CurrentUTC(void) {
	struct timeval cur;
	gettimeofday(&cur, NULL);
	return (hc_uint64)cur.tv_sec + UNIX_EPOCH_SECONDS;
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

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end) {
	if (end < beg) return 0;
	return tb_diff_usec(end, beg);
}

hc_uint64 Stopwatch_Measure(void) {
	return mftb();
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static char root_buffer[NATIVE_STR_LEN];
static hc_string root_path = String_FromArray(root_buffer);

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
*#############################################################################################################p############*/
void Thread_Sleep(hc_uint32 milliseconds) { mdelay(milliseconds); }

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	*handle = NULL; // TODO
}

void Thread_Start2(void* handle, Thread_StartFunc func) {// TODO
}

void Thread_Detach(void* handle) {// TODO
}

void Thread_Join(void* handle) {// TODO
}

void* Mutex_Create(const char* name) {
	return Mem_AllocCleared(1, sizeof(int), "mutex");
}

void Mutex_Free(void* handle) {
	Mem_Free(handle);
}
void Mutex_Lock(void* handle)   {  } // TODO
void Mutex_Unlock(void* handle) {  } // TODO

void* Waitable_Create(const char* name) {
	return NULL; // TODO
}

void Waitable_Free(void* handle) {
	// TODO
}

void Waitable_Signal(void* handle) { } // TODO }
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
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) {
	xenos_init(VIDEO_MODE_AUTO);
	console_init();
	//network_init();
}

void Platform_Free(void) {
}

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
#define MACHINE_KEY "360_360_360_360_"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
