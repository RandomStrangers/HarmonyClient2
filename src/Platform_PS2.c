#include "Core.h"
#if defined PLAT_PS2

#define LIBCGLUE_SYS_SOCKET_ALIASES 0
#define LIBCGLUE_SYS_SOCKET_NO_ALIASES
#define LIBCGLUE_ARPA_INET_NO_ALIASES
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
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/time.h>
#include <kernel.h>
#include <delaythread.h>
#include <debug.h>
#include <ps2ip.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <netman.h>
#include <ps2ip.h>
#include <dma.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <io_common.h>
#include <iox_stat.h>
#include <libcdvd.h>
#include "_PlatformConsole.h"

const hc_result ReturnCode_FileShareViolation = 1000000000; // not used
const hc_result ReturnCode_FileNotFound       = -4;
const hc_result ReturnCode_DirectoryExists    = -8;

const hc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const hc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const hc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = " (PS2)";
hc_bool Platform_ReadonlyFilesystem;

// extern unsigned char DEV9_irx[];
// extern unsigned int  size_DEV9_irx;
#define STRINGIFY(a) #a
#define SifLoadBuffer(name) \
	extern unsigned char name[]; \
	extern unsigned int  size_ ## name; \
	ret = SifExecModuleBuffer(name, size_ ## name, 0, NULL, NULL); \
    if (ret < 0) Platform_Log1("SifExecModuleBuffer " STRINGIFY(name) " failed: %i", &ret);


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char tmp[2048 + 1];
	len = min(len, 2048);
	Mem_Copy(tmp, msg, len); tmp[len] = '\0';
	_print("%s\n", tmp);
}

// https://stackoverflow.com/a/42340213
static HC_INLINE int UnBCD(unsigned char bcd) {
    return bcd - 6 * (bcd >> 4);
}

#define JST_OFFSET -9 * 60 // UTC+9 -> UTC
static time_t CurrentUnixTime(void) {
    sceCdCLOCK raw;
    struct tm tim;
    sceCdReadClock(&raw);

    tim.tm_sec  = UnBCD(raw.second);
    tim.tm_min  = UnBCD(raw.minute);
    tim.tm_hour = UnBCD(raw.hour);
    tim.tm_mday = UnBCD(raw.day);
	// & 0x1F, since a user had issues with upper 3 bits being set to 1
    tim.tm_mon  = UnBCD(raw.month & 0x1F) - 1;
    tim.tm_year = UnBCD(raw.year) + 100;

	// mktime will normalise the time anyways
    tim.tm_min += JST_OFFSET;
    return mktime(&tim);
}

TimeMS DateTime_CurrentUTC(void) {
	time_t rtc_sec = CurrentUnixTime();
	return (hc_uint64)rtc_sec + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct DateTime* t) {
	time_t rtc_sec = CurrentUnixTime();
	struct tm loc_time;
	localtime_r(&rtc_sec, &loc_time);

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
#define US_PER_SEC 1000000ULL

hc_uint64 Stopwatch_Measure(void) { 
	return GetTimerSystemTime();
}

hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, h_uint64 end) {
	if (end < beg) return 0;
	return (end - beg) * US_PER_SEC / kBUSCLK;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static const hc_string root_path = String_FromConst("mass:/HarmonyClient/");

void Platform_EncodePath(hc_filepath* dst, const hc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

hc_result Directory_Create(const hc_filepath* path) {
	return fioMkdir(path->buffer);
}

int File_Exists(const hc_filepath* path) {
	io_stat_t sb;
	return fioGetstat(path->buffer, &sb) >= 0 && (sb.mode & FIO_SO_IFREG);
}

// For some reason fioDread seems to be returning a iox_dirent_t, instead of a io_dirent_t
// The offset of 'name' in iox_dirent_t is different to 'iox_dirent_t', so naively trying
// to use entry.name doesn't work
// TODO: Properly investigate why this is happening
static char* GetEntryName(char* src) {
	for (int i = 0; i < 256; i++)
	{
		if (src[i]) return &src[i];
	}
	return NULL;
}

hc_result Directory_Enum(const hc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	hc_string path; 
	char pathBuffer[FILENAME_SIZE];
	hc_filepath str;
	io_dirent_t entry;
	int fd, res;

	Platform_EncodePath(&str, dirPath);
	fd = fioDopen(str.buffer);
	if (fd < 0) return fd;
	
	res = 0;
	String_InitArray(path, pathBuffer);
	Mem_Set(&entry, 0, sizeof(entry));

	while ((res = fioDread(fd, &entry)) > 0) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		char* src = GetEntryName(entry.name);
		if (!src) continue;

		// ignore . and .. entry
		if (src[0] == '.' && src[1] == '\0') continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;

		int len = String_Length(src);
		String_AppendUtf8(&path, src, len);

		int is_dir = FIO_SO_ISDIR(entry.stat.mode);
		callback(&path, obj, is_dir);
	}

	fioDclose(fd);
	return res;
}

static hc_result File_Do(hc_file* file, const char* path, int mode) {
	int res = fioOpen(path, mode);
	*file   = res;
	return res < 0 ? res : 0;
}

hc_result File_Open(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, FIO_O_RDONLY);
}
hc_result File_Create(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, FIO_O_RDWR | FIO_O_CREAT | FIO_O_TRUNC);
}
hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path) {
	return File_Do(file, path->buffer, FIO_O_RDWR | FIO_O_CREAT);
}

hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead) {
	int res    = fioRead(file, data, count);
	*bytesRead = res;
	return res < 0 ? res : 0;
}

hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote) {
	int res     = fioWrite(file, data, count);
	*bytesWrote = res;
	return res < 0 ? res : 0;
}

hc_result File_Close(hc_file file) {
	int res = fioClose(file);
	return res < 0 ? res : 0;
}

hc_result File_Seek(hc_file file, int offset, int seekType) {
	static hc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	
	int res = fioLseek(file, offset, modes[seekType]);
	return res < 0 ? res : 0;
}

hc_result File_Position(hc_file file, hc_uint32* pos) {
	int res = fioLseek(file, 0, SEEK_CUR);
	*pos    = res;
	return res < 0 ? res : 0;
}

hc_result File_Length(hc_file file, hc_uint32* len) {
	int cur_pos = fioLseek(file, 0, SEEK_CUR);
	if (cur_pos < 0) return cur_pos; // error occurred
	
	// get end and then restore position
	int res = fioLseek(file, 0, SEEK_END);
	fioLseek(file, cur_pos, SEEK_SET);
	
	*len    = res;
	return res < 0 ? res : 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(hc_uint32 milliseconds) {
	DelayThread(milliseconds * 1000);
}

static int ExecThread(void* param) {
	((Thread_StartFunc)param)(); 
	
	int thdID = GetThreadId();
	ee_thread_status_t info;
	
	int res = ReferThreadStatus(thdID, &info);
	if (res > 0 && info.stack) Mem_Free(info.stack); // TODO is it okay to free stack of running thread ????
	
	return 0; // TODO detach ?
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	ee_thread_t thread = { 0 };
	thread.func        = ExecThread;
	thread.stack       = Mem_Alloc(stackSize, 1, "Thread stack");
	thread.stack_size  = stackSize;
	thread.gp_reg      = &_gp;
	thread.initial_priority = 18;
	
	int thdID = CreateThread(&thread);
	if (thdID < 0) Logger_Abort2(thdID, "Creating thread");
	*handle = (void*)thdID;
	
	int res = StartThread(thdID, (void*)func);
	if (res < 0) Logger_Abort2(res, "Running thread");
}

void Thread_Detach(void* handle) {
	// TODO do something
}

void Thread_Join(void* handle) {
	int thdID = (int)handle;
	ee_thread_status_t info;
	
	for (;;)
	{
		int res = ReferThreadStatus(thdID, &info);
		if (res) Logger_Abort("Checking thread status");
		
		if (info.status == THS_DORMANT) break;
		Thread_Sleep(10); // TODO better solution
	}
}

void* Mutex_Create(const char* name) {
	ee_sema_t sema  = { 0 };
	sema.init_count = 1;
	sema.max_count  = 1;
	
	int semID = CreateSema(&sema);
	if (semID < 0) Logger_Abort2(semID, "Creating mutex");
	return (void*)semID;
}

void Mutex_Free(void* handle) {
	int semID = (int)handle;
	int res   = DeleteSema(semID);
	
	if (res) Logger_Abort2(res, "Destroying mutex");
}

void Mutex_Lock(void* handle) {
	int semID = (int)handle;
	int res   = WaitSema(semID);
	
	if (res < 0) Logger_Abort2(res, "Locking mutex");
}

void Mutex_Unlock(void* handle) {
	int semID = (int)handle;
	int res   = SignalSema(semID);
	
	if (res < 0) Logger_Abort2(res, "Unlocking mutex");
}

void* Waitable_Create(const char* name) {
	ee_sema_t sema  = { 0 };
	sema.init_count = 0;
	sema.max_count  = 1;
	
	int semID = CreateSema(&sema);
	if (semID < 0) Logger_Abort2(semID, "Creating waitable");
	return (void*)semID;
}

void Waitable_Free(void* handle) {
	int semID = (int)handle;
	int res   = DeleteSema(semID);
	
	if (res < 0) Logger_Abort2(res, "Destroying waitable");
}

void Waitable_Signal(void* handle) {
	int semID = (int)handle;
	int res   = SignalSema(semID);
	
	if (res < 0) Logger_Abort2(res, "Signalling event");
}

void Waitable_Wait(void* handle) {
	int semID = (int)handle;
	int res   = WaitSema(semID);
	
	if (res < 0) Logger_Abort2(res, "Signalling event");
}

void Waitable_WaitFor(void* handle, hc_uint32 milliseconds) {
	Logger_Abort("Can't wait for");
	// TODO implement support
}


/*########################################################################################################################*
*-------------------------------------------------------Networking--------------------------------------------------------*
*#########################################################################################################################*/
int	ps2ip_getconfig(char* netif_name,t_ip_info* ip_info);
int ps2ip_setconfig(const t_ip_info* ip_info);

// https://github.com/ps2dev/ps2sdk/blob/master/NETMAN.txt
// https://github.com/ps2dev/ps2sdk/blob/master/ee/network/tcpip/samples/tcpip_dhcp/ps2ip.c
static void ethStatusCheckCb(s32 alarm_id, u16 time, void *common) {
	int threadID = *(int*)common;
	iWakeupThread(threadID);
}

static int WaitValidNetState(int (*checkingFunction)(void)) {
	// Wait for a valid network status
	int threadID = GetThreadId();
	
	for (int retries = 0; checkingFunction() == 0; retries++)
	{	
		// Sleep for 500ms
		SetAlarm(500 * 16, &ethStatusCheckCb, &threadID);
		SleepThread();

		if (retries >= 15) return -1; // 7.5s = 15 * 500ms 
	}
	return 0;
}

static int ethGetNetIFLinkStatus(void) {
	int status = NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0);
	return status == NETMAN_NETIF_ETH_LINK_STATE_UP;
}

static int ethWaitValidNetIFLinkState(void) {
	return WaitValidNetState(&ethGetNetIFLinkStatus);
}

static int ethGetDHCPStatus(void) {
	t_ip_info ip_info;
	int result;
	if ((result = ps2ip_getconfig("sm0", &ip_info)) < 0) return result;
	
	if (ip_info.dhcp_enabled) {
		return ip_info.dhcp_status == DHCP_STATE_BOUND || ip_info.dhcp_status == DHCP_STATE_OFF;
	}
	return -1;
}

static int ethWaitValidDHCPState(void) {
	return WaitValidNetState(&ethGetDHCPStatus);
}

static int ethEnableDHCP(void) {
	t_ip_info ip_info;
	int result;
	// SMAP is registered as the "sm0" device to the TCP/IP stack.
	if ((result = ps2ip_getconfig("sm0", &ip_info)) < 0) return result;

	if (!ip_info.dhcp_enabled) {
		ip_info.dhcp_enabled = 1;	
		return ps2ip_setconfig(&ip_info);
	}
	return 1;
}

static void Networking_Setup(void) {
	NetManInit();

	struct ip4_addr IP  = { 0 }, NM = { 0 }, GW = { 0 };
	ps2ipInit(&IP, &NM, &GW);

	int res = ethEnableDHCP();
	if (res < 0) Platform_Log1("Error %i enabling DHCP", &res);

	Platform_LogConst("Waiting for net link connection...");
	if(ethWaitValidNetIFLinkState() != 0) {
		Platform_LogConst("Failed to establish net link");
		return;
	}

	Platform_LogConst("Waiting for DHCP lease...");
	if (ethWaitValidDHCPState() != 0) {
		Platform_LogConst("Failed to acquire DHCP lease");
		return;
	}
	Platform_LogConst("Network setup done");
}

static void Networking_LoadIOPModules(void) {
	int ret;
	
	SifLoadBuffer(DEV9_irx);
	SifLoadBuffer(NETMAN_irx);
	SifLoadBuffer(SMAP_irx);
}

/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/

int  lwip_shutdown(int s, int how);
int  lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int  lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int  lwip_close(int s);
int  lwip_connect(int s, const struct sockaddr *name, socklen_t namelen);
int  lwip_recv(int s, void *mem, size_t len, int flags);
int  lwip_send(int s, const void *dataptr, size_t size, int flags);
int  lwip_socket(int domain, int type, int protocol);
int  lwip_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout);
int  lwip_ioctl(int s, long cmd, void *argp);
int  lwip_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
void lwip_freeaddrinfo(struct addrinfo *ai);
int  ip4addr_aton(const char *cp, ip4_addr_t *addr);

static hc_result ParseHost(const char* host, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	char portRaw[32]; 
	hc_string portStr;
	struct addrinfo hints = { 0 };
	struct addrinfo* result;
	struct addrinfo* cur;
	int res, i = 0;

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	String_InitArray(portStr,  portRaw);
	String_AppendInt(&portStr, port);
	portRaw[portStr.length] = '\0';

	res = lwip_getaddrinfo(host, portRaw, &hints, &result);
	if (res == -NO_DATA) return SOCK_ERR_UNKNOWN_HOST;
	if (res) return res;

	for (cur = result; cur && i < SOCKET_MAX_ADDRS; cur = cur->ai_next, i++) 
	{
		SocketAddr_Set(&addrs[i], cur->ai_addr, cur->ai_addrlen);
	}
	
	lwip_freeaddrinfo(result);
	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
}

hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs) {
	struct sockaddr_in* addr4 = (struct sockaddr_in*)addrs[0].data;
	char str[NATIVE_STR_LEN];
	
	String_EncodeUtf8(str, address);
	*numValidAddrs = 0;

	if (ip4addr_aton(str, (ip4_addr_t*)&addr4->sin_addr) > 0) {
		addr4->sin_family = AF_INET;
		addr4->sin_port   = htons(port);
		
		addrs[0].size  = sizeof(*addr4);
		*numValidAddrs = 1;
		return 0;
	}
	
	return ParseHost(str, port, addrs, numValidAddrs);
}

static hc_result GetSocketError(hc_socket s) {
	socklen_t resultSize = sizeof(socklen_t);
	hc_result res = 0;
	lwip_getsockopt(s, SOL_SOCKET, SO_ERROR, &res, &resultSize);
	return res;
}

hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = lwip_socket(raw->sa_family, SOCK_STREAM, 0);
	if (*s < 0) return *s;

	if (nonblocking) {
		int blocking_raw = 1;
		int res = lwip_ioctl(*s, FIONBIO, &blocking_raw);
		//Platform_Log2("RESSS %i: %i", s, &res);
	}
	return 0;
}

hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	int res = lwip_connect(s, raw, addr->size);
	return res == -1 ? GetSocketError(s) : 0;
}

hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	//Platform_Log1("PREPARE TO READ: %i", &count);
	int recvCount = lwip_recv(s, data, count, 0);
	//Platform_Log1(" .. read %i", &recvCount);
	if (recvCount != -1) { *modified = recvCount; return 0; }
	
	int ERR = GetSocketError(s);
	Platform_Log1("ERR: %i", &ERR);
	*modified = 0; return ERR;
}

hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified) {
	//Platform_Log1("PREPARE TO WRITE: %i", &count);
	int sentCount = lwip_send(s, data, count, 0);
	//Platform_Log1(" .. wrote %i", &sentCount);
	if (sentCount != -1) { *modified = sentCount; return 0; }
	
	int ERR = GetSocketError(s);
	Platform_Log1("ERR: %i", &ERR);
	*modified = 0; return ERR;
}

void Socket_Close(hc_socket s) {
	lwip_shutdown(s, SHUT_RDWR);
	lwip_close(s);
}

static hc_result Socket_Poll(hc_socket s, int mode, hc_bool* success) {
	fd_set read_set, write_set, error_set;
	struct timeval time = { 0 };
	int selectCount;

	FD_ZERO(&read_set);
	FD_SET(s, &read_set);
	FD_ZERO(&write_set);
	FD_SET(s, &write_set);
	FD_ZERO(&error_set);
	FD_SET(s, &error_set);

	selectCount = lwip_select(s + 1, &read_set, &write_set, &error_set, &time);

	//Platform_Log4("SELECT %i = %h / %h / %h", &selectCount, &read_set, &write_set, &error_set);
	if (selectCount == -1) { *success = false; return errno; }
	*success = FD_ISSET(s, &write_set) != 0; return 0;
}

hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable) {
	//Platform_LogConst("POLL READ");
	return Socket_Poll(s, SOCKET_POLL_READ, readable);
}

static int tries;
hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable) {
	hc_result res = Socket_Poll(s, SOCKET_POLL_WRITE, writable);
	//Platform_Log1("POLL WRITE: %i", &res);
	if (res || *writable) return res;

	// INPROGRESS error code returned if connect is still in progress
	res = GetSocketError(s);
	Platform_Log1("POLL FAIL: %i", &res);
	if (res == EINPROGRESS) res = 0;
	
	if (tries++ > 20) { *writable = true; }
	return res;
}


/*########################################################################################################################*
*----------------------------------------------------USB mass storage-----------------------------------------------------*
*#########################################################################################################################*/
static void USBStorage_LoadIOPModules(void) {   
    int ret;
    // TODO: Seems that
    // BDM, BDMFS_FATFS, USBMASS_BD - newer ?
    // USBHDFSD - older ?
    
	SifLoadBuffer(USBD_irx);  
	//SifLoadBuffer(USBHDFSD_irx);
    
	SifLoadBuffer(BDM_irx);
	SifLoadBuffer(BDMFS_FATFS_irx);
    
	SifLoadBuffer(USBMASS_BD_irx);
	SifLoadBuffer(USBMOUSE_irx);
	SifLoadBuffer(USBKBD_irx);
}

// TODO Maybe needed ???
/*
static void USBStorage_WaitUntilDeviceReady() {
	io_stat_t sb;
	Thread_Sleep(50);

	for (int retry = 0; retry < 50; retry++)
	{
		if (fioGetstat("mass:/", &sb) >= 0) return;
		
    	nopdelay();
    }
    Platform_LogConst("USB device still not ready ??");
}*/


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
// Note that resetting IOP does mean can't debug through ps2client
// 	https://github.com/libsdl-org/SDL/commit/d355ea9981358a1df335b1f7485ce94768bbf255
// 	https://github.com/libsdl-org/SDL/pull/6022
static void ResetIOP(void) { // reboots the IOP
	SifInitRpc(0);
	while (!SifIopReset("", 0)) { }
	while (!SifIopSync())       { }
}

static void LoadIOPModules(void) {
	int ret;
	
	// file I/O module
	ret = SifLoadModule("rom0:FILEIO",  0, NULL);
    if (ret < 0) Platform_Log1("SifLoadModule FILEIO failed: %i", &ret);
    sbv_patch_fileio();
	
	// serial I/O module (needed for memory card & input pad modules)
	ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (ret < 0) Platform_Log1("SifLoadModule SIO2MAN failed: %i", &ret);
	
	
	// memory card module
	ret = SifLoadModule("rom0:MCMAN",   0, NULL);
    if (ret < 0) Platform_Log1("SifLoadModule MCMAN failed: %i", &ret);
	
	// memory card server module
	ret = SifLoadModule("rom0:MCSERV",  0, NULL);
    if (ret < 0) Platform_Log1("SifLoadModule MCSERV failed: %i", &ret);
    
    
    // Input pad module
    ret = SifLoadModule("rom0:PADMAN",  0, NULL);
    if (ret < 0) Platform_Log1("SifLoadModule PADMAN failed: %i", &ret);
 
}

void Platform_Init(void) {
	//InitDebug();
	ResetIOP();
	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
	sbv_patch_enable_lmb(); // Allows loading IRX modules from a buffer in EE RAM

	LoadIOPModules();
	USBStorage_LoadIOPModules();
	//USBStorage_WaitUntilDeviceReady();
	
	Networking_LoadIOPModules();
	Networking_Setup();
	
	hc_filepath* root = FILEPATH_RAW("mass:/HarmonyClient");
	int res = Directory_Create(root);
	Platform_Log1("ROOT DIRECTORY CREATE %i", &res);
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
#define MACHINE_KEY "PS2_PS2_PS2_PS2_"

static hc_result GetMachineID(hc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}
#endif
