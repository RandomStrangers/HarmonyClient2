#ifndef HC_SSL_H
#define HC_SSL_H
#include "Platform.h"
HC_BEGIN_HEADER

/* 
Wraps a socket connection in a TLS/SSL connection
Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/

void SSLBackend_Init(hc_bool verifyCerts);
hc_bool SSLBackend_DescribeError(hc_result res, hc_string* dst);

hc_result SSL_Init(hc_socket socket, const hc_string* host, void** ctx);
hc_result SSL_Read(void* ctx, hc_uint8* data, hc_uint32 count, hc_uint32* read);
hc_result SSL_WriteAll(void* ctx, const hc_uint8* data, hc_uint32 count);
hc_result SSL_Free(void* ctx);

HC_END_HEADER
#endif
