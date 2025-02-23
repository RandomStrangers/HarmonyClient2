#include "Core.h"
#ifndef HC_BUILD_WEB
#include "_HttpBase.h"

/* Allocates initial data buffer to store response contents */
static void Http_BufferInit(struct HttpRequest* req) {
	req->progress  = 0;
	req->_capacity = req->contentLength ? req->contentLength : 1;
	req->data      = (hc_uint8*)Mem_Alloc(req->_capacity, 1, "http data");
	req->size      = 0;
}

/* Ensures data buffer has enough space left to append amount bytes, reallocates if not */
static void Http_BufferEnsure(struct HttpRequest* req, hc_uint32 amount) {
	hc_uint32 newSize = req->size + amount;
	if (newSize <= req->_capacity) return;

	req->_capacity = newSize;
	req->data      = (hc_uint8*)Mem_Realloc(req->data, newSize, 1, "http data+");
}

/* Increases size and updates current progress */
static void Http_BufferExpanded(struct HttpRequest* req, hc_uint32 read) {
	req->size += read;
	if (req->contentLength) req->progress = (int)(100.0f * req->size / req->contentLength);
}


/*########################################################################################################################*
*---------------------------------------------------Common backend code---------------------------------------------------*
*#########################################################################################################################*/
static void Http_ParseCookie(struct HttpRequest* req, const hc_string* value) {
	hc_string name, data;
	int dataEnd;
	String_UNSAFE_Separate(value, '=', &name, &data);
	/* Cookie is: __cfduid=xyz; expires=abc; path=/; domain=.classicube.net; HttpOnly */
	/* However only the __cfduid=xyz part of the cookie should be stored */
	dataEnd = String_IndexOf(&data, ';');
	if (dataEnd >= 0) data.length = dataEnd;

	EntryList_Set(req->cookies, &name, &data, '=');
}

static void Http_ParseContentLength(struct HttpRequest* req, const hc_string* value) {
	int contentLen = 0;
	Convert_ParseInt(value, &contentLen);
	
	if (contentLen <= 0) return;
	req->contentLength = contentLen;
}

/* Parses a HTTP header */
static void Http_ParseHeader(struct HttpRequest* req, const hc_string* line) {
	static const hc_string httpVersion = String_FromConst("HTTP");
	hc_string name, value, parts[3];
	int numParts;

	/* HTTP[version] [status code] [status reason] */
	if (String_CaselessStarts(line, &httpVersion)) {
		numParts = String_UNSAFE_Split(line, ' ', parts, 3);
		if (numParts >= 2) Convert_ParseInt(&parts[1], &req->statusCode);
	}
	/* For all other headers:  name: value */
	if (!String_UNSAFE_Separate(line, ':', &name, &value)) return;

	if (String_CaselessEqualsConst(&name, "ETag")) {
		String_CopyToRawArray(req->etag, &value);
	} else if (String_CaselessEqualsConst(&name, "Content-Length")) {
		Http_ParseContentLength(req, &value);
	} else if (String_CaselessEqualsConst(&name, "X-Dropbox-Content-Length")) {
		/* dropbox stopped returning Content-Length header since switching to chunked transfer */
		/*  https://www.dropboxforum.com/t5/Discuss-Dropbox-Developer-API/Dropbox-media-can-t-be-access-by-azure-blob/td-p/575458 */
		Http_ParseContentLength(req, &value);
	} else if (String_CaselessEqualsConst(&name, "Last-Modified")) {
		String_CopyToRawArray(req->lastModified, &value);
	} else if (req->cookies && String_CaselessEqualsConst(&name, "Set-Cookie")) {
		Http_ParseCookie(req, &value);
	}
}

/* Adds a http header to the request headers. */
static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value);

/* Adds all the appropriate headers for a request. */
static void Http_SetRequestHeaders(struct HttpRequest* req) {
	static const hc_string contentType = String_FromConst("application/x-www-form-urlencoded");
	hc_string str, cookies; char cookiesBuffer[1024];
	int i;

	if (req->lastModified[0]) {
		str = String_FromRawArray(req->lastModified);
		Http_AddHeader(req, "If-Modified-Since", &str);
	}
	if (req->etag[0]) {
		str = String_FromRawArray(req->etag);
		Http_AddHeader(req, "If-None-Match", &str);
	}

	if (req->data) Http_AddHeader(req, "Content-Type", &contentType);
	if (!req->cookies || !req->cookies->count) return;

	String_InitArray(cookies, cookiesBuffer);
	for (i = 0; i < req->cookies->count; i++) {
		if (i) String_AppendConst(&cookies, "; ");
		str = StringsBuffer_UNSAFE_Get(req->cookies, i);
		String_AppendString(&cookies, &str);
	}
	Http_AddHeader(req, "Cookie", &cookies);
}

/* TODO: Rewrite to use a local variable instead */
static hc_string* Http_GetUserAgent_UNSAFE(void) {
	static char userAgentBuffer[STRING_SIZE];
	static hc_string userAgent;

	String_InitArray(userAgent, userAgentBuffer);
	String_AppendConst(&userAgent, GAME_APP_NAME);
	String_AppendConst(&userAgent, Platform_AppNameSuffix);
	return &userAgent;
}


#if HC_NET_BACKEND == HC_NET_BACKEND_LIBCURL
/*########################################################################################################################*
*-----------------------------------------------------libcurl backend-----------------------------------------------------*
*#########################################################################################################################*/
#include "Errors.h"
#include <stddef.h>
/* === BEGIN CURL HEADERS === */
typedef void CURL;
struct curl_slist;
typedef int CURLcode;

#define CURL_GLOBAL_DEFAULT ((1<<0) | (1<<1)) /* SSL and Win32 options */
#define CURLOPT_WRITEDATA      (10000 + 1)
#define CURLOPT_URL            (10000 + 2)
#define CURLOPT_ERRORBUFFER    (10000 + 10)
#define CURLOPT_WRITEFUNCTION  (20000 + 11)
#define CURLOPT_POSTFIELDS     (10000 + 15)
#define CURLOPT_USERAGENT      (10000 + 18)
#define CURLOPT_HTTPHEADER     (10000 + 23)
#define CURLOPT_HEADERDATA     (10000 + 29)
#define CURLOPT_VERBOSE        (0     + 41)
#define CURLOPT_HEADER         (0     + 42)
#define CURLOPT_NOBODY         (0     + 44)
#define CURLOPT_POST           (0     + 47)
#define CURLOPT_FOLLOWLOCATION (0     + 52)
#define CURLOPT_POSTFIELDSIZE  (0     + 60)
#define CURLOPT_SSL_VERIFYPEER (0     + 64)
#define CURLOPT_MAXREDIRS      (0     + 68)
#define CURLOPT_HEADERFUNCTION (20000 + 79)
#define CURLOPT_HTTPGET        (0     + 80)
#define CURLOPT_SSL_VERIFYHOST (0     + 81)
#define CURLOPT_HTTP_VERSION   (0     + 84)

#define CURL_HTTP_VERSION_1_1   2L /* stick to HTTP 1.1 */

#if defined _WIN32
#define APIENTRY __cdecl
#else
#define APIENTRY
#endif

static CURLcode (APIENTRY *_curl_global_init)(long flags);
static void     (APIENTRY *_curl_global_cleanup)(void);
static CURL*    (APIENTRY *_curl_easy_init)(void);
static CURLcode (APIENTRY *_curl_easy_perform)(CURL *c);
static CURLcode (APIENTRY *_curl_easy_setopt)(CURL *c, int opt, ...);
static void     (APIENTRY *_curl_easy_cleanup)(CURL* c);
static void     (APIENTRY *_curl_slist_free_all)(struct curl_slist* l);
static struct curl_slist* (APIENTRY *_curl_slist_append)(struct curl_slist* l, const char* v);
static const char* (APIENTRY *_curl_easy_strerror)(CURLcode res);
/* === END CURL HEADERS === */

#if definedHC_BUILD_WIN
static const hc_string curlLib = String_FromConst("libcurl.dll");
static const hc_string curlAlt = String_FromConst("curl.dll");
#elif defined HC_BUILD_DARWIN
static const hc_string curlLib = String_FromConst("libcurl.4.dylib");
static const hc_string curlAlt = String_FromConst("libcurl.dylib");
#elif defined HC_BUILD_NETBSD
static const hc_string curlLib = String_FromConst("libcurl.so");
static const hc_string curlAlt = String_FromConst("/usr/pkg/lib/libcurl.so");
#elif defined HC_BUILD_BSD
static const hc_string curlLib = String_FromConst("libcurl.so");
static const hc_string curlAlt = String_FromConst("libcurl.so");
#elif defined HC_BUILD_SERENITY
static const hc_string curlLib = String_FromConst("/usr/local/lib/libcurl.so");
static const hc_string curlAlt = String_FromConst("/usr/local/lib/libcurl.so");
#elif defined HC_BUILD_OS2
static const hc_string curlLib = String_FromConst("/@unixroot/usr/lib/curl4.dll");
static const hc_string curlAlt = String_FromConst("/@unixroot/usr/local/lib/curl4.dll");
#else
static const hc_string curlLib = String_FromConst("libcurl.so.4");
static const hc_string curlAlt = String_FromConst("libcurl.so.3");
#endif

static hc_bool LoadCurlFuncs(void) {
	static const struct DynamicLibSym funcs[] = {
#if !defined HC_BUILD_OS2
		DynamicLib_Sym(curl_global_init),    DynamicLib_Sym(curl_global_cleanup),
		DynamicLib_Sym(curl_easy_init),      DynamicLib_Sym(curl_easy_perform),
		DynamicLib_Sym(curl_easy_setopt),    DynamicLib_Sym(curl_easy_cleanup),
		DynamicLib_Sym(curl_slist_free_all), DynamicLib_Sym(curl_slist_append)
#else
		DynamicLib_SymC(curl_global_init),    DynamicLib_SymC(curl_global_cleanup),
		DynamicLib_SymC(curl_easy_init),      DynamicLib_SymC(curl_easy_perform),
		DynamicLib_SymC(curl_easy_setopt),    DynamicLib_SymC(curl_easy_cleanup),
		DynamicLib_SymC(curl_slist_free_all), DynamicLib_SymC(curl_slist_append)
#endif
	};
	hc_bool success;
	void* lib;

	success = DynamicLib_LoadAll(&curlLib,     funcs, Array_Elems(funcs), &lib);
	if (!lib) { 
		success = DynamicLib_LoadAll(&curlAlt, funcs, Array_Elems(funcs), &lib);
	}

	/* Non-essential function missing in older curl versions */
	_curl_easy_strerror = DynamicLib_Get2(lib, "curl_easy_strerror");
	return success;
}

static CURL* curl;
static hc_bool curlSupported, curlVerbose;

static hc_bool HttpBackend_DescribeError(hc_result res, hc_string* dst) {
	const char* err;
	
	if (!_curl_easy_strerror) return false;
	err = _curl_easy_strerror((CURLcode)res);
	if (!err) return false;

	String_AppendConst(dst, err);
	return true;
}

static void HttpBackend_Init(void) {
	static const hc_string msg = String_FromConst("Failed to init libcurl. All HTTP requests will therefore fail.");
	CURLcode res;

	if (!LoadCurlFuncs()) { Logger_WarnFunc(&msg); return; }
	res = _curl_global_init(CURL_GLOBAL_DEFAULT);
	if (res) { Logger_SimpleWarn(res, "initing curl"); return; }
	curl = _curl_easy_init();
	if (!curl) { Logger_SimpleWarn(res, "initing curl_easy"); return; }

	curlSupported = true;
	curlVerbose = Options_GetBool("curl-verbose", false);
}

static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value) {
	hc_string tmp; char tmpBuffer[1024];
	String_InitArray_NT(tmp, tmpBuffer);
	String_Format2(&tmp, "%c: %s", key, value);

	tmp.buffer[tmp.length] = '\0';
	req->meta = _curl_slist_append((struct curl_slist*)req->meta, tmp.buffer);
}

/* Processes a HTTP header downloaded from the server */
static size_t Http_ProcessHeader(char* buffer, size_t size, size_t nitems, void* userdata) {
	struct HttpRequest* req = (struct HttpRequest*)userdata;
	size_t len = nitems;
	hc_string line;
	/* line usually has \r\n at end */
	if (len && buffer[len - 1] == '\n') len--;
	if (len && buffer[len - 1] == '\r') len--;

	line = String_Init(buffer, len, len);
	Http_ParseHeader(req, &line);
	return nitems;
}

/* Processes a chunk of data downloaded from the web server */
static size_t Http_ProcessData(char *buffer, size_t size, size_t nitems, void* userdata) {
	struct HttpRequest* req = (struct HttpRequest*)userdata;

	if (!req->_capacity) Http_BufferInit(req);
	Http_BufferEnsure(req, nitems);

	Mem_Copy(&req->data[req->size], buffer, nitems);
	Http_BufferExpanded(req, nitems);
	return nitems;
}

/* Sets general curl options for a request */
static void Http_SetCurlOpts(struct HttpRequest* req) {
	_curl_easy_setopt(curl, CURLOPT_USERAGENT,      GAME_APP_NAME);
	_curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	_curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      20L);
	_curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,   CURL_HTTP_VERSION_1_1);

	_curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Http_ProcessHeader);
	_curl_easy_setopt(curl, CURLOPT_HEADERDATA,     req);
	_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  Http_ProcessData);
	_curl_easy_setopt(curl, CURLOPT_WRITEDATA,      req);

	if (curlVerbose) _curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	if (httpsVerify) return;
	_curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
}

static hc_result HttpBackend_Do(struct HttpRequest* req, hc_string* url) {
	char urlStr[NATIVE_STR_LEN];
	void* post_data = req->data;
	CURLcode res;
	if (!curlSupported) return ERR_NOT_SUPPORTED;

	req->meta = NULL;
	Http_SetRequestHeaders(req);
	_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req->meta);

	Http_SetCurlOpts(req);
	String_EncodeUtf8(urlStr, url);
	_curl_easy_setopt(curl, CURLOPT_URL, urlStr);

	if (req->requestType == REQUEST_TYPE_HEAD) {
		_curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	} else if (req->requestType == REQUEST_TYPE_POST) {
		_curl_easy_setopt(curl, CURLOPT_POST,   1L);
		_curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req->size);
		_curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    req->data);

		/* per curl docs, we must persist POST data until request finishes */
		req->data = NULL;
		req->size = 0;
	} else {
		/* Undo POST/HEAD state */
		_curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	}

	/* must be at least CURL_ERROR_SIZE (256) in size */
	req->error = Mem_TryAllocCleared(257, 1);
	_curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, req->error);
	/* TODO stackalloc instead and then copy to dynamic array later? */
	/*  probably not worth the extra complexity though */

	req->_capacity = 0;
	req->progress  = HTTP_PROGRESS_FETCHING_DATA;
	res = _curl_easy_perform(curl);
	req->progress  = 100;

	/* Free error string if it isn't needed */
	if (req->error && !req->error[0]) {
		Mem_Free(req->error);
		req->error = NULL;
	}

	_curl_slist_free_all((struct curl_slist*)req->meta);
	/* can free now that request has finished */
	Mem_Free(post_data);
	_curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, NULL);
	return res;
}
#elif HC_NET_BACKEND == HC_NET_BACKEND_BUILTIN
#include "Errors.h"
#include "PackedCol.h"
#include "SSL.h"

/*########################################################################################################################*
*---------------------------------------------------------HttpUrl---------------------------------------------------------*
*#########################################################################################################################*/
/* Components of a URL */
struct HttpUrl {
	hc_bool https;      /* Whether HTTPS or just HTTP protocol */
	hc_string address;  /* Address of server (e.g. "classicube.net:8080") */
	hc_string resource; /* Path being accessed (and query string) */
	char _addressBuffer[STRING_SIZE + 8];
	char _resourceBuffer[STRING_SIZE * 4];
};

static void HttpUrl_EncodeUrl(hc_string* dst, const hc_string* src) {
	hc_uint8 data[4];
	int i, len;
	char c;

	for (i = 0; i < src->length; i++) {
		c   = src->buffer[i];
		len = Convert_CP437ToUtf8(c, data);

		/* URL path/query must not be URL encoded (it normally would be) */
		if (c == '/' || c == '?' || c == '=') {
			String_Append(dst, c);
		} else {
			Http_UrlEncode(dst, data, len);
		}
	}
}

/* Splits up the components of a URL */
static void HttpUrl_Parse(const hc_string* src, struct HttpUrl* url) {
	hc_string scheme, path, addr, resource;
	/* URL is of form [scheme]://[server host]:[server port]/[resource] */
	/* For simplicity, parsed as [scheme]://[server address]/[resource] */
	int idx = String_IndexOfConst(src, "://");

	scheme = idx == -1 ? String_Empty : String_UNSAFE_Substring(src,   0, idx);
	path   = idx == -1 ? *src         : String_UNSAFE_SubstringAt(src, idx + 3);

	url->https = String_CaselessEqualsConst(&scheme, "https");
	String_UNSAFE_Separate(&path, '/', &addr, &resource);

	String_InitArray(url->address, url->_addressBuffer);
	String_Copy(&url->address, &addr);

	String_InitArray(url->resource, url->_resourceBuffer);
	String_Append(&url->resource, '/');
	/* Address may have unicode characters - need to percent encode them */
	HttpUrl_EncodeUrl(&url->resource, &resource);
}

static hc_result HttpUrl_ResolveRedirect(struct HttpUrl* parts, const hc_string* url) {
	/* absolute URL */
	if (String_IndexOfConst(url, "http://") == 0 || String_IndexOfConst(url, "https://") == 0) {		
		HttpUrl_Parse(url, parts);
		return 0;
	} 

	/* Root relative URL */
	if (url->buffer[0] == '/' && (url->length == 1 || url->buffer[1] != '/')) {
		parts->resource.length = 0;
		HttpUrl_EncodeUrl(&parts->resource, url);
		return 0;
	}

	/* TODO scheme relative or relative URL or invalid */
	return HTTP_ERR_RELATIVE;
}


/*########################################################################################################################*
*------------------------------------------------------HttpConnection-----------------------------------------------------*
*#########################################################################################################################*/
struct HttpConnection {
	hc_socket socket;
	void* sslCtx;
	hc_bool valid;
};

static void HttpConnection_Close(struct HttpConnection* conn) {
	if (conn->sslCtx) {
		SSL_Free(conn->sslCtx);
		conn->sslCtx = NULL;
	}

	if (conn->socket != -1) {
		Socket_Close(conn->socket);
		conn->socket = -1;
	}
	conn->valid = false;
}

static void ExtractHostPort(const struct HttpUrl* url, hc_string* host, hc_string* port) {
	/* address can have the form of either "host" or "host:port" */
	/* Slightly more complicated because IPv6 hosts can be e.g. [::1] */
	const hc_string* addr = &url->address;
	int idx = String_LastIndexOf(addr, ':');

	if (idx == -1) {
		*host = *addr;
		*port = String_Empty;
	} else {
		*host = String_UNSAFE_Substring(addr, 0, idx);
		*port = String_UNSAFE_SubstringAt(addr, idx + 1);
	}
}

static hc_result HttpConnection_Open(struct HttpConnection* conn, const struct HttpUrl* url) {
	hc_string host, port;
	hc_uint16 portNum;
	hc_result res;
	hc_sockaddr addrs[SOCKET_MAX_ADDRS];
	int i, numValidAddrs;

	ExtractHostPort(url, &host, &port);
	if (!Convert_ParseUInt16(&port, &portNum)) {
		portNum = url->https ? 443 : 80;
	}

	conn->socket = -1;
	conn->sslCtx = NULL;

	if ((res = Socket_ParseAddress(&host, portNum, addrs, &numValidAddrs))) return res;
	res = ERR_INVALID_ARGUMENT; /* in case 0 valid addresses */

	/* TODO: Connect in parallel instead of serial, but that's a lot of work */
	for (i = 0; i < numValidAddrs; i++)
	{
		res = Socket_Create(&conn->socket, &addrs[i], false);
		if (res) { HttpConnection_Close(conn); continue; }

		res = Socket_Connect(conn->socket, &addrs[i]);
		if (res) { HttpConnection_Close(conn); continue; }

		break; /* Successful connection */
	}
	if (res) return res;

	conn->valid = true;
	if (!url->https) return 0;
	return SSL_Init(conn->socket, &host, &conn->sslCtx);
}

static hc_result HttpConnection_Read(struct HttpConnection* conn, hc_uint8* data, hc_uint32 count, hc_uint32* read) {
	if (conn->sslCtx)
		return SSL_Read(conn->sslCtx, data, count, read);

	return Socket_Read(conn->socket,  data, count, read);
}

static hc_result HttpConnection_Write(struct HttpConnection* conn, const hc_uint8* data, hc_uint32 count) {
	if (conn->sslCtx) 
		return SSL_WriteAll(conn->sslCtx, data, count);

	return Socket_WriteAll(conn->socket,  data, count);
}


/*########################################################################################################################*
*-----------------------------------------------------Connection Pool-----------------------------------------------------*
*#########################################################################################################################*/
static struct ConnectionPoolEntry {
	struct HttpConnection conn;
	hc_string addr;
	char addrBuffer[STRING_SIZE];
	hc_bool https;
} connection_pool[10];

static hc_result ConnectionPool_Insert(int i, struct HttpConnection** conn, const struct HttpUrl* url) {
	struct ConnectionPoolEntry* e = &connection_pool[i];
	*conn = &e->conn;

	String_InitArray(e->addr, e->addrBuffer);
	String_Copy(&e->addr, &url->address);
	e->https = url->https;
	return HttpConnection_Open(&e->conn, url);
}

static hc_result ConnectionPool_Open(struct HttpConnection** conn, const struct HttpUrl* url) {
	struct ConnectionPoolEntry* e;
	int i;

	for (i = 0; i < Array_Elems(connection_pool); i++)
	{
		e = &connection_pool[i];
		if (e->conn.valid && e->https == url->https && String_Equals(&e->addr, &url->address)) {
			*conn = &connection_pool[i].conn;
			return 0;
		}
	}

	for (i = 0; i < Array_Elems(connection_pool); i++)
	{
		e = &connection_pool[i];
		if (!e->conn.valid) return ConnectionPool_Insert(i, conn, url);
	}

	/* TODO: Should we be consistent in which entry gets evicted? */
	i = (hc_uint8)Stopwatch_Measure() % Array_Elems(connection_pool);
	HttpConnection_Close(&connection_pool[i].conn);
	return ConnectionPool_Insert(i, conn, url);
}


/*########################################################################################################################*
*--------------------------------------------------------HttpClient-------------------------------------------------------*
*#########################################################################################################################*/
enum HTTP_RESPONSE_STATE {
	HTTP_RESPONSE_STATE_INITIAL,
	HTTP_RESPONSE_STATE_HEADER,
	HTTP_RESPONSE_STATE_DATA,
	HTTP_RESPONSE_STATE_CHUNK_HEADER,
	HTTP_RESPONSE_STATE_CHUNK_END_R,
	HTTP_RESPONSE_STATE_CHUNK_END_N,
	HTTP_RESPONSE_STATE_CHUNK_TRAILERS,
	HTTP_RESPONSE_STATE_DONE
};
#define HTTP_HEADER_MAX_LENGTH   4096
#define HTTP_LOCATION_MAX_LENGTH 256

struct HttpClientState {
	enum HTTP_RESPONSE_STATE state;
	struct HttpConnection* conn;
	struct HttpRequest* req;
	hc_uint32 dataLeft; /* Number of bytes still to read from the current chunk or body */
	int chunked;
	hc_bool autoClose;
	hc_string header, location;
	struct HttpUrl url;
	char _headerBuffer[HTTP_HEADER_MAX_LENGTH];
	char _locationBuffer[HTTP_LOCATION_MAX_LENGTH];
};

static void HttpClientState_Reset(struct HttpClientState* state) {
	state->state       = HTTP_RESPONSE_STATE_INITIAL;
	state->chunked     = 0;
	state->dataLeft    = 0;
	state->autoClose   = false;
	String_InitArray(state->header,   state->_headerBuffer);
	String_InitArray(state->location, state->_locationBuffer);
}

static void HttpClientState_Init(struct HttpClientState* state) {
	HttpClientState_Reset(state);
}


static void HttpClient_Serialise(struct HttpClientState* state) {
	static const char* verbs[] = { "GET", "HEAD", "POST" };

	struct HttpRequest* req = state->req;
	hc_string* buffer = (hc_string*)req->meta;
	/* TODO move to other functions */
	/* Write request message headers */
	String_Format2(buffer, "%c %s HTTP/1.1\r\n",
					verbs[req->requestType], &state->url.resource);

	Http_AddHeader(req, "Host",       &state->url.address);
	Http_AddHeader(req, "User-Agent", Http_GetUserAgent_UNSAFE());
	if (req->data) String_Format1(buffer, "Content-Length: %i\r\n", &req->size);

	Http_SetRequestHeaders(req);
	String_AppendConst(buffer, "\r\n");
	
	/* Write request message body */
	if (req->data) {
		String_AppendAll(buffer, req->data, req->size);
		HttpRequest_Free(req);
	} /* TODO post redirect handling */
}

static hc_result HttpClient_SendRequest(struct HttpClientState* state) {
	char inputBuffer[16384];
	hc_string inputMsg;

	String_InitArray(inputMsg, inputBuffer);
	state->req->meta     = &inputMsg;
	state->req->progress = HTTP_PROGRESS_FETCHING_DATA;
	HttpClient_Serialise(state);

	return HttpConnection_Write(state->conn, (hc_uint8*)inputBuffer, inputMsg.length);
}


static void HttpClient_ParseHeader(struct HttpClientState* state, const hc_string* line) {
	static const hc_string HTTP_10_VERSION = String_FromConst("HTTP/1.0");
	hc_string name, value;
	/* HTTP 1.0 defaults to auto closing connection */
	if (String_CaselessStarts(line, &HTTP_10_VERSION)) state->autoClose = true;

	/* name: value */
	if (!String_UNSAFE_Separate(line, ':', &name, &value)) return;

	if (String_CaselessEqualsConst(&name, "Transfer-Encoding")) {
		state->chunked = String_CaselessEqualsConst(&value, "chunked");
	} else if (String_CaselessEqualsConst(&name, "Location")) {
		String_Copy(&state->location, &value);
	} else if (String_CaselessEqualsConst(&name, "Connection")) {
		if (String_CaselessEqualsConst(&value, "keep-alive")) state->autoClose = false;
		if (String_CaselessEqualsConst(&value, "close"))      state->autoClose = true;
	}
}

/* RFC 7230, section 3.3.3 - Message Body Length */
static hc_bool HttpClient_HasBody(struct HttpRequest* req) {
	/* HEAD responses never have a message body */
	if (req->requestType == REQUEST_TYPE_HEAD) return false;
	/* 1XX (Information) responses don't have message body */
	if (req->statusCode >= 100 && req->statusCode <= 199) return false;
	/* 204 (No Content) and 304 (Not Modified) also don't */
	if (req->statusCode == 204 || req->statusCode == 304) return false;

	return true;
}

static int HttpClient_BeginBody(struct HttpRequest* req, struct HttpClientState* state) {
	if (!HttpClient_HasBody(req))
		return HTTP_RESPONSE_STATE_DONE;
	
	if (state->chunked) {
		Http_BufferInit(req);
		return HTTP_RESPONSE_STATE_CHUNK_HEADER;
	}
	if (req->contentLength) {
		Http_BufferInit(req);
		return HTTP_RESPONSE_STATE_DATA;
	}
	/* Zero length response */
	return HTTP_RESPONSE_STATE_DONE;
}

/* RFC 7230, section 4.1 - Chunked Transfer Coding */
static int HttpClient_GetChunkLength(const hc_string* line) {
	int length = 0, i, part;

	for (i = 0; i < line->length; i++) 
	{
		char c = line->buffer[i];
		/* RFC 7230, section 4.1.1 - Chunk Extensions */
		if (c == ';') break;

		part = PackedCol_DeHex(c);
		if (part == -1) return -1;
		length = (length << 4) | part;
	}
	return length;
}

/* https://httpwg.org/specs/rfc7230.html */
static hc_result HttpClient_Process(struct HttpClientState* state, char* buffer, int total) {
	struct HttpRequest* req = state->req;
	hc_uint32 left, avail, read;
	int offset = 0, chunkLen;

	while (offset < total) {
		switch (state->state) {
		case HTTP_RESPONSE_STATE_INITIAL:
			state->state = HTTP_RESPONSE_STATE_HEADER;
			break;

		case HTTP_RESPONSE_STATE_HEADER:
		{
			for (; offset < total;) 
			{
				char c = buffer[offset++];
				if (c == '\r') continue;
				if (c != '\n') {
					/* Warn when a header would be truncated */
					if (state->header.length == HTTP_HEADER_MAX_LENGTH) 
						return HTTP_ERR_TRUNCATED;

					String_Append(&state->header, c); 
					continue; 
				}

				/* Zero length header = end of message headers */
				if (state->header.length == 0) {
					state->state = HttpClient_BeginBody(req, state);

					/* The rest of the request body is just content/data */
					if (state->state == HTTP_RESPONSE_STATE_DATA) {
						Http_BufferEnsure(req, req->contentLength);
						state->dataLeft = req->contentLength;
					}
					break;
				}

				Http_ParseHeader(state->req, &state->header);
				HttpClient_ParseHeader(state, &state->header);
				state->header.length = 0;
			}
		}
		break;

		case HTTP_RESPONSE_STATE_DATA:
		{
			left  = total - offset;
			avail = state->dataLeft;
			read  = min(left, avail);

			Mem_Copy(req->data + req->size, buffer + offset, read);
			Http_BufferExpanded(req, read); state->dataLeft -= read;
			offset += read;

			if (!state->dataLeft) {
				state->state = state->chunked ? HTTP_RESPONSE_STATE_CHUNK_END_R : HTTP_RESPONSE_STATE_DONE;
			}
		}
		break;


		/* RFC 7230, section 4.1 - Chunked Transfer Coding */
		case HTTP_RESPONSE_STATE_CHUNK_HEADER:
		{
			for (; offset < total;) 
			{
				char c = buffer[offset++];
				if (c == '\r') continue;
				if (c != '\n') { String_Append(&state->header, c); continue; }

				chunkLen = HttpClient_GetChunkLength(&state->header);
				if (chunkLen < 0) return HTTP_ERR_CHUNK_SIZE;
				state->header.length = 0;

				if (chunkLen == 0) {
					state->state = HTTP_RESPONSE_STATE_CHUNK_TRAILERS;
				} else {
					state->state = HTTP_RESPONSE_STATE_DATA;
					Http_BufferEnsure(req, chunkLen);
					state->dataLeft = chunkLen;
				}
				break;
			}
		}
		break;

		/* Chunks are terminated by \r\n */
		case HTTP_RESPONSE_STATE_CHUNK_END_R:
			if (buffer[offset++] != '\r') return ERR_INVALID_ARGUMENT;

			state->state = HTTP_RESPONSE_STATE_CHUNK_END_N;
			break;

		case HTTP_RESPONSE_STATE_CHUNK_END_N:
			if (buffer[offset++] != '\n') return ERR_INVALID_ARGUMENT;

			state->state = HTTP_RESPONSE_STATE_CHUNK_HEADER;
			break;

		/* RFC 7230, section 4.1.2 - Chunked Trailer Part */
		case HTTP_RESPONSE_STATE_CHUNK_TRAILERS:
		{
			for (; offset < total;) 
			{
				char c = buffer[offset++];
				if (c == '\r') continue;
				if (c != '\n') { String_Append(&state->header, c); continue; }

				/* Zero length header = end of message trailers */
				if (state->header.length == 0) {
					state->state = HTTP_RESPONSE_STATE_DONE;
					break;
				}
				state->header.length = 0;
			}
		} 
		break;

		default:
			return 0;
		}
	}
	return 0;
}

#define INPUT_BUFFER_LEN 8192
static hc_result HttpClient_ParseResponse(struct HttpClientState* state) {
	struct HttpRequest* req = state->req;
	hc_uint8 buffer[INPUT_BUFFER_LEN];
	hc_uint8* dst;
	hc_uint32 total;
	hc_result res;

	for (;;) 
	{
		dst = state->dataLeft > INPUT_BUFFER_LEN ? (req->data + req->size) : buffer;
		res = HttpConnection_Read(state->conn, dst, INPUT_BUFFER_LEN, &total);
		if (res) return res;

		if (total == 0) {
			Platform_Log1("Http read unexpectedly returned 0 in state %i", &state->state);
			return state->state == HTTP_RESPONSE_STATE_INITIAL ? HTTP_ERR_NO_RESPONSE : ERR_END_OF_STREAM;
		}

		if (dst != buffer) {
			/* When there is more than INPUT_BUFFER_LEN bytes of unread data/content, */
			/*  there is no need to run the HTTP client state machine - just read directly */
			/*  into the output buffer to avoid a pointless Mem_Copy call */
			Http_BufferExpanded(req, total); 
			state->dataLeft -= total;
		} else {
			res = HttpClient_Process(state, (char*)buffer, total);
		}

		if (res) return res;
		if (state->state == HTTP_RESPONSE_STATE_DONE) return 0;
	}
}

static hc_bool HttpClient_IsRedirect(struct HttpRequest* req) {
	return req->statusCode >= 300 && req->statusCode <= 399 && req->statusCode != 304;
}

static hc_result HttpClient_HandleRedirect(struct HttpClientState* state) {
	hc_result res = HttpUrl_ResolveRedirect(&state->url, &state->location);
	if (res) return res;

	HttpRequest_Free(state->req);
	Platform_Log1("  Redirecting to: %s", &state->location);
	state->req->contentLength = 0; /* TODO */
	return 0;
}


/*########################################################################################################################*
*-----------------------------------------------Http backend implementation-----------------------------------------------*
*#########################################################################################################################*/
static void HttpBackend_Init(void) {
	SSLBackend_Init(httpsVerify);
	//httpOnly = true; // TODO: insecure
}

static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value) {
	String_Format2((hc_string*)req->meta, "%c:%s\r\n", key, value);
}

static hc_result HttpBackend_PerformRequest(struct HttpClientState* state) {
	hc_result res;

	res = ConnectionPool_Open(&state->conn, &state->url);
	if (res) { HttpConnection_Close(state->conn); return res; }

	res = HttpClient_SendRequest(state);
	if (res) { HttpConnection_Close(state->conn); return res; }

	res = HttpClient_ParseResponse(state);
	if (res) HttpConnection_Close(state->conn);

	return res;
}

static hc_result HttpBackend_Do(struct HttpRequest* req, hc_string* urlStr) {
	struct HttpClientState state;
	hc_bool retried = false;
	int redirects   = 0;
	hc_result res;

	HttpClientState_Init(&state);
	HttpUrl_Parse(urlStr, &state.url);
	state.req = req;

	for (;;) {
		res = HttpBackend_PerformRequest(&state);
		/* TODO: Can we handle this while preserving the TCP connection */
		if (res == SSL_ERR_CONTEXT_DEAD && !retried) {
			Platform_LogConst("Resetting connection due to SSL context being dropped..");
			res = HttpBackend_PerformRequest(&state);
			retried = true;
		}
		if (res == HTTP_ERR_NO_RESPONSE && !retried) {
			Platform_LogConst("Resetting connection due to empty response..");
			res = HttpBackend_PerformRequest(&state);
			retried = true;
		}
		if (res == ReturnCode_SocketDropped && !retried) {
			Platform_LogConst("Resetting connection due to being dropped..");
			res = HttpBackend_PerformRequest(&state);
			retried = true;
		}

		if (res || !HttpClient_IsRedirect(req)) break;
		if (redirects >= 20) return HTTP_ERR_REDIRECTS;

		/* TODO FOLLOW LOCATION PROPERLY */
		redirects++;
		res = HttpClient_HandleRedirect(&state);
		if (res) break;
		HttpClientState_Reset(&state);
	}
	return res;
}

static hc_bool HttpBackend_DescribeError(hc_result res, hc_string* dst) {
	return SSLBackend_DescribeError(res, dst);
}
#elif defined HC_BUILD_ANDROID
/*########################################################################################################################*
*-----------------------------------------------------Android backend-----------------------------------------------------*
*#########################################################################################################################*/
struct HttpRequest* java_req;
static jmethodID JAVA_httpInit, JAVA_httpSetHeader, JAVA_httpPerform, JAVA_httpSetData;
static jmethodID JAVA_httpDescribeError;

static hc_bool HttpBackend_DescribeError(hc_result res, hc_string* dst) {
	char buffer[NATIVE_STR_LEN];
	hc_string err;
	JNIEnv* env;
	jvalue args[1];
	jobject obj;
	
	JavaGetCurrentEnv(env);
	args[0].i = res;
	obj       = JavaSCall_Obj(env, JAVA_httpDescribeError, args);
	if (!obj) return false;

	err = JavaGetString(env, obj, buffer);
	String_AppendString(dst, &err);
	(*env)->DeleteLocalRef(env, obj);
	return true;
}

static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value) {
	JNIEnv* env;
	jvalue args[2];

	JavaGetCurrentEnv(env);
	args[0].l = JavaMakeConst(env,  key);
	args[1].l = JavaMakeString(env, value);

	JavaSCall_Void(env, JAVA_httpSetHeader, args);
	(*env)->DeleteLocalRef(env, args[0].l);
	(*env)->DeleteLocalRef(env, args[1].l);
}

/* Processes a HTTP header downloaded from the server */
static void JNICALL java_HttpParseHeader(JNIEnv* env, jobject o, jstring header) {
	char buffer[NATIVE_STR_LEN];
	hc_string line = JavaGetString(env, header, buffer);
	Http_ParseHeader(java_req, &line);
}

/* Processes a chunk of data downloaded from the web server */
static void JNICALL java_HttpAppendData(JNIEnv* env, jobject o, jbyteArray arr, jint len) {
	struct HttpRequest* req = java_req;
	if (!req->_capacity) Http_BufferInit(req);

	Http_BufferEnsure(req, len);
	(*env)->GetByteArrayRegion(env, arr, 0, len, (jbyte*)(&req->data[req->size]));
	Http_BufferExpanded(req, len);
}

static const JNINativeMethod methods[] = {
	{ "httpParseHeader", "(Ljava/lang/String;)V", java_HttpParseHeader },
	{ "httpAppendData",  "([BI)V",                java_HttpAppendData }
};
static void CacheMethodRefs(JNIEnv* env) {
	JAVA_httpInit      = JavaGetSMethod(env, "httpInit",      "(Ljava/lang/String;Ljava/lang/String;)I");
	JAVA_httpSetHeader = JavaGetSMethod(env, "httpSetHeader", "(Ljava/lang/String;Ljava/lang/String;)V");
	JAVA_httpPerform   = JavaGetSMethod(env, "httpPerform",   "()I");
	JAVA_httpSetData   = JavaGetSMethod(env, "httpSetData",   "([B)I");

	JAVA_httpDescribeError = JavaGetSMethod(env, "httpDescribeError", "(I)Ljava/lang/String;");
}

static void HttpBackend_Init(void) {
	JNIEnv* env;
	JavaGetCurrentEnv(env);
	JavaRegisterNatives(env, methods);
	CacheMethodRefs(env);
}

static hc_result Http_InitReq(JNIEnv* env, struct HttpRequest* req, hc_string* url) {
	static const char* verbs[3] = { "GET", "HEAD", "POST" };
	jvalue args[2];
	jint res;

	args[0].l = JavaMakeString(env, url);
	args[1].l = JavaMakeConst(env,  verbs[req->requestType]);

	res = JavaSCall_Int(env, JAVA_httpInit, args);
	(*env)->DeleteLocalRef(env, args[0].l);
	(*env)->DeleteLocalRef(env, args[1].l);
	return res;
}

static hc_result Http_SetData(JNIEnv* env, struct HttpRequest* req) {
	jvalue args[1];
	jint res;

	args[0].l = JavaMakeBytes(env, req->data, req->size);
	res = JavaSCall_Int(env, JAVA_httpSetData, args);
	(*env)->DeleteLocalRef(env, args[0].l);
	return res;
}

static hc_result HttpBackend_Do(struct HttpRequest* req, hc_string* url) {
	JNIEnv* env;
	jint res;

	JavaGetCurrentEnv(env);
	if ((res = Http_InitReq(env, req, url))) return res;
	java_req = req;

	Http_SetRequestHeaders(req);
	Http_AddHeader(req, "User-Agent", Http_GetUserAgent_UNSAFE());
	if (req->data && (res = Http_SetData(env, req))) return res;

	req->_capacity = 0;
	req->progress  = HTTP_PROGRESS_FETCHING_DATA;
	res = JavaSCall_Int(env, JAVA_httpPerform, NULL);
	req->progress  = 100;
	return res;
}
#elif defined HC_BUILD_CFNETWORK
/*########################################################################################################################*
*----------------------------------------------------CFNetwork backend----------------------------------------------------*
*#########################################################################################################################*/
#include "Errors.h"
#include <stddef.h>
#include <CFNetwork/CFNetwork.h>

static hc_bool HttpBackend_DescribeError(hc_result res, hc_string* dst) {
    return false;
}

static void HttpBackend_Init(void) {
    
}

static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value) {
    char tmp[NATIVE_STR_LEN];
    CFStringRef keyCF, valCF;
    CFHTTPMessageRef msg = (CFHTTPMessageRef)req->meta;
    String_EncodeUtf8(tmp, value);
    
    keyCF = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    valCF = CFStringCreateWithCString(NULL, tmp, kCFStringEncodingUTF8);
    CFHTTPMessageSetHeaderFieldValue(msg, keyCF, valCF);
    CFRelease(keyCF);
    CFRelease(valCF);
}

static void Http_CheckHeader(const void* k, const void* v, void* ctx) {
    hc_string line; char lineBuffer[2048];
    char keyBuf[128]  = { 0 };
    char valBuf[1024] = { 0 };
    String_InitArray(line, lineBuffer);
    
    CFStringGetCString((CFStringRef)k, keyBuf, sizeof(keyBuf), kCFStringEncodingUTF8);
    CFStringGetCString((CFStringRef)v, valBuf, sizeof(valBuf), kCFStringEncodingUTF8);
    
    String_Format2(&line, "%c:%c", keyBuf, valBuf);
    Http_ParseHeader((struct HttpRequest*)ctx, &line);
    ctx = NULL;
}

static hc_result ParseResponseHeaders(struct HttpRequest* req, CFReadStreamRef stream) {
    CFHTTPMessageRef response = CFReadStreamCopyProperty(stream, kCFStreamPropertyHTTPResponseHeader);
    if (!response) return ERR_INVALID_ARGUMENT;
    
    CFDictionaryRef headers = CFHTTPMessageCopyAllHeaderFields(response);
    CFDictionaryApplyFunction(headers, Http_CheckHeader, req);
    req->statusCode = CFHTTPMessageGetResponseStatusCode(response);
    
    CFRelease(headers);
    CFRelease(response);
    return 0;
}

static hc_result HttpBackend_Do(struct HttpRequest* req, hc_string* url) {
    static CFStringRef verbs[] = { CFSTR("GET"), CFSTR("HEAD"), CFSTR("POST") };
    hc_bool gotHeaders = false;
    char tmp[NATIVE_STR_LEN];
    CFHTTPMessageRef request;
    CFStringRef urlCF;
    CFURLRef urlRef;
    cc_result result = 0;
    
    String_EncodeUtf8(tmp, url);
    urlCF  = CFStringCreateWithCString(NULL, tmp, kCFStringEncodingUTF8);
    urlRef = CFURLCreateWithString(NULL, urlCF, NULL);
    // TODO e.g. "http://www.example.com/skin/1 2.png" causes this to return null
    // TODO release urlCF
    if (!urlRef) return ERR_INVALID_DATA_URL;
    
    request = CFHTTPMessageCreateRequest(NULL, verbs[req->requestType], urlRef, kCFHTTPVersion1_1);
    req->meta = request;
    Http_SetRequestHeaders(req);
    Http_AddHeader(req, "User-Agent", Http_GetUserAgent_UNSAFE());
    CFRelease(urlRef);
    
    if (req->data && req->size) {
        CFDataRef body = CFDataCreate(NULL, req->data, req->size);
        CFHTTPMessageSetBody(request, body);
        CFRelease(body); /* TODO: ???? */
        
        req->data = NULL;
        req->size = 0;
        Mem_Free(req->data);
    }
    
    CFReadStreamRef stream = CFReadStreamCreateForHTTPRequest(NULL, request);
    CFReadStreamSetProperty(stream, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanTrue);
    //CFHTTPReadStreamSetRedirectsAutomatically(stream, TRUE);
    CFReadStreamOpen(stream);
    UInt8 buf[1024];
    
    for (;;) {
        CFIndex read = CFReadStreamRead(stream, buf, sizeof(buf));
        if (read <= 0) break;
        
        // reading headers before for loop doesn't work
        if (!gotHeaders) {
            gotHeaders = true;
            if ((result = ParseResponseHeaders(req, stream))) break;
        }
        
        if (!req->_capacity) Http_BufferInit(req);
        Http_BufferEnsure(req, read);
        
        Mem_Copy(&req->data[req->size], buf, read);
        Http_BufferExpanded(req, read);
    }
    
    if (!gotHeaders)
        result = ParseResponseHeaders(req, stream);
    
    //Thread_Sleep(1000);
    CFRelease(request);
    return result;
}
#elif !defined HC_BUILD_NETWORKING
/*########################################################################################################################*
*------------------------------------------------------Null backend-------------------------------------------------------*
*#########################################################################################################################*/
#include "Errors.h"

static hc_bool HttpBackend_DescribeError(hc_result res, hc_string* dst) {
	return false;
}

static void HttpBackend_Init(void) { }

static void Http_AddHeader(struct HttpRequest* req, const char* key, const hc_string* value) { }

static cc_result HttpBackend_Do(struct HttpRequest* req, hc_string* url) {
	req->progress = 100;
	return ERR_NOT_SUPPORTED;
}
#endif


static void* workerWaitable;
static void* workerThread;

static void* pendingMutex;
static struct RequestList pendingReqs;

static void* curRequestMutex;
static struct HttpRequest http_curRequest;


/*########################################################################################################################*
*----------------------------------------------------Http public api------------------------------------------------------*
*#########################################################################################################################*/
hc_bool Http_GetResult(int reqID, struct HttpRequest* item) {
	int i;
	Mutex_Lock(processedMutex);
	{
		i = RequestList_Find(&processedReqs, reqID);
		if (i >= 0) HttpRequest_Copy(item, &processedReqs.entries[i]);
		if (i >= 0) RequestList_RemoveAt(&processedReqs, i);
	}
	Mutex_Unlock(processedMutex);
	return i >= 0;
}

hc_bool Http_GetCurrent(int* reqID, int* progress) {
	Mutex_Lock(curRequestMutex);
	{
		*reqID    = http_curRequest.id;
		*progress = http_curRequest.progress;
	}
	Mutex_Unlock(curRequestMutex);
	return *reqID != 0;
}

int Http_CheckProgress(int reqID) {
	int curReqID, progress;
	Http_GetCurrent(&curReqID, &progress);

	if (reqID != curReqID) progress = HTTP_PROGRESS_NOT_WORKING_ON;
	return progress;
}

void Http_ClearPending(void) {
	Mutex_Lock(pendingMutex);
	{
		RequestList_Free(&pendingReqs);
	}
	Mutex_Unlock(pendingMutex);
}

void Http_TryCancel(int reqID) {
	Mutex_Lock(pendingMutex);
	{
		RequestList_TryFree(&pendingReqs, reqID);
	}
	Mutex_Unlock(pendingMutex);

	Mutex_Lock(processedMutex);
	{
		RequestList_TryFree(&processedReqs, reqID);
	}
	Mutex_Unlock(processedMutex);
}


/*########################################################################################################################*
*-----------------------------------------------------Http worker---------------------------------------------------------*
*#########################################################################################################################*/
/* Sets up state to begin a http request */
static void PrepareCurrentRequest(struct HttpRequest* req, hc_string* url) {
	static const char* verbs[] = { "GET", "HEAD", "POST" };
	Http_GetUrl(req, url);
	Platform_Log2("Fetching %s (%c)", url, verbs[req->requestType]);
	/* TODO change to verbs etc */

	Mutex_Lock(curRequestMutex);
	{
		HttpRequest_Copy(&http_curRequest, req);
		http_curRequest.progress = HTTP_PROGRESS_MAKING_REQUEST;
	}
	Mutex_Unlock(curRequestMutex);
}

static void PerformRequest(struct HttpRequest* req, hc_string* url) {
	hc_uint64 beg, end;
	int elapsed;

	beg = Stopwatch_Measure();
	req->result = HttpBackend_Do(req, url);
	end = Stopwatch_Measure();

	elapsed = Stopwatch_ElapsedMS(beg, end);
	Platform_Log4("HTTP: result %e (http %i) in %i ms (%i bytes)",
		&req->result, &req->statusCode, &elapsed, &req->size);

	Http_FinishRequest(req);
}

static void ClearCurrentRequest(void) {
	Mutex_Lock(curRequestMutex);
	{
		http_curRequest.id       = 0;
		http_curRequest.progress = HTTP_PROGRESS_NOT_WORKING_ON;
	}
	Mutex_Unlock(curRequestMutex);
}

static void DoRequest(struct HttpRequest* request) {
	char urlBuffer[URL_MAX_SIZE]; hc_string url;

	String_InitArray(url, urlBuffer);
	PrepareCurrentRequest(request, &url);
	PerformRequest(&http_curRequest, &url);
	ClearCurrentRequest();
}

static void WorkerLoop(void) {
	struct HttpRequest request;
	hc_bool hasRequest;

	for (;;) {
		hasRequest = false;

		Mutex_Lock(pendingMutex);
		{
			if (pendingReqs.count) {
				HttpRequest_Copy(&request, &pendingReqs.entries[0]);
				hasRequest = true;
				RequestList_RemoveAt(&pendingReqs, 0);
			}
		}
		Mutex_Unlock(pendingMutex);

		if (hasRequest) {
			DoRequest(&request);
		} else {
			/* Block until another thread submits a request to do */
			Platform_LogConst("Download queue empty, going back to sleep...");
			Waitable_Wait(workerWaitable);
		}
	}
}

/* Adds a req to the list of pending requests, waking up worker thread if needed */
static void HttpBackend_Add(struct HttpRequest* req, hc_uint8 flags) {
#if defined HC_BUILD_PSP || defined HC_BUILD_NDS
	/* TODO why doesn't threading work properly on PSP */
	DoRequest(req);
#else
	Mutex_Lock(pendingMutex);
	{
		RequestList_Append(&pendingReqs, req, flags);
	}
	Mutex_Unlock(pendingMutex);
	Waitable_Signal(workerWaitable);
#endif
}

/*########################################################################################################################*
*-----------------------------------------------------Http component------------------------------------------------------*
*#########################################################################################################################*/
static void Http_Init(void) {
	Http_InitCommon();
	http_curRequest.progress = HTTP_PROGRESS_NOT_WORKING_ON;
	/* Http component gets initialised multiple times on Android */
	if (workerThread) return;

	HttpBackend_Init();
	RequestList_Init(&pendingReqs);
	RequestList_Init(&processedReqs);

	workerWaitable  = Waitable_Create("HTTP wakeup");
	pendingMutex    = Mutex_Create("HTTP pending");
	processedMutex  = Mutex_Create("HTTP processed");
	curRequestMutex = Mutex_Create("HTTP current");
	
	Thread_Run(&workerThread, WorkerLoop, 128 * 1024, "HTTP");
}
#endif
