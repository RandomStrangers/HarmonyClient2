#ifndef HC_LOGGER_H
#define HC_LOGGER_H
#include "Core.h"
HC_BEGIN_HEADER

/* 
Logs warnings/errors and also abstracts platform specific logging for fatal errors
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

typedef hc_bool (*Logger_DescribeError)(hc_result res, hc_string* dst);
typedef void (*Logger_DoWarn)(const hc_string* msg);
/* Informs the user about a non-fatal error. */
/*  By default this shows a message box, but changes to in-game chat when game is running. */
extern Logger_DoWarn Logger_WarnFunc;
/* The title shown for warning dialogs. */
extern const char* Logger_DialogTitle;
/* Shows a warning message box with the given message. */
void Logger_DialogWarn(const hc_string* msg);

/* Format: "Error [result] when [action]  \n  Error meaning: [desc]" */
/* If describeErr returns false, then 'Error meaning' line is omitted. */
void Logger_FormatWarn(hc_string* msg, hc_result res, const char* action, Logger_DescribeError describeErr);
/* Format: "Error [result] when [action] '[path]'  \n  Error meaning: [desc]" */
/* If describeErr returns false, then 'Error meaning' line is omitted. */
void Logger_FormatWarn2(hc_string* msg, hc_result res, const char* action, const hc_string* path, Logger_DescribeError describeErr);
/* Informs the user about a non-fatal error, with an optional meaning message. */
void Logger_Warn(hc_result res, const char* action, Logger_DescribeError describeErr);
/* Informs the user about a non-fatal error, with an optional meaning message. */
void Logger_Warn2(hc_result res, const char* action, const hc_string* path, Logger_DescribeError describeErr);

/* Shortcut for Logger_Warn with no error describer function */
void Logger_SimpleWarn(hc_result res, const char* action);
/* Shorthand for Logger_Warn2 with no error describer function */
void Logger_SimpleWarn2(hc_result res, const char* action, const hc_string* path);
/* Shows a warning for a failed DynamicLib_Load2/Get2 call. */
/* Format: "Error [action] '[path]'  \n  Error meaning: [desc]" */
void Logger_DynamicLibWarn(const char* action, const hc_string* path);
/* Shortcut for Logger_Warn with Platform_DescribeError */
void Logger_SysWarn(hc_result res, const char* action);
/* Shortcut for Logger_Warn2 with Platform_DescribeError */
void Logger_SysWarn2(hc_result res, const char* action, const hc_string* path);

/* Hooks the operating system's unhandled error callback/signal. */
/* This is used to attempt to log some information about a crash due to invalid memory read, etc. */
void Logger_Hook(void);
/* Generates a backtrace based on the platform-specific CPU context. */
/* NOTE: The provided CPU context may be modified (e.g on Windows) */
void Logger_Backtrace(hc_string* trace, void* ctx);
/* Logs a message to client.log on disc. */
/* NOTE: The message is written raw, it is NOT converted to unicode (unlike Stream_WriteLine) */
void Logger_Log(const hc_string* msg);
/* Displays a message box with raw_msg body, logs state to disc, then immediately terminates/quits. */
/* Typically used to abort due to an unrecoverable error. (e.g. out of memory) */
void Logger_Abort(const char* raw_msg);
/* Displays a message box with raw_msg body, logs state to disc, then immediately terminates/quits. */
/* Typically used to abort due to an unrecoverable error. (e.g. out of memory) */
HC_NOINLINE void Logger_Abort2(hc_result result, const char* raw_msg);
void Logger_FailToStart(const char* raw_msg);

HC_END_HEADER
#endif
