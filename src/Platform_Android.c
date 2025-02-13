#include "Core.h"
#if defined HC_BUILD_ANDROID
#include "Chat.h"
#include "Constants.h"
#include "Errors.h"
#include "Funcs.h"
#include "String.h"
#include "Platform.h"
#include <errno.h>
#include <unistd.h>
#include <android/log.h>


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char tmp[2048 + 1];
	len = min(len, 2048);

	Mem_Copy(tmp, msg, len); tmp[len] = '\0';
	/* log using logchat */
	__android_log_write(ANDROID_LOG_DEBUG, "HarmonyDevClient", tmp);
}


/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
hc_result Process_StartOpen(const hc_string* args) {
	JavaCall_String_Void("startOpen", args);
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Updater----------------------------------------------------------*
*#########################################################################################################################*/
const struct UpdaterInfo Updater_Info = {
	"&eRedownload from github and reinstall to update", 0
};
hc_bool Updater_Clean(void) { return true; }

hc_result Updater_GetBuildTime(hc_uint64* t) {
	JNIEnv* env;
	JavaGetCurrentEnv(env);
	*t = JavaCallLong(env, "getApkUpdateTime", "()J", NULL);
	return 0;
}

hc_result Updater_Start(const char** action)   { *action = "Updating game"; return ERR_NOT_SUPPORTED; }
hc_result Updater_MarkExecutable(void)         { return 0; }
hc_result Updater_SetNewBuildTime(hc_uint64 t) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_TryLogJavaError(void) {
	JNIEnv* env;
	jthrowable err;
	JavaGetCurrentEnv(env);

	err = (*env)->ExceptionOccurred(env);
	if (!err) return;

	Platform_LogConst("PANIC");
	(*env)->ExceptionDescribe(env);
	(*env)->ExceptionClear(env);
	/* TODO actually do something */
}

void Platform_ShareScreenshot(const hc_string* filename) {
	hc_string path; char pathBuffer[FILENAME_SIZE];
	String_InitArray(path, pathBuffer);

	JavaCall_String_String("shareScreenshot", filename, &path);
	if (!path.length) return;

	Chat_AddRaw("&cError sharing screenshot");
	Chat_Add1("  &c%s", &path);
}

void Directory_GetCachePath(hc_string* path) {
	// TODO cache method ID
	JavaCall_Void_String("getGameCacheDirectory", path);
}


/*########################################################################################################################*
*-----------------------------------------------------Configuration-------------------------------------------------------*
*#########################################################################################################################*/
#include "Window.h"
hc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) {
	hc_string dir; char dirBuffer[FILENAME_SIZE + 1];
	String_InitArray_NT(dir, dirBuffer);

	JavaCall_Void_String("getGameDataDirectory", &dir);
	dir.buffer[dir.length] = '\0';
	Platform_Log1("DATA DIR: %s|", &dir);

	int res = chdir(dir.buffer) == -1 ? errno : 0;
	if (!res) return 0;

	// show the path to the user
	// TODO there must be a better way
	Window_ShowDialog("Failed to set working directory to", dir.buffer);
	return res;
}


/*########################################################################################################################*
*-----------------------------------------------------Java Interop--------------------------------------------------------*
*#########################################################################################################################*/
jclass  App_Class;
jobject App_Instance;
JavaVM* VM_Ptr;

/* JNI helpers */
hc_string JavaGetString(JNIEnv* env, jstring str, char* buffer) {
	const char* src; int len;
	hc_string dst;
	src = (*env)->GetStringUTFChars(env, str, NULL);
	len = (*env)->GetStringUTFLength(env, str);

	dst.buffer   = buffer;
	dst.length   = 0;
	dst.capacity = NATIVE_STR_LEN;
	String_AppendUtf8(&dst, src, len);

	(*env)->ReleaseStringUTFChars(env, str, src);
	return dst;
}

jobject JavaMakeString(JNIEnv* env, const hc_string* str) {
	hc_uint8 tmp[2048 + 4];
	hc_uint8* cur;
	int i, len = 0;

	for (i = 0; i < str->length && len < 2048; i++) {
		cur = tmp + len;
		len += Convert_CP437ToUtf8(str->buffer[i], cur);
	}
	tmp[len] = '\0';
	return (*env)->NewStringUTF(env, (const char*)tmp);
}

jbyteArray JavaMakeBytes(JNIEnv* env, const void* src, hc_uint32 len) {
	if (!len) return NULL;
	jbyteArray arr = (*env)->NewByteArray(env, len);
	(*env)->SetByteArrayRegion(env, arr, 0, len, src);
	return arr;
}

void JavaCallVoid(JNIEnv* env, const char* name, const char* sig, jvalue* args) {
	jmethodID method = (*env)->GetMethodID(env, App_Class, name, sig);
	(*env)->CallVoidMethodA(env, App_Instance, method, args);
}

jlong JavaCallLong(JNIEnv* env, const char* name, const char* sig, jvalue* args) {
	jmethodID method = (*env)->GetMethodID(env, App_Class, name, sig);
	return (*env)->CallLongMethodA(env, App_Instance, method, args);
}

jobject JavaCallObject(JNIEnv* env, const char* name, const char* sig, jvalue* args) {
	jmethodID method = (*env)->GetMethodID(env, App_Class, name, sig);
	return (*env)->CallObjectMethodA(env, App_Instance, method, args);
}

void JavaCall_String_Void(const char* name, const hc_string* value) {
	JNIEnv* env;
	jvalue args[1];
	JavaGetCurrentEnv(env);

	args[0].l = JavaMakeString(env, value);
	JavaCallVoid(env, name, "(Ljava/lang/String;)V", args);
	(*env)->DeleteLocalRef(env, args[0].l);
}

static void ReturnString(JNIEnv* env, jobject obj, hc_string* dst) {
	const jchar* src;
	jsize len;
	if (!obj) return;

	src = (*env)->GetStringChars(env,  obj, NULL);
	len = (*env)->GetStringLength(env, obj);
	String_AppendUtf16(dst, src, len * 2);
	(*env)->ReleaseStringChars(env, obj, src);
	(*env)->DeleteLocalRef(env,     obj);
}

void JavaCall_Void_String(const char* name, hc_string* dst) {
	JNIEnv* env;
	jobject obj;
	JavaGetCurrentEnv(env);

	obj = JavaCallObject(env, name, "()Ljava/lang/String;", NULL);
	ReturnString(env, obj, dst);
}

void JavaCall_String_String(const char* name, const hc_string* arg, hc_string* dst) {
	JNIEnv* env;
	jobject obj;
	jvalue args[1];
	JavaGetCurrentEnv(env);

	args[0].l = JavaMakeString(env, arg);
	obj       = JavaCallObject(env, name, "(Ljava/lang/String;)Ljava/lang/String;", args);
	ReturnString(env, obj, dst);
	(*env)->DeleteLocalRef(env, args[0].l);
}


/*########################################################################################################################*
*----------------------------------------------------Initialisation-------------------------------------------------------*
*#########################################################################################################################*/
extern void android_main(void);
static void JNICALL java_updateInstance(JNIEnv* env, jobject instance) {
	Platform_LogConst("App instance updated!");
	App_Instance = (*env)->NewGlobalRef(env, instance);
	/* TODO: Do we actually need to remove that global ref later? */
}

/* Called eventually by the activity java class to actually start the game */
static void JNICALL java_runGameAsync(JNIEnv* env, jobject instance) {
	void* thread;
	java_updateInstance(env, instance);

	Platform_LogConst("Running game async!");
	/* The game must be run on a separate thread, as blocking the */
	/* main UI thread will cause a 'App not responding..' messagebox */
	Thread_Run(&thread, android_main, 1024 * 1024, "Game"); // TODO check stack size needed
	Thread_Detach(thread);
}
static const JNINativeMethod methods[] = {
	{ "updateInstance", "()V", java_updateInstance },
	{ "runGameAsync",   "()V", java_runGameAsync }
};

/* This method is automatically called by the Java VM when the */
/*  activity java class calls 'System.loadLibrary("harmonydevclient");' */
HC_API jint JNI_OnLoad(JavaVM* vm, void* reserved) {
	jclass klass;
	JNIEnv* env;
	VM_Ptr = vm;
	JavaGetCurrentEnv(env);

	klass     = (*env)->FindClass(env, "com/harmonydevclient/MainActivity");
	App_Class = (*env)->NewGlobalRef(env, klass);
	JavaRegisterNatives(env, methods);
	return JNI_VERSION_1_4;
}
#endif
