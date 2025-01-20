#ifndef HC_HTTP_H
#define HC_HTTP_H
#include "Constants.h"
#include "Core.h"
HC_BEGIN_HEADER

/* 
Aysnchronously performs http GET, HEAD, and POST requests
  Typically this is used to download skins, texture packs, etc
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct IGameComponent;
struct ScheduledTask;
struct StringsBuffer;

#define URL_MAX_SIZE (STRING_SIZE * 2)
#define HTTP_FLAG_PRIORITY 0x01
#define HTTP_FLAG_NOCACHE  0x02

extern struct IGameComponent Http_Component;

enum HttpRequestType { REQUEST_TYPE_GET, REQUEST_TYPE_HEAD, REQUEST_TYPE_POST };
enum HttpProgress {
	HTTP_PROGRESS_NOT_WORKING_ON = -3,
	HTTP_PROGRESS_MAKING_REQUEST = -2,
	HTTP_PROGRESS_FETCHING_DATA  = -1
};

struct HttpRequest {
	char url[URL_MAX_SIZE];   /* URL data is downloaded from/uploaded to. */
	int id;                   /* Unique identifier for this request. */
	volatile int progress;    /* Progress with downloading this request */
	hc_uint64 timeDownloaded; /* Time response contents were completely downloaded. */
	int statusCode;           /* HTTP status code returned in the response. */
	hc_uint32 contentLength;  /* HTTP content length returned in the response. */

	hc_result result; /* 0 on success, otherwise platform-specific error. */
	hc_uint8*   data; /* Contents of the response. (i.e. result data) */
	hc_uint32   size; /* Size of the contents. */
	hc_uint32 _capacity; /* (private) Maximum size of data buffer */
	void* meta;          /* Pointer to backend specific data */
	char* error;         /* Pointer to dynamically allocated error message */

	char lastModified[STRING_SIZE]; /* Time item cached at (if at all) */
	char etag[STRING_SIZE];         /* ETag of cached item (if any) */
	hc_uint8 requestType;           /* See the various REQUEST_TYPE_ */
	hc_bool success;                /* Whether Result is 0, status is 200, and data is not NULL */
	struct StringsBuffer* cookies;  /* Cookie list sent in requests. May be modified by the response. */
};

/* Frees all dynamically allocated data from a HTTP request */
void HttpRequest_Free(struct HttpRequest* request);

/* Aschronously performs a http GET request to download a skin. */
/* If url is a skin, downloads from there. (if not, downloads from SKIN_SERVER/[skinName].png) */
int Http_AsyncGetSkin(const hc_string* skinName, hc_uint8 flags);
/* Asynchronously performs a http GET request. (e.g. to download data) */
int Http_AsyncGetData(const hc_string* url, hc_uint8 flags);
/* Asynchronously performs a http HEAD request. (e.g. to get Content-Length header) */
int Http_AsyncGetHeaders(const hc_string* url, hc_uint8 flags);
/* Asynchronously performs a http POST request. (e.g. to submit data) */
/* NOTE: You don't have to persist data, a copy is made of it. */
int Http_AsyncPostData(const hc_string* url, hc_uint8 flags, const void* data, hc_uint32 size, struct StringsBuffer* cookies);
/* Asynchronously performs a http GET request. (e.g. to download data) */
/* Also sets the If-Modified-Since and If-None-Match headers. (if not NULL)  */
int Http_AsyncGetDataEx(const hc_string* url, hc_uint8 flags, const hc_string* lastModified, const hc_string* etag, struct StringsBuffer* cookies);
/* Attempts to remove given request from pending and finished request lists. */
/* NOTE: Won't cancel the request if it is currently in progress. */
void Http_TryCancel(int reqID);

/* Encodes data using % or URL encoding. */
void Http_UrlEncode(hc_string* dst, const hc_uint8* data, int len);
/* Converts characters to UTF8, then calls Http_UrlEncode on them. */
void Http_UrlEncodeUtf8(hc_string* dst, const hc_string* src);
/* Outputs more detailed information about errors with http requests. */
hc_bool Http_DescribeError(hc_result res, hc_string* dst);

/* Attempts to retrieve a fully completed request. */
/* NOTE: You MUST check Success for whether it completed successfully. */
/* (Data may still be non NULL even on error, e.g. on a http 404 error) */
hc_bool Http_GetResult(int reqID, struct HttpRequest* item);
/* Retrieves information about the request currently being processed. */
hc_bool Http_GetCurrent(int* reqID, int* progress);
/* Retrieves information about the download progress of the given request. */
/* NOTE: This may return HTTP_PROGRESS_NOT_WORKING_ON if download has finished. */
/*   As such, this method should always be paired with a call to Http_GetResult. */
int Http_CheckProgress(int reqID);
/* Clears the list of pending requests. */
void Http_ClearPending(void);

void Http_LogError(const char* action, const struct HttpRequest* item);

HC_END_HEADER
#endif
