// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __HTTP_H__
#define __HTTP_H__

#include <sys/socket.h>
#include "ht.h"

typedef Hashtable Http_Headers;

// HTTP methods
typedef enum {
	M_UNKNOWN = 0,
	M_GET,
	M_HEAD,
	M_POST,
	M_PUT,
	M_PATH,
	M_DELETE,
	M_OPTIONS,
	M_TRACE
} HTTP_Method;

// Header field names, normalized to lower-case
extern const char * H_CONTENT_LENGTH;
extern const char * H_EXPECT;
extern const char * H_CONNECTION;
extern const char * H_UPGRADE;

// Header values
extern const char * HV_EXPECT_100_CONTINUE;

#define MAX_HTTP_REQ 8192
#define MAX_HTTP_HEADER 8192

extern int http_init(const char * static_files_dir);
extern int http_client_connect(int fd_client_in, int fd_client_out);

#endif // __HTTP_H__
