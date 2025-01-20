#ifndef HC_PLATFORM_H
#define HC_PLATFORM_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Abstracts platform specific memory management, I/O, etc
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct DateTime;

enum Socket_PollMode { SOCKET_POLL_READ, SOCKET_POLL_WRITE };
#if defined HC_BUILD_WIN || defined HC_BUILD_XBOX
typedef hc_uintptr hc_socket;
typedef void* hc_file;
#define _NL "\r\n"
#define NATIVE_STR_LEN 300
#else
typedef int hc_socket;
typedef int hc_file;
#define _NL "\n"
#define NATIVE_STR_LEN 600
#endif
#define UPDATE_FILE "ClassiCube.update"

/* Origin points for when seeking in a file. */
/*  NOTE: These have same values as SEEK_SET/SEEK_CUR/SEEK_END, do not change them */
enum File_SeekFrom { FILE_SEEKFROM_BEGIN, FILE_SEEKFROM_CURRENT, FILE_SEEKFROM_END };
/* Number of seconds since 01/01/0001 to start of unix time. */
#define UNIX_EPOCH_SECONDS 62135596800ULL

extern const hc_result ReturnCode_FileShareViolation;
extern const hc_result ReturnCode_FileNotFound;
extern const hc_result ReturnCode_DirectoryExists;
extern const hc_result ReturnCode_SocketInProgess;
extern const hc_result ReturnCode_SocketWouldBlock;
/* Result code for when a socket connection has been dropped by the other side */
extern const hc_result ReturnCode_SocketDropped;

/* Whether the launcher and game must both be run in the same process */
/*  (e.g. can't start a separate process on Mobile or Consoles) */
extern hc_bool Platform_SingleProcess;
/* Suffix added to app name sent to the server */
extern const char* Platform_AppNameSuffix;
/* Whether the filesystem is readonly (i.e. cannot make chat logs, cache, etc) */
extern hc_bool Platform_ReadonlyFilesystem;

#ifdef HC_BUILD_WIN
typedef struct hc_winstring_ {
	hc_unichar uni[NATIVE_STR_LEN]; /* String represented using UTF16 format */
	char ansi[NATIVE_STR_LEN]; /* String lossily represented using ANSI format */
} hc_winstring;
/* Encodes a string into the platform native string format */
void Platform_EncodeString(hc_winstring* dst, const hc_string* src);

hc_bool Platform_DescribeErrorExt(hc_result res, hc_string* dst, void* lib);
#endif

#ifdef HC_BUILD_WIN
typedef hc_winstring hc_filepath;
#else
typedef struct hc_filepath_ { char buffer[NATIVE_STR_LEN]; } hc_filepath;
#define FILEPATH_RAW(raw) ((hc_filepath*)raw)
#endif
/* Converts the provided path into a platform native file path */
void Platform_EncodePath(hc_filepath* dst, const hc_string* src);

/* Initialises the platform specific state. */
void Platform_Init(void);
/* Frees the platform specific state. */
void Platform_Free(void);
/* Sets the appropriate default current/working directory. */
hc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv);
/* Gets the command line arguments passed to the program. */
int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, hc_string* args);

/* Encrypts data in a platform-specific manner. (may not be supported) */
hc_result Platform_Encrypt(const void* data, int len, hc_string* dst);
/* Decrypts data in a platform-specific manner. (may not be supported) */
hc_result Platform_Decrypt(const void* data, int len, hc_string* dst);
/* Outputs more detailed information about errors with operating system functions. */
/* NOTE: This is for general functions like file I/O. If a more specific 
describe exists (e.g. Http_DescribeError), that should be preferred. */
hc_bool Platform_DescribeError(hc_result res, hc_string* dst);

/* Starts the game with the given arguments. */
HC_API hc_result Process_StartGame2(const hc_string* args, int numArgs);
/* Terminates the process with the given return code. */
HC_API void Process_Exit(hc_result code);
/* Starts the platform-specific program to open the given url or filename. */
/* For example, provide a http:// url to open a website in the user's web browser. */
HC_API hc_result Process_StartOpen(const hc_string* args);
/* Whether opening URLs is supported by the platform */
extern hc_bool Process_OpenSupported;

struct UpdaterBuild { 
	const char* name; 
	const char* path; 
};
extern const struct UpdaterInfo {
	const char* info;
	/* Number of compiled builds available for this platform */
	int numBuilds;
	/* Metadata for the compiled builds available for this platform */
	const struct UpdaterBuild builds[2]; // TODO name and path
} Updater_Info;
/* Whether updating is supported by the platform */
extern hc_bool Updater_Supported;

/* Attempts to clean up any leftover files from an update */
hc_bool Updater_Clean(void);
/* Starts the platform-specific method to update then start the game using the UPDATE_FILE file. */
/* If an error occurs, action indicates which part of the updating process failed. */
hc_result Updater_Start(const char** action);
/* Returns the last time the application was modified, as a unix timestamp. */
hc_result Updater_GetBuildTime(hc_uint64* timestamp);
/* Marks the UPDATE_FILE file as being executable. (Needed for some platforms) */
hc_result Updater_MarkExecutable(void);
/* Sets the last time UPDATE_FILE file was modified, as a unix timestamp. */
hc_result Updater_SetNewBuildTime(hc_uint64 timestamp);

/* TODO: Rename _Load2 to _Load on next plugin API version */
/* Attempts to load a native dynamic library from the given path. */
HC_API void* DynamicLib_Load2(const hc_string* path);
/* Attempts to get the address of the symbol in the given dynamic library. */
/* NOTE: Do NOT use this to load OpenGL functions, use GLContext_GetAddress. */
HC_API void* DynamicLib_Get2(void* lib, const char* name);
/* Outputs more detailed information about errors with the DynamicLib functions. */
/* NOTE: You MUST call this immediately after DynamicLib_Load2/DynamicLib_Get2 returns NULL. */
HC_API hc_bool DynamicLib_DescribeError(hc_string* dst);

/* The default file extension used for dynamic libraries on this platform. */
extern const hc_string DynamicLib_Ext;
#define DYNAMICLIB_QUOTE(x) #x
#define DynamicLib_Sym(sym) { DYNAMICLIB_QUOTE(sym), (void**)&_ ## sym }
#define DynamicLib_Sym2(name, sym) { name,           (void**)&_ ## sym }
#if defined HC_BUILD_OS2
#define DynamicLib_SymC(sym) { DYNAMICLIB_QUOTE(_ ## sym), (void**)&_ ## sym }
#endif

HC_API hc_result DynamicLib_Load(const hc_string* path, void** lib); /* OBSOLETE */
HC_API hc_result DynamicLib_Get(void* lib, const char* name, void** symbol); /* OBSOLETE */
/* Contains a name and a pointer to variable that will hold the loaded symbol */
/*  static int (APIENTRY *_myGetError)(void); --- (for example) */
/*  static struct DynamicLibSym sym = { "myGetError", (void**)&_myGetError }; */
struct DynamicLibSym { const char* name; void** symAddr; };
/* Loads all symbols using DynamicLib_Get2 in the given list */
/* Returns true if all symbols were successfully retrieved */
hc_bool DynamicLib_LoadAll(const hc_string* path, const struct DynamicLibSym* syms, int count, void** lib);

/* Allocates a block of memory, with undetermined contents. Returns NULL on allocation failure. */
HC_API void* Mem_TryAlloc(hc_uint32 numElems, hc_uint32 elemsSize);
/* Allocates a block of memory, with contents of all 0. Returns NULL on allocation failure. */
HC_API void* Mem_TryAllocCleared(hc_uint32 numElems, hc_uint32 elemsSize);
/* Reallocates a block of memory, with undetermined contents. Returns NULL on reallocation failure. */
HC_API void* Mem_TryRealloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize);

/* Allocates a block of memory, with undetermined contents. Exits process on allocation failure. */
HC_API void* Mem_Alloc(hc_uint32 numElems, hc_uint32 elemsSize, const char* place);
/* Allocates a block of memory, with contents of all 0. Exits process on allocation failure. */
HC_API void* Mem_AllocCleared(hc_uint32 numElems, hc_uint32 elemsSize, const char* place);
/* Reallocates a block of memory, with undetermined contents. Exits process on reallocation failure. */
HC_API void* Mem_Realloc(void* mem, hc_uint32 numElems, hc_uint32 elemsSize, const char* place);
/* Frees an allocated a block of memory. Does nothing when passed NULL. */
HC_API void  Mem_Free(void* mem);

/* Sets the contents of a block of memory to the given value. */
void* Mem_Set(void* dst, hc_uint8 value, unsigned numBytes);
/* Copies a block of memory to another block of memory. */
/* NOTE: These blocks MUST NOT overlap. */
void* Mem_Copy(void* dst, const void* src, unsigned numBytes);
/* Moves a block of memory to another block of memory. */
/* NOTE: These blocks can overlap. */
void* Mem_Move(void* dst, const void* src, unsigned numBytes);
/* Returns non-zero if the two given blocks of memory have equal contents. */
int Mem_Equal(const void* a, const void* b, hc_uint32 numBytes);

/* Logs a debug message to console. */
void Platform_Log(const char* msg, int len);
void Platform_LogConst(const char* message);
void Platform_Log1(const char* format, const void* a1);
void Platform_Log2(const char* format, const void* a1, const void* a2);
void Platform_Log3(const char* format, const void* a1, const void* a2, const void* a3);
void Platform_Log4(const char* format, const void* a1, const void* a2, const void* a3, const void* a4);

/* Returns the current UTC time, as number of seconds since 1/1/0001 */
HC_API TimeMS DateTime_CurrentUTC(void);
/* Returns the current local Time. */
HC_API void DateTime_CurrentLocal(struct DateTime* t);
/* Takes a platform-specific stopwatch measurement. */
/* NOTE: The value returned is platform-specific - do NOT try to interpret the value. */
HC_API hc_uint64 Stopwatch_Measure(void);
/* Returns total elapsed microseconds between two stopwatch measurements. */
HC_API hc_uint64 Stopwatch_ElapsedMicroseconds(hc_uint64 beg, hc_uint64 end);
/* Returns total elapsed milliseconds between two stopwatch measurements. */
int Stopwatch_ElapsedMS(hc_uint64 beg, hc_uint64 end);

/* Attempts to create a new directory. */
hc_result Directory_Create(const hc_filepath* path);
/* Callback function invoked for each file found. */
typedef void (*Directory_EnumCallback)(const hc_string* filename, void* obj, int isDirectory);
/* Invokes a callback function on all filenames in the given directory (and its sub-directories) */
HC_API hc_result Directory_Enum(const hc_string* path, void* obj, Directory_EnumCallback callback);
/* Returns non-zero if the given file exists. */
int File_Exists(const hc_filepath* path);
void Directory_GetCachePath(hc_string* path);

/* Attempts to create a new (or overwrite) file for writing. */
/* NOTE: If the file already exists, its contents are discarded. */
hc_result File_Create(hc_file* file, const hc_filepath* path);
/* Attempts to open an existing file for reading. */
hc_result File_Open(hc_file* file, const hc_filepath* path);
/* Attempts to open an existing or create a new file for reading and writing. */
hc_result File_OpenOrCreate(hc_file* file, const hc_filepath* path);
/* Attempts to read data from the file. */
hc_result File_Read(hc_file file, void* data, hc_uint32 count, hc_uint32* bytesRead);
/* Attempts to write data to the file. */
hc_result File_Write(hc_file file, const void* data, hc_uint32 count, hc_uint32* bytesWrote);
/* Attempts to close the given file. */
hc_result File_Close(hc_file file);
/* Attempts to seek to a position in the given file. */
hc_result File_Seek(hc_file file, int offset, int seekType);
/* Attempts to get the current position in the given file. */
hc_result File_Position(hc_file file, hc_uint32* pos);
/* Attempts to retrieve the length of the given file. */
hc_result File_Length(hc_file file, hc_uint32* len);

typedef void (*Thread_StartFunc)(void);
/* Blocks the current thread for the given number of milliseconds. */
HC_API void Thread_Sleep(hc_uint32 milliseconds);
/* Initialises and starts a new thread that runs the given function. */
/* NOTE: Threads must either be detached or joined, otherwise data leaks. */
HC_API void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name);
/* Frees the platform specific persistent data associated with the thread. */
/* NOTE: Once a thread has been detached, Thread_Join can no longer be used. */
HC_API void Thread_Detach(void* handle);
/* Blocks the current thread, until the given thread has finished. */
/* NOTE: This cannot be used on a thread that has been detached. */
HC_API void Thread_Join(void* handle);

/* Allocates a new mutex. (used to synchronise access to a shared resource) */
HC_API void* Mutex_Create(const char* name);
/* Frees an allocated mutex. */
HC_API void  Mutex_Free(void* handle);
/* Locks the given mutex, blocking other threads from entering. */
HC_API void  Mutex_Lock(void* handle);
/* Unlocks the given mutex, allowing other threads to enter. */
HC_API void  Mutex_Unlock(void* handle);

/* Allocates a new waitable. (used to conditionally wake-up a blocked thread) */
HC_API void* Waitable_Create(const char* name);
/* Frees an allocated waitable. */
HC_API void  Waitable_Free(void* handle);
/* Signals a waitable, waking up blocked threads. */
HC_API void  Waitable_Signal(void* handle);
/* Blocks the calling thread until the waitable gets signalled. */
HC_API void  Waitable_Wait(void* handle);
/* Blocks the calling thread until the waitable gets signalled, or milliseconds delay passes. */
HC_API void  Waitable_WaitFor(void* handle, hc_uint32 milliseconds);

/* Calls SysFonts_Register on each font that is available on this platform. */
void Platform_LoadSysFonts(void);

#define HC_SOCKETADDR_MAXSIZE 512
#define SOCKET_MAX_ADDRS 5

typedef struct hc_sockaddr_ {
	int size; /* Actual size of the raw socket address */
	hc_uint8 data[HC_SOCKETADDR_MAXSIZE]; /* Raw socket address (e.g. sockaddr_in) */
} hc_sockaddr;

/* Checks if the given socket is currently readable (i.e. has data available to read) */
/* NOTE: A closed socket is also considered readable */
hc_result Socket_CheckReadable(hc_socket s, hc_bool* readable);
/* Checks if the given socket is currently writable (i.e. has finished connecting) */
hc_result Socket_CheckWritable(hc_socket s, hc_bool* writable);
/* If the input represents an IP address, then parses the input into a single IP address */
/* Otherwise, attempts to resolve the input via DNS into one or more IP addresses */
hc_result Socket_ParseAddress(const hc_string* address, int port, hc_sockaddr* addrs, int* numValidAddrs);

/* Allocates a new socket that is capable of connecting to the given address */
hc_result Socket_Create(hc_socket* s, hc_sockaddr* addr, hc_bool nonblocking);
/* Begins connecting to the given address */
hc_result Socket_Connect(hc_socket s, hc_sockaddr* addr);
/* Attempts to read data from the given socket */
/* NOTE: A closed socket may set modified to 0, but still return 'success' (i.e. 0) */
hc_result Socket_Read(hc_socket s, hc_uint8* data, hc_uint32 count, hc_uint32* modified);
/* Attempts to write data to the given socket */
hc_result Socket_Write(hc_socket s, const hc_uint8* data, hc_uint32 count, hc_uint32* modified);
/* Attempts to close the given socket */
void Socket_Close(hc_socket s);
/* Attempts to write all data to the given socket, returning ERR_END_OF_STREAM if it could not */
hc_result Socket_WriteAll(hc_socket socket, const hc_uint8* data, hc_uint32 count);

#ifdef HC_BUILD_MOBILE
void Platform_ShareScreenshot(const hc_string* filename);
#endif

#ifdef HC_BUILD_ANDROID
#include <jni.h>
extern jclass  App_Class;
extern jobject App_Instance;
extern JavaVM* VM_Ptr;
void Platform_TryLogJavaError(void);

#define JavaGetCurrentEnv(env) (*VM_Ptr)->AttachCurrentThread(VM_Ptr, &env, NULL)
#define JavaMakeConst(env, str) (*env)->NewStringUTF(env, str)

#define JavaRegisterNatives(env, methods) (*env)->RegisterNatives(env, App_Class, methods, Array_Elems(methods));
#define JavaGetIMethod(env, name, sig) (*env)->GetMethodID(env, App_Class, name, sig)
#define JavaGetSMethod(env, name, sig) (*env)->GetStaticMethodID(env, App_Class, name, sig)

/* Creates a string from the given java string. buffer must be at least NATIVE_STR_LEN long. */
/* NOTE: Don't forget to call env->ReleaseStringUTFChars. Only works with ASCII strings. */
hc_string JavaGetString(JNIEnv* env, jstring str, char* buffer);
/* Allocates a java string from the given string. */
jobject JavaMakeString(JNIEnv* env, const hc_string* str);
/* Allocates a java byte array from the given block of memory. */
jbyteArray JavaMakeBytes(JNIEnv* env, const void* src, hc_uint32 len);
/* Calls a method in the activity class that returns nothing. */
void JavaCallVoid(JNIEnv*  env, const char* name, const char* sig, jvalue* args);
/* Calls a method in the activity class that returns a jint. */
jlong JavaCallLong(JNIEnv* env, const char* name, const char* sig, jvalue* args);
/* Calls a method in the activity class that returns a jobject. */
jobject JavaCallObject(JNIEnv* env, const char* name, const char* sig, jvalue* args);
/* Calls a method in the activity class that takes a string and returns nothing. */
void JavaCall_String_Void(const char* name, const ch_string* value);
/* Calls a method in the activity class that takes no arguments and returns a string. */
void JavaCall_Void_String(const char* name, hc_string* dst);
/* Calls a method in the activity class that takes a string and returns a string. */
void JavaCall_String_String(const char* name, const hc_string* arg, hc_string* dst);

/* Calls an instance method in the activity class that returns nothing */
#define JavaICall_Void(env, method, args) (*env)->CallVoidMethodA(env,  App_Instance, method, args)
/* Calls an instance method in the activity class that returns a jint */
#define JavaICall_Int(env,  method, args) (*env)->CallIntMethodA(env,   App_Instance, method, args)
/* Calls an instance method in the activity class that returns a jlong */
#define JavaICall_Long(env, method, args) (*env)->CallLongMethodA(env,  App_Instance, method, args)
/* Calls an instance method in the activity class that returns a jfloat */
#define JavaICall_Float(env,method, args) (*env)->CallFloatMethodA(env, App_Instance, method, args)
/* Calls an instance method in the activity class that returns a jobject */
#define JavaICall_Obj(env,  method, args) (*env)->CallObjectMethodA(env,App_Instance, method, args)

/* Calls a static method in the activity class that returns nothing */
#define JavaSCall_Void(env, method, args) (*env)->CallStaticVoidMethodA(env,  App_Class, method, args)
/* Calls a static method in the activity class that returns a jint */
#define JavaSCall_Int(env,  method, args) (*env)->CallStaticIntMethodA(env,   App_Class, method, args)
/* Calls a static method in the activity class that returns a jlong */
#define JavaSCall_Long(env, method, args) (*env)->CallStaticLongMethodA(env,  App_Class, method, args)
/* Calls a static method in the activity class that returns a jfloat */
#define JavaSCall_Float(env,method, args) (*env)->CallStaticFloatMethodA(env, App_Class, method, args)
/* Calls a static method in the activity class that returns a jobject */
#define JavaSCall_Obj(env,  method, args) (*env)->CallStaticObjectMethodA(env,App_Class, method, args)
#endif

HC_END_HEADER
#endif
